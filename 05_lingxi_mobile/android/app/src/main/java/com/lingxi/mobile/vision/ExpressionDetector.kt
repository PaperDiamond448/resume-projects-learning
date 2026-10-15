package com.lingxi.mobile.vision

/**
 * 表情极性判别（纯逻辑、从 MediaPipe FaceLandmarker 的 52 个 blendshape 分数算）。
 *
 * 第三路【弱】应答模态：微笑→positive、皱眉/压眉→negative，对齐 brain/fusion/conflict.py 的
 * `("expression","positive"/"negative")`。表情**权重低于**点头/摇头/手势（conflict.py 的 STRONG_VISION
 * 不含 expression）——因为表情可能是"底色情绪"而非对当前问题的应答，故只当辅助证据。
 *
 * 隐私：只吃 blendshape 数字（端侧算），不出端人脸图像（守 D5）。
 * blendshape 名取自 MediaPipe face_landmarker 输出（mouthSmileLeft/Right、browDownLeft/Right、mouthFrownLeft/Right）。
 */
object ExpressionDetector {

    enum class Expr { POSITIVE, NEGATIVE, NEUTRAL }

    private const val SMILE_T = 0.40f
    private const val NEG_T = 0.40f

    /** 返回 (极性, 强度)。强度=主导 blendshape 分数，用作置信度参考。 */
    fun classify(bs: Map<String, Float>): Pair<Expr, Float> {
        val smile = avg(bs["mouthSmileLeft"], bs["mouthSmileRight"])
        val frown = avg(bs["mouthFrownLeft"], bs["mouthFrownRight"])
        val browDown = avg(bs["browDownLeft"], bs["browDownRight"])
        val neg = maxOf(frown, browDown)
        return when {
            smile >= SMILE_T && smile >= neg -> Expr.POSITIVE to smile
            neg >= NEG_T && neg > smile -> Expr.NEGATIVE to neg
            else -> Expr.NEUTRAL to 0f
        }
    }

    /** 大脑契约里的 expression label；NEUTRAL→null（不作为冲突证据上报）。 */
    fun label(e: Expr): String? = when (e) {
        Expr.POSITIVE -> "positive"
        Expr.NEGATIVE -> "negative"
        Expr.NEUTRAL -> null
    }

    private fun avg(a: Float?, b: Float?): Float = ((a ?: 0f) + (b ?: 0f)) / 2f
}
