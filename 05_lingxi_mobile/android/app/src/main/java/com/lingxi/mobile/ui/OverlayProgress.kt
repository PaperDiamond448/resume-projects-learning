package com.lingxi.mobile.ui

import android.content.Context
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.TextView
import com.lingxi.mobile.accessibility.LingxiAccessibilityService

/**
 * GUI-agent 操作进度悬浮窗（浮在被操作 App 上层，实时显示"它在想什么/到第几步"）+ 取消按钮。
 *
 * 为什么需要：agent 操作 b站/酷狗等 App 时灵犀自己在后台，对话区进度看不见。悬浮窗浮最上层显示
 * 每步【💭思考/动作】（对标豆包"看得见的操作"），右上角「✕ 结束」让用户随时硬停（治"我已退回主界面
 * 它还在瞎找"——人工介入反扰 AI）。
 *
 * 一个进度窗口：短触不取消，长按进度框才取消任务。旧版右上角「✕ 结束」小窗会和 App 顶部
 * 搜索/关闭按钮冲突（淘宝/京东/知乎尤其明显），所以取消入口收进任务框长按里。
 * 始终复用已连接的灵犀无障碍服务创建 TYPE_ACCESSIBILITY_OVERLAY，
 * 不再保留 SYSTEM_ALERT_WINDOW 的第二套权限与窗口路径。
 */
object OverlayProgress {

    private val main = Handler(Looper.getMainLooper())
    private var textView: TextView? = null
    private var wm: WindowManager? = null
    private val autoDismiss = Runnable { removeViews() }

    /** 取消回调：用户点「✕ 结束」时触发（AppAutomator 设它为置取消标志）。 */
    @Volatile
    var onCancel: (() -> Unit)? = null

    /** 取消后压制后续 update，防"已结束"又把浮窗弹回来。每次新任务 startSession 重置。 */
    @Volatile
    private var suppressed = false

    /** 新任务开始：装取消回调、解除压制。 */
    fun startSession(cancel: () -> Unit) { onCancel = cancel; suppressed = false }

    /** 任务结束：卸回调、收浮窗。 */
    fun endSession() { onCancel = null; hide() }

    fun canShow(@Suppress("UNUSED_PARAMETER") ctx: Context): Boolean =
        LingxiAccessibilityService.instance != null

    /** 更新进度文字（首次自动显示）。内部 post 到主线程。8s 无更新自动消失。 */
    fun update(@Suppress("UNUSED_PARAMETER") ctx: Context, text: String) {
        if (text.isBlank() || suppressed) return
        val overlayCtx = LingxiAccessibilityService.instance ?: return
        main.post {
            ensureViews(overlayCtx)
            textView?.text = text
            main.removeCallbacks(autoDismiss)
            main.postDelayed(autoDismiss, 8000)
        }
    }

    fun hide() = main.post { main.removeCallbacks(autoDismiss); removeViews() }

    private fun ensureViews(app: Context) {
        if (textView != null) return
        val dp = app.resources.displayMetrics.density
        val w = app.getSystemService(Context.WINDOW_SERVICE) as WindowManager

        // ① 进度文字窗（透传，不挡点击）
        val tv = TextView(app).apply {
            setTextColor(Color.WHITE)
            text = "长按此框可结束任务"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            maxLines = 3
            ellipsize = android.text.TextUtils.TruncateAt.END
            val p = (12 * dp).toInt()
            setPadding(p, (8 * dp).toInt(), p, (8 * dp).toInt())
            background = GradientDrawable().apply {
                cornerRadius = 18 * dp; setColor(Color.parseColor("#E6202124"))
            }
            setOnLongClickListener {
                onCancel?.invoke()
                text = "已结束"
                true
            }
        }
        val lpText = WindowManager.LayoutParams(
            (300 * dp).toInt(), WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT,
        ).apply { gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL; y = (150 * dp).toInt() }

        runCatching { w.addView(tv, lpText) }
            .onSuccess { textView = tv; wm = w }
            .onFailure { textView = null; wm = null }
    }

    private fun removeViews() {
        textView?.let { v -> runCatching { wm?.removeView(v) } }
        textView = null; wm = null
    }
}
