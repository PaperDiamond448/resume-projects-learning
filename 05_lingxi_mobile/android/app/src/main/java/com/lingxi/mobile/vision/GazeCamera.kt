package com.lingxi.mobile.vision

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Matrix
import android.util.Log
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.core.UseCase
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import com.google.mediapipe.framework.image.BitmapImageBuilder
import com.google.mediapipe.tasks.core.BaseOptions
import com.google.mediapipe.tasks.vision.core.RunningMode
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarker
import com.google.mediapipe.tasks.vision.facelandmarker.FaceLandmarkerResult
import android.view.Surface
import android.view.TextureView
import java.util.concurrent.Executors
import kotlin.math.atan2
import kotlin.math.sqrt

/**
 * 注视感知相机 —— CameraX 前置头 → MediaPipe FaceLandmarker(LIVE_STREAM, 开 blendshapes)
 * → [GazeEstimator] 出归一化注视点 (nx,ny)∈[0,1]。**图像只在端侧处理、绝不出端**(守 D5)。
 *
 * 用法：start() 绑定生命周期开跑，每帧回调 onGaze(nx,ny,faceFound)；stop() 释放。
 * 注视点的"看哪个元素"由 [GazeDeixis] 把它吸附到无障碍元素框完成（本类只负责"看哪儿"）。
 */
class GazeCamera(
    private val context: Context,
    private val lifecycleOwner: LifecycleOwner,
    private val previewTexture: TextureView? = null,
    // nx,ny=校准后注视点(0..1)；rawGx,rawGy=原始注视方向(供校准采集)；
    // rawHYaw,rawHPitch=原始头姿(度，供校准采集，T-16 Step B新增)；lm=人脸网格点(归一化,镜像)
    private val onGaze: (
        nx: Float, ny: Float, rawGx: Float, rawGy: Float,
        rawHYaw: Float, rawHPitch: Float, faceFound: Boolean, lm: FloatArray?,
    ) -> Unit,
) : GazeProvider {

    private var landmarker: FaceLandmarker? = null
    private var provider: ProcessCameraProvider? = null
    private val executor = Executors.newSingleThreadExecutor()
    @Volatile private var latest: GazePoint? = null
    private var lastTs = 0L
    @Volatile private var running = false      // 防"关闭中仍喂帧"导致 native SIGBUS
    private val lock = Any()                    // 串行化 detect 与 close，杜绝竞态
    // 指数滑动平均(治逐帧噪声"乱飞")；alpha 越大越稳但越钝
    private var smX = 0.5f
    private var smY = 0.5f
    private val alpha = 0.78f

    fun start() {
        if (running) return
        running = true
        val base = BaseOptions.builder()
            .setModelAssetPath("mediapipe/face_landmarker.task").build()
        val opts = FaceLandmarker.FaceLandmarkerOptions.builder()
            .setBaseOptions(base)
            .setRunningMode(RunningMode.LIVE_STREAM)
            .setNumFaces(1)
            .setOutputFaceBlendshapes(true)        // 关键：拿 eyeLookIn/Out/Up/Down
            .setOutputFacialTransformationMatrixes(true)  // T-16 Step B：拿头姿矩阵算 hyaw/hpitch，同 ConflictVisionCapture
            .setResultListener { result, _ -> onResult(result) }
            .setErrorListener { e -> Log.e(TAG, "FaceLandmarker 错误", e) }
            .build()
        landmarker = FaceLandmarker.createFromOptions(context, opts)

        val future = ProcessCameraProvider.getInstance(context)
        future.addListener({
            provider = future.get()
            val analysis = ImageAnalysis.Builder()
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888)
                .build()
            analysis.setAnalyzer(executor) { analyze(it) }
            val useCases = mutableListOf<UseCase>(analysis)
            previewTexture?.let { texture ->
                useCases.add(Preview.Builder().build().also { preview ->
                    preview.setSurfaceProvider { request ->
                        fun provideSurface() {
                            val surfaceTexture = texture.surfaceTexture ?: return
                            val res = request.resolution
                            surfaceTexture.setDefaultBufferSize(res.width, res.height)
                            val surface = Surface(surfaceTexture)
                            request.provideSurface(surface, executor) { surface.release() }
                        }
                        if (texture.isAvailable) {
                            provideSurface()
                        } else {
                            texture.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
                                override fun onSurfaceTextureAvailable(st: android.graphics.SurfaceTexture, w: Int, h: Int) = provideSurface()
                                override fun onSurfaceTextureSizeChanged(st: android.graphics.SurfaceTexture, w: Int, h: Int) = Unit
                                override fun onSurfaceTextureDestroyed(st: android.graphics.SurfaceTexture): Boolean = false
                                override fun onSurfaceTextureUpdated(st: android.graphics.SurfaceTexture) = Unit
                            }
                        }
                    }
                })
            }
            runCatching {
                provider?.unbindAll()
                provider?.bindToLifecycle(
                    lifecycleOwner, CameraSelector.DEFAULT_FRONT_CAMERA, *useCases.toTypedArray()
                )
            }.onFailure { Log.e(TAG, "绑定相机失败", it) }
        }, ContextCompat.getMainExecutor(context))
    }

    private fun analyze(proxy: ImageProxy) {
        try {
            if (!running) return
            val bmp = proxy.toBitmap()
            val rot = proxy.imageInfo.rotationDegrees
            val up = if (rot != 0) rotate(bmp, rot) else bmp
            val ts = maxOf(System.currentTimeMillis(), lastTs + 1)  // 必须单调递增
            lastTs = ts
            synchronized(lock) {                 // 与 close 互斥，杜绝"关闭中喂帧"SIGBUS
                if (running) landmarker?.detectAsync(BitmapImageBuilder(up).build(), ts)
            }
        } catch (e: Exception) {
            Log.e(TAG, "分析帧失败", e)
        } finally {
            proxy.close()
        }
    }

    private fun onResult(result: FaceLandmarkerResult) {
        val shapes = result.faceBlendshapes()
        if (!shapes.isPresent || shapes.get().isEmpty()) {
            latest = null; post(smX, smY, 0f, 0f, 0f, 0f, false, null); return  // 丢脸:保持上次位置,不跳回中心
        }
        // 人脸网格点(归一化,镜像成自拍视角)供可视化
        val lmArr = result.faceLandmarks().takeIf { it.isNotEmpty() }?.let { faces ->
            val pts = faces[0]
            FloatArray(pts.size * 2).also { a ->
                for (i in pts.indices) { a[i * 2] = 1f - pts[i].x(); a[i * 2 + 1] = pts[i].y() }
            }
        }
        val (hyaw, hpitch) = headPose(result)
        val bs = HashMap<String, Float>()
        for (c in shapes.get()[0]) bs[c.categoryName()] = c.score()
        val raw = GazeEstimator.rawFromBlendshapes(bs)
        if (raw == null) { post(smX, smY, 0f, 0f, hyaw, hpitch, false, lmArr); return }
        val cal = GazeEstimator.calib.apply(raw.first, raw.second, hyaw, hpitch)
        smX = alpha * smX + (1 - alpha) * cal.first      // 平滑(显示用)
        smY = alpha * smY + (1 - alpha) * cal.second
        latest = GazePoint(smX, smY)
        post(smX, smY, raw.first, raw.second, hyaw, hpitch, true, lmArr)  // 原始方向不平滑,交校准自己平均
    }

    /** 头姿(俯仰/偏航,度)：从 facialTransformationMatrixes 抽取，公式与 [ConflictVisionCapture] 同款
     *  （已真机验证准确，见 T-03 M3/M4）。无矩阵/丢脸返回 (0,0)——校准的头姿系数默认为 0，安全退化。 */
    private fun headPose(result: FaceLandmarkerResult): Pair<Float, Float> {
        val mats = result.facialTransformationMatrixes()
        if (!mats.isPresent || mats.get().isEmpty()) return 0f to 0f
        val m = mats.get()[0]
        if (m.size < 11) return 0f to 0f
        val pitch = Math.toDegrees(atan2(m[6].toDouble(), m[10].toDouble())).toFloat()
        val yaw = Math.toDegrees(
            atan2((-m[2]).toDouble(), sqrt((m[6] * m[6] + m[10] * m[10]).toDouble()))
        ).toFloat()
        return yaw to pitch
    }

    private fun post(
        nx: Float, ny: Float, rgx: Float, rgy: Float,
        hyaw: Float, hpitch: Float, face: Boolean, lm: FloatArray?,
    ) = ContextCompat.getMainExecutor(context).execute { onGaze(nx, ny, rgx, rgy, hyaw, hpitch, face, lm) }

    /** 归一化注视点 (x,y)∈[0,1]；无脸返回 null。供 GazeDeixis 按屏幕尺寸放大后吸附元素。 */
    override fun currentGaze(): GazePoint? = latest

    fun stop() {
        running = false
        runCatching { provider?.unbindAll() }   // 先停喂帧
        executor.shutdown()
        runCatching { executor.awaitTermination(300, java.util.concurrent.TimeUnit.MILLISECONDS) }
        synchronized(lock) {                     // 确保无 detect 在飞时才 close
            runCatching { landmarker?.close() }
            landmarker = null
        }
    }

    private fun rotate(b: Bitmap, deg: Int): Bitmap {
        val m = Matrix().apply { postRotate(deg.toFloat()) }
        return Bitmap.createBitmap(b, 0, 0, b.width, b.height, m, true)
    }

    companion object { private const val TAG = "GazeCamera" }
}
