package com.lingxi.mobile.action

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Calendar
import java.util.TimeZone

class CalendarQueryPolicyTest {

    private val utc = TimeZone.getTimeZone("UTC")
    private val now = Calendar.getInstance(utc).apply {
        set(2026, Calendar.JULY, 16, 13, 45, 12)
        set(Calendar.MILLISECOND, 345)
    }.timeInMillis

    @Test
    fun `tomorrow range follows local natural day`() {
        val range = CalendarQueryPolicy.range(1, 1, now, utc)
        val start = Calendar.getInstance(utc).apply { timeInMillis = range.startMillis }
        val end = Calendar.getInstance(utc).apply { timeInMillis = range.endMillis }

        assertEquals("明天", range.label)
        assertEquals(17, start.get(Calendar.DAY_OF_MONTH))
        assertEquals(0, start.get(Calendar.HOUR_OF_DAY))
        assertEquals(18, end.get(Calendar.DAY_OF_MONTH))
        assertEquals(24L * 60L * 60L * 1000L, range.endMillis - range.startMillis)
    }

    @Test
    fun `range clamps unreasonable values`() {
        val range = CalendarQueryPolicy.range(-9, 99, now, utc)
        assertEquals(0, range.dayOffset)
        assertEquals(7, range.days)
        assertEquals("今天起7天", range.label)
    }

    @Test
    fun `empty calendar is explicit`() {
        val range = CalendarQueryPolicy.range(0, 1, now, utc)
        assertEquals("今天没有查到日程。", CalendarQueryPolicy.summarize(emptyList(), range, utc))
    }

    @Test
    fun `summary sorts limits and reports remaining count`() {
        val range = CalendarQueryPolicy.range(0, 1, now, utc)
        val events = (0 until 7).reversed().map { index ->
            CalendarQueryPolicy.Event(
                title = if (index == 0) "" else "事项$index",
                beginMillis = range.startMillis + (8 + index) * 60L * 60L * 1000L,
                endMillis = range.startMillis + (9 + index) * 60L * 60L * 1000L,
                allDay = false,
            )
        }
        val result = CalendarQueryPolicy.summarize(events, range, utc)

        assertTrue(result.startsWith("今天共有7项日程：08:00 未命名日程；09:00 事项1"))
        assertTrue(result.endsWith("；另有2项。"))
    }

    @Test
    fun `all day event does not invent a clock time`() {
        val range = CalendarQueryPolicy.range(1, 1, now, utc)
        val result = CalendarQueryPolicy.summarize(
            listOf(CalendarQueryPolicy.Event("纪念日", range.startMillis, range.endMillis, true)),
            range,
            utc,
        )
        assertEquals("明天共有1项日程：全天 纪念日。", result)
    }
}
