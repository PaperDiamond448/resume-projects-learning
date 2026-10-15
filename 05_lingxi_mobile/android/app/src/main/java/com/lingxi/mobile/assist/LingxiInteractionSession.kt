package com.lingxi.mobile.assist

import android.app.assist.AssistContent
import android.app.assist.AssistStructure
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Bundle
import android.service.voice.VoiceInteractionSession
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import com.lingxi.mobile.MainActivity
import com.lingxi.mobile.ui.LingxiWakeIslandView
import com.lingxi.mobile.voice.KwsForegroundService

/**
 * 灵犀助手会话 —— 长按侧键/home 唤起后显示的悬浮界面。
 *
 * 关键能力（对标 Siri 的"看得见当前屏幕"）：onHandleAssist 拿到系统给的
 * AssistStructure（当前前台 App 的视图树），提取可见文本作为"当前界面上下文"。
 * 这正是赛题要求的"结合当前界面理解意图"——后续把这段文本作为 screen 传给大脑。
 */
class LingxiInteractionSession(context: Context) : VoiceInteractionSession(context) {

    private lateinit var status: TextView
    private lateinit var island: LingxiWakeIslandView
    private var receiverRegistered = false
    private val density get() = context.resources.displayMetrics.density

    private val voiceReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context?, i: Intent?) {
            if (i?.action != KwsForegroundService.ACTION_EVENT) return
            if (i.getStringExtra(KwsForegroundService.EXTRA_KIND) != KwsForegroundService.KIND_ENERGY) return
            if (::island.isInitialized) {
                island.setVoiceEnergy(i.getFloatExtra(KwsForegroundService.EXTRA_ENERGY, 0f))
                island.setListening(i.getBooleanExtra(KwsForegroundService.EXTRA_LISTENING, true))
            }
        }
    }

    override fun onCreateContentView(): View {
        val pad = (16 * density).toInt()
        val root = FrameLayout(context).apply {
            setBackgroundColor(0xF8000000.toInt())
            setPadding(0, pad, 0, pad)
        }
        island = LingxiWakeIslandView(context).apply {
            setListening(true)
            setOnClickListener {
                context.startActivity(Intent(context, MainActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
                })
                hide()
            }
        }
        root.addView(island, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            (280 * density).toInt(),
            Gravity.TOP or Gravity.CENTER_HORIZONTAL,
        ))
        status = TextView(context).apply {
            text = "正在读取当前屏幕..."
            textSize = 14f
            setTextColor(0x88B8C0D0.toInt())
            setPadding(pad, 0, pad, 0)
            maxLines = 2
        }
        root.addView(status, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL,
        ))
        return root
    }

    /** 被唤起（长按 Home/侧键）即进"按住说话"：复用端侧 ASR→大脑→TTS 全链路。 */
    override fun onShow(args: Bundle?, showFlags: Int) {
        super.onShow(args, showFlags)
        registerEnergyReceiver()
        KwsForegroundService.listenNow(context)
        if (::island.isInitialized) island.setListening(true)
        if (::status.isInitialized) status.text = "我在听，请说..."
    }

    override fun onHide() {
        unregisterEnergyReceiver()
        super.onHide()
    }

    override fun onDestroy() {
        unregisterEnergyReceiver()
        super.onDestroy()
    }

    /** 旧签名（API30+ 默认实现会回调到这里），兼容 minSdk 26。 */
    @Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
    override fun onHandleAssist(
        data: Bundle?, structure: AssistStructure?, content: AssistContent?
    ) {
        val screen = structure?.let { extractText(it) }.orEmpty()
        status.text = if (screen.isNotBlank())
            "我看到当前屏幕：\n$screen"
        else "（未获取到屏幕文本）"
        // TODO 下一步：把 screen 作为界面上下文连同语音/文本指令送大脑 /interact
    }

    /** 递归提取 AssistStructure 中的可见文本（即"当前界面摘要"）。 */
    private fun extractText(structure: AssistStructure): String {
        val sb = StringBuilder()
        for (i in 0 until structure.windowNodeCount) {
            dump(structure.getWindowNodeAt(i).rootViewNode, sb)
        }
        return sb.toString().trim().take(600)
    }

    private fun dump(node: AssistStructure.ViewNode, sb: StringBuilder) {
        node.text?.toString()?.let { if (it.isNotBlank()) sb.append(it).append("  ") }
        node.contentDescription?.toString()?.let { if (it.isNotBlank()) sb.append(it).append("  ") }
        for (i in 0 until node.childCount) dump(node.getChildAt(i), sb)
    }

    private fun registerEnergyReceiver() {
        if (receiverRegistered) return
        val filter = IntentFilter(KwsForegroundService.ACTION_EVENT)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(voiceReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            context.registerReceiver(voiceReceiver, filter)
        }
        receiverRegistered = true
    }

    private fun unregisterEnergyReceiver() {
        if (!receiverRegistered) return
        runCatching { context.unregisterReceiver(voiceReceiver) }
        receiverRegistered = false
    }
}
