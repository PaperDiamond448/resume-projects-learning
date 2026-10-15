package com.lingxi.mobile.action

/**
 * 教学演示的"页面签名"工具：从屏幕文本里提取【稳定结构 token】（去掉缓冲/加载/人数/时间等动态噪声）。
 *
 * 治 b站 直播卡自动播放预览把"正在缓冲"录进签名→回放误判失配。v1 里签名只做【辅助提示】，
 * 真正的自纠错靠回放时"点完屏幕有没有变化"（见 AppAutomator.replayDemo）。
 */
object DemoSig {
    private val DYNAMIC = Regex(
        "缓冲|加载|loading|buffering|正在|直播中|人气|点赞|弹幕|刚刚|今天|昨天|分钟前|小时前|天前|秒",
        RegexOption.IGNORE_CASE,
    )
    private val DYN_PREFIX = listOf("ACD_", "acd_")

    /** 提取稳定 token：丢动态关键词、纯数字/人数/时间串、ACD_ 噪声、超短串。 */
    fun stableTokens(text: String): Set<String> =
        text.split(Regex("\\s+"))
            .map { it.trim() }
            .filter { it.length >= 2 }
            .filterNot { t -> t.all { c -> c.isDigit() || c == '.' || c == ':' || c == '万' || c == '亿' || c == '+' || c == '%' } }
            .filterNot { t -> DYN_PREFIX.any { t.startsWith(it) } }
            .filterNot { DYNAMIC.containsMatchIn(it) }
            .toSet()

    /** 把屏幕文本压成稳定签名（存进演示步骤；限长控存储）。 */
    fun signature(text: String): String =
        stableTokens(text).take(60).joinToString(" ")

    /** 当前页 vs 教学时签名 的稳定 token 重合度；记录侧稳定 token <3（签名不可靠）→ 返 1f（不参与判定）。 */
    fun overlap(current: String, sig: String): Float {
        val tb = stableTokens(sig)
        if (tb.size < 3) return 1f
        val ta = stableTokens(current)
        if (ta.isEmpty()) return 0f
        return (ta intersect tb).size.toFloat() / tb.size
    }
}
