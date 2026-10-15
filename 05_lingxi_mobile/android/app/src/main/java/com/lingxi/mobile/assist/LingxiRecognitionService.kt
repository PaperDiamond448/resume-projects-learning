package com.lingxi.mobile.assist

import android.content.Intent
import android.speech.RecognitionService
import android.speech.SpeechRecognizer

/**
 * 占位语音识别服务 —— 仅为满足"默认助手三件套"对 `recognitionService` 的声明要求。
 *
 * 灵犀真实的端侧识别走 [com.lingxi.mobile.voice.KwsForegroundService] 里的 sherpa-onnx ASR
 * （唤醒后/助手唤起后转写一句指令）。系统 SpeechRecognizer 这条标准通路本 MVP 不实现，
 * 故这里直接回 ERROR_RECOGNIZER_BUSY，不误导调用方以为能识别。
 */
class LingxiRecognitionService : RecognitionService() {

    override fun onStartListening(recognizerIntent: Intent?, listener: Callback?) {
        runCatching { listener?.error(SpeechRecognizer.ERROR_RECOGNIZER_BUSY) }
    }

    override fun onCancel(listener: Callback?) {}

    override fun onStopListening(listener: Callback?) {}
}
