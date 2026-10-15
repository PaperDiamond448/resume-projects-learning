package com.lingxi.mobile.ui

import android.accessibilityservice.AccessibilityService
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PixelFormat
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager

/**
 * 注视追踪常开的悬浮反馈点（T-16 v2，苹果式 UX：开着的时候屏幕上有个半透明小圆点实时
 * 跟随注视点，让人当场看到"系统真的在追踪"；关掉总开关，点立即消失）。
 *
 * 画法照抄 [com.lingxi.mobile.vision.GazeOverlay]（调试界面同款：绿色实心圆+白色圆环=有脸，
 * 灰色=丢脸），只是加了透明度、且挂在 TYPE_ACCESSIBILITY_OVERLAY（复用 [OverlayProgress] 那套
 * 不依赖 SYSTEM_ALERT_WINDOW 的机制）而不是独立 Activity 的全屏 View。
 *
 * 全程 FLAG_NOT_TOUCHABLE——这是一个纯反馈指示器，绝不能拦截用户对底下 App 的真实操作。
 */
object GazeFeedbackOverlay {

    private val main = Handler(Looper.getMainLooper())
    private var dotView: DotView? = null
    private var wm: WindowManager? = null
    private var touchHandle: View? = null
    private var touchHandleLp: WindowManager.LayoutParams? = null

    /** 开总开关时调用：挂悬浮点。必须传 AccessibilityService（TYPE_ACCESSIBILITY_OVERLAY 要求）。 */
    fun show(service: AccessibilityService) {
        main.post {
            if (dotView != null) return@post
            val view = DotView(service)
            val lp = WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE,
                PixelFormat.TRANSLUCENT,
            )
            val w = service.getSystemService(Context.WINDOW_SERVICE) as WindowManager
            runCatching { w.addView(view, lp) }
                .onSuccess { dotView = view; wm = w }
        }
    }

    /** 每帧回调更新反馈点位置(nx,ny∈[0,1]，已是校准后坐标，与 GazeCamera 回调值同源)。 */
    fun update(nx: Float, ny: Float, face: Boolean) {
        dotView?.updateFromGaze(nx, ny, face)
    }

    /**
     * 明确标注的拍摄辅助：让用户直接拖动圆点。触控窗只覆盖圆点附近，屏幕其余区域仍可操作。
     * 该模式不把触控样本写成相机/眼动数据；输入来源由调用方单独记录。
     */
    fun setTouchAssist(
        service: AccessibilityService,
        enabled: Boolean,
        onMove: (Float, Float) -> Unit,
    ) {
        main.post {
            dotView?.setTouchAssist(enabled)
            removeTouchHandle()
            if (!enabled) return@post

            val w = wm ?: (service.getSystemService(Context.WINDOW_SERVICE) as WindowManager)
            val dm = service.resources.displayMetrics
            val sizePx = (112f * dm.density).toInt().coerceAtLeast(112)
            val lp = WindowManager.LayoutParams(
                sizePx,
                sizePx,
                WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                    WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT,
            ).apply {
                gravity = Gravity.TOP or Gravity.START
                x = (dm.widthPixels - sizePx) / 2
                y = (dm.heightPixels - sizePx) / 2
            }
            val handle = View(service).apply {
                setOnTouchListener { _, event ->
                    when (event.actionMasked) {
                        MotionEvent.ACTION_DOWN,
                        MotionEvent.ACTION_MOVE,
                        MotionEvent.ACTION_UP -> {
                            moveTouchHandle(event.rawX, event.rawY, onMove)
                            true
                        }
                        else -> false
                    }
                }
            }
            runCatching { w.addView(handle, lp) }
                .onSuccess {
                    touchHandle = handle
                    touchHandleLp = lp
                    this.wm = w
                    dotView?.updateManual(0.5f, 0.5f)
                    onMove(0.5f, 0.5f)
                }
        }
    }

    /** 关总开关/服务销毁时调用：立即移除悬浮点（进程级别，非只隐藏）。 */
    fun hide() {
        main.post {
            removeTouchHandle()
            dotView?.let { v -> runCatching { wm?.removeView(v) } }
            dotView = null; wm = null
        }
    }

    /**
     * 语音确认要点击圆点下方目标时，短暂把拖拽手柄设为不可触摸，让坐标手势穿到下层 App。
     * 全屏反馈层本来就是 FLAG_NOT_TOUCHABLE，因此只需处理这个 112dp 的拖拽窗。
     */
    fun temporarilyPassThrough(durationMs: Long = 900L) {
        val work = {
            val view = touchHandle
            val lp = touchHandleLp
            if (view != null && lp != null) {
                lp.flags = lp.flags or WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                runCatching { wm?.updateViewLayout(view, lp) }
                main.postDelayed({
                    val currentView = touchHandle
                    val currentLp = touchHandleLp
                    if (currentView != null && currentLp != null) {
                        currentLp.flags = currentLp.flags and
                            WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE.inv()
                        runCatching { wm?.updateViewLayout(currentView, currentLp) }
                    }
                }, durationMs)
            }
        }
        if (Looper.myLooper() == Looper.getMainLooper()) work() else main.post(work)
    }

    private fun moveTouchHandle(rawX: Float, rawY: Float, onMove: (Float, Float) -> Unit) {
        val view = touchHandle ?: return
        val lp = touchHandleLp ?: return
        val dm = view.resources.displayMetrics
        val nx = (rawX / dm.widthPixels).coerceIn(0f, 1f)
        val ny = (rawY / dm.heightPixels).coerceIn(0f, 1f)
        lp.x = (rawX - lp.width / 2f).toInt().coerceIn(0, (dm.widthPixels - lp.width).coerceAtLeast(0))
        lp.y = (rawY - lp.height / 2f).toInt().coerceIn(0, (dm.heightPixels - lp.height).coerceAtLeast(0))
        runCatching { wm?.updateViewLayout(view, lp) }
        dotView?.updateManual(nx, ny)
        onMove(nx, ny)
    }

    private fun removeTouchHandle() {
        touchHandle?.let { v -> runCatching { wm?.removeView(v) } }
        touchHandle = null
        touchHandleLp = null
    }

    private class DotView(context: Context) : View(context) {
        private var nx = 0.5f
        private var ny = 0.5f
        private var face = false
        private var touchAssist = false

        private val dot = Paint(Paint.ANTI_ALIAS_FLAG).apply { alpha = 170 }
        private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE; strokeWidth = 5f; color = Color.WHITE; alpha = 170
        }

        fun updateFromGaze(x: Float, y: Float, f: Boolean) {
            if (touchAssist) return
            nx = x; ny = y; face = f
            postInvalidate()
        }

        fun updateManual(x: Float, y: Float) {
            nx = x; ny = y; face = true
            postInvalidate()
        }

        fun setTouchAssist(on: Boolean) {
            touchAssist = on
            postInvalidate()
        }

        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            val cx = nx * width
            val cy = ny * height
            dot.color = when {
                touchAssist -> Color.parseColor("#FF9800")
                face -> Color.parseColor("#4CD964")
                else -> Color.parseColor("#888888")
            }
            canvas.drawCircle(cx, cy, 28f, dot)
            canvas.drawCircle(cx, cy, 40f, ring)
        }
    }
}
