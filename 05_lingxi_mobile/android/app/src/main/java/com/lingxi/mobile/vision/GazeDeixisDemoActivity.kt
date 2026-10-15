package com.lingxi.mobile.vision

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.lingxi.mobile.model.InteractRequest
import com.lingxi.mobile.net.BrainClient
import kotlinx.coroutines.launch

/**
 * 注视指代 demo —— 赛题旗舰场景"看着某个联系人说给他发消息"的端到端验证（办公室可测：看+点，不用出声）。
 *
 * 链路：GazeCamera 注视点 ⊕ 联系人行坐标 → [GazeDeixis].resolve 吸附出"看的是谁" → 点按钮 →
 * 把解析出的人当 gaze_target 连同"给他发消息"送大脑 → 引擎按规则3([注视焦点]>界面>历史)消解"他"。
 * 图像不出端、只把解析文本(gaze_target)出端（守 D5）。
 */
class GazeDeixisDemoActivity : AppCompatActivity() {

    private val contacts = listOf("张伟", "李娜", "王芳", "妈妈")
    private val rows = ArrayList<TextView>()
    private lateinit var root: FrameLayout
    private lateinit var overlay: HighlightView
    private lateinit var result: TextView
    private var cam: GazeCamera? = null
    private val brain = BrainClient()
    @Volatile private var gazedName: String? = null
    // dwell+迟滞：候选连续稳定 STABLE_FRAMES 帧才锁定，治中间相邻行来回跳
    private var pending: String? = null
    private var pendingCount = 0

    private val reqCamera = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { if (it) startCam() else result.text = "需要摄像头权限" }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        root = FrameLayout(this).apply { setBackgroundColor(Color.parseColor("#0E1116")) }

        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(40), dp(20), dp(20))
        }
        content.addView(label("注视指代 demo：盯住一个联系人 →点按钮", 18f))
        contacts.forEach { name ->
            val tv = label(name, 30f).apply {
                setPadding(dp(24), dp(22), dp(24), dp(22))
                setBackgroundColor(Color.parseColor("#1B2230"))
            }
            (tv.layoutParams as? LinearLayout.LayoutParams ?: LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).also { tv.layoutParams = it }).topMargin = dp(10)
            rows.add(tv); content.addView(tv)
        }
        val btn = Button(this).apply {
            text = "给 TA 发消息（用我看的那个人）"
            setOnClickListener { onSend() }
        }
        content.addView(btn)
        result = label("（先看着一个名字；没校准就先去注视测试点屏幕校准）", 15f)
        content.addView(result)

        root.addView(content)
        overlay = HighlightView(this).apply { isClickable = false }   // 不拦触摸,按钮可点
        root.addView(overlay, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        setContentView(root)

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        ) reqCamera.launch(Manifest.permission.CAMERA)
    }

    override fun onResume() {
        super.onResume()
        GazeEstimator.calib = GazeCalibration.load(this)
        if (!GazeCalibration.isCalibrated(this))
            result.text = "⚠ 还没校准——先去“灵犀注视测试”点屏幕做25点校准会准很多"
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            == PackageManager.PERMISSION_GRANTED
        ) startCam()
    }

    override fun onPause() { cam?.stop(); cam = null; super.onPause() }

    private fun startCam() {
        if (cam != null) return
        cam = GazeCamera(this, this) { nx, ny, _, _, hyaw, hpitch, face, lm ->
            val posture = GazePosture.evaluate(lm, hyaw, hpitch, face)
            onGaze(nx, ny, face && posture.ok)
        }
        cam?.start()
    }

    /** 每帧：把注视点吸附到某个联系人行，高亮它。 */
    private fun onGaze(nx: Float, ny: Float, face: Boolean) {
        val gx = nx * root.width
        val gy = ny * root.height
        val rootLoc = IntArray(2).also { root.getLocationInWindow(it) }
        val elements = rows.map { tv ->
            val l = IntArray(2).also { tv.getLocationInWindow(it) }
            ScreenElement(
                text = tv.text.toString(),
                bounds = Bounds(l[0] - rootLoc[0], l[1] - rootLoc[1],
                    l[0] - rootLoc[0] + tv.width, l[1] - rootLoc[1] + tv.height),
            )
        }
        val hitName = if (face) GazeDeixis.resolve(gx, gy, elements)?.text else null
        // dwell+迟滞：只有候选连续稳定够帧数才把"看的是谁"切过去，绿框不再抖
        if (hitName == gazedName) {
            pendingCount = 0
        } else {
            if (hitName == pending) pendingCount++ else { pending = hitName; pendingCount = 1 }
            if (pendingCount >= STABLE_FRAMES) { gazedName = hitName; pendingCount = 0 }
        }
        // 高亮锁定后的那一行(稳定)，而非每帧的原始命中
        val stableRect = gazedName?.let { name ->
            elements.firstOrNull { it.text == name }?.bounds?.let {
                RectF(it.left.toFloat(), it.top.toFloat(), it.right.toFloat(), it.bottom.toFloat())
            }
        }
        overlay.update(gx, gy, face, stableRect)
    }

    companion object { private const val STABLE_FRAMES = 5 }  // ~0.4s 稳定才锁定

    private fun onSend() {
        val who = gazedName
        if (who == null) { result.text = "没看准是谁，再盯一下某个名字"; return }
        result.text = "看的是「$who」→ 发给大脑：给他发消息（gaze_target=$who）…"
        lifecycleScope.launch {
            runCatching {
                brain.interact(InteractRequest(text = "给他发消息", gaze_target = who))
            }.onSuccess { r ->
                result.text = "看的是「$who」\n意图=${r.intent}\n灵犀：${r.reply}"
            }.onFailure { result.text = "调大脑失败：${it.message}（大脑+adb reverse 起了吗）" }
        }
    }

    private fun label(s: String, size: Float) = TextView(this).apply {
        text = s; textSize = size; setTextColor(Color.WHITE)
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}

/** 高亮层：画注视点 + 框住看中的联系人。 */
class HighlightView(context: Context) : View(context) {
    private var gx = 0f; private var gy = 0f; private var face = false
    private var hit: RectF? = null
    private val dot = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#4CD964") }
    private val box = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 6f; color = Color.parseColor("#4CD964")
    }

    fun update(gx: Float, gy: Float, face: Boolean, hit: RectF?) {
        this.gx = gx; this.gy = gy; this.face = face; this.hit = hit
        postInvalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        hit?.let { canvas.drawRoundRect(it, 12f, 12f, box) }
        if (face) canvas.drawCircle(gx, gy, 22f, dot)
    }
}
