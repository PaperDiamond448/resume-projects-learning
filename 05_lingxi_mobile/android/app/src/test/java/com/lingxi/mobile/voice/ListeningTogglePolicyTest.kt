package com.lingxi.mobile.voice

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ListeningTogglePolicyTest {
    @Test
    fun runningPersistentListenerTurnsOff() {
        assertFalse(ListeningTogglePolicy.nextRequested(requested = true, running = true))
    }

    @Test
    fun staleRequestedStateRestartsInsteadOfSendingStop() {
        assertTrue(ListeningTogglePolicy.nextRequested(requested = true, running = false))
    }

    @Test
    fun fullyStoppedListenerTurnsOn() {
        assertTrue(ListeningTogglePolicy.nextRequested(requested = false, running = false))
    }

    @Test
    fun oneShotListenerCanBePromotedToPersistentListening() {
        assertTrue(ListeningTogglePolicy.nextRequested(requested = false, running = true))
    }
}
