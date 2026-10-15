package com.lingxi.mobile.voice

import android.content.res.AssetManager
import com.k2fsa.sherpa.onnx.FeatureConfig
import com.k2fsa.sherpa.onnx.KeywordSpotter
import com.k2fsa.sherpa.onnx.KeywordSpotterConfig
import com.k2fsa.sherpa.onnx.OnlineModelConfig
import com.k2fsa.sherpa.onnx.OnlineStream
import com.k2fsa.sherpa.onnx.OnlineTransducerModelConfig

/**
 * 端侧关键词唤醒（KWS）引擎 —— 包一层 sherpa-onnx KeywordSpotter。
 *
 * 用双语 zipformer KWS 模型（int8，chunk-16，~5MB，CPU 单线程低功耗，常驻麦克风）。
 * 关键词由 assets/kws-zh-en/keywords.txt 提供（含「你好灵犀」「嘿Siri」两个预设）。
 * 音频不出端：只在本机做声学比对，命中才唤起后续 ASR。隐私边界 D5。
 */
class SherpaKws(assets: AssetManager) {

    private val spotter: KeywordSpotter
    private var stream: OnlineStream

    init {
        val config = KeywordSpotterConfig(
            featConfig = FeatureConfig(sampleRate = 16000, featureDim = 80),
            modelConfig = OnlineModelConfig(
                transducer = OnlineTransducerModelConfig(
                    encoder = "$DIR/encoder.int8.onnx",
                    decoder = "$DIR/decoder.onnx",
                    joiner = "$DIR/joiner.int8.onnx",
                ),
                tokens = "$DIR/tokens.txt",
                modelType = "zipformer2",
                numThreads = 1,        // 常驻=低功耗优先
                provider = "cpu",
            ),
            keywordsFile = "$DIR/keywords.txt",
            keywordsScore = 2.0f,      // 适度提升召回
            keywordsThreshold = 0.25f, // 触发门限（越低越灵敏、误触发越多），真机标定
        )
        spotter = KeywordSpotter(assetManager = assets, config = config)
        stream = spotter.createStream()
    }

    /**
     * 喂入一帧 PCM（[-1,1] 归一化，16kHz）。命中则返回唤醒词显示名（@后的名字），否则 null。
     * 命中后自动 reset，避免对同一段音频反复触发。
     */
    fun accept(samples: FloatArray): String? {
        stream.acceptWaveform(samples, 16000)
        while (spotter.isReady(stream)) spotter.decode(stream)
        val kw = spotter.getResult(stream).keyword
        if (kw.isNotEmpty()) {
            spotter.reset(stream)
            return kw
        }
        return null
    }

    /** 清空声学上下文（回到聆听态前调用，丢弃处理期间的残留）。 */
    fun reset() = spotter.reset(stream)

    fun release() {
        stream.release()
        spotter.release()
    }

    companion object {
        private const val DIR = "kws-zh-en"
    }
}
