package com.lingxi.mobile.vision

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Matrix
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import com.google.mediapipe.framework.image.BitmapImageBuilder
import com.google.mediapipe.tasks.core.BaseOptions
import com.google.mediapipe.tasks.vision.core.RunningMode
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarker
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarkerResult
import com.google.mediapipe.tasks.vision.gesturerecognizer.GestureRecognizer
import com.google.mediapipe.tasks.vision.gesturerecognizer.GestureRecognizerResult
import com.lingxi.mobile.model.VisionMark
import java.util.UUID
import java.util.concurrent.Executors
import kotlin.math.atan2
import kotlin.math.sqrt

/**
 * 冲突消解·端侧应答模态采集 —— 在【确认环节】开前置摄像头一个短窗口，捕获用户的
 * 点头/摇头（[HeadMotionDetector]）和手势（MediaPipe GestureRecognizer），
 * 产出带【统一时间轴时间戳】的 [VisionMark] 交大脑与语音对齐 + 冲突消解。
 *
 * 隐私（守 D5）：人脸图像/手部图像只在端侧处理，**绝不出端**；只把解析出的语义标签
 *（nod/shake/thumb_up…）+ 时间戳 + 置信度当文本上报。
 *
 * 复用 [GazeCamera] 同一套 CameraX + MediaPipe LIVE_STREAM 模式；窗口结束（或手动 stop）
 * 时回调 onDone(marks)。head_motion 取整窗采样判别，gesture 取窗口内最高票手势。
 */
class ConflictVisionCapture(
    private val context: Context,
    private val lifecycleOwner: LifecycleOwner,
    private val onDone: (marks: List<VisionMark>) -> Unit,
) {

    private var face: FaceLandmarker? = null
    private var gesture: GestureRecognizer? = null
    private var provider: ProcessCameraProvider? = null
    private val executor = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    private val lock = Any()
    @Volatile private var running = false
    private var lastTs = 0L
    private var startMs = 0L
    private var confirmationWindowId = ""
    @Volatile private var cameraDegraded = false
    @Volatile private var faceDegraded = false
    @Volatile private var gestureDegraded = false

    private val samples = ArrayList<HeadMotionDetector.HeadSample>()       // 头姿序列（窗口内）
    private val gestureVotes = HashMap<String, Int>()                       // 手势标签 → 票数
    private val gestureScoreSums = HashMap<String, Float>()                 // 手势标签 → 真实逐帧分数和
    private var gestureFirstMs = HashMap<String, Long>()                    // 手势标签 → 首次出现时刻
    private var gestureLastMs = HashMap<String, Long>()                     // 手势标签 → 最后出现时刻
    private val exprVotes = HashMap<String, Int>()                          // 表情极性标签 → 票数（弱信号）
    private var exprFirstMs = HashMap<String, Long>()

    /** 开窗采集；windowMs 后自动结束并回调。返回是否成功起摄像头。 */
    fun start(
        windowMs: Long = 3000L,
        timelineOriginMs: Long? = null,
        confirmationWindowId: String? = null,
    ) {
        if (running) return
        running = true
        startMs = timelineOriginMs ?: SystemClock.elapsedRealtime()
        this.confirmationWindowId = confirmationWindowId ?: UUID.randomUUID().toString()
        lastTs = 0L
        cameraDegraded = false
        faceDegraded = false
        gestureDegraded = false
        synchronized(lock) {
            samples.clear(); gestureVotes.clear(); gestureScoreSums.clear()
            gestureFirstMs = HashMap(); gestureLastMs = HashMap()
            exprVotes.clear(); exprFirstMs = HashMap()
        }

        val faceOpts = FaceLandmarker.FaceLandmarkerOptions.builder()
            .setBaseOptions(BaseOptions.builder().setModelAssetPath("mediapipe/face_landmarker.task").build())
            .setRunningMode(RunningMode.LIVE_STREAM)
            .setNumFaces(1)
            .setOutputFacialTransformationMatrixes(true)   // 关键：拿头姿矩阵算 pitch/yaw（点头/摇头）
            .setOutputFaceBlendshapes(true)                // 拿 blendshapes 算表情（微笑/皱眉，弱信号）
            .setResultListener { r, _ -> onFace(r) }
            .setErrorListener { e -> faceDegraded = true; Log.e(TAG, "FaceLandmarker 错误", e) }
            .build()
        val gestOpts = GestureRecognizer.GestureRecognizerOptions.builder()
            .setBaseOptions(BaseOptions.builder().setModelAssetPath("mediapipe/gesture_recognizer.task").build())
            .setRunningMode(RunningMode.LIVE_STREAM)
            .setNumHands(1)
            .setResultListener { r, _ -> onGesture(r) }
            .setErrorListener { e -> gestureDegraded = true; Log.e(TAG, "GestureRecognizer 错误", e) }
            .build()
        face = runCatching { FaceLandmarker.createFromOptions(context, faceOpts) }
            .onFailure { faceDegraded = true; Log.e(TAG, "FaceLandmarker 创建失败", it) }.getOrNull()
        gesture = runCatching { GestureRecognizer.createFromOptions(context, gestOpts) }
            .onFailure {
                gestureDegraded = true
                Log.e(TAG, "GestureRecognizer 创建失败（缺 gesture_recognizer.task?）", it)
            }.getOrNull()

        val future = ProcessCameraProvider.getInstance(context)
        future.addListener({
            provider = future.get()
            val analysis = ImageAnalysis.Builder()
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888)
                .build()
            analysis.setAnalyzer(executor) { analyze(it) }
            runCatching {
                provider?.unbindAll()
                provider?.bindToLifecycle(lifecycleOwner, CameraSelector.DEFAULT_FRONT_CAMERA, analysis)
            }.onFailure { cameraDegraded = true; Log.e(TAG, "绑定相机失败", it) }
        }, ContextCompat.getMainExecutor(context))

        main.postDelayed({ stop() }, windowMs)
    }

    private fun analyze(proxy: ImageProxy) {
        try {
            if (!running) return
            val bmp = proxy.toBitmap()
            val rot = proxy.imageInfo.rotationDegrees
            val up = if (rot != 0) rotate(bmp, rot) else bmp
            val ts = maxOf(SystemClock.elapsedRealtime(), lastTs + 1)
            lastTs = ts
            synchronized(lock) {
                if (running) {
                    val img = BitmapImageBuilder(up).build()
                    face?.detectAsync(img, ts)
                    gesture?.recognizeAsync(BitmapImageBuilder(up).build(), ts)
                }
            }
        } catch (e: Exception) {
            cameraDegraded = true
            Log.e(TAG, "分析帧失败", e)
        } finally {
            proxy.close()
        }
    }

    private fun onFace(result: FaceLandmarkerResult) {
        if (!running) return
        val now = SystemClock.elapsedRealtime() - startMs
        val mats = result.facialTransformationMatrixes()
        if (mats.isPresent && mats.get().isNotEmpty()) {
            val m = mats.get()[0]   // float[16]，列主序 4x4
            if (m.size >= 11) {
                // 头姿欧拉角（度）。pitch=俯仰(点头)，yaw=偏航(摇头)。
                // 注：符号/轴向在真机上以实测为准微调；判别器只看幅度/反转，对符号翻转鲁棒。
                val pitch = Math.toDegrees(atan2(m[6].toDouble(), m[10].toDouble())).toFloat()
                val yaw = Math.toDegrees(atan2((-m[2]).toDouble(),
                    sqrt((m[6] * m[6] + m[10] * m[10]).toDouble()))).toFloat()
                synchronized(lock) { samples.add(HeadMotionDetector.HeadSample(now, pitch, yaw)) }
            }
        }
        // 表情（弱信号）：从 blendshapes 算
        val shapes = result.faceBlendshapes()
        if (shapes.isPresent && shapes.get().isNotEmpty()) {
            val bs = HashMap<String, Float>()
            for (c in shapes.get()[0]) bs[c.categoryName()] = c.score()
            val (expr, _) = ExpressionDetector.classify(bs)
            val label = ExpressionDetector.label(expr)
            if (label != null) synchronized(lock) {
                exprVotes[label] = (exprVotes[label] ?: 0) + 1
                exprFirstMs.putIfAbsent(label, now)
            }
        }
    }

    private fun onGesture(result: GestureRecognizerResult) {
        if (!running) return
        val gestures = result.gestures()
        if (gestures.isEmpty() || gestures[0].isEmpty()) return
        val top = gestures[0][0]
        val label = GestureLabels.toMark(top.categoryName()) ?: return
        if (top.score() < 0.5f) return
        synchronized(lock) {
            gestureVotes[label] = (gestureVotes[label] ?: 0) + 1
            gestureScoreSums[label] = (gestureScoreSums[label] ?: 0f) + top.score()
            val eventMs = SystemClock.elapsedRealtime() - startMs
            gestureFirstMs.putIfAbsent(label, eventMs)
            gestureLastMs[label] = eventMs
        }
    }

    /** 结束采集：判别头动 + 取最高票手势 → 组装带时间戳的 VisionMark，回调 onDone。可被外部提前调用。 */
    fun stop() {
        if (!running) return
        running = false
        runCatching { provider?.unbindAll() }
        executor.shutdown()
        runCatching { executor.awaitTermination(300, java.util.concurrent.TimeUnit.MILLISECONDS) }
        synchronized(lock) {
            runCatching { face?.close() }; face = null
            runCatching { gesture?.close() }; gesture = null
        }
        val marks = ArrayList<VisionMark>()
        synchronized(lock) {
            if (samples.size >= 5) {
                val r = HeadMotionDetector.classify(samples)
                // NONE 也必须留原始判别摘要，否则真机失败只能猜是幅度、路径、反转还是轴向占优没过。
                Log.i(
                    TAG,
                    "头动判别：motion=${r.motion} pitchAmp=${r.pitchAmp} yawAmp=${r.yawAmp} " +
                        "pitchRev=${r.pitchReversals} yawRev=${r.yawReversals} dominantPath=${r.dominantPath}",
                )
                val label = r.label()
                if (label != null) {
                    val bounds = HeadMotionDetector.eventBounds(samples, r.motion)
                    val t0 = (bounds?.startMs ?: samples.first().tMs) / 1000.0
                    val t1 = (bounds?.endMs ?: samples.last().tMs) / 1000.0
                    marks.add(VisionMark(
                        kind = "head_motion",
                        label = label,
                        confidence = r.confidence.toDouble(),
                        t_start = t0,
                        t_end = t1,
                        event_id = UUID.randomUUID().toString(),
                        window_id = confirmationWindowId,
                        stable_count = samples.size,
                        localized = bounds?.localized ?: false,
                        quality = if (cameraDegraded || faceDegraded) "degraded" else "ok",
                    ))
                    Log.i(TAG, "头动局部窗：$label [$t0,$t1] localized=${bounds?.localized}")
                }
            }
            val best = gestureVotes.maxByOrNull { it.value }
            // 2～4 帧的瞬时手型在真机 V02R2 中会产生漂移 mark；至少 5 帧才上报正式手势证据。
            if (best != null && best.value >= MIN_GESTURE_STABLE_FRAMES) {
                val t0 = (gestureFirstMs[best.key] ?: 0L) / 1000.0
                val t1 = (gestureLastMs[best.key] ?: gestureFirstMs[best.key] ?: 0L) / 1000.0
                val confidence = (gestureScoreSums[best.key] ?: 0f) / best.value
                marks.add(VisionMark(
                    kind = "gesture",
                    label = best.key,
                    confidence = confidence.toDouble(),
                    t_start = t0,
                    t_end = maxOf(t0, t1),
                    event_id = UUID.randomUUID().toString(),
                    window_id = confirmationWindowId,
                    stable_count = best.value,
                    localized = true,
                    quality = if (cameraDegraded || gestureDegraded) "degraded" else "ok",
                ))
            }
            // 表情（弱信号）：要稳定出现（≥半数帧）才上报，避免一闪而过的微表情干扰
            val bestExpr = exprVotes.maxByOrNull { it.value }
            if (bestExpr != null && bestExpr.value >= maxOf(5, samples.size / 2)) {
                val t0 = (exprFirstMs[bestExpr.key] ?: 0L) / 1000.0
                marks.add(VisionMark(
                    kind = "expression",
                    label = bestExpr.key,
                    confidence = 0.6,
                    t_start = t0,
                    t_end = samples.lastOrNull()?.let { it.tMs / 1000.0 } ?: (t0 + 0.5),
                    event_id = UUID.randomUUID().toString(),
                    window_id = confirmationWindowId,
                    stable_count = bestExpr.value,
                    localized = false,
                    quality = if (cameraDegraded || faceDegraded) "degraded" else "ok",
                ))
            }
        }
        Log.i(TAG, "采集结束：head样本=${samples.size} 手势票=$gestureVotes 表情票=$exprVotes → marks=$marks")
        main.post { onDone(marks) }
    }

    private fun rotate(b: Bitmap, deg: Int): Bitmap {
        val mtx = Matrix().apply { postRotate(deg.toFloat()) }
        return Bitmap.createBitmap(b, 0, 0, b.width, b.height, mtx, true)
    }

    companion object {
        private const val TAG = "ConflictVision"
        private const val MIN_GESTURE_STABLE_FRAMES = 5
    }
}
