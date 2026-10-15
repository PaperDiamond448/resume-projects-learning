package com.lingxi.mobile.vision

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 注视指代融合的纯逻辑回归（JVM 跑、无需真机）。
 * 模拟"微信通讯录 3 个联系人行"，验证粗注视吸附到正确的人 → 解出"他"是谁。
 */
class GazeDeixisTest {

    // 1080 宽，3 行联系人，每行高 160px（典型列表行，远大于眼动 ~1.7cm 误差）
    private val contacts = listOf(
        ScreenElement("张伟", Bounds(0, 100, 1080, 260), clickable = true),
        ScreenElement("李娜", Bounds(0, 260, 1080, 420), clickable = true),
        ScreenElement("王芳", Bounds(0, 420, 1080, 580), clickable = true),
    )

    @Test
    fun gaze_inside_row_resolves_that_contact() {
        // 注视落在中间那行 → 李娜
        assertEquals("李娜", GazeDeixis.resolve(540f, 340f, contacts)?.text)
        // 第一行
        assertEquals("张伟", GazeDeixis.resolve(300f, 180f, contacts)?.text)
        // 第三行
        assertEquals("王芳", GazeDeixis.resolve(800f, 500f, contacts)?.text)
    }

    @Test
    fun gaze_slightly_off_snaps_to_nearest_within_threshold() {
        // 注视掉到列表下方一点点（y=620，距王芳底 40px）→ 仍吸附到王芳
        assertEquals("王芳", GazeDeixis.resolve(540f, 620f, contacts)?.text)
    }

    @Test
    fun gaze_far_away_returns_null_not_a_wild_guess() {
        // 注视在很远的角落（超过阈值）→ 不猜，返回 null（交大脑澄清）
        assertNull(GazeDeixis.resolve(540f, 1600f, contacts))
    }

    @Test
    fun nested_elements_pick_the_most_specific() {
        // 一个大卡片里套着小文字 → 落入时取最小（最具体）那个
        val nested = listOf(
            ScreenElement("整张卡片", Bounds(0, 0, 1080, 400)),
            ScreenElement("标题：降噪耳机", Bounds(40, 40, 700, 120)),
        )
        assertEquals("标题：降噪耳机", GazeDeixis.resolve(200f, 80f, nested)?.text)
    }

    @Test
    fun deixis_detection() {
        assertTrue(GazeDeixis.hasDeixis("给他发消息"))
        assertTrue(GazeDeixis.hasDeixis("把这个加入购物车"))
        assertTrue(GazeDeixis.hasDeixis("打电话给她"))
        assertFalse(GazeDeixis.hasDeixis("打开微信"))
        assertFalse(GazeDeixis.hasDeixis("今天天气怎么样"))
    }

    @Test
    fun empty_elements_is_safe() {
        assertNull(GazeDeixis.resolve(100f, 100f, emptyList()))
    }

    @Test
    fun filter_keeps_clickable_name_like_rows_only() {
        val raw = listOf(
            ScreenElement("张伟", Bounds(0, 100, 1080, 260), clickable = true),      // 保留：可点+名字样式
            ScreenElement("8:32", Bounds(900, 100, 1080, 150), clickable = true),   // 排除：纯时间戳
            ScreenElement("99+", Bounds(950, 160, 1080, 200), clickable = true),    // 排除：纯数字角标
            ScreenElement("这是一段很长很长很长很长很长很长很长很长的聊天预览文字", Bounds(0, 260, 1080, 420), clickable = true), // 排除：过长
            ScreenElement("李娜", Bounds(0, 420, 1080, 580), clickable = false),     // 排除：不可点
        )
        val kept = GazeDeixis.filterCandidates(raw)
        assertEquals(listOf("张伟"), kept.map { it.text })
    }
}
