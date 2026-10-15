package com.lingxi.mobile.vision

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.os.Bundle
import android.view.View
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat

/**
 * 注视调试界面 —— 前置摄像头跑 MediaPipe，把估计的注视点画在屏上。
 * 真机验证：看屏幕四角/中心，绿点应大致跟着你的视线走（粗注视）。
 * 标定：看着点偏的方向，调 [GazeEstimator] 的 flipX/gainX/gainY/biasY 再装。
 * 隐私：不显示摄像头画面、图像只在端侧处理不出端（守 D5）。
 */
class GazeDebugActivity : AppCompatActivity() {

    private lateinit var overlay: GazeOverlay
    private var cam: GazeCamera? = null

    private val reqCamera = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) startCam()
        else overlay.setStatus("需要摄像头权限才能测注视")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        overlay = GazeOverlay(this)
        setContentView(overlay)
        overlay.setOnClickListener {            // 点屏幕 → 开始 25 点校准
            startActivity(Intent(this, CalibrationActivity::class.java))
        }
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        ) reqCamera.launch(Manifest.permission.CAMERA)
    }

    override fun onResume() {
        super.onResume()
        GazeEstimator.calib = GazeCalibration.load(this)   // 反映最新校准(校准完返回后生效)
        overlay.calibrated = GazeCalibration.isCalibrated(this)
        overlay.postInvalidate()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            == PackageManager.PERMISSION_GRANTED
        ) startCam()
    }

    override fun onPause() {
        stopCam()          // 离开(含跳校准)即释放相机+引擎，避免双实例 SIGBUS
        super.onPause()
    }

    private fun startCam() {
        if (cam != null) return
        overlay.setStatus("点屏幕开始校准（25点）；校准后绿点才准")
        cam = GazeCamera(this, this) { nx, ny, _, _, _, _, face, lm -> overlay.update(nx, ny, face, lm) }
        cam?.start()
    }

    private fun stopCam() {
        cam?.stop()
        cam = null
    }

    override fun onDestroy() {
        stopCam()
        super.onDestroy()
    }
}

/** 全屏覆盖层：画注视点 + 状态文字。 */
class GazeOverlay(context: Context) : View(context) {

    private var nx = 0.5f
    private var ny = 0.5f
    private var face = false
    private var status = "启动相机中…"
    private var lm: FloatArray? = null
    var calibrated = false

    private val dot = Paint(Paint.ANTI_ALIAS_FLAG)
    private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 6f; color = Color.WHITE
    }
    private val mesh = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#3AC0FF") }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE; textSize = 44f
    }

    fun update(nx: Float, ny: Float, face: Boolean, lm: FloatArray?) {
        this.nx = nx; this.ny = ny; this.face = face; this.lm = lm
        postInvalidate()
    }

    fun setStatus(s: String) { status = s; postInvalidate() }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(Color.parseColor("#101418"))

        // 478 点人脸网格（你脸的实时点云，证明 MediaPipe 在跟）
        val pts = lm
        if (pts != null) {
            var i = 0
            while (i < pts.size - 1) {
                canvas.drawCircle(pts[i] * width, pts[i + 1] * height, 3.2f, mesh)
                i += 2
            }
        }

        // 注视点（已平滑）
        val cx = nx * width
        val cy = ny * height
        dot.color = if (face) Color.parseColor("#4CD964") else Color.parseColor("#888888")
        canvas.drawCircle(cx, cy, 40f, dot)
        canvas.drawCircle(cx, cy, 56f, ring)

        canvas.drawText(status, 40f, 90f, text)
        canvas.drawText(
            "脸:${if (face) "✓" else "✗"}  注视 nx=%.2f ny=%.2f  网格:%d点"
                .format(nx, ny, (pts?.size ?: 0) / 2), 40f, 150f, text
        )
        canvas.drawText(
            if (calibrated) "已校准 ✓  点屏幕可重新校准" else "⚠ 未校准——点屏幕做25点校准",
            40f, height - 60f, text
        )
    }
}
