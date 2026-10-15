package com.lingxi.mobile.confirmation

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ConfirmationCapturePolicyTest {
    @Test
    fun onlyNewPendingStartsAutomaticVisualCapture() {
        assertTrue(ConfirmationCapturePolicy.shouldAutoStart("pending_confirmation", "NEW_PENDING"))
        assertFalse(ConfirmationCapturePolicy.shouldAutoStart("pending_confirmation", "VISION_ONLY_DISABLED"))
        assertFalse(ConfirmationCapturePolicy.shouldAutoStart("pending_confirmation", "LOW_VISUAL_CONFIDENCE"))
        assertFalse(ConfirmationCapturePolicy.shouldAutoStart("pending_confirmation", "UNSUPPORTED_VISUAL_EVENT"))
        assertFalse(ConfirmationCapturePolicy.shouldAutoStart("cancelled", "EXPLICIT_VISUAL_REJECTION"))
    }
}
