package com.lingxi.mobile.voice

import android.content.res.AssetManager
import com.k2fsa.sherpa.onnx.EndpointConfig
import com.k2fsa.sherpa.onnx.FeatureConfig
import com.k2fsa.sherpa.onnx.OnlineModelConfig
import com.k2fsa.sherpa.onnx.OnlineRecognizer
import com.k2fsa.sherpa.onnx.OnlineRecognizerConfig
import com.k2fsa.sherpa.onnx.OnlineStream
import com.k2fsa.sherpa.onnx.OnlineZipformer2CtcModelConfig

/**
 * 端侧流式语音识别（ASR）—— 包一层 sherpa-onnx OnlineRecognizer。
 *
 * 用小体积中文流式 zipformer-CTC 模型（int8，~26MB，CPU）。唤醒后只转写一句指令，
 * 靠内置端点检测（静音 ~1.4s）自动判句尾。识别结果（文本）再交大脑，**音频不出端**。
 * 英文 app 名等少量跨语言由大脑「世界知识+已装清单」兜底（见 CLAUDE.md）。
 */
class SherpaAsr(assets: AssetManager) {

    private val recognizer: OnlineRecognizer

    init {
        val config = OnlineRecognizerConfig(
            featConfig = FeatureConfig(sampleRate = 16000, featureDim = 80),
            modelConfig = OnlineModelConfig(
                zipformer2Ctc = OnlineZipformer2CtcModelConfig(model = "$DIR/model.int8.onnx"),
                tokens = "$DIR/tokens.txt",
                numThreads = 2,        // 转写求快
                provider = "cpu",
            ),
            endpointConfig = EndpointConfig(),
            enableEndpoint = true,
            decodingMethod = "greedy_search",
        )
        recognizer = OnlineRecognizer(assetManager = assets, config = config)
    }

    fun newStream(): OnlineStream = recognizer.createStream()

    /** 喂入一帧 PCM（[-1,1]，16kHz）并解码。 */
    fun accept(stream: OnlineStream, samples: FloatArray) {
        stream.acceptWaveform(samples, 16000)
        while (recognizer.isReady(stream)) recognizer.decode(stream)
    }

    /** 是否检测到句尾（静音端点）。 */
    fun isEndpoint(stream: OnlineStream): Boolean = recognizer.isEndpoint(stream)

    /** 当前累计转写文本。 */
    fun text(stream: OnlineStream): String = recognizer.getResult(stream).text

    fun reset(stream: OnlineStream) = recognizer.reset(stream)

    fun release() = recognizer.release()

    companion object {
        private const val DIR = "asr-ctc-zh"
    }
}
