package com.lingxi.mobile.voice

import kotlin.math.sqrt

/**
 * 确认应答的轻量语音活动窗，只用于给多模态对齐提供真实起止，不替代 ASR/VAD。
 *
 * - 忽略起录后短暂蜂鸣，避免把提示音当成人声起点；
 * - RMS 超阈值作为主证据；ASR 首个非空 partial 只在能量漏检时兜底；
 * - 时间均相对同一个 monotonic timeline origin，供视觉事件直接比较。
 */
class SpeechActivityWindow(
    private val armDelayMs: Long = DEFAULT_ARM_DELAY_MS,
    private val rmsThreshold: Float = DEFAULT_RMS_THRESHOLD,
    private val partialLookbackMs: Long = DEFAULT_PARTIAL_LOOKBACK_MS,
    private val activeTailMs: Long = DEFAULT_ACTIVE_TAIL_MS,
) {
    data class Window(val startMs: Long, val endMs: Long, val source: String)

    private var firstActiveMs: Long? = null
    private var lastActiveMs: Long? = null
    private var firstPartialMs: Long? = null

    fun observe(tMs: Long, rms: Float, partialText: String) {
        if (tMs < 0L) return
        if (partialText.isNotBlank() && firstPartialMs == null) firstPartialMs = tMs
        if (tMs >= armDelayMs && rms >= rmsThreshold) {
            if (firstActiveMs == null) firstActiveMs = tMs
            lastActiveMs = tMs
        }
    }

    fun finish(endpointMs: Long): Window {
        val activeStart = firstActiveMs
        val activeEnd = lastActiveMs
        if (activeStart != null && activeEnd != null) {
            val end = (activeEnd + activeTailMs).coerceAtMost(endpointMs)
                .coerceAtLeast(activeStart + 1L)
            return Window(activeStart, end, "rms")
        }
        val partial = firstPartialMs
        if (partial != null) {
            val start = (partial - partialLookbackMs).coerceAtLeast(armDelayMs)
            return Window(start, endpointMs.coerceAtLeast(start + 1L), "asr_partial_fallback")
        }
        return Window(armDelayMs, endpointMs.coerceAtLeast(armDelayMs + 1L), "endpoint_fallback")
    }

    companion object {
        const val DEFAULT_ARM_DELAY_MS = 300L
        const val DEFAULT_RMS_THRESHOLD = 0.025f
        const val DEFAULT_PARTIAL_LOOKBACK_MS = 400L
        const val DEFAULT_ACTIVE_TAIL_MS = 100L

        fun rms(samples: FloatArray): Float {
            if (samples.isEmpty()) return 0f
            var sum = 0.0
            for (sample in samples) sum += sample * sample
            return sqrt(sum / samples.size).toFloat()
        }
    }
}
