package com.lingxi.mobile.vision

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.TextureView
import android.widget.FrameLayout
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import java.io.BufferedWriter
import java.io.File
import java.io.FileWriter
import kotlin.math.sin

/**
 * 注视校准 —— 5×5=25 点逐个盯，采集每点的原始注视方向 → 二次拟合 → 存本地。
 * 点数密(覆盖边角)+二次模型，才学得准（用户经验：9 点远不够）。校准物=眼睛，图像不出端。
 *
 * T-16 v2 曾试过"中心补采2次+更长settle/collect"想解决用户反馈的"中心不如上下准"，
 * 但真机复测反而更飘（详见 feasibility.md §8 2026-07-06 条目）——已回退到本版本（原 25 点+原时长），
 * 假说本身不成立或被别的因素掩盖，不要重复这个方向，除非有新证据支持。
 */
class CalibrationActivity : AppCompatActivity() {

    private lateinit var view: CalibView
    private lateinit var preview: TextureView
    private var cam: GazeCamera? = null
    private val handler = Handler(Looper.getMainLooper())

    // 5×5 网格（覆盖到边角 0.08~0.92）
    private val grid = buildList {
        val f = floatArrayOf(0.08f, 0.30f, 0.5f, 0.70f, 0.92f)
        for (r in f.indices) {
            val cols = if (r % 2 == 0) f.indices else f.indices.reversed()  // 蛇形,视线顺
            for (c in cols) add(f[c] to f[r])
        }
    }
    private val samples = ArrayList<FloatArray>()
    private var idx = -1
    private var collecting = false
    private var startedCalibration = false
    private var stableSince = 0L
    private var pointRun = 0
    private var accX = 0f; private var accY = 0f; private var accN = 0
    private var accHYaw = 0f; private var accHPitch = 0f  // T-16 Step B：同步采头姿，供 8 维校准拟合
    private val acceptedFrames = ArrayList<GazeCalibrationSamplePool.Frame>()
    private var traceWriter: BufferedWriter? = null
    private var traceFile: File? = null

    private val reqCamera = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { if (it) startCam() else { toast("需要摄像头权限"); finish() } }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        preview = TextureView(this).apply { scaleX = -1f } // 自拍预览镜像，用户更容易把脸放进圈里
        view = CalibView(this)
        val root = FrameLayout(this).apply {
            addView(preview, FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT,
            ))
            addView(view, FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT,
            ))
        }
        setContentView(root)
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            == PackageManager.PERMISSION_GRANTED
        ) startCam() else reqCamera.launch(Manifest.permission.CAMERA)
    }

    private fun startCam() {
        startTrace()
        view.progress = "把脸放进圆圈，保持稳定 3 秒后开始"
        cam = GazeCamera(this, this, preview) { _, _, rgx, rgy, hyaw, hpitch, face, lm ->
            view.face = face
            view.posture = GazePosture.evaluate(lm, hyaw, hpitch, face)
            maybeStartAfterStablePosture()
            val accepted = collecting && face && view.posture.ok
            if (accepted) {
                accX += rgx; accY += rgy; accHYaw += hyaw; accHPitch += hpitch; accN++
                grid.getOrNull(idx)?.let { (tx, ty) ->
                    acceptedFrames.add(GazeCalibrationSamplePool.Frame(rgx, rgy, hyaw, hpitch, tx, ty))
                }
            }
            writeTraceFrame(rgx, rgy, hyaw, hpitch, face, view.posture, accepted)
            view.postInvalidate()
        }
        cam?.start()
    }

    private fun maybeStartAfterStablePosture() {
        if (startedCalibration) return
        val now = System.currentTimeMillis()
        if (view.posture.ok) {
            if (stableSince == 0L) stableSince = now
            val remaining = PREPARE_STABLE_MS - (now - stableSince)
            if (remaining <= 0L) {
                startedCalibration = true
                idx = 0
                scheduleCurrentPoint("校准中 1/${grid.size} · 继续保持姿势，盯住圆点")
            } else {
                view.progress = "姿势合适，保持 %.1f 秒后开始".format(remaining / 1000f)
            }
        } else {
            stableSince = 0L
            view.progress = "${view.posture.message}，稳定 3 秒后开始"
        }
    }

    private fun finishCurrentPoint(run: Int) {
        if (run != pointRun || !startedCalibration) return
        // 收上一个点
        if (idx in grid.indices) {
            if (accN > 0) {
                val (tx, ty) = grid[idx]
                samples.add(floatArrayOf(accX / accN, accY / accN, accHYaw / accN, accHPitch / accN, tx, ty))
            } else if (collecting) {
                scheduleCurrentPoint("姿势不稳，重采 ${idx + 1}/${grid.size} · 把脸放进圆圈")
                return
            }
        }
        idx++
        if (idx >= grid.size) { finishCalib(); return }
        scheduleCurrentPoint("校准中 ${idx + 1}/${grid.size} · 继续保持姿势，盯住圆点")
    }

    private fun scheduleCurrentPoint(message: String) {
        pointRun++
        val run = pointRun
        accX = 0f; accY = 0f; accHYaw = 0f; accHPitch = 0f; accN = 0; collecting = false
        view.target = grid[idx]; view.progress = message
        view.postInvalidate()
        handler.postDelayed({ if (run == pointRun) collecting = true }, SETTLE)        // settle 后开始采
        handler.postDelayed({ finishCurrentPoint(run) }, SETTLE + COLLECT)             // 采够进下一个
    }

    private fun finishCalib() {
        closeTrace()
        val pooledSamples = if (samples.size == grid.size)
            GazeCalibrationSamplePool.buildSamplesIncluding(this, acceptedFrames)
        else emptyList()
        val calib = GazeCalibration.fit(pooledSamples).also {
            if (it != null) GazeCalibrationSamplePool.saveRun(this, acceptedFrames)
        } ?: GazeCalibration.fit(samples)
        if (calib != null) {
            calib.save(this)
            GazeEstimator.calib = calib
            val sessions = GazeCalibrationSamplePool.sessionCount(this)
            toast("校准完成！融合 ${sessions} 次样本；采到 ${samples.size}/${grid.size} 点")
        } else {
            toast("校准没采够脸，重试一下（光线/正对摄像头）")
        }
        finish()
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_LONG).show()

    private fun startTrace() {
        if (traceWriter != null) return
        val dir = File(getExternalFilesDir(null) ?: filesDir, "gaze_diagnostics").also { it.mkdirs() }
        traceFile = File(dir, "gaze_calib_${System.currentTimeMillis()}.csv")
        traceWriter = BufferedWriter(FileWriter(traceFile)).also { w ->
            w.write(
                "time_ms,point_index,target_x,target_y,collecting,accepted,face," +
                    "raw_gx,raw_gy,hyaw,hpitch,posture_ok,posture_message," +
                    "face_center_x,face_center_y,face_width,face_height\n"
            )
        }
    }

    private fun writeTraceFrame(
        rgx: Float, rgy: Float, hyaw: Float, hpitch: Float,
        face: Boolean, posture: GazePosture.Quality, accepted: Boolean,
    ) {
        val point = grid.getOrNull(idx)
        val targetX = point?.first ?: Float.NaN
        val targetY = point?.second ?: Float.NaN
        traceWriter?.runCatching {
            write(
                "${System.currentTimeMillis()},$idx,$targetX,$targetY,$collecting,$accepted,$face," +
                    "$rgx,$rgy,$hyaw,$hpitch,${posture.ok},${csv(posture.message)}," +
                    "${posture.centerX},${posture.centerY},${posture.faceWidth},${posture.faceHeight}\n"
            )
        }
    }

    private fun csv(s: String) = "\"" + s.replace("\"", "\"\"") + "\""

    private fun closeTrace() {
        traceWriter?.runCatching { flush(); close() }
        traceWriter = null
    }

    override fun onPause() {
        handler.removeCallbacksAndMessages(null)
        closeTrace()
        cam?.stop(); cam = null      // 离开即释放，调试界面 onResume 再开，避免双实例
        super.onPause()
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        closeTrace()
        cam?.stop(); cam = null
        super.onDestroy()
    }

    companion object {
        private const val SETTLE = 550L   // 移到新点后稳定时间
        private const val COLLECT = 650L  // 采样时长
        private const val PREPARE_STABLE_MS = 3000L
    }
}

/** 校准界面：当前目标点 + 进度。 */
class CalibView(context: Context) : View(context) {
    var target: Pair<Float, Float>? = null
    var progress = "准备校准…"
    var face = false
    var posture = GazePosture.NO_FACE

    private val bg = Color.parseColor("#0E1116")
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#4CD964") }
    private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 5f; color = Color.WHITE
    }
    private val guide = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 6f; color = Color.parseColor("#7AA7FF"); alpha = 180
    }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textSize = 42f }
    private val smallText = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textSize = 34f }
    private val shade = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(96, 0, 0, 0) }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawRect(0f, 0f, width.toFloat(), 150f, shade)
        canvas.drawRect(0f, height - 150f, width.toFloat(), height.toFloat(), shade)
        val guideW = width * 0.48f
        val guideH = height * 0.36f
        val guideRect = RectF(
            (width - guideW) / 2f,
            height * 0.18f,
            (width + guideW) / 2f,
            height * 0.18f + guideH,
        )
        guide.color = if (posture.ok) Color.parseColor("#4CD964") else Color.parseColor("#7AA7FF")
        canvas.drawOval(guideRect, guide)

        target?.let { (tx, ty) ->
            val cx = tx * width; val cy = ty * height
            val pulse = (sin(System.currentTimeMillis() / 200.0).toFloat() * 0.5f + 0.5f)
            canvas.drawCircle(cx, cy, 14f + pulse * 8f, fill)   // 实心点(脉动)
            canvas.drawCircle(cx, cy, 34f + pulse * 14f, ring)  // 外圈
        }
        drawText(canvas, progress, 40f, 90f, text)
        drawText(canvas, "姿势:${if (posture.ok) "✓" else "✗"} ${posture.message}", 40f, height - 108f, text)
        drawText(canvas, "脸:${if (face) "✓" else "✗"}  距离=%.2f".format(posture.faceHeight), 40f, height - 56f, smallText)
    }

    private fun drawText(canvas: Canvas, s: String, x: Float, y: Float, p: Paint) {
        p.setShadowLayer(8f, 0f, 2f, Color.BLACK)
        canvas.drawText(s, x, y, p)
        p.clearShadowLayer()
    }
}
