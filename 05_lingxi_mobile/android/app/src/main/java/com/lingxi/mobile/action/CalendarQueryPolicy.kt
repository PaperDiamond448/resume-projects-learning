package com.lingxi.mobile.action

import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * 日程查询的纯逻辑边界：计算本地日期窗，并把 CalendarContract 返回的事件压成简短应答。
 *
 * 不持有 Context、不联网，便于 JVM 单测冻结“按自然日查询、最多播报五项”的契约。
 */
object CalendarQueryPolicy {

    data class Range(
        val dayOffset: Int,
        val days: Int,
        val startMillis: Long,
        val endMillis: Long,
        val label: String,
    )

    data class Event(
        val title: String,
        val beginMillis: Long,
        val endMillis: Long,
        val allDay: Boolean,
    )

    fun range(
        requestedOffset: Int,
        requestedDays: Int,
        nowMillis: Long = System.currentTimeMillis(),
        timeZone: TimeZone = TimeZone.getDefault(),
    ): Range {
        val offset = requestedOffset.coerceIn(0, 30)
        val days = requestedDays.coerceIn(1, 7)
        val start = Calendar.getInstance(timeZone).apply {
            timeInMillis = nowMillis
            set(Calendar.HOUR_OF_DAY, 0)
            set(Calendar.MINUTE, 0)
            set(Calendar.SECOND, 0)
            set(Calendar.MILLISECOND, 0)
            add(Calendar.DAY_OF_YEAR, offset)
        }
        val end = (start.clone() as Calendar).apply { add(Calendar.DAY_OF_YEAR, days) }
        val oneDayLabel = when (offset) {
            0 -> "今天"
            1 -> "明天"
            2 -> "后天"
            else -> SimpleDateFormat("M月d日", Locale.CHINA).apply {
                this.timeZone = timeZone
            }.format(Date(start.timeInMillis))
        }
        val label = if (days == 1) oneDayLabel else "${oneDayLabel}起${days}天"
        return Range(offset, days, start.timeInMillis, end.timeInMillis, label)
    }

    fun summarize(
        events: List<Event>,
        range: Range,
        timeZone: TimeZone = TimeZone.getDefault(),
        limit: Int = 5,
    ): String {
        if (events.isEmpty()) return "${range.label}没有查到日程。"
        val ordered = events.sortedBy { it.beginMillis }
        val formatter = SimpleDateFormat(
            if (range.days == 1) "HH:mm" else "M月d日 HH:mm",
            Locale.CHINA,
        ).apply { this.timeZone = timeZone }
        val shown = ordered.take(limit.coerceAtLeast(1)).map { event ->
            val title = event.title.trim().ifBlank { "未命名日程" }
            val whenText = if (event.allDay) "全天" else formatter.format(Date(event.beginMillis))
            "$whenText $title"
        }
        val remaining = ordered.size - shown.size
        val suffix = if (remaining > 0) "；另有${remaining}项" else ""
        return "${range.label}共有${ordered.size}项日程：${shown.joinToString("；")}$suffix。"
    }
}
