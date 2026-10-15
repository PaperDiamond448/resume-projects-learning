package com.lingxi.mobile.voice

import java.util.ArrayDeque

/**
 * KWS 与 ASR 之间的短时滚动音频桥。
 *
 * 唤醒词命中的时刻通常已经晚于用户开始说后续命令。这里仅在内存中保留最近
 * 一小段采样，命中后回放给新建的 ASR 流，避免“你好灵犀，打开这个”被截成
 * “这个”或空串。采样不会写盘或上传。
 */
internal class WakeAudioPreRoll(
    private val maxSamples: Int,
) {
    private val chunks = ArrayDeque<FloatArray>()
    var sampleCount: Int = 0
        private set

    val chunkCount: Int
        get() = chunks.size

    init {
        require(maxSamples > 0) { "maxSamples must be positive" }
    }

    fun append(samples: FloatArray) {
        if (samples.isEmpty()) return
        chunks.addLast(samples.copyOf())
        sampleCount += samples.size

        while (sampleCount > maxSamples && chunks.isNotEmpty()) {
            val excess = sampleCount - maxSamples
            val first = chunks.removeFirst()
            if (first.size <= excess) {
                sampleCount -= first.size
            } else {
                chunks.addFirst(first.copyOfRange(excess, first.size))
                sampleCount -= excess
            }
        }
    }

    fun replay(consumer: (FloatArray) -> Unit): Int {
        chunks.forEach(consumer)
        return chunks.size
    }

    fun clear() {
        chunks.clear()
        sampleCount = 0
    }
}
