package com.lingxi.mobile.net

import java.net.URI

data class EndpointValidation(
    val valid: Boolean,
    val normalized: String = "",
    val reason: String = "",
)

/** Pure validation kept separate from Android storage so it has JVM regression tests. */
object BrainConnectionPolicy {
    private val TOKEN = Regex("^lx1_[A-Za-z0-9_-]{32,252}$")
    private val DEBUG_HTTP_HOSTS = setOf("127.0.0.1", "localhost", "10.0.2.2")

    fun validateEndpoint(raw: String, isDebug: Boolean): EndpointValidation {
        val value = raw.trim().trimEnd('/')
        if (value.isBlank()) return EndpointValidation(false, reason = "大脑地址不能为空")
        val uri = runCatching { URI(value) }.getOrNull()
            ?: return EndpointValidation(false, reason = "大脑地址格式无效")
        val scheme = uri.scheme?.lowercase().orEmpty()
        val host = uri.host?.lowercase().orEmpty()
        if (host.isBlank() || uri.userInfo != null || uri.query != null || uri.fragment != null) {
            return EndpointValidation(false, reason = "大脑地址不能包含账号、查询参数或片段")
        }
        if (uri.path !in listOf("", "/")) {
            return EndpointValidation(false, reason = "请只填写服务根地址，不要附加接口路径")
        }
        if (uri.port == 0 || uri.port > 65535) {
            return EndpointValidation(false, reason = "端口无效")
        }
        if (scheme != "https" && !(isDebug && scheme == "http" && host in DEBUG_HTTP_HOSTS)) {
            return EndpointValidation(
                false,
                reason = if (isDebug) "HTTP调试仅允许127.0.0.1/localhost/10.0.2.2" else "正式版只允许HTTPS",
            )
        }
        val normalized = runCatching {
            URI(scheme, null, host, uri.port, null, null, null).toString()
        }.getOrNull() ?: return EndpointValidation(false, reason = "大脑地址格式无效")
        return EndpointValidation(true, normalized = normalized)
    }

    fun validToken(token: String): Boolean = TOKEN.matches(token.trim())
}
