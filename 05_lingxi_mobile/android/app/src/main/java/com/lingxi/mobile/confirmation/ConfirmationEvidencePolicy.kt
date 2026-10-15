package com.lingxi.mobile.confirmation

import com.lingxi.mobile.model.VisionMark

/** The real text channel that produced a confirmation answer. */
enum class ConfirmationTextOrigin(val wireValue: String) {
    SPOKEN("spoken"),
    TYPED("typed"),
}

data class ConfirmationEvidence(
    val normalizedText: String,
    val source: String,
)

/**
 * Pure, deterministic confirmation-source mapping shared by the Activity and KWS service.
 *
 * The most important invariant is that an empty/unsupported visual result never becomes an
 * affirmative confirmation. `victory` and expression-only marks are intentionally excluded
 * from high-risk authorization until they have independent production validation.
 */
object ConfirmationEvidencePolicy {
    private val visualAccept = setOf("nod", "thumb_up")
    private val visualReject = setOf("shake", "thumb_down")

    fun build(
        text: String?,
        origin: ConfirmationTextOrigin?,
        marks: List<VisionMark>,
    ): ConfirmationEvidence? {
        val normalized = text.orEmpty().trim()
        if (normalized.isNotEmpty()) {
            val base = origin?.wireValue ?: "unknown"
            val source = if (marks.isEmpty() || base == "unknown") base else "${base}_visual"
            return ConfirmationEvidence(normalized, source)
        }

        val labels = marks.asSequence().map { it.label }.toSet()
        return when {
            labels.any(visualReject::contains) -> ConfirmationEvidence("取消", "vision_only")
            labels.any(visualAccept::contains) -> ConfirmationEvidence("确认", "vision_only")
            else -> null
        }
    }

    fun isExplicitConfirmationText(text: String): Boolean {
        val compact = text.filterNot(Char::isWhitespace)
        return listOf(
            "确认", "确认发送", "确认执行", "确认拨打", "确认发出",
            "可以发送", "可以拨打", "同意发送", "同意拨打",
            "发出去", "发送吧", "拨打吧",
        ).any(compact::contains)
    }

    fun isExplicitRejectionText(text: String): Boolean {
        val compact = text.filterNot(Char::isWhitespace)
        return listOf(
            "不要", "不用", "别发", "不发", "取消", "停止", "算了",
            "别打", "不要拨打", "不要发送", "取消拨打", "取消发送",
        ).any(compact::contains)
    }

    /** Only explicit authorization answers receive pending IDs and a typed source. */
    fun isExplicitPendingResponseText(text: String): Boolean =
        isExplicitConfirmationText(text) || isExplicitRejectionText(text)
}
