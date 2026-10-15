package com.lingxi.mobile.ui

import android.content.Context
import android.graphics.BlurMaskFilter
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.SweepGradient
import android.os.SystemClock
import android.util.AttributeSet
import android.view.View
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.abs

/**
 * 灵犀唤醒/聆听态的程序化视觉组件。
 *
 * 视觉参考来自用户给的灵动岛教程图：深色液态玻璃岛、中央色散球体、底部彩虹能量光带。
 * 不依赖静态切图，后续只要持续喂 [setVoiceEnergy] 就能接真实麦克风音量。
 */
class LingxiWakeIslandView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    private val density = resources.displayMetrics.density
    private val island = RectF()
    private val tmp = RectF()
    private val clipPath = Path()
    private val lightPath = Path()
    private val orbClip = Path()

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        letterSpacingCompat(0f)
    }

    private var targetEnergy = 0f
    private var displayEnergy = 0f
    private var listening = false
    private var prompt = GREETINGS.random()
    private var started = false

    /** 色散球配色：每次 View 创建（=每次开屏）从 [ORB_PALETTES] 里随机挑一套，灵感取自 metasidd/Orb 的预设命名。 */
    private val palette = ORB_PALETTES.random()

    private val frame = object : Runnable {
        override fun run() {
            displayEnergy += (targetEnergy - displayEnergy) * 0.18f
            invalidate()
            val needsAnotherFrame = listening || abs(targetEnergy - displayEnergy) >= 0.005f
            if (started && needsAnotherFrame) postDelayed(this, FRAME_INTERVAL_MS)
            else started = false
        }
    }

    init {
        setLayerType(LAYER_TYPE_SOFTWARE, null)
        isClickable = true
        contentDescription = "灵犀唤醒聆听入口"
    }

    fun setVoiceEnergy(value: Float) {
        targetEnergy = value.coerceIn(0f, 1f)
        if (!started) startFrames()
    }

    fun setListening(value: Boolean) {
        listening = value
        if (!value) targetEnergy = 0f
        startFrames()
        invalidate()
    }

    fun setPrompt(text: String) {
        prompt = text.ifBlank { "我能为你做什么？" }
        invalidate()
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        startFrames()
    }

    override fun onDetachedFromWindow() {
        started = false
        removeCallbacks(frame)
        super.onDetachedFromWindow()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        if (w <= 0f || h <= 0f) return

        val now = SystemClock.uptimeMillis() / 1000f
        val idlePulse = ((sin(now * 1.8f) + 1f) * 0.5f).toFloat()
        val energy = if (listening) max(displayEnergy, 0.08f + idlePulse * 0.05f) else displayEnergy * 0.35f

        drawSpace(canvas, w, h, now)
        layoutIsland(w, h)
        drawIslandShell(canvas, energy)

        canvas.save()
        clipPath.reset()
        clipPath.addRoundRect(island, dp(42f), dp(42f), Path.Direction.CW)
        canvas.clipPath(clipPath)
        drawIslandInterior(canvas, now, energy)
        canvas.restore()
    }

    private fun layoutIsland(w: Float, h: Float) {
        val side = min(w - dp(24f), dp(390f))
        val left = (w - side) / 2f
        val top = dp(12f)
        val bottom = min(h - dp(10f), top + max(dp(210f), h - dp(22f)))
        island.set(left, top, left + side, bottom)
    }

    private fun drawSpace(canvas: Canvas, w: Float, h: Float, t: Float) {
        fill.style = Paint.Style.FILL
        fill.shader = LinearGradient(
            0f, 0f, 0f, h,
            intArrayOf(Color.BLACK, Color.rgb(2, 5, 10), Color.BLACK),
            floatArrayOf(0f, 0.52f, 1f),
            Shader.TileMode.CLAMP,
        )
        canvas.drawRect(0f, 0f, w, h, fill)
        fill.shader = null

        fill.maskFilter = null
        for (i in 0 until 44) {
            val px = pseudo(i * 13 + 7) * w
            val py = pseudo(i * 29 + 3) * h
            val twinkle = (0.35f + 0.65f * ((sin(t * (0.6f + i * 0.013f) + i) + 1f) * 0.5f)).toFloat()
            fill.color = Color.argb((28 + 90 * twinkle).toInt(), 255, 255, 255)
            canvas.drawCircle(px, py, dp(0.6f + pseudo(i + 100) * 1.1f), fill)
        }
    }

    private fun drawIslandShell(canvas: Canvas, energy: Float) {
        fill.style = Paint.Style.FILL
        fill.maskFilter = BlurMaskFilter(dp(28f + energy * 10f), BlurMaskFilter.Blur.NORMAL)
        fill.color = Color.argb(120, 0, 0, 0)
        canvas.drawRoundRect(island, dp(42f), dp(42f), fill)
        fill.maskFilter = null

        fill.shader = LinearGradient(
            island.left, island.top, island.right, island.bottom,
            intArrayOf(Color.rgb(7, 9, 13), Color.rgb(2, 3, 6), Color.rgb(10, 14, 18)),
            floatArrayOf(0f, 0.48f, 1f),
            Shader.TileMode.CLAMP,
        )
        canvas.drawRoundRect(island, dp(42f), dp(42f), fill)
        fill.shader = null
    }

    private fun drawIslandInterior(canvas: Canvas, t: Float, energy: Float) {
        val cx = island.centerX()
        val top = island.top
        val bottom = island.bottom
        val iw = island.width()
        val ih = island.height()

        drawSoftGlass(canvas)
        drawAmbientDots(canvas, t)
        drawLowerTexture(canvas, cx, bottom - ih * 0.24f, iw, energy)
        drawBottomLightBand(canvas, t, energy)
        drawOrb(canvas, cx, top + ih * 0.32f, dp(34f) * (1f + energy * 0.12f), t, energy)
        drawListeningText(canvas, cx, top + ih * 0.58f, energy)
    }

    private fun drawSoftGlass(canvas: Canvas) {
        fill.style = Paint.Style.FILL
        fill.shader = LinearGradient(
            island.left, island.top, island.right, island.bottom,
            intArrayOf(
                Color.argb(190, 4, 5, 8),
                Color.argb(238, 0, 0, 0),
                Color.argb(185, 12, 22, 28),
            ),
            floatArrayOf(0f, 0.52f, 1f),
            Shader.TileMode.CLAMP,
        )
        canvas.drawRoundRect(island, dp(42f), dp(42f), fill)
        fill.shader = null

        fill.maskFilter = BlurMaskFilter(dp(18f), BlurMaskFilter.Blur.NORMAL)
        fill.color = Color.argb(42, 180, 210, 255)
        canvas.drawOval(
            island.left + island.width() * 0.04f,
            island.bottom - island.height() * 0.42f,
            island.left + island.width() * 0.38f,
            island.bottom - island.height() * 0.03f,
            fill,
        )
        fill.color = Color.argb(26, 255, 235, 210)
        canvas.drawOval(
            island.right - island.width() * 0.34f,
            island.bottom - island.height() * 0.38f,
            island.right - island.width() * 0.04f,
            island.bottom,
            fill,
        )
        fill.maskFilter = null
    }

    private fun drawAmbientDots(canvas: Canvas, t: Float) {
        val y0 = island.top + island.height() * 0.66f
        fill.maskFilter = null
        for (row in 0 until 5) {
            for (col in 0 until 36) {
                val alpha = (8 + 20 * pseudo(row * 41 + col)).toInt()
                val px = island.left + island.width() * (0.12f + col / 42f)
                val py = y0 + row * dp(5f) + sin(t + col * 0.2f).toFloat() * dp(0.8f)
                fill.color = Color.argb(alpha, 210, 220, 230)
                canvas.drawCircle(px, py, dp(0.65f), fill)
            }
        }
    }

    private fun drawBottomLightBand(canvas: Canvas, t: Float, energy: Float) {
        val baseY = island.bottom - island.height() * 0.16f
        val bandH = dp(5f + energy * 22f)
        val spread = island.width() * (0.46f + energy * 0.16f)
        val cx = island.centerX() + sin(t * 1.8f).toFloat() * dp(10f)

        drawGlowOval(canvas, cx, baseY + dp(4f), spread * 0.72f, bandH * 3.8f, Color.argb((72 + energy * 120).toInt(), 255, 245, 250), dp(22f))
        drawGlowOval(canvas, cx - spread * 0.16f, baseY + dp(3f), spread * 0.62f, bandH * 4.4f, Color.argb((42 + energy * 80).toInt(), 255, 80, 30), dp(26f))
        drawGlowOval(canvas, cx + spread * 0.18f, baseY + dp(3f), spread * 0.62f, bandH * 4.4f, Color.argb((38 + energy * 80).toInt(), 70, 160, 255), dp(26f))

        lightPath.reset()
        val left = cx - spread
        val right = cx + spread
        val mid = cx + sin(t * 1.2f).toFloat() * dp(18f)
        lightPath.moveTo(left, baseY)
        lightPath.cubicTo(left + spread * 0.28f, baseY - bandH, mid - spread * 0.2f, baseY - bandH * 1.2f, mid, baseY - bandH * 0.8f)
        lightPath.cubicTo(mid + spread * 0.2f, baseY - bandH * 0.45f, right - spread * 0.3f, baseY - bandH * 0.16f, right, baseY)
        lightPath.lineTo(right, baseY + bandH * 0.34f)
        lightPath.cubicTo(right - spread * 0.35f, baseY + bandH * 0.65f, left + spread * 0.35f, baseY + bandH * 0.65f, left, baseY + bandH * 0.22f)
        lightPath.close()

        fill.maskFilter = BlurMaskFilter(dp(4f), BlurMaskFilter.Blur.NORMAL)
        fill.shader = LinearGradient(
            left, baseY, right, baseY,
            intArrayOf(
                Color.rgb(255, 64, 46),
                Color.rgb(255, 230, 52),
                Color.WHITE,
                Color.rgb(78, 255, 185),
                Color.rgb(92, 156, 255),
                Color.rgb(160, 70, 255),
            ),
            null,
            Shader.TileMode.CLAMP,
        )
        canvas.drawPath(lightPath, fill)
        fill.shader = null
        fill.maskFilter = null

        stroke.style = Paint.Style.STROKE
        stroke.strokeWidth = dp(1.2f + energy * 1.8f)
        stroke.strokeCap = Paint.Cap.ROUND
        stroke.shader = LinearGradient(
            left, baseY, right, baseY,
            intArrayOf(Color.rgb(255, 95, 52), Color.rgb(255, 240, 80), Color.WHITE, Color.rgb(82, 255, 190), Color.rgb(96, 160, 255)),
            null,
            Shader.TileMode.CLAMP,
        )
        stroke.maskFilter = BlurMaskFilter(dp(2.2f), BlurMaskFilter.Blur.NORMAL)
        canvas.drawLine(left + spread * 0.08f, baseY + bandH * 0.02f, right - spread * 0.08f, baseY - bandH * 0.02f, stroke)
        stroke.shader = null
        stroke.maskFilter = null
    }

    private fun drawOrb(canvas: Canvas, cx: Float, cy: Float, r: Float, t: Float, energy: Float) {
        drawGlowOval(canvas, cx, cy, r * (1.5f + energy * 0.3f), r * (1.4f + energy * 0.3f), withAlpha(palette.glow, (36 + energy * 60).toInt()), dp(12f))

        orbClip.reset()
        orbClip.addCircle(cx, cy, r, Path.Direction.CW)
        canvas.save()
        canvas.clipPath(orbClip)

        fill.maskFilter = null
        fill.shader = RadialGradient(
            cx + r * 0.32f, cy - r * 0.3f, r * 1.35f,
            intArrayOf(
                withAlpha(lighten(palette.glow, 0.6f), 255),
                withAlpha(palette.primary, 255),
                withAlpha(darken(palette.secondary, 0.35f), 255),
                withAlpha(darken(palette.secondary, 0.7f), 255),
            ),
            floatArrayOf(0f, 0.42f, 0.8f, 1f),
            Shader.TileMode.CLAMP,
        )
        canvas.drawCircle(cx, cy, r, fill)

        fill.shader = SweepGradient(
            cx, cy,
            intArrayOf(
                Color.TRANSPARENT,
                withAlpha(palette.glow, 150),
                withAlpha(palette.secondary, 130),
                withAlpha(palette.primary, 150),
                withAlpha(palette.glow, 130),
                Color.TRANSPARENT,
            ),
            floatArrayOf(0f, 0.18f, 0.32f, 0.55f, 0.72f, 1f),
        )
        canvas.rotate((t * 18f) % 360f, cx, cy)
        canvas.drawCircle(cx, cy, r * 0.98f, fill)
        canvas.rotate(-((t * 18f) % 360f), cx, cy)
        fill.shader = null

        stroke.style = Paint.Style.STROKE
        stroke.strokeWidth = r * 0.14f
        stroke.strokeCap = Paint.Cap.ROUND
        stroke.maskFilter = BlurMaskFilter(dp(3f), BlurMaskFilter.Blur.NORMAL)
        stroke.shader = LinearGradient(
            cx - r * 0.9f, cy - r * 0.65f, cx + r * 0.88f, cy + r * 0.62f,
            intArrayOf(Color.TRANSPARENT, withAlpha(lighten(palette.glow, 0.5f), 220), withAlpha(lighten(palette.primary, 0.35f), 235), Color.TRANSPARENT),
            null,
            Shader.TileMode.CLAMP,
        )
        val arc = Path().apply {
            moveTo(cx - r * 0.55f, cy - r * 0.48f)
            cubicTo(cx - r * 0.18f, cy - r * 0.04f, cx + r * 0.1f, cy + r * 0.34f, cx + r * 0.86f, cy + r * 0.42f)
        }
        canvas.drawPath(arc, stroke)
        stroke.shader = null
        stroke.maskFilter = null
        canvas.restore()
    }

    private fun drawListeningText(canvas: Canvas, cx: Float, y: Float, energy: Float) {
        textPaint.typeface = android.graphics.Typeface.create(android.graphics.Typeface.SANS_SERIF, android.graphics.Typeface.BOLD)
        textPaint.textSize = sp(25f)
        textPaint.color = Color.argb((235 + energy * 20).toInt().coerceAtMost(255), 255, 255, 255)
        canvas.drawText(prompt, cx, y, textPaint)
    }

    private fun drawLowerTexture(canvas: Canvas, cx: Float, y: Float, width: Float, energy: Float) {
        val alpha = (10 + energy * 18).toInt()
        fill.maskFilter = BlurMaskFilter(dp(18f), BlurMaskFilter.Blur.NORMAL)
        fill.color = Color.argb(alpha, 120, 220, 255)
        canvas.drawOval(cx - width * 0.42f, y - dp(34f), cx - width * 0.02f, y + dp(42f), fill)
        fill.color = Color.argb(alpha, 255, 110, 70)
        canvas.drawOval(cx + width * 0.03f, y - dp(34f), cx + width * 0.42f, y + dp(42f), fill)
        fill.maskFilter = null
    }

    private fun drawGlowOval(canvas: Canvas, cx: Float, cy: Float, rx: Float, ry: Float, color: Int, blur: Float) {
        fill.style = Paint.Style.FILL
        fill.color = color
        fill.maskFilter = BlurMaskFilter(blur, BlurMaskFilter.Blur.NORMAL)
        tmp.set(cx - rx, cy - ry, cx + rx, cy + ry)
        canvas.drawOval(tmp, fill)
        fill.maskFilter = null
    }

    private fun startFrames() {
        if (started) return
        started = true
        removeCallbacks(frame)
        postDelayed(frame, FRAME_INTERVAL_MS)
    }

    private fun dp(v: Float) = v * density
    private fun sp(v: Float) = v * resources.displayMetrics.scaledDensity

    private fun pseudo(seed: Int): Float {
        val x = sin(seed * 12.9898 + 78.233) * 43758.5453
        return (x - kotlin.math.floor(x)).toFloat()
    }

    private fun Paint.letterSpacingCompat(@Suppress("UNUSED_PARAMETER") value: Float) {
        // Paint has no letterSpacing before using TextPaint; keep text default spacing intentionally.
    }

    private fun withAlpha(color: Int, alpha: Int): Int =
        Color.argb(alpha.coerceIn(0, 255), Color.red(color), Color.green(color), Color.blue(color))

    private fun lighten(color: Int, amount: Float): Int {
        val r = Color.red(color); val g = Color.green(color); val b = Color.blue(color)
        return Color.rgb(
            (r + (255 - r) * amount).toInt().coerceIn(0, 255),
            (g + (255 - g) * amount).toInt().coerceIn(0, 255),
            (b + (255 - b) * amount).toInt().coerceIn(0, 255),
        )
    }

    private fun darken(color: Int, amount: Float): Int {
        val r = Color.red(color); val g = Color.green(color); val b = Color.blue(color)
        return Color.rgb(
            (r * (1f - amount)).toInt().coerceIn(0, 255),
            (g * (1f - amount)).toInt().coerceIn(0, 255),
            (b * (1f - amount)).toInt().coerceIn(0, 255),
        )
    }

    private data class OrbPalette(val label: String, val primary: Int, val secondary: Int, val glow: Int)

    private companion object {
        /** 软件模糊绘制在旧机上很贵；10fps 足够表达能量变化，避免首页空转吃满一个 CPU 核。 */
        const val FRAME_INTERVAL_MS = 100L

        /** 开屏问候语池：每次 View 创建随机挑一句，苹果 Siri 范儿的简短英文问候。 */
        val GREETINGS = listOf(
            "Hey, ready when you are",
            "Hi there, what's on your mind?",
            "Hello, how can I help?",
            "Ready whenever you are",
            "What can I do for you today?",
            "Let's get things done",
            "Good to see you again",
        )

        /** 配色灵感来自 metasidd/Orb（SwiftUI 库，仅供设计参考，非直接依赖）的预设命名与配色思路。 */
        val ORB_PALETTES = listOf(
            OrbPalette("Mystic", Color.rgb(139, 92, 246), Color.rgb(109, 93, 252), Color.rgb(167, 139, 250)),
            OrbPalette("Nature", Color.rgb(16, 185, 129), Color.rgb(52, 211, 153), Color.rgb(110, 231, 183)),
            OrbPalette("Sunset", Color.rgb(249, 115, 22), Color.rgb(239, 68, 68), Color.rgb(251, 191, 36)),
            OrbPalette("Ocean", Color.rgb(6, 182, 212), Color.rgb(59, 130, 246), Color.rgb(103, 232, 249)),
            OrbPalette("Minimal", Color.rgb(203, 209, 219), Color.rgb(235, 238, 242), Color.rgb(255, 255, 255)),
            OrbPalette("Cosmic", Color.rgb(236, 72, 153), Color.rgb(139, 92, 246), Color.rgb(244, 114, 182)),
            OrbPalette("Fire", Color.rgb(245, 158, 11), Color.rgb(251, 146, 60), Color.rgb(253, 230, 138)),
            OrbPalette("Arctic", Color.rgb(96, 165, 250), Color.rgb(191, 219, 254), Color.rgb(224, 242, 254)),
            OrbPalette("Shadow", Color.rgb(107, 114, 128), Color.rgb(55, 65, 81), Color.rgb(156, 163, 175)),
        )
    }
}
