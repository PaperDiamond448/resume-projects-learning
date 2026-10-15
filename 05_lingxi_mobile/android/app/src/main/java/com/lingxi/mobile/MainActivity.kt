package com.lingxi.mobile

import android.Manifest
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.graphics.Color
import android.media.projection.MediaProjectionManager
import android.os.Bundle
import android.provider.Settings
import android.text.InputType
import android.util.Base64
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import com.lingxi.mobile.accessibility.LingxiAccessibilityService
import com.lingxi.mobile.action.AppAutomator
import com.lingxi.mobile.action.ActionExecutionMessagePolicy
import com.lingxi.mobile.action.DeviceActionExecutor
import com.lingxi.mobile.confirmation.ConfirmationCapturePolicy
import com.lingxi.mobile.confirmation.ConfirmationEvidencePolicy
import com.lingxi.mobile.confirmation.ConfirmationTextOrigin
import com.lingxi.mobile.confirmation.PendingConfirmationOwner
import com.lingxi.mobile.confirmation.PendingConfirmationStore
import com.lingxi.mobile.confirmation.TypedConfirmationHandoffDecision
import com.lingxi.mobile.data.ConversationMessage
import com.lingxi.mobile.data.ConversationRecord
import com.lingxi.mobile.data.ConversationStore
import com.lingxi.mobile.data.MessageCard
import com.lingxi.mobile.databinding.ActivityMainBinding
import com.lingxi.mobile.model.InteractRequest
import com.lingxi.mobile.model.VisionMark
import com.lingxi.mobile.net.BrainClient
import com.lingxi.mobile.net.BrainConnectionStore
import com.lingxi.mobile.ui.ChatAdapter
import com.lingxi.mobile.ui.HistoryAdapter
import com.lingxi.mobile.ui.HistoryListItem
import com.lingxi.mobile.vision.CalibrationActivity
import com.lingxi.mobile.vision.CloudVisionConsentStore
import com.lingxi.mobile.vision.ConflictVisionCapture
import com.lingxi.mobile.vision.ContactPicker
import com.lingxi.mobile.vision.GazeCalibration
import com.lingxi.mobile.vision.GazeDeixis
import com.lingxi.mobile.vision.GazeEstimator
import com.lingxi.mobile.vision.GazeTrackingStore
import com.lingxi.mobile.vision.ScreenCaptureService
import com.lingxi.mobile.voice.KwsForegroundService
import com.lingxi.mobile.voice.ListeningTogglePolicy
import com.lingxi.mobile.voice.WakeCommandNormalizer
import com.lingxi.mobile.voice.WakeWord
import com.lingxi.mobile.voice.WakeWords
import com.lingxi.mobile.voice.WakeWordStore
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.TimeUnit

/**
 * 灵犀瘦客户端主界面。两种输入入口：
 *  ① 文本输入（模拟/调试用）→ 大脑 /interact → 显示应答 + 落地设备动作。
 *  ② 语音唤醒（对标 Siri）：开关常驻 KwsForegroundService，喊「你好灵犀/嘿Siri」→ 端侧 ASR → 大脑 → TTS。
 *     语音链路全程在服务里跑，本界面只负责开关、选唤醒词、回显事件。
 */
class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private val brain = BrainClient()
    private lateinit var executor: DeviceActionExecutor
    private lateinit var conversationStore: ConversationStore
    private lateinit var chatAdapter: ChatAdapter
    private val cardsGson = Gson()
    private var currentConversation: ConversationRecord? = null
    private var screenMode: ScreenMode = ScreenMode.HOME

    /** 主页面折叠菜单：默认收起，只留⚙入口，点开才露出唤醒/屏幕视觉/无障碍等功能按钮，保持首屏干净。 */
    private var controlsExpanded = false

    /** 待权限授予后再开启聆听（用户点了"唤醒：开"但麦克风权限还没批）。 */
    private var pendingStartListening = false

    /** 待权限授予后执行一次显式聆听（用户点击首页中央聆听岛）。 */
    private var pendingListenNow = false

    /** 多模态确认：端侧点头/摇头/手势采集（确认环节开前置摄像头一个短窗口）。 */
    private var conflictCapture: ConflictVisionCapture? = null
    private var pendingConflictRequested = false
    private var pendingConflictSpoken: String? = null
    private var autoConfirmCapturing = false
    private var pendingAutoConfirmSpoken: String? = null
    private var activePendingId: String? = null
    private var activeConfirmationWindowId: String? = null
    private var lastAgentReport: String = ""

    /** T-16 v2：待相机权限批准后开启注视追踪常开总开关（用户点了"注视追踪：开"但权限还没批）。 */
    private var pendingGazeTrackingRequested = false

    /** 注视指代没解出目标时，只有真的要展示联系人卡片才申请通讯录权限。 */
    private var pendingContactPickerRequested = false

    private val requestPerms = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { result ->
        if (pendingListenNow) {
            pendingListenNow = false
            if (result[Manifest.permission.RECORD_AUDIO] == true) KwsForegroundService.listenNow(this)
            else appendLog("灵犀", "没有麦克风权限，无法开始聆听～可在系统设置里授予。")
        }
        if (pendingStartListening) {
            pendingStartListening = false
            if (result[Manifest.permission.RECORD_AUDIO] == true) startListening()
            else appendLog("灵犀", "没有麦克风权限，语音唤醒开不了～可在系统设置里授予。")
        }
        if (pendingConflictRequested) {
            pendingConflictRequested = false
            if (result[Manifest.permission.CAMERA] == true) startConflictCapture(pendingConflictSpoken)
            else appendLog("灵犀", "没有相机权限，多模态确认用不了～可在系统设置里授予。")
            pendingConflictSpoken = null
        }
        if (pendingGazeTrackingRequested) {
            pendingGazeTrackingRequested = false
            if (result[Manifest.permission.CAMERA] == true) enableGazeTracking()
                else appendLog("灵犀", "没有相机权限，注视追踪开不了～可在系统设置里授予。")
        }
        if (pendingContactPickerRequested) {
            pendingContactPickerRequested = false
            if (result[Manifest.permission.READ_CONTACTS] == true) showContactPickerCard()
            else appendLog("灵犀", "未授予通讯录权限，我保留文字追问，你也可以直接说联系人姓名。")
        }
    }

    /** 路线C 屏幕视觉：用户同意 MediaProjection 后，携授权令牌起截图服务（供 GUI-agent 卡住时看屏）。 */
    private val projectionLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val data = result.data
        if (result.resultCode == RESULT_OK && data != null) {
            ScreenCaptureService.start(this, result.resultCode, data)
            appendLog("灵犀", "本机屏幕视觉已开启；这不会自动上传截图。云端截图需在设置中另行明确开启。")
        } else {
            appendLog("灵犀", "没开屏幕视觉也行，我先用读屏文本操作；遇到看不懂的界面可能点不准。")
        }
        // startForegroundService() 是异步的，ScreenCaptureService.isReady 要等服务真正 onCreate 完才会翻 true，
        // 这里立刻读会读到旧值（状态胶囊显示"未开"）——跟下面 toggleScreenVision() 关闭分支一样延迟一拍再读。
        refreshScreenVisionUi()
        binding.root.postDelayed({ refreshScreenVisionUi() }, 300)
        binding.root.postDelayed({ refreshScreenVisionUi() }, 900)
    }

    /** 接收唤醒服务广播的对话事件，回显到聊天区。 */
    private val voiceReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context?, i: Intent?) {
            if (i?.action != KwsForegroundService.ACTION_EVENT) return
            if (i.getStringExtra(KwsForegroundService.EXTRA_KIND) == KwsForegroundService.KIND_ENERGY) {
                binding.wakeIsland.setVoiceEnergy(i.getFloatExtra(KwsForegroundService.EXTRA_ENERGY, 0f))
                val listening = i.getBooleanExtra(KwsForegroundService.EXTRA_LISTENING, true)
                binding.wakeIsland.setListening(listening)
                binding.wakeStatus.text = if (listening) "语音: 聆听中" else "语音: 处理中"
                return
            }
            appendLog(
                i.getStringExtra(KwsForegroundService.EXTRA_WHO) ?: "灵犀",
                i.getStringExtra(KwsForegroundService.EXTRA_TEXT) ?: ""
            )
            showChat()
        }
    }

    /** 无障碍服务异步连接/断开、注视相机启停后，立即同步首页显示。 */
    private val accessibilityStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context?, i: Intent?) {
            if (i?.action != LingxiAccessibilityService.ACTION_STATE_CHANGED) return
            refreshAccessibilityUi()
            refreshGazeTrackingUi()
        }
    }

    /** adb 真机回归入口：只在主界面前台时有效，收到文本后走与"发送"按钮完全相同的链路。 */
    private val debugSubmitReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context?, i: Intent?) {
            if (i?.action != ACTION_DEBUG_SUBMIT) return
            val text = decodeB64(i.getStringExtra("text_b64").orEmpty()).trim()
            if (text.isNotEmpty()
                && !handleTeachCommand(text)
                && !handleMultimodalCommand(text)
                && !handleKwsTypedConfirmation(text)
                && !handleAutoConfirmText(text)
            ) {
                showChat()
                submitText(text)
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        executor = DeviceActionExecutor(this)
        conversationStore = ConversationStore(this)
        currentConversation = conversationStore.newConversation()
        // 🐛 用户追问"校准数据是不是永久存着"时揪出：GazeEstimator.calib 之前只在
        // CalibrationActivity/调试界面里主动 load()过，App 进程被系统杀掉重启后（MIUI 常见）
        // 会静默退化回粗糙的 default() 参数。这里补上，保证只要校准过就一定生效。
        GazeEstimator.calib = GazeCalibration.load(this)

        chatAdapter = ChatAdapter(onCardClick = { card -> resumeApp(card.packageName) })
        binding.messageList.layoutManager = LinearLayoutManager(this)
        binding.messageList.adapter = chatAdapter

        binding.sendButton.setOnClickListener { onSend() }
        // 首页中央的聆听岛就是显式语音入口：点击后跳过唤醒词，直接进入一次 ASR 采集。
        // LingxiWakeIslandView 自身只负责绘制；若这里不绑定，界面虽然显示 clickable，
        // 实际点击不会启动 KwsForegroundService，状态胶囊还可能把持久化开关误显示成“聆听中”。
        binding.wakeIsland.setOnClickListener {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
                != PackageManager.PERMISSION_GRANTED
            ) {
                pendingListenNow = true
                requestPerms.launch(arrayOf(
                    Manifest.permission.RECORD_AUDIO,
                    Manifest.permission.POST_NOTIFICATIONS,
                ))
            } else {
                KwsForegroundService.listenNow(this)
            }
        }
        binding.navButton.setOnClickListener {
            if (screenMode == ScreenMode.HOME) showHistoryDialog() else showHome()
        }
        binding.newConversationButton.setOnClickListener {
            startFreshConversation()
            showHome()
        }
        binding.wakeToggle.setOnClickListener { toggleListening() }
        binding.settingsToggle.setOnClickListener { toggleControlsPanel() }
        binding.brainConnectionButton.setOnClickListener { showBrainConnectionDialog() }
        binding.wakeWordButton.setOnClickListener { showWakeWordPicker(firstRun = false) }
        binding.screenVisionButton.setOnClickListener { toggleScreenVision() }
        binding.cloudVisionConsentButton.setOnClickListener { toggleCloudVisionConsent() }
        binding.accessibilityButton.setOnClickListener { openAccessibilitySettings() }
        // T-16 之前只能靠 adb 手动启动校准 Activity，普通用户/评委根本发现不了这个入口；
        // 补一个真实按钮，"看着联系人说给他发消息"这个旗舰场景要靠谱，校准是前提不是可选项。
        binding.gazeCalibrationButton.setOnClickListener {
            startActivity(Intent(this, CalibrationActivity::class.java))
        }
        // T-16 v2：苹果式常开总开关——开着时无障碍服务持续跑相机+挂悬浮反馈点；关掉即刻释放。
        binding.gazeTrackingButton.setOnClickListener { toggleGazeTracking() }
        binding.gazeTouchAssistButton.setOnClickListener { toggleGazeTouchAssist() }
        refreshScreenVisionUi()
        refreshCloudVisionUi()
        refreshAccessibilityUi()
        refreshGazeTrackingUi()
        refreshBrainConnectionUi()

        refreshWakeUi()
        showHome()

        // 首次启动：问用户想要什么唤醒词（你点名要的体验 😄）
        if (!WakeWordStore.onboarded(this)) {
            binding.root.post { showWakeWordPicker(firstRun = true) }
        }
        handleAgentReportIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleAgentReportIntent(intent)
    }

    override fun onResume() {
        super.onResume()
        ContextCompat.registerReceiver(
            this, voiceReceiver, IntentFilter(KwsForegroundService.ACTION_EVENT),
            ContextCompat.RECEIVER_NOT_EXPORTED
        )
        ContextCompat.registerReceiver(
            this,
            accessibilityStateReceiver,
            IntentFilter(LingxiAccessibilityService.ACTION_STATE_CHANGED),
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        if ((applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
            ContextCompat.registerReceiver(
                this, debugSubmitReceiver, IntentFilter(ACTION_DEBUG_SUBMIT),
                ContextCompat.RECEIVER_EXPORTED
            )
        }
        if (WakeWordStore.isEnabled(this)
            && ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
            && !KwsForegroundService.isRunning
        ) {
            KwsForegroundService.start(this)
        }
        refreshWakeUi()
        binding.root.postDelayed({ refreshWakeUi() }, 800)
        refreshScreenVisionUi()
        refreshCloudVisionUi()
        refreshAccessibilityUi()
        refreshGazeTrackingUi()
        binding.root.postDelayed({ refreshAccessibilityUi(); refreshGazeTrackingUi() }, 300)
        binding.root.postDelayed({ refreshAccessibilityUi(); refreshGazeTrackingUi() }, 1000)
    }

    override fun onPause() {
        super.onPause()
        runCatching { unregisterReceiver(voiceReceiver) }
        runCatching { unregisterReceiver(accessibilityStateReceiver) }
        runCatching { unregisterReceiver(debugSubmitReceiver) }
        conflictCapture?.stop()   // 离开界面释放前置摄像头，避免占用/耗电
        conflictCapture = null
        autoConfirmCapturing = false
        pendingAutoConfirmSpoken = null
    }

    // ---- 唤醒词选择（首启 + 手动切换）----
    private fun showWakeWordPicker(firstRun: Boolean) {
        val presets = WakeWords.PRESETS
        val labels = presets.map { it.label }.toTypedArray()
        val current = WakeWordStore.get(this)
        var checked = presets.indexOfFirst { it.id == current.id }.coerceAtLeast(0)

        AlertDialog.Builder(this)
            .setTitle(R.string.wake_pick_title)
            .setSingleChoiceItems(labels, checked) { _, which -> checked = which }
            .setPositiveButton(if (firstRun) "就用它" else "确定") { _, _ ->
                applyWakeWord(presets[checked], firstRun)
            }
            .apply { if (!firstRun) setNegativeButton("取消", null) }
            .setCancelable(!firstRun)
            .show()
    }

    private fun applyWakeWord(word: WakeWord, firstRun: Boolean) {
        WakeWordStore.set(this, word)
        refreshWakeUi()
        appendLog("灵犀", "好嘞，叫我「${word.label}」就行。")
        if (firstRun) {
            setListening(true)               // 首选后顺手开启聆听
            binding.root.post { showPersonalizationOnboarding() }  // 接着引导个性化（让"懂你"被感觉到）
        } else if (WakeWordStore.isEnabled(this)) {
            KwsForegroundService.start(this) // 已在聆听 → 推送新唤醒词（服务热更新 chosen）
        }
    }

    /**
     * 首启个性化引导：选一种语气立刻生效（演示"它记住了"），并教会用户用一句话长期定制。
     * 复用大脑的 set_preference 意图——选项直接说成自然语言发给大脑，无需任何额外接口。
     */
    private fun showPersonalizationOnboarding() {
        val tones = arrayOf("自然（默认）", "温柔", "活泼", "简洁干脆", "正式")
        AlertDialog.Builder(this)
            .setTitle("想让我用什么语气跟你说话？")
            .setItems(tones) { _, which ->
                if (which > 0) sendPreference("对我说话${tones[which]}一点")
                appendLog("灵犀",
                    "记住啦～以后你随时可以对我说：「叫我X」「说话简短点」「别用emoji」"
                        + "「记住我喜欢周杰伦」，或问「你还记得我什么」、说「忘掉我的偏好」。")
            }
            .setNegativeButton("以后再说") { _, _ ->
                appendLog("灵犀",
                    "好～想让我更懂你时，对我说「叫我X，说话温柔点」之类就行，随时能改。")
            }
            .show()
    }

    /** 把一句个性化设置发给大脑（走 set_preference）。 */
    private fun sendPreference(text: String) {
        lifecycleScope.launch {
            runCatching { brain.interact(InteractRequest(text = text)) }
                .onSuccess { appendLog("灵犀", it.reply) }
                .onFailure { /* 没连大脑也不打断引导，静默 */ }
        }
    }

    /** Cloud-2手动配对：endpoint明文保存，Bearer只以Android Keystore密文落盘。 */
    private fun showBrainConnectionDialog() {
        val density = resources.displayMetrics.density
        fun dp(value: Int) = (value * density).toInt()
        val panel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(12), dp(22), dp(4))
        }
        val endpointInput = EditText(this).apply {
            hint = "https://brain.example.com"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setSingleLine(true)
            setText(BrainConnectionStore.configuredEndpoint() ?: BrainClient.DEFAULT_BASE_URL)
            setTextColor(Color.rgb(242, 244, 252))
            setHintTextColor(Color.rgb(154, 164, 186))
        }
        val tokenInput = EditText(this).apply {
            hint = if (BrainConnectionStore.hasToken()) "令牌已保存；留空则保持不变" else "粘贴一次性设备令牌 lx1_…"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
            setSingleLine(true)
            setTextColor(Color.rgb(242, 244, 252))
            setHintTextColor(Color.rgb(154, 164, 186))
        }
        val verificationText = TextView(this).apply {
            setPadding(0, dp(14), 0, 0)
            setTextColor(Color.rgb(148, 222, 190))
            text = BrainConnectionStore.tokenFingerprint()?.let { "当前令牌指纹：$it" }
                ?: "尚未保存设备令牌"
        }
        panel.addView(endpointInput)
        panel.addView(tokenInput)
        panel.addView(verificationText)
        val dialog = AlertDialog.Builder(this, R.style.Theme_Lingxi_BrainDialog)
            .setTitle("配置云端大脑")
            .setMessage("正式版只接受 HTTPS；设备令牌由 Android Keystore 加密，保存后不再显示。")
            .setView(panel)
            .setPositiveButton("安全保存并验证", null)
            .setNeutralButton("清除配置", null)
            .setNegativeButton("关闭", null)
            .create()
        dialog.setOnShowListener {
            val saveButton = dialog.getButton(AlertDialog.BUTTON_POSITIVE)
            saveButton.setOnClickListener {
                runCatching {
                    BrainConnectionStore.save(
                        endpointInput.text.toString(),
                        tokenInput.text.toString().takeIf { it.isNotBlank() },
                    )
                }.onSuccess {
                    tokenInput.text?.clear()
                    refreshBrainConnectionUi()
                    binding.brainStatus.text = "大脑配置已安全保存 · 正在验证连接"
                    verificationText.text = "配置已加密保存 · 正在验证…"
                    saveButton.isEnabled = false
                    lifecycleScope.launch {
                        val result = brain.healthResult()
                        val message = result.userMessage()
                        val fingerprint = BrainConnectionStore.tokenFingerprint()
                        binding.brainStatus.text = message
                        verificationText.text = buildString {
                            append(message)
                            if (fingerprint != null) append("\n令牌指纹：").append(fingerprint)
                        }
                        verificationText.setTextColor(
                            if (result.available) Color.rgb(148, 222, 190)
                            else Color.rgb(255, 173, 173)
                        )
                        saveButton.isEnabled = true
                    }
                }.onFailure {
                    val message = it.message ?: "配置无效"
                    verificationText.text = message
                    verificationText.setTextColor(Color.rgb(255, 173, 173))
                    Toast.makeText(this, message, Toast.LENGTH_LONG).show()
                }
            }
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener {
                BrainConnectionStore.clear()
                refreshBrainConnectionUi()
                binding.brainStatus.text = "云端大脑配置已清除"
                dialog.dismiss()
            }
        }
        dialog.show()
    }

    private fun refreshBrainConnectionUi() {
        binding.brainConnectionButton.text = if (BrainConnectionStore.hasToken()) {
            val fingerprint = BrainConnectionStore.tokenFingerprint()
            "云端大脑：设备令牌已加密保存${fingerprint?.let { "（$it）" }.orEmpty()}"
        } else {
            "云端大脑：未配置设备令牌"
        }
    }

    // ---- 屏幕视觉开关（路线C 视觉兜底的总闸）----
    private fun toggleScreenVision() {
        if (ScreenCaptureService.isReady) {
            ScreenCaptureService.stop(this)
            appendLog("灵犀", "屏幕视觉已关闭。")
            binding.root.postDelayed({ refreshScreenVisionUi() }, 300)
            return
        }
        // 截屏授权（MediaProjection 系统弹框，用户点"立即开始"）。操作进度浮窗现在走无障碍 overlay，
        // 不再要求 MIUI 的"显示在其他应用上层"权限。
        val mpm = getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        projectionLauncher.launch(mpm.createScreenCaptureIntent())
    }

    private fun refreshScreenVisionUi() {
        val ready = ScreenCaptureService.isReady
        val cloud = CloudVisionConsentStore.isEnabled(this)
        binding.screenVisionButton.text = if (ready) "本机视觉:开" else "本机屏幕视觉"
        binding.visionStatus.text =
            "本机视觉:${if (ready) "已授权" else "未开"} · 云端截图:${if (cloud) "已同意" else "关闭"}"
        binding.visionStatus.setTextColor(Color.parseColor(if (ready) "#1F6F5B" else "#5B6472"))
    }

    // ---- Cloud-3 云端截图独立同意（默认关闭；与MediaProjection系统授权不是一回事）----
    private fun toggleCloudVisionConsent() {
        if (CloudVisionConsentStore.isEnabled(this)) {
            CloudVisionConsentStore.setEnabled(this, false)
            refreshCloudVisionUi()
            refreshScreenVisionUi()
            appendLog("灵犀", "云端截图已关闭；后续只使用读屏文本、教学回放或本地能力。")
            return
        }
        AlertDialog.Builder(this, R.style.Theme_Lingxi_BrainDialog)
            .setTitle("允许必要界面截图上云？")
            .setMessage(
                "仅当读屏/教学回放无法完成当前步骤时，灵犀才会把当前 App 界面截图发送给固定的" +
                    " Qwen2.5-VL-3B 服务。密码、验证码、支付、银行等敏感页面会被代码硬拦；" +
                    "你可以随时关闭。人脸和语音不会走这条链路。"
            )
            .setPositiveButton("我同意开启") { _, _ ->
                CloudVisionConsentStore.setEnabled(this, true)
                refreshCloudVisionUi()
                refreshScreenVisionUi()
                appendLog("灵犀", "云端截图已明确开启；敏感页仍会自动阻止上传。")
            }
            .setNegativeButton("保持关闭", null)
            .show()
    }

    private fun refreshCloudVisionUi() {
        val enabled = CloudVisionConsentStore.isEnabled(this)
        binding.cloudVisionConsentButton.text =
            if (enabled) "云端截图：已同意（点此关闭）" else "云端截图：关闭（默认）"
        binding.cloudVisionConsentButton.setTextColor(
            Color.parseColor(if (enabled) "#9FE8CC" else "#D7DEEA")
        )
    }

    // ---- 一键开无障碍（重装后在手机上自助重开，免电脑端 adb）----
    private fun openAccessibilitySettings() {
        if (LingxiAccessibilityService.isConnected) {
            appendLog("灵犀", "「灵犀无障碍」已经开着啦，不用重复开 ✓")
            return
        }
        appendLog("灵犀", "在列表里找到「灵犀无障碍（操作 App 的手）」，打开开关即可。")
        runCatching {
            startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        }.onFailure { appendLog("灵犀", "打不开无障碍设置页：${it.message}") }
    }

    private fun refreshAccessibilityUi() {
        val ready = LingxiAccessibilityService.isConnected
        val enabledInSystem = isLingxiAccessibilityEnabledInSystem()
        binding.accessibilityButton.text =
            when {
                ready -> "无障碍:已开 ✓"
                enabledInSystem -> "无障碍:正在连接…"
                else -> "开启无障碍（操作 App 的手）"
            }
        binding.accessibilityStatus.text = when {
            ready -> "执行: 已开"
            enabledInSystem -> "执行: 连接中"
            else -> "执行: 未开"
        }
        binding.accessibilityStatus.setTextColor(Color.parseColor(if (ready) "#1F6F5B" else "#5B6472"))
    }

    private fun isLingxiAccessibilityEnabledInSystem(): Boolean {
        val expected = ComponentName(this, LingxiAccessibilityService::class.java).flattenToString()
        val enabled = Settings.Secure.getString(
            contentResolver,
            Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES,
        ).orEmpty()
        return enabled.split(':').any { it.equals(expected, ignoreCase = true) }
    }

    // ---- T-16 v2：注视追踪常开总开关（苹果式 UX，替代关键词触发短窗）----
    // 实际的相机+悬浮反馈点生命周期挂在 LingxiAccessibilityService 里（需要 TYPE_ACCESSIBILITY_OVERLAY
    // 挂悬浮点、且该服务本来就在无障碍开着时持续存活）；这里只管开关意愿+权限+UI。
    private fun toggleGazeTracking() {
        if (GazeTrackingStore.isEnabled(this)) {
            GazeTrackingStore.setTouchAssistEnabled(this, false)
            LingxiAccessibilityService.instance?.applyGazeTouchAssistPreference()
            GazeTrackingStore.setEnabled(this, false)
            LingxiAccessibilityService.instance?.applyGazeTrackingPreference()
            refreshGazeTrackingUi()
            appendLog("灵犀", "注视追踪已关闭，摄像头已释放。")
            return
        }
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        ) {
            pendingGazeTrackingRequested = true
            requestPerms.launch(arrayOf(Manifest.permission.CAMERA))
            return
        }
        enableGazeTracking()
    }

    private fun enableGazeTracking() {
        if (!LingxiAccessibilityService.isConnected) {
            appendLog("灵犀", "注视追踪需要先开启「灵犀无障碍」（悬浮反馈点靠它显示）。")
            return
        }
        GazeTrackingStore.setEnabled(this, true)
        LingxiAccessibilityService.instance?.applyGazeTrackingPreference()
        refreshGazeTrackingUi()
        appendLog("灵犀", "👁 注视追踪已开启，看看屏幕四角/联系人，小圆点应该大致跟着你的视线走。")
    }

    private fun refreshGazeTrackingUi() {
        val requested = GazeTrackingStore.isEnabled(this)
        val service = LingxiAccessibilityService.instance
        binding.gazeTrackingButton.text = when {
            requested && service?.isGazeTrackingOn() == true -> "注视追踪：开 ✓（点击关闭）"
            requested && service != null -> "注视追踪：正在启动…"
            requested -> "注视追踪：待恢复（请开启无障碍）"
            else -> "注视追踪：关（点击开启小圆点跟随视线）"
        }
        binding.gazeTouchAssistButton.text = when {
            GazeTrackingStore.isTouchAssistEnabled(this) && service?.isGazeTouchAssistOn() == true ->
                "触控辅助演示：开 ✓（拖动橙色圆点；非眼动输入）"
            GazeTrackingStore.isTouchAssistEnabled(this) ->
                "触控辅助演示：等待注视服务恢复…"
            else -> "触控辅助演示：关（仅用于明确标注的录制）"
        }
    }

    private fun toggleGazeTouchAssist() {
        val service = LingxiAccessibilityService.instance
        if (GazeTrackingStore.isTouchAssistEnabled(this)) {
            GazeTrackingStore.setTouchAssistEnabled(this, false)
            service?.applyGazeTouchAssistPreference()
            refreshGazeTrackingUi()
            appendLog("灵犀", "触控辅助演示已关闭，圆点恢复由真实注视控制。")
            return
        }
        if (!GazeTrackingStore.isEnabled(this) || service?.isGazeTrackingOn() != true) {
            appendLog("灵犀", "请先开启并恢复注视追踪，再开启触控辅助演示。")
            return
        }
        GazeTrackingStore.setTouchAssistEnabled(this, true)
        service.applyGazeTouchAssistPreference()
        refreshGazeTrackingUi()
        appendLog("灵犀", "🟠 触控辅助演示已开启：直接拖动橙色圆点到目标；本模式输入来源为触控。")
    }

    // ---- 唤醒开关 ----
    private fun toggleListening() = setListening(
        ListeningTogglePolicy.nextRequested(
            requested = WakeWordStore.isEnabled(this),
            running = KwsForegroundService.isRunning,
        )
    )

    private fun setListening(on: Boolean) {
        if (on) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
                != PackageManager.PERMISSION_GRANTED
            ) {
                pendingStartListening = true
                requestPerms.launch(arrayOf(Manifest.permission.RECORD_AUDIO, Manifest.permission.POST_NOTIFICATIONS))
                return
            }
            startListening()
        } else {
            // SharedPreferences.apply() updates in-memory state immediately.  Commit the
            // user's request before dispatching the asynchronous service command so the
            // next UI refresh cannot display a stale "on" state.
            WakeWordStore.setEnabled(this, false)
            KwsForegroundService.stop(this)
            refreshWakeUi()
        }
    }

    private fun startListening() {
        // Keep requested state and lifecycle command ordered.  Previously the service was
        // responsible for setting this later in onStartCommand(), so an immediate refresh
        // could still show "off" and the next tap could enqueue an unintended STOP.
        WakeWordStore.setEnabled(this, true)
        KwsForegroundService.start(this)
        refreshWakeUi()
        binding.root.postDelayed({ refreshWakeUi() }, 800)
        appendLog("灵犀", "语音唤醒已开启，对我说「${WakeWordStore.get(this).label}」试试。")
    }

    private fun refreshWakeUi() {
        val requested = WakeWordStore.isEnabled(this)
        val active = requested && KwsForegroundService.isRunning
        binding.wakeToggle.setText(if (requested) R.string.wake_on else R.string.wake_off)
        binding.wakeWordButton.text = WakeWordStore.get(this).label
        binding.wakeStatus.text = when {
            active -> "语音: 聆听中"
            requested -> "语音: 正在恢复"
            else -> "语音: 关闭"
        }
        binding.wakeStatus.setTextColor(Color.parseColor(if (active) "#1F6F5B" else "#5B6472"))
        binding.wakeIsland.setListening(active)
    }

    // ---- 文本输入链路（保留，调试/无网播报时仍可用）----
    private fun onSend() {
        val text = binding.input.text.toString().trim()
        if (text.isEmpty()) return
        binding.input.setText("")
        showChat()
        if (handleTeachCommand(text)) return   // 教学模式快捷指令（学真人操作）
        if (handleMultimodalCommand(text)) return  // 多模态确认（点头/摇头/手势 + 语音冲突消解）
        if (handleKwsTypedConfirmation(text)) return
        if (handleAutoConfirmText(text)) return
        // 普通输入允许把当前唤醒词当作称呼前缀；安全确认和特殊命令已在上方按原文分流，
        // 因此这里的归一化不会把“你好灵犀，确认”等输入扩大成有效授权。
        val normalized = WakeCommandNormalizer
            .stripLeadingWakeWord(text, WakeWordStore.get(this))
            .ifBlank { text }
        if (normalized == text) {
            submitText(text)
        } else {
            appendLog("你", text)
            submitText(normalized, echoUser = false)
        }
    }

    /**
     * 多模态确认快捷指令（赛题题眼：模态对齐 + 冲突消解的端侧演示）：
     *  - 「多模态确认」/「点头确认」：免手确认——开前置摄像头，点头=确认/摇头=取消（也认拇指）。
     *  - 「多模态:不要」：冲突演示——把"不要"当口头应答 + 同窗采到的点头/摇头一起发大脑，
     *    大脑对齐两模态、消解冲突（低风险动作下点头压过口头否定）。命中返回 true。
     */
    private fun handleMultimodalCommand(text: String): Boolean {
        when {
            text == "多模态确认" || text == "点头确认" -> {
                appendLog("你", text)
                startConflictCapture(spoken = null)
                return true
            }
            text.startsWith("多模态:") || text.startsWith("多模态：") -> {
                appendLog("你", text)
                val spoken = text.removePrefix("多模态").trimStart(':', '：', ' ')
                if (spoken.isBlank()) appendLog("灵犀", "格式：多模态:不要（说一句，同时让我看你点头/摇头）")
                else startConflictCapture(spoken = spoken)
                return true
            }
        }
        return false
    }

    /** 开一个采集窗口；spoken=null 表示纯免手确认（头动/手势即应答），非 null 表示口头应答+视觉一起发。 */
    private fun startConflictCapture(spoken: String?) {
        PendingConfirmationStore.load(this)?.let {
            activePendingId = it.pendingId
            activeConfirmationWindowId = it.windowId
        }
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED) {
            pendingConflictRequested = true
            pendingConflictSpoken = spoken
            requestPerms.launch(arrayOf(Manifest.permission.CAMERA))
            return
        }
        appendLog("灵犀", if (spoken == null) "👁 看着我：点头确认 / 摇头取消（3 秒）"
                          else "👁 看着我：3 秒内点头(同意)或摇头(拒绝)")
        updateEvidence(
            if (spoken == null) "等待多模态确认：正在采集点头/摇头/手势，窗口 3 秒。"
            else "等待冲突消解：语音「$spoken」将和 3 秒内的视觉动作一起对齐。"
        )
        autoConfirmCapturing = false
        pendingAutoConfirmSpoken = null
        conflictCapture?.stop()
        conflictCapture = ConflictVisionCapture(this, this) { marks -> onConflictMarks(spoken, marks) }
            .also {
                it.start(
                    windowMs = 3000L,
                    confirmationWindowId = activeConfirmationWindowId,
                )
            }
    }

    /** 文本链路产品化确认：大脑 pending 后自动开窗；窗口内打字会作为文字证据一起融合。 */
    private fun startAutoConfirmCapture() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED) return

        appendLog("灵犀", "👁 正在看你：点头=确认 / 摇头=取消（3 秒）")
        updateEvidence("等待确认：可直接点头/摇头，也可以在 3 秒内输入「确认」或「不要」一起融合。")
        autoConfirmCapturing = true
        pendingAutoConfirmSpoken = null
        conflictCapture?.stop()
        conflictCapture = ConflictVisionCapture(this, this) { marks ->
            val spoken = pendingAutoConfirmSpoken
            autoConfirmCapturing = false
            pendingAutoConfirmSpoken = null
            onConflictMarks(spoken, marks, allowSpokenOnly = spoken != null)
        }.also {
            it.start(
                windowMs = AUTO_CONFIRM_WINDOW_MS,
                confirmationWindowId = activeConfirmationWindowId,
            )
        }
    }

    private fun handleAutoConfirmText(text: String): Boolean {
        if (!autoConfirmCapturing) return false
        appendLog("你", text)
        pendingAutoConfirmSpoken = text
        updateEvidence("确认应答：文字「$text」已收到，正在等待同一窗口内的点头/摇头证据。")
        return true
    }

    /**
     * KWS 创建的 pending 由 KWS 自己持有视觉窗口。显式 typed 应答必须注入同一窗口，
     * 不能由 Activity 先单独执行、再让 KWS 用旧 ID 发送一条 vision-only 重放请求。
     */
    private fun handleKwsTypedConfirmation(text: String): Boolean {
        if (!ConfirmationEvidencePolicy.isExplicitPendingResponseText(text)) return false
        val binding = PendingConfirmationStore.load(this) ?: return false
        if (binding.owner == PendingConfirmationOwner.ACTIVITY) return false
        if (binding.owner == PendingConfirmationOwner.UNKNOWN) {
            appendLog("你", text)
            appendLog("灵犀", "确认上下文来自旧版本，本次没有执行。请重新说出完整指令。")
            updateEvidence("确认来源：unknown\n安全结果：未执行；请重新发起完整任务。")
            return true
        }

        appendLog("你", text)
        val decision = KwsForegroundService.handoffTypedConfirmation(
            text = text,
            pendingId = binding.pendingId,
            confirmationWindowId = binding.windowId,
        )
        return if (decision == TypedConfirmationHandoffDecision.ACCEPT) {
            updateEvidence("确认应答：typed 文字已绑定当前 KWS 窗口；等待同窗视觉采集结束后一次提交。")
            true
        } else {
            appendLog("灵犀", "当前语音确认窗口已关闭或绑定不一致，本次没有执行。请重新说出完整指令。")
            updateEvidence("KWS typed handoff：$decision\n安全结果：未执行。")
            true
        }
    }

    /** 采集窗口结束：把头动/手势组装成应答文本 + 视觉证据，发大脑做对齐与冲突消解。 */
    private fun onConflictMarks(
        spoken: String?,
        marks: List<VisionMark>,
        allowSpokenOnly: Boolean = false,
    ) {
        conflictCapture = null
        if (marks.isEmpty()) {
            if (allowSpokenOnly && !spoken.isNullOrBlank()) {
                appendLog("多模态", "没捕捉到动作 → 按文字「$spoken」确认")
                updateEvidence("视觉证据：无稳定动作\n文字证据：$spoken\n送入大脑：按纯文字确认继续")
                val evidence = ConfirmationEvidencePolicy.build(
                    spoken, ConfirmationTextOrigin.TYPED, emptyList(),
                ) ?: return
                submitText(
                    evidence.normalizedText,
                    echoUser = false,
                    confirmationSource = evidence.source,
                    pendingId = activePendingId,
                    confirmationWindowId = activeConfirmationWindowId,
                )
                return
            }
            appendLog("灵犀", "没看清点头/摇头/手势，咱们再来一次，或直接说「确认」。")
            return
        }
        val evidence = ConfirmationEvidencePolicy.build(
            spoken,
            if (spoken.isNullOrBlank()) null else ConfirmationTextOrigin.TYPED,
            marks,
        )
        if (evidence == null) {
            appendLog("灵犀", "视觉结果不属于已验证的确认或取消手势，本次没有执行。")
            updateEvidence("视觉证据：不支持或不完整\n安全决策：无证据不授权")
            return
        }
        val text = evidence.normalizedText
        appendLog("多模态", "看到[${marks.joinToString("，") { "${it.kind}:${it.label}" }}] → 应答「$text」")
        updateEvidence(
            "视觉证据：${marks.joinToString("，") { "${it.kind}:${it.label}(${String.format("%.2f", it.confidence)})" }}\n" +
                "语音证据：${spoken ?: "免手确认"}\n" +
                "时间关系：窗口内对齐\n" +
                "送入大脑：等待最终意图判断"
        )
        // 统一时间轴：本窗口 [0,3]s = 这次应答的话语窗；视觉 marks 的时间戳落在窗口内→大脑判 during 对齐。
        submitText(
            text,
            vision = marks,
            speechStart = 0.0,
            speechEnd = 3.0,
            confirmationSource = evidence.source,
            pendingId = activePendingId,
            confirmationWindowId = activeConfirmationWindowId,
        )
    }

    /** 输入「教学:进入第一个直播间」开始教学；输入「结束教学」结束并存为可回放技能。命中返回 true。 */
    private fun handleTeachCommand(text: String): Boolean {
        val acc = LingxiAccessibilityService.instance
        when {
            text.startsWith("教学:") || text.startsWith("教学：") -> {
                appendLog("你", text)
                val goal = text.removePrefix("教学").trimStart(':', '：', ' ')
                when {
                    acc == null -> appendLog("灵犀", "请先开启「灵犀无障碍」再教学")
                    goal.isBlank() -> appendLog("灵犀", "格式：教学:进入第一个直播间")
                    else -> {
                        acc.startTeach(goal)
                        appendLog("灵犀", "📚 开始教学「$goal」。去目标 App 手动做一遍，回来发「结束教学」")
                    }
                }
                return true
            }
            text == "结束教学" || text == "停止教学" -> {
                appendLog("你", text)
                if (acc == null) appendLog("灵犀", "无障碍未开")
                else { acc.stopTeach(); appendLog("灵犀", "✅ 教学结束，正在保存为可回放技能") }
                return true
            }
        }
        return false
    }

    private fun submitText(
        text: String,
        vision: List<VisionMark> = emptyList(),
        speechStart: Double? = null,
        speechEnd: Double? = null,
        echoUser: Boolean = true,
        confirmationSource: String? = null,
        pendingId: String? = null,
        confirmationWindowId: String? = null,
    ) {
        // vision 非空时是多模态确认链路的内部转发，外层已回显过用户输入，这里不重复打印
        if (vision.isEmpty() && echoUser) appendLog("你", text)

        lifecycleScope.launch {
            val apps = AppAutomator.installedAppsForBrain(this@MainActivity)
            // 结合当前界面（灵犀在前台时为 null，文本路零回归；无障碍在别的 App 上才有上下文）
            val screen = LingxiAccessibilityService.currentScreen(packageName)
            PendingConfirmationStore.load(this@MainActivity)?.let {
                activePendingId = it.pendingId
                activeConfirmationWindowId = it.windowId
            }
            val inferredTypedSource = confirmationSource ?: if (
                activePendingId != null && ConfirmationEvidencePolicy.isExplicitPendingResponseText(text)
            ) "typed" else null
            val requestPendingId = pendingId ?: if (inferredTypedSource != null) activePendingId else null
            val requestWindowId = confirmationWindowId
                ?: if (inferredTypedSource != null) activeConfirmationWindowId else null
            runCatching {
                brain.interact(InteractRequest(
                    text = text, vision = vision, apps = apps, screen = screen,
                    speech_t_start = speechStart, speech_t_end = speechEnd,
                    pending_id = requestPendingId,
                    confirmation_window_id = requestWindowId,
                    confirmation_source = inferredTypedSource,
                ))
            }
                .onSuccess { resp ->
                    binding.brainStatus.text = "大脑已响应 · 证据融合与执行链路正常"
                    // 大脑只能决定“要做什么”，Android 执行器/系统权限才决定“是否真的完成”。
                    // 短信、电话在权限闸门结束前绝不能先把 LLM 的“已完成”展示给用户。
                    val deviceAction = resp.device_action
                    appendLog(
                        "灵犀",
                        ActionExecutionMessagePolicy.beforeExecution(deviceAction?.type, resp.reply),
                    )
                    if (resp.conflict && resp.conflict_resolution.isNotBlank()) {
                        appendLog("多模态", "语音与动作有冲突 → 已消解：${resp.conflict_resolution}")
                        updateEvidence(
                            "系统判断：检测到语音/动作冲突\n" +
                                "消解结果：${resp.conflict_resolution}\n" +
                            "最终意图：${resp.device_action?.type ?: "已生成回复"}"
                        )
                    }
                    if (resp.status == "pending_confirmation") {
                        activePendingId = resp.pending_id
                        activeConfirmationWindowId = resp.confirmation_window_id
                        PendingConfirmationStore.save(
                            this@MainActivity,
                            resp.pending_id,
                            resp.confirmation_window_id,
                            PendingConfirmationOwner.ACTIVITY,
                        )
                        if (ConfirmationCapturePolicy.shouldAutoStart(resp.status, resp.decision_reason)) {
                            startAutoConfirmCapture()
                        } else {
                            autoConfirmCapturing = false
                            pendingAutoConfirmSpoken = null
                            conflictCapture?.stop()
                            conflictCapture = null
                        }
                    } else {
                        activePendingId = null
                        activeConfirmationWindowId = null
                        PendingConfirmationStore.clear(this@MainActivity)
                    }
                    deviceAction?.let { act ->
                        val r = executor.execute(act.type, act.slots)
                        appendLog("执行", r.message)
                    }
                    // T-16 Step D：说"给他发消息"但注视没解出目标（Step C 已试过、拿到 null）
                    // →大脑常规追问"给谁"，此处弹自己的大卡片联系人选择兜底，比干打字更符合
                    // 赛题旗舰场景的体验。选中的名字按现有多轮机制当作追问的回答原样提交，
                    // 不新增任何大脑侧逻辑。
                    if (resp.need_clarification && GazeDeixis.hasDeixis(text)) {
                        showContactPickerCard()
                    }
                }
                .onFailure {
                    binding.brainStatus.text = "大脑连接异常 · 请检查 brain 服务"
                    appendLog("错误", it.message ?: "网络/服务异常")
                }
        }
    }

    /** T-16 Step D：注视消解落空时的兜底 UI——大卡片列出几个常用联系人，点一个就当"就是他"
     *  的回答提交（复用现有多轮追问机制）。没有可用联系人（无权限/通讯录空）就不弹，
     *  别拿一张空卡片糊弄用户，让原本的文字追问顶上。 */
    private fun showContactPickerCard() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_CONTACTS)
            != PackageManager.PERMISSION_GRANTED
        ) {
            pendingContactPickerRequested = true
            requestPerms.launch(arrayOf(Manifest.permission.READ_CONTACTS))
            return
        }
        val names = ContactPicker.loadFrequentContacts(this)
        if (names.isEmpty()) return
        AlertDialog.Builder(this)
            .setTitle("看着谁说的？")
            .setItems(names.toTypedArray()) { _, which -> submitText(names[which]) }
            .setNegativeButton("我打字说", null)
            .show()
    }

    private fun updateEvidence(text: String) {
        binding.evidencePanel.text = text
    }

    private fun handleAgentReportIntent(intent: Intent?) {
        val report = intent?.getStringExtra(EXTRA_AGENT_REPORT_TEXT).orEmpty()
        if (report.isBlank() || report == lastAgentReport) return
        lastAgentReport = report
        val title = intent?.getStringExtra(EXTRA_AGENT_REPORT_TITLE).orEmpty().ifBlank { "Agent 汇报" }
        val cards = runCatching {
            val json = intent?.getStringExtra(EXTRA_AGENT_REPORT_CARDS)
            if (json.isNullOrBlank()) emptyList()
            else cardsGson.fromJson<List<MessageCard>>(json, object : TypeToken<List<MessageCard>>() {}.type)
        }.getOrDefault(emptyList())
        showChat()
        appendLog("灵犀·报告", "$title\n$report", cards)
        updateEvidence(report)
    }

    /** resume 已经操作到位的目标 App（不重新搜索一次）——跟 AppAutomator 打开 App 是同一条公开通道。 */
    private fun resumeApp(packageName: String) {
        if (packageName.isBlank()) return
        AppAutomator.launchByPackage(this, packageName)
    }

    private fun appendLog(who: String, text: String, cards: List<MessageCard> = emptyList()) {
        if (text.isBlank() && cards.isEmpty()) return
        val message = ConversationMessage(
            role = roleFor(who),
            who = who,
            text = text.trim(),
            cards = cards,
        )
        conversationStore.addMessage(activeConversation(), message)
        chatAdapter.submit(activeConversation().messages)
        scrollChatToBottom()
    }

    private fun scrollChatToBottom() {
        binding.messageList.post {
            if (chatAdapter.itemCount > 0) binding.messageList.scrollToPosition(chatAdapter.itemCount - 1)
        }
    }

    private fun startFreshConversation() {
        currentConversation = conversationStore.newConversation()
        chatAdapter.submit(emptyList())
        updateEvidence("多模态证据会在这里出现：语音、点头/摇头、时间重叠与最终判断。")
    }

    private fun showHome() {
        screenMode = ScreenMode.HOME
        binding.navButton.text = "☰"
        binding.topTitle.text = ""
        binding.newConversationButton.visibility = View.VISIBLE
        binding.wakeIsland.visibility = View.VISIBLE
        updateControlsVisibility()
        binding.evidencePanel.visibility = View.GONE
        binding.messageList.visibility = View.GONE
        binding.input.hint = "Ask anything..."
    }

    private fun showChat() {
        screenMode = ScreenMode.CHAT
        binding.navButton.text = "‹"
        binding.topTitle.text = "灵犀 Assistant"
        binding.brainStatus.text = "Online · 对话进行中"
        binding.newConversationButton.visibility = View.VISIBLE
        binding.wakeIsland.visibility = View.GONE
        updateControlsVisibility()
        binding.evidencePanel.visibility = View.VISIBLE
        binding.messageList.visibility = View.VISIBLE
        binding.input.hint = getString(R.string.hint_input)
    }

    /** ⚙ 折叠菜单：默认收起，主页面只留一个入口，点开才展开唤醒/屏幕视觉/无障碍等功能按钮。 */
    private fun toggleControlsPanel() {
        controlsExpanded = !controlsExpanded
        androidx.transition.TransitionManager.beginDelayedTransition(binding.root)
        updateControlsVisibility()
    }

    private fun updateControlsVisibility() {
        binding.homeControls.visibility =
            if (screenMode == ScreenMode.HOME && controlsExpanded) View.VISIBLE else View.GONE
    }

    private fun showHistoryDialog() {
        val conversations = conversationStore.all()
        if (conversations.isEmpty()) {
            AlertDialog.Builder(this)
                .setTitle("历史对话")
                .setMessage("还没有保存过的对话。发出第一句话后，我会按时间帮你留在这里。")
                .setPositiveButton("知道啦", null)
                .show()
            return
        }
        val timeFormat = SimpleDateFormat("HH:mm", Locale.CHINA)
        val items = mutableListOf<HistoryListItem>()
        var lastBucket: String? = null
        conversations.forEach { record ->
            val bucket = historyBucket(record.updatedAtMs)
            if (bucket != lastBucket) {
                items += HistoryListItem.Header(bucket)
                lastBucket = bucket
            }
            items += HistoryListItem.Row(record, "${timeFormat.format(Date(record.updatedAtMs))} · ${record.messages.size} 条消息")
        }
        val view = layoutInflater.inflate(R.layout.dialog_history, null)
        val dialog = AlertDialog.Builder(this, R.style.Theme_Lingxi_DarkDialog).setView(view).create()
        view.findViewById<androidx.recyclerview.widget.RecyclerView>(R.id.historyList).apply {
            layoutManager = LinearLayoutManager(this@MainActivity)
            adapter = HistoryAdapter(items) { record ->
                dialog.dismiss()
                openConversation(record)
            }
        }
        dialog.show()
    }

    private fun openConversation(conversation: ConversationRecord) {
        currentConversation = conversationStore.find(conversation.id) ?: conversation
        renderConversation(activeConversation())
        showChat()
    }

    private fun renderConversation(conversation: ConversationRecord) {
        chatAdapter.submit(conversation.messages)
        scrollChatToBottom()
    }

    private fun activeConversation(): ConversationRecord {
        return currentConversation ?: conversationStore.newConversation().also { currentConversation = it }
    }

    private fun roleFor(who: String): String = when {
        who == "你" -> ConversationStore.ROLE_USER
        who == "执行" -> ConversationStore.ROLE_EXECUTION
        who == "错误" -> ConversationStore.ROLE_ERROR
        who.contains("报告") -> ConversationStore.ROLE_REPORT
        who == "多模态" -> ConversationStore.ROLE_SYSTEM
        else -> ConversationStore.ROLE_ASSISTANT
    }

    private fun historyBucket(updatedAtMs: Long): String {
        val ageDays = TimeUnit.MILLISECONDS.toDays(System.currentTimeMillis() - updatedAtMs)
        return when {
            ageDays <= 0L -> "今天"
            ageDays == 1L -> "昨天"
            ageDays < 7L -> "7天内"
            ageDays < 30L -> "30天内"
            else -> SimpleDateFormat("yyyy年M月", Locale.CHINA).format(Date(updatedAtMs))
        }
    }


    private enum class ScreenMode {
        HOME,
        CHAT,
    }

    private fun decodeB64(s: String): String =
        runCatching { String(Base64.decode(s, Base64.DEFAULT), Charsets.UTF_8) }.getOrDefault(s)

    companion object {
        const val ACTION_DEBUG_SUBMIT = "com.lingxi.mobile.DEBUG_SUBMIT"
        const val EXTRA_AGENT_REPORT_TITLE = "agent_report_title"
        const val EXTRA_AGENT_REPORT_TEXT = "agent_report_text"
        const val EXTRA_AGENT_REPORT_CARDS = "agent_report_cards"
        private const val AUTO_CONFIRM_WINDOW_MS = 3000L
    }
}
