package com.lingxi.mobile.voice

import android.content.Context
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import java.util.Locale

/**
 * 语音播报 —— 安卓系统 TextToSpeech（对标 PC 端 pyttsx3，离线、零额外依赖）。
 *
 * 播报完会回调 [onAllDone]，供唤醒服务在"说完话"后再恢复聆听（避免把自己的 TTS 当成新指令）。
 */
class Speaker(context: Context) {

    @Volatile private var ready = false
    private var pendingDone: (() -> Unit)? = null

    private val tts = TextToSpeech(context.applicationContext) { status ->
        if (status == TextToSpeech.SUCCESS) {
            ready = true
        }
    }.apply {
        runCatching { language = Locale.CHINESE }
        setOnUtteranceProgressListener(object : UtteranceProgressListener() {
            override fun onStart(utteranceId: String?) {}
            override fun onDone(utteranceId: String?) { fireDone() }
            @Deprecated("deprecated in API level 21")
            override fun onError(utteranceId: String?) { fireDone() }
            override fun onError(utteranceId: String?, errorCode: Int) { fireDone() }
        })
    }

    private fun fireDone() {
        val cb = pendingDone
        pendingDone = null
        cb?.invoke()
    }

    /** 念出 [text]，念完（或失败/为空）回调 [onAllDone]。 */
    fun speak(text: String, onAllDone: (() -> Unit)? = null) {
        if (!ready || text.isBlank()) {
            onAllDone?.invoke()
            return
        }
        pendingDone = onAllDone
        runCatching { tts.language = Locale.CHINESE }
        tts.speak(text, TextToSpeech.QUEUE_FLUSH, null, "lingxi-reply")
    }

    fun release() {
        runCatching { tts.stop() }
        runCatching { tts.shutdown() }
    }
}
