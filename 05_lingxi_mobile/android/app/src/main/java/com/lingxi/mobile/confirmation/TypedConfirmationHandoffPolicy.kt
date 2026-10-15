package com.lingxi.mobile.confirmation

enum class TypedConfirmationHandoffDecision {
    ACCEPT,
    NOT_KWS_OWNED,
    INVALID_TEXT,
    BINDING_MISMATCH,
    WINDOW_CLOSED,
}

data class ResolvedConfirmationText(
    val text: String,
    val origin: ConfirmationTextOrigin?,
)

/** Pure rules for handing typed evidence into a confirmation window owned by KWS. */
object TypedConfirmationHandoffPolicy {
    fun decide(
        text: String,
        requestedPendingId: String?,
        requestedWindowId: String?,
        storedBinding: PendingConfirmationBinding?,
        activePendingId: String?,
        activeWindowId: String?,
        serviceRunning: Boolean,
        acceptingWindow: Boolean,
    ): TypedConfirmationHandoffDecision {
        if (!ConfirmationEvidencePolicy.isExplicitPendingResponseText(text)) {
            return TypedConfirmationHandoffDecision.INVALID_TEXT
        }
        if (storedBinding?.owner != PendingConfirmationOwner.KWS) {
            return TypedConfirmationHandoffDecision.NOT_KWS_OWNED
        }
        val matches = !requestedPendingId.isNullOrBlank() &&
            !requestedWindowId.isNullOrBlank() &&
            requestedPendingId == storedBinding.pendingId &&
            requestedWindowId == storedBinding.windowId &&
            requestedPendingId == activePendingId &&
            requestedWindowId == activeWindowId
        if (!matches) return TypedConfirmationHandoffDecision.BINDING_MISMATCH
        if (!serviceRunning || !acceptingWindow) {
            return TypedConfirmationHandoffDecision.WINDOW_CLOSED
        }
        return TypedConfirmationHandoffDecision.ACCEPT
    }

    /**
     * Keep the real text origin and make rejection dominant across typed/spoken evidence.
     * Ambiguous concurrent speech deliberately loses an authoritative source so Brain fails closed.
     */
    fun resolveText(typedText: String?, spokenText: String?): ResolvedConfirmationText {
        val typed = typedText.orEmpty().trim()
        val spoken = spokenText.orEmpty().trim()
        if (typed.isBlank()) {
            return ResolvedConfirmationText(
                spoken,
                if (spoken.isBlank()) null else ConfirmationTextOrigin.SPOKEN,
            )
        }
        if (spoken.isBlank()) {
            return ResolvedConfirmationText(typed, ConfirmationTextOrigin.TYPED)
        }

        val typedRejects = ConfirmationEvidencePolicy.isExplicitRejectionText(typed)
        val spokenRejects = ConfirmationEvidencePolicy.isExplicitRejectionText(spoken)
        return when {
            typedRejects -> ResolvedConfirmationText(typed, ConfirmationTextOrigin.TYPED)
            spokenRejects -> ResolvedConfirmationText(spoken, ConfirmationTextOrigin.SPOKEN)
            ConfirmationEvidencePolicy.isExplicitConfirmationText(typed) &&
                ConfirmationEvidencePolicy.isExplicitConfirmationText(spoken) ->
                ResolvedConfirmationText(typed, ConfirmationTextOrigin.TYPED)
            else -> ResolvedConfirmationText(typed, null)
        }
    }
}
