package com.lingxi.mobile.vision

import com.lingxi.mobile.vision.HeadMotionDetector.HeadSample
import com.lingxi.mobile.vision.HeadMotionDetector.Motion
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 点头/摇头判别的纯逻辑回归（JVM 跑、无需真机）。
 * 喂合成头姿序列，验证：真点头→NOD、真摇头→SHAKE、静止/一次性看一眼→NONE（不误报）。
 */
class HeadMotionDetectorTest {

    private fun seq(pitchYaw: List<Pair<Float, Float>>): List<HeadSample> =
        pitchYaw.mapIndexed { i, (p, y) -> HeadSample(i * 60L, p, y) }

    @Test
    fun clear_nod_is_detected() {
        // pitch 下-上振荡（低头再抬头），yaw 基本不动
        val s = seq(listOf(0f to 0f, -6f to 1f, -12f to 0f, -16f to 1f, -9f to 0f, -3f to 1f, 0f to 0f))
        val r = HeadMotionDetector.classify(s)
        assertEquals(Motion.NOD, r.motion)
        assertEquals("nod", r.label())
        assertTrue("置信度应较高: ${r.confidence}", r.confidence > 0.5f)
    }

    @Test
    fun clear_shake_is_detected() {
        // yaw 左-右-左振荡，pitch 基本不动
        val s = seq(listOf(0f to 0f, 1f to -10f, 0f to 2f, -1f to 12f, 0f to -2f, 1f to -11f, 0f to 0f))
        val r = HeadMotionDetector.classify(s)
        assertEquals(Motion.SHAKE, r.motion)
        assertEquals("shake", r.label())
        assertTrue(r.yawReversals >= 2)
    }

    @Test
    fun single_side_move_and_partial_return_is_not_a_shake() {
        // V02R2 同型：举手时脸从中心侧移、略微回摆，只形成一次反转；不能当明确摇头取消。
        val s = seq(listOf(
            0f to 0f, 0f to 9f, 0f to 18f, 0f to 28f, 0f to 24f, 0f to 20f,
        ))
        val r = HeadMotionDetector.classify(s)
        assertEquals(1, r.yawReversals)
        assertEquals(Motion.NONE, r.motion)
    }

    @Test
    fun nod_event_bounds_exclude_static_prefix_and_suffix() {
        val values = buildList {
            repeat(8) { add(0f to 0f) }
            addAll(listOf(0f to 0f, -6f to 1f, -12f to 0f, -16f to 1f,
                -9f to 0f, -3f to 1f, 0f to 0f))
            repeat(8) { add(0f to 0f) }
        }
        val samples = seq(values)
        val result = HeadMotionDetector.classify(samples)
        val bounds = HeadMotionDetector.eventBounds(samples, result.motion)
        assertEquals(Motion.NOD, result.motion)
        assertTrue(bounds != null && bounds.localized)
        assertTrue("不应从整窗起点开始: $bounds", bounds!!.startMs > samples.first().tMs)
        assertTrue("不应延伸到整窗终点: $bounds", bounds.endMs < samples.last().tMs)
    }

    @Test
    fun static_face_is_none() {
        // 只有轻微抖动 → 不该误报
        val s = seq(listOf(0f to 0f, 1f to -1f, -1f to 1f, 0f to 0f, 1f to 1f, -1f to -1f, 0f to 0f))
        val r = HeadMotionDetector.classify(s)
        assertEquals(Motion.NONE, r.motion)
        assertNull(r.label())
    }

    @Test
    fun single_look_down_without_recovery_is_not_a_nod() {
        // 一次性低头并保持（无回正/无反转）→ 不算点头
        val s = seq(listOf(0f to 0f, -5f to 0f, -10f to 1f, -15f to 0f, -15f to 0f, -15f to 1f))
        val r = HeadMotionDetector.classify(s)
        assertEquals(Motion.NONE, r.motion)
    }

    @Test
    fun too_few_samples_is_none() {
        assertEquals(Motion.NONE, HeadMotionDetector.classify(seq(listOf(0f to 0f, -10f to 0f))).motion)
    }

    @Test
    fun gesture_label_mapping() {
        assertEquals("thumb_up", GestureLabels.toMark("Thumb_Up"))
        assertEquals("thumb_down", GestureLabels.toMark("Thumb_Down"))
        assertEquals("victory", GestureLabels.toMark("Victory"))
        assertNull(GestureLabels.toMark("Open_Palm"))
        assertNull(GestureLabels.toMark("None"))
        assertNull(GestureLabels.toMark(null))
    }

    @Test
    fun expression_from_blendshapes() {
        // 明显微笑 → positive
        val smile = mapOf("mouthSmileLeft" to 0.7f, "mouthSmileRight" to 0.65f,
            "browDownLeft" to 0.05f, "browDownRight" to 0.05f)
        val (e1, _) = ExpressionDetector.classify(smile)
        assertEquals(ExpressionDetector.Expr.POSITIVE, e1)
        assertEquals("positive", ExpressionDetector.label(e1))

        // 压眉/皱眉 → negative
        val frown = mapOf("browDownLeft" to 0.6f, "browDownRight" to 0.55f,
            "mouthSmileLeft" to 0.05f, "mouthSmileRight" to 0.05f)
        val (e2, _) = ExpressionDetector.classify(frown)
        assertEquals(ExpressionDetector.Expr.NEGATIVE, e2)

        // 中性 → neutral（不上报）
        val neutral = mapOf("mouthSmileLeft" to 0.1f, "browDownLeft" to 0.1f)
        val (e3, _) = ExpressionDetector.classify(neutral)
        assertEquals(ExpressionDetector.Expr.NEUTRAL, e3)
        assertNull(ExpressionDetector.label(e3))
    }
}
