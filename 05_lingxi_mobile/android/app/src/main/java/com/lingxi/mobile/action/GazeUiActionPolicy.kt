package com.lingxi.mobile.action

/**
 * “看着某个控件说打开这个”的端侧安全入口。
 *
 * 只接收极窄的浏览/进入类话术；支付、删除、发送、下单等可能产生外部影响的目标
 * 永远不靠注视直接点击。注视只负责消解“这个”，不承担高风险授权。
 */
object GazeUiActionPolicy {

    private val directOpen = Regex(
        "^(?:(?:请|麻烦)(?:你)?|帮我|给我|我想)*" +
            "(?:打开|点开|点击|进入|查看|看看)(?:一下)?" +
            "(?:这个|这一个|那个|那一个|它|他|她)(?:一下)?(?:吧|啊|呀|呢)?$"
    )
    private val separators = Regex("[\\s，,。.!！?？:：；;、\\\"“”'‘’]+")
    private val touchAssistNegative = Regex(
        "(?:不要|别|取消|停止|算了|不想|不能|无法|没有|没|打不开|开不了|为什么|怎么|失败)"
    )
    private val touchAssistOpen = Regex("(?:打开|点开|点击|进入|查看|看看)")
    private val touchAssistDeictic = Regex("(?:这个|这一个|那个|那一个|它|他|她)")

    private val blockedTargetWords = listOf(
        "支付", "付款", "转账", "购买", "立即购买", "下单", "提交订单",
        "发送", "发出", "拨打", "删除", "清空", "移除", "注销", "卸载",
        "确认", "授权", "允许", "安装", "兑换", "领取",
    )

    private val ambiguousTargets = setOf(
        "确定", "继续", "下一步", "完成", "更多", "按钮", "图标", "这里", "这个", "那个",
    )

    fun isDirectOpenCommand(text: String): Boolean {
        val normalized = normalize(text)
        return directOpen.matches(normalized)
    }

    /**
     * 仅供用户显式开启的“触控辅助演示”模式使用。
     *
     * 端侧流式 ASR 偶尔会把“打开这个”截成“打开”或“这个”。触控模式已经由用户
     * 主动把圆点放到目标上，后续仍需通过 [isSafeTarget]，因此允许命令的任意一半
     * 作为低风险点击触发；出现否定、取消或故障询问词时一律不触发。
     */
    fun isTouchAssistFallbackCommand(text: String): Boolean {
        val normalized = normalize(text)
        if (normalized.isBlank() || normalized.length > 18) return false
        if (touchAssistNegative.containsMatchIn(normalized)) return false
        return touchAssistOpen.containsMatchIn(normalized) ||
            touchAssistDeictic.containsMatchIn(normalized)
    }

    private fun normalize(text: String): String = text.trim()
        .replace(separators, "")
        .replace(Regex("^(?:你好)?灵犀"), "")

    fun isSafeTarget(target: String): Boolean {
        val normalized = target.trim()
        if (normalized.length !in 2..40) return false
        if (normalized in ambiguousTargets) return false
        return blockedTargetWords.none { normalized.contains(it) }
    }
}
