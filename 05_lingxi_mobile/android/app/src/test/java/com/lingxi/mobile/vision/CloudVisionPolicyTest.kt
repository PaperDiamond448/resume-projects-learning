package com.lingxi.mobile.vision

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CloudVisionPolicyTest {
    @Test
    fun `cloud screenshot consent is independently required`() {
        val decision = CloudVisionPolicy.decide(
            consentEnabled = false,
            screenText = "首页 推荐 视频列表",
            purpose = CloudVisionPolicy.PURPOSE_VISUAL_PICK,
        )
        assertFalse(decision.allowed)
        assertEquals("CLOUD_VISION_CONSENT_REQUIRED", decision.code)
    }

    @Test
    fun `unknown screen sensitivity fails closed`() {
        val decision = CloudVisionPolicy.decide(
            consentEnabled = true,
            screenText = "   ",
            purpose = CloudVisionPolicy.PURPOSE_VISUAL_PICK,
        )
        assertFalse(decision.allowed)
        assertEquals("SCREEN_SENSITIVITY_UNKNOWN", decision.code)
    }

    @Test
    fun `password payment otp and banking pages are blocked`() {
        listOf("请输入支付密码", "短信验证码 123456", "银行卡转账", "确认付款").forEach { screen ->
            val decision = CloudVisionPolicy.decide(
                consentEnabled = true,
                screenText = screen,
                purpose = CloudVisionPolicy.PURPOSE_STRUCTURED_EXTRACT,
            )
            assertFalse(screen, decision.allowed)
            assertEquals(screen, "SENSITIVE_SCREEN_BLOCKED", decision.code)
        }
        assertFalse(
            CloudVisionPolicy.decide(
                true,
                "首页",
                CloudVisionPolicy.PURPOSE_ACTION_REFLECT,
                appName = "某某银行",
            ).allowed
        )
        assertEquals("sensitive", CloudVisionPolicy.classifySensitivity("我的聊天记录"))
        assertEquals(
            "sensitive",
            CloudVisionPolicy.classifySensitivity("张三 发消息 按住 说话", "微信"),
        )
        assertEquals("clear", CloudVisionPolicy.classifySensitivity("视频列表 热门推荐"))
    }

    @Test
    fun `clear necessary screen produces purpose bound sha256 evidence`() {
        val decision = CloudVisionPolicy.decide(
            consentEnabled = true,
            screenText = "视频列表 热门 推荐",
            purpose = CloudVisionPolicy.PURPOSE_VISUAL_PICK,
        )
        assertTrue(decision.allowed)
        val evidence = CloudVisionPolicy.evidence(
            byteArrayOf(0, 1, 2, 127, -1),
            CloudVisionPolicy.PURPOSE_VISUAL_PICK,
        )
        assertEquals(CloudVisionPolicy.VERSION, evidence.policyVersion)
        assertEquals(CloudVisionPolicy.PURPOSE_VISUAL_PICK, evidence.purpose)
        assertEquals("clear", evidence.screenSensitivity)
        assertEquals(64, evidence.screenshotSha256.length)
        assertTrue(evidence.screenshotSha256.matches(Regex("[0-9a-f]{64}")))
    }

    @Test
    fun `unknown screenshot purpose is blocked`() {
        val decision = CloudVisionPolicy.decide(true, "首页", "other")
        assertFalse(decision.allowed)
        assertEquals("CLOUD_VISION_PURPOSE_INVALID", decision.code)
    }
}
