package com.lingxi.mobile.assist

import android.service.voice.VoiceInteractionService
import android.util.Log

/**
 * 灵犀默认助手服务（对标 Siri 的"被系统唤起"入口）。
 *
 * 用户在 设置→默认应用→数字助理 里把灵犀设为默认助手后，
 * 长按 home / 侧键手势会唤起本服务的会话（LingxiInteractionSession）。
 * 这是"统领全局"的合法入口：第三方 App 占据系统助手角色。
 */
class LingxiInteractionService : VoiceInteractionService() {
    override fun onReady() {
        super.onReady()
        Log.i(TAG, "灵犀助手服务就绪（已成为默认助手）")
    }

    companion object {
        const val TAG = "LingxiAssist"
    }
}
