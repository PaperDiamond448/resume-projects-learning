package com.lingxi.mobile.vision

import com.lingxi.mobile.vision.GazeVote.Sample
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 注视目标投票的纯逻辑回归（JVM 跑、无需真机）。
 * 喂合成的逐帧解析结果序列，验证：稳定注视→稳定命中，抖动/半信半疑→不瞎猜返回 null。
 */
class GazeVoteTest {

    private fun seq(targets: List<String?>): List<Sample> =
        targets.mapIndexed { i, t -> Sample(i * 100L, t) }

    @Test
    fun steady_gaze_locks_onto_target() {
        // 10 帧里 9 帧都看着"张伟"，1 帧丢脸 → 稳定命中
        val s = seq(listOf("张伟", "张伟", "张伟", null, "张伟", "张伟", "张伟", "张伟", "张伟", "张伟"))
        val r = GazeVote.vote(s)
        assertEquals("张伟", r?.target)
        assertTrue("置信度应较高: ${r?.confidence}", (r?.confidence ?: 0f) > 0.8f)
    }

    @Test
    fun t0_t1_span_first_to_last_hit() {
        val s = seq(listOf("李娜", "李娜", "李娜", "李娜", "李娜"))
        val r = GazeVote.vote(s)
        assertEquals(0L, r?.t0Ms)
        assertEquals(400L, r?.t1Ms)
    }

    @Test
    fun flickering_between_two_targets_is_not_stable() {
        // 注视在两人之间来回跳，谁都没占到稳定多数 → 不该瞎猜
        val s = seq(listOf("张伟", "李娜", "张伟", "李娜", "张伟", "李娜"))
        assertNull(GazeVote.vote(s))
    }

    @Test
    fun mostly_no_face_with_occasional_glance_is_not_stable() {
        // 大半时间没解出目标(丢脸/没落入候选)，偶尔瞄一眼"王芳" → 占比不够，不算稳定注视
        val s = seq(listOf(null, null, null, "王芳", null, null, null, null))
        assertNull(GazeVote.vote(s))
    }

    @Test
    fun too_few_samples_returns_null() {
        assertNull(GazeVote.vote(seq(listOf("张伟", "张伟"))))
    }

    @Test
    fun all_null_returns_null() {
        assertNull(GazeVote.vote(seq(listOf(null, null, null, null))))
    }

    @Test
    fun exact_majority_confidence_matches_ratio() {
        // 4/5 帧命中"张伟" → confidence 应约等于 0.8
        val s = seq(listOf("张伟", "张伟", "张伟", "张伟", "李娜"))
        val r = GazeVote.vote(s)
        assertEquals("张伟", r?.target)
        assertEquals(0.8f, r?.confidence ?: 0f, 0.001f)
    }
}
