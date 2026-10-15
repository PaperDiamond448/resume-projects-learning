package com.lingxi.mobile.confirmation

import com.lingxi.mobile.model.VisionMark
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ConfirmationEvidencePolicyTest {
    @Test
    fun emptyEvidenceNeverBecomesConfirmation() {
        assertNull(ConfirmationEvidencePolicy.build(null, null, emptyList()))
    }

    @Test
    fun expressionOnlyNeverBecomesConfirmation() {
        val marks = listOf(VisionMark("expression", "positive", 0.99))
        assertNull(ConfirmationEvidencePolicy.build(null, null, marks))
    }

    @Test
    fun victoryIsNotAHighRiskAuthorizationGesture() {
        val marks = listOf(VisionMark("gesture", "victory", 0.99))
        assertNull(ConfirmationEvidencePolicy.build(null, null, marks))
    }

    @Test
    fun reliableVisualLabelsMapToVisionOnlyIntent() {
        val accept = ConfirmationEvidencePolicy.build(
            null, null, listOf(VisionMark("gesture", "thumb_up", 0.99)),
        )
        val reject = ConfirmationEvidencePolicy.build(
            null, null, listOf(VisionMark("head_motion", "shake", 0.99)),
        )
        assertEquals(ConfirmationEvidence("确认", "vision_only"), accept)
        assertEquals(ConfirmationEvidence("取消", "vision_only"), reject)
    }

    @Test
    fun explicitTextKeepsItsRealChannel() {
        val spoken = ConfirmationEvidencePolicy.build(
            "确认发送", ConfirmationTextOrigin.SPOKEN, emptyList(),
        )
        val typedVisual = ConfirmationEvidencePolicy.build(
            "不要", ConfirmationTextOrigin.TYPED,
            listOf(VisionMark("head_motion", "nod", 0.99)),
        )
        assertEquals("spoken", spoken?.source)
        assertEquals("typed_visual", typedVisual?.source)
    }

    @Test
    fun typedRejectionIsAnExplicitPendingResponse() {
        assertEquals(true, ConfirmationEvidencePolicy.isExplicitPendingResponseText("不要发送"))
        assertEquals(true, ConfirmationEvidencePolicy.isExplicitPendingResponseText("取消拨打"))
        assertEquals(true, ConfirmationEvidencePolicy.isExplicitPendingResponseText("确认发送"))
        assertEquals(false, ConfirmationEvidencePolicy.isExplicitPendingResponseText("今天天气怎么样"))
        assertEquals(false, ConfirmationEvidencePolicy.isExplicitPendingResponseText("收件人换成小明"))
    }
}
