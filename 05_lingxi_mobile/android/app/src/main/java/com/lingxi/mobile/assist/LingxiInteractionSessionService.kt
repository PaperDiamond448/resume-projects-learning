package com.lingxi.mobile.assist

import android.os.Bundle
import android.service.voice.VoiceInteractionSession
import android.service.voice.VoiceInteractionSessionService

/** 会话工厂：每次唤起助手创建一个 LingxiInteractionSession。 */
class LingxiInteractionSessionService : VoiceInteractionSessionService() {
    override fun onNewSession(args: Bundle?): VoiceInteractionSession {
        return LingxiInteractionSession(this)
    }
}
