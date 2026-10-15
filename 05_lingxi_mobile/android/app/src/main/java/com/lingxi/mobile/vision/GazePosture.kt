package com.lingxi.mobile.vision

import kotlin.math.abs

/**
 * 注视追踪的姿势质量门控。
 *
 * MediaPipe 只告诉我们"有人脸/有 blendshape"，但注视校准对距离、居中、头姿非常敏感。
 * 这里用人脸网格的归一化包围框 + 头姿角做一个 Apple 式的轻量引导：脸放进中间圈、距离适中、头别偏。
 */
object GazePosture {

    data class Quality(
        val ok: Boolean,
        val message: String,
        val centerX: Float = 0.5f,
        val centerY: Float = 0.5f,
        val faceWidth: Float = 0f,
        val faceHeight: Float = 0f,
    )

    val NO_FACE = Quality(false, "请把脸放进中间圆圈")

    private const val TARGET_CENTER_X = 0.50f
    private const val TARGET_CENTER_Y = 0.48f
    private const val MAX_CENTER_DX = 0.22f
    private const val MAX_CENTER_DY = 0.24f
    private const val MIN_FACE_HEIGHT = 0.28f
    private const val MAX_FACE_HEIGHT = 0.62f
    private const val MAX_ABS_YAW_DEG = 18f
    private const val MAX_ABS_PITCH_DEG = 20f

    fun evaluate(lm: FloatArray?, hyaw: Float, hpitch: Float, faceFound: Boolean = true): Quality {
        if (!faceFound || lm == null || lm.size < 4) return NO_FACE

        var minX = 1f
        var minY = 1f
        var maxX = 0f
        var maxY = 0f
        var i = 0
        while (i < lm.size - 1) {
            val x = lm[i].coerceIn(0f, 1f)
            val y = lm[i + 1].coerceIn(0f, 1f)
            if (x < minX) minX = x
            if (x > maxX) maxX = x
            if (y < minY) minY = y
            if (y > maxY) maxY = y
            i += 2
        }

        val w = maxX - minX
        val h = maxY - minY
        val cx = (minX + maxX) / 2f
        val cy = (minY + maxY) / 2f

        if (h < MIN_FACE_HEIGHT) return Quality(false, "靠近一点", cx, cy, w, h)
        if (h > MAX_FACE_HEIGHT) return Quality(false, "离远一点", cx, cy, w, h)
        if (abs(cx - TARGET_CENTER_X) > MAX_CENTER_DX || abs(cy - TARGET_CENTER_Y) > MAX_CENTER_DY) {
            return Quality(false, "把脸放进中间圆圈", cx, cy, w, h)
        }
        if (abs(hyaw) > MAX_ABS_YAW_DEG || abs(hpitch) > MAX_ABS_PITCH_DEG) {
            return Quality(false, "头摆正一点", cx, cy, w, h)
        }
        return Quality(true, "姿势合适", cx, cy, w, h)
    }
}
