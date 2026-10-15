package com.lingxi.mobile.confirmation

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TypedConfirmationHandoffPolicyTest {
    private val kwsBinding = PendingConfirmationBinding(
        pendingId = "pending-1",
        windowId = "window-1",
        owner = PendingConfirmationOwner.KWS,
    )

    @Test
    fun acceptsOnlyMatchingExplicitTextForAnOpenKwsWindow() {
        val decision = TypedConfirmationHandoffPolicy.decide(
            text = "确认拨打",
            requestedPendingId = "pending-1",
            requestedWindowId = "window-1",
            storedBinding = kwsBinding,
            activePendingId = "pending-1",
            activeWindowId = "window-1",
            serviceRunning = true,
            acceptingWindow = true,
        )
        assertEquals(TypedConfirmationHandoffDecision.ACCEPT, decision)
    }

    @Test
    fun activityOwnedPendingIsNotInterceptedByKws() {
        val decision = TypedConfirmationHandoffPolicy.decide(
            text = "确认拨打",
            requestedPendingId = "pending-1",
            requestedWindowId = "window-1",
            storedBinding = kwsBinding.copy(owner = PendingConfirmationOwner.ACTIVITY),
            activePendingId = "pending-1",
            activeWindowId = "window-1",
            serviceRunning = true,
            acceptingWindow = true,
        )
        assertEquals(TypedConfirmationHandoffDecision.NOT_KWS_OWNED, decision)
    }

    @Test
    fun staleIdsAndClosedWindowsFailClosed() {
        val mismatch = TypedConfirmationHandoffPolicy.decide(
            text = "不要拨打",
            requestedPendingId = "pending-old",
            requestedWindowId = "window-1",
            storedBinding = kwsBinding,
            activePendingId = "pending-1",
            activeWindowId = "window-1",
            serviceRunning = true,
            acceptingWindow = true,
        )
        val closed = TypedConfirmationHandoffPolicy.decide(
            text = "不要拨打",
            requestedPendingId = "pending-1",
            requestedWindowId = "window-1",
            storedBinding = kwsBinding,
            activePendingId = "pending-1",
            activeWindowId = "window-1",
            serviceRunning = true,
            acceptingWindow = false,
        )
        assertEquals(TypedConfirmationHandoffDecision.BINDING_MISMATCH, mismatch)
        assertEquals(TypedConfirmationHandoffDecision.WINDOW_CLOSED, closed)
    }

    @Test
    fun nonConfirmationTextIsNeverHandoffEvidence() {
        val decision = TypedConfirmationHandoffPolicy.decide(
            text = "今天天气怎么样",
            requestedPendingId = "pending-1",
            requestedWindowId = "window-1",
            storedBinding = kwsBinding,
            activePendingId = "pending-1",
            activeWindowId = "window-1",
            serviceRunning = true,
            acceptingWindow = true,
        )
        assertEquals(TypedConfirmationHandoffDecision.INVALID_TEXT, decision)
    }

    @Test
    fun typedTextKeepsTypedOriginWhenNoSpokenMeaningExists() {
        val resolved = TypedConfirmationHandoffPolicy.resolveText(
            typedText = "确认拨打",
            spokenText = "",
        )
        assertEquals("确认拨打", resolved.text)
        assertEquals(ConfirmationTextOrigin.TYPED, resolved.origin)
    }

    @Test
    fun anyExplicitRejectionDominatesCrossChannelConfirmation() {
        val typedReject = TypedConfirmationHandoffPolicy.resolveText(
            typedText = "不要拨打",
            spokenText = "确认拨打",
        )
        val spokenReject = TypedConfirmationHandoffPolicy.resolveText(
            typedText = "确认拨打",
            spokenText = "不要拨打",
        )
        assertEquals("不要拨打", typedReject.text)
        assertEquals(ConfirmationTextOrigin.TYPED, typedReject.origin)
        assertEquals("不要拨打", spokenReject.text)
        assertEquals(ConfirmationTextOrigin.SPOKEN, spokenReject.origin)
    }

    @Test
    fun ambiguousConcurrentSpeechLosesAuthoritativeSource() {
        val resolved = TypedConfirmationHandoffPolicy.resolveText(
            typedText = "确认拨打",
            spokenText = "等一下可能要改",
        )
        assertEquals("确认拨打", resolved.text)
        assertNull(resolved.origin)
    }
}
