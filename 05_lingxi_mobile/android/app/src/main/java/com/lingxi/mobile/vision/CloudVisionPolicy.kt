package com.lingxi.mobile.vision

import java.security.MessageDigest

data class CloudVisionDecision(
    val allowed: Boolean,
    val code: String,
    val userMessage: String,
    val sensitivity: String,
)

data class CloudVisionEvidence(
    val consent: Boolean,
    val policyVersion: String,
    val purpose: String,
    val screenSensitivity: String,
    val screenshotSha256: String,
)

/** Cloud-3界面截图上云的本地第一道硬闸；不依赖大模型临场判断。 */
object CloudVisionPolicy {
    const val VERSION = "cloud-screen.1"
    const val PURPOSE_VISUAL_PICK = "visual_pick"
    const val PURPOSE_STRUCTURED_EXTRACT = "structured_extract"
    const val PURPOSE_ACTION_REFLECT = "action_reflect"

    private val allowedPurposes = setOf(
        PURPOSE_VISUAL_PICK,
        PURPOSE_STRUCTURED_EXTRACT,
        PURPOSE_ACTION_REFLECT,
    )

    private val sensitiveScreen = Regex(
        "密码|口令|验证码|动态码|短信码|OTP|PIN|CVV|安全码|" +
            "支付|付款|收银台|转账|汇款|扣款|银行卡|信用卡|银行账号|钱包|余额|交易|" +
            "身份证|护照|证件号码|刷脸|指纹验证|私密|聊天记录",
        RegexOption.IGNORE_CASE,
    )
    private val sensitiveApp = Regex(
        "支付宝|银行|云闪付|wallet|bank|支付",
        RegexOption.IGNORE_CASE,
    )
    private val privateChatApp = Regex(
        "微信|企业微信|QQ|钉钉|WhatsApp|Telegram|Messenger",
        RegexOption.IGNORE_CASE,
    )
    private val privateChatScreen = Regex(
        "发消息|输入消息|按住.{0,2}说话|语音通话|视频通话|聊天信息|对方正在输入|已读",
        RegexOption.IGNORE_CASE,
    )

    fun decide(
        consentEnabled: Boolean,
        screenText: String,
        purpose: String,
        appName: String = "",
    ): CloudVisionDecision {
        if (!consentEnabled) {
            return CloudVisionDecision(
                false,
                "CLOUD_VISION_CONSENT_REQUIRED",
                "云端截图默认关闭；需要时请在灵犀设置中单独开启。",
                "unknown",
            )
        }
        if (purpose !in allowedPurposes) {
            return CloudVisionDecision(
                false,
                "CLOUD_VISION_PURPOSE_INVALID",
                "本次截图用途不在允许范围内，已停止上传。",
                "unknown",
            )
        }
        val sensitivity = classifySensitivity(screenText, appName)
        if (sensitivity == "unknown") {
            return CloudVisionDecision(
                false,
                "SCREEN_SENSITIVITY_UNKNOWN",
                "当前页面无法完成敏感信息检查，已停止截图上传。",
                "unknown",
            )
        }
        if (sensitivity == "sensitive") {
            return CloudVisionDecision(
                false,
                "SENSITIVE_SCREEN_BLOCKED",
                "检测到密码、验证码、支付或其他敏感页面，截图不会上传。",
                "sensitive",
            )
        }
        return CloudVisionDecision(true, "ALLOW", "", "clear")
    }

    fun classifySensitivity(screenText: String, appName: String = ""): String {
        val screen = screenText.trim()
        if (screen.isEmpty()) return "unknown"
        return if (
            sensitiveScreen.containsMatchIn(screen) ||
            sensitiveApp.containsMatchIn(appName) ||
            (privateChatApp.containsMatchIn(appName) && privateChatScreen.containsMatchIn(screen))
        ) "sensitive" else "clear"
    }

    fun evidence(png: ByteArray, purpose: String): CloudVisionEvidence =
        CloudVisionEvidence(
            consent = true,
            policyVersion = VERSION,
            purpose = purpose,
            screenSensitivity = "clear",
            screenshotSha256 = MessageDigest.getInstance("SHA-256")
                .digest(png)
                .joinToString("") { "%02x".format(it.toInt() and 0xff) },
        )
}
