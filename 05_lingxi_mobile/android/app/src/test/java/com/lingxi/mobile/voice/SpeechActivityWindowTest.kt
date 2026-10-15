package com.lingxi.mobile.voice

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SpeechActivityWindowTest {
    @Test
    fun prompt_beep_before_arm_delay_is_ignored() {
        val tracker = SpeechActivityWindow()
        tracker.observe(100L, 0.20f, "")
        tracker.observe(500L, 0.04f, "不")
        tracker.observe(600L, 0.05f, "不要")
        val window = tracker.finish(1100L)
        assertEquals(500L, window.startMs)
        assertEquals(700L, window.endMs)
        assertEquals("rms", window.source)
    }

    @Test
    fun partial_text_is_a_bounded_fallback_when_energy_misses() {
        val tracker = SpeechActivityWindow()
        tracker.observe(900L, 0.01f, "不要")
        val window = tracker.finish(1300L)
        assertEquals(500L, window.startMs)
        assertEquals(1300L, window.endMs)
        assertEquals("asr_partial_fallback", window.source)
    }

    @Test
    fun commandMeasurementCanUseLowerThresholdWithoutChangingDefault() {
        val commandTracker = SpeechActivityWindow(rmsThreshold = 0.012f)
        commandTracker.observe(500L, 0.015f, "现在")
        assertEquals("rms", commandTracker.finish(900L).source)

        val confirmationDefault = SpeechActivityWindow()
        confirmationDefault.observe(500L, 0.015f, "确认")
        assertEquals("asr_partial_fallback", confirmationDefault.finish(900L).source)
    }

    @Test
    fun empty_confirmation_still_returns_valid_window() {
        val window = SpeechActivityWindow().finish(1200L)
        assertTrue(window.endMs > window.startMs)
        assertEquals("endpoint_fallback", window.source)
    }

    @Test
    fun rms_is_zero_for_empty_and_positive_for_signal() {
        assertEquals(0f, SpeechActivityWindow.rms(floatArrayOf()))
        assertTrue(SpeechActivityWindow.rms(floatArrayOf(0.1f, -0.1f)) > 0.09f)
    }
}
