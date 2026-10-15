package com.lingxi.mobile.voice

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import android.media.ToneGenerator
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import com.lingxi.mobile.MainActivity
import com.lingxi.mobile.accessibility.LingxiAccessibilityService
import com.lingxi.mobile.action.AppAutomator
import com.lingxi.mobile.action.DeviceActionExecutor
import com.lingxi.mobile.action.GazeUiActionPolicy
import com.lingxi.mobile.confirmation.ConfirmationCapturePolicy
import com.lingxi.mobile.confirmation.ConfirmationEvidencePolicy
import com.lingxi.mobile.confirmation.ConfirmationTextOrigin
import com.lingxi.mobile.confirmation.PendingConfirmationOwner
import com.lingxi.mobile.confirmation.PendingConfirmationStore
import com.lingxi.mobile.confirmation.TypedConfirmationHandoffDecision
import com.lingxi.mobile.confirmation.TypedConfirmationHandoffPolicy
import com.lingxi.mobile.model.InteractRequest
import com.lingxi.mobile.model.VisionMark
import com.lingxi.mobile.net.BrainClient
import com.lingxi.mobile.vision.ConflictVisionCapture
import com.lingxi.mobile.vision.GazeCalibration
import com.lingxi.mobile.vision.GazeDeixis
import com.lingxi.mobile.vision.GazeEstimator
import com.k2fsa.sherpa.onnx.OnlineStream
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.sqrt

/**
 * 语音唤醒前台服务（对标 Siri 的"常在聆听"）。
 *
 * 全链路（音频不出端，只文本进大脑，守隐私 D5）：
 *   常驻麦克风 → 端侧 KWS 比对唤醒词 → 命中「你好灵犀/嘿Siri」→ 端侧 ASR 转写一句指令
 *   → BrainClient.interact 调大脑 → 应答文本 + device_action → 系统 TTS 念 + 真办事 → 回到聆听。
 *
 * 单条音频采集线程跑状态机（LISTENING→CAPTURING→PROCESSING），同一个 AudioRecord 复用：
 *  - LISTENING：喂 KWS；命中→进 CAPTURING。
 *  - CAPTURING：喂 ASR；端点(静音)或超时→取文本→进 PROCESSING。
 *  - PROCESSING：丢弃麦克风（不把自己的 TTS 当成新指令）；大脑+TTS 完成→reset→回 LISTENING。
 */
class KwsForegroundService : Service() {

    private enum class State { LISTENING, CAPTURING, PROCESSING }

    @Volatile private var running = false
    @Volatile private var state = State.LISTENING
    /** 助手"长按 Home 唤起"用：跳过唤醒词、直接进按住说话。 */
    @Volatile private var listenNowRequested = false
    /** 临时唤起（非常驻聆听模式）办完一句即停，不长占麦克风。 */
    @Volatile private var oneShot = false
    /** 上一轮大脑回 pending_confirmation → 下一句是"确认应答"，同窗开摄像头采点头/摇头/手势。 */
    @Volatile private var awaitingConfirm = false
    /** 本次 CAPTURING 是确认应答、且已起视觉采集（endpoint 时停采集、走多模态融合）。 */
    @Volatile private var confirmCapturing = false
    @Volatile private var pendingSpokenConfirmText = ""
    @Volatile private var pendingTypedConfirmText = ""
    private var capture: ConflictVisionCapture? = null
    private var captureOwner: ServiceLifecycleOwner? = null
    private var confirmSpeechWindow: SpeechActivityWindow? = null
    private var pendingConfirmSpeechStartSec = 0.0
    private var pendingConfirmSpeechEndSec = 1.0
    @Volatile private var activePendingId: String? = null
    @Volatile private var activeConfirmationWindowId: String? = null
    private var lastEnergyBroadcastMs = 0L
    private val mainHandler = Handler(Looper.getMainLooper())
    private var audioThread: Thread? = null

    private var kws: SherpaKws? = null
    private var asr: SherpaAsr? = null
    private var speaker: Speaker? = null
    private var tone: ToneGenerator? = null
    private val brain = BrainClient()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private lateinit var chosen: WakeWord

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        isRunning = true
        activeInstance = this
        createChannel()
        speaker = Speaker(this)   // TTS 在主线程创建，回调更稳
        // 🐛 用户追问揪出：GazeEstimator.calib 只在 CalibrationActivity/调试界面里主动 load()过，
        // 若 App 进程被系统杀掉重启(MIUI 常见)，这个服务可能在 MainActivity 完全没跑过的新进程里
        // 起来——不补这行，已校准过的用户会静默退化回粗糙的 default() 参数，且没有任何提示。
        GazeEstimator.calib = GazeCalibration.load(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            stopEverything()
            return START_NOT_STICKY
        }
        chosen = WakeWordStore.get(this)
        val listenNow = intent?.action == ACTION_LISTEN_NOW
        startForegroundNotif(if (listenNow) "我在听，请说…" else idleText())

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED
        ) {
            updateNotif("缺少麦克风权限，请在 app 内开启后重试")
            broadcast("灵犀", "没有麦克风权限，语音功能无法开启。")
            stopSelf()
            return START_NOT_STICKY
        }

        if (listenNow) {
            // 助手唤起（长按 Home）：直接按住说话；若非常驻模式则办完即停
            listenNowRequested = true
            if (!running) oneShot = !WakeWordStore.isEnabled(this)
        } else {
            oneShot = false
            WakeWordStore.setEnabled(this, true)
        }

        if (!running) {
            running = true
            audioThread = Thread({ audioLoop() }, "lingxi-kws").also { it.start() }
        }
        return START_STICKY
    }

    // ---- 采集 + 状态机主循环（独立线程）----
    private fun audioLoop() {
        val minBuf = AudioRecord.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        val record = try {
            @Suppress("MissingPermission")
            AudioRecord(
                MediaRecorder.AudioSource.VOICE_RECOGNITION,
                SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT,
                maxOf(minBuf, CHUNK * 2)
            )
        } catch (e: Exception) {
            Log.e(TAG, "AudioRecord 创建失败", e); broadcast("灵犀", "麦克风初始化失败：${e.message}"); stopSelf(); return
        }

        try {
            kws = SherpaKws(assets)
            asr = SherpaAsr(assets)
        } catch (e: Throwable) {
            Log.e(TAG, "语音引擎加载失败", e); broadcast("灵犀", "语音引擎加载失败：${e.message}"); record.release(); stopSelf(); return
        }

        record.startRecording()
        broadcast("灵犀", "语音唤醒已开启，对我说「${chosen.label}」试试。")

        val buf = ShortArray(CHUNK)
        var asrStream: OnlineStream? = null
        var captureStart = 0L
        // 正式端到端延时证据：普通指令也复用确认链已经验证过的轻量语音活动窗。
        // 只保留单调时钟上的起止点，不记录音频、转写文本或回复正文。
        var commandSpeechWindow: SpeechActivityWindow? = null
        // 仅 KWS 唤醒路径设置。显式“按住说话”与确认采集不得剥离用户正文。
        var commandWakeWord: WakeWord? = null
        // 在线 ASR 的末帧假设偶尔会比前一帧短（真机曾把完整命令回退成“发短信说…”）。
        // 每次采集保留最长非空假设，endpoint 时择长提交，避免把已经识别出的首尾静默丢掉。
        var bestText = ""
        // KWS 命中发生在唤醒词说完之后。若用户把“你好灵犀，打开这个”连着说，
        // 后半句开头可能已经落在 ASR 流创建之前；用短时内存环形缓冲把这段音频补给 ASR。
        val wakeAudioPreRoll = WakeAudioPreRoll(
            maxSamples = SAMPLE_RATE * WAKE_PREROLL_MS / 1000,
        )

        while (running) {
            val n = record.read(buf, 0, buf.size)
            if (n <= 0) continue
            val samples = FloatArray(n) { buf[it] / 32768.0f }
            broadcastEnergy(samples)

            // 助手唤起 / 两段确认：跳过唤醒词，立刻进按住说话
            if (listenNowRequested) {
                listenNowRequested = false
                wakeAudioPreRoll.clear()
                if (state != State.CAPTURING) {
                    // 必须先建流、再给用户提示音：旧顺序在低端机上 newStream() 有启动延迟，
                    // 用户一听到蜂鸣就开口时，开头“给妈妈”可能落在流真正就绪之前。
                    asrStream = asr?.newStream()
                    bestText = ""
                    captureStart = SystemClock.elapsedRealtime()
                    state = State.CAPTURING
                    beep()
                    if (awaitingConfirm) {
                        // 确认应答：同时开前置摄像头采点头/摇头/手势（图像不出端，守 D5）
                        awaitingConfirm = false
                        confirmCapturing = true
                        confirmSpeechWindow = SpeechActivityWindow()
                        commandSpeechWindow = null
                        commandWakeWord = null
                        if (hasCameraPermission()) {
                            startConfirmCapture(captureStart)
                        }
                        updateNotif("请确认：点头同意 / 摇头取消，或直接说")
                        broadcast("灵犀", "请确认，可以点头或摇头，也可以直接说。")
                    } else {
                        commandSpeechWindow = SpeechActivityWindow(
                            rmsThreshold = COMMAND_LATENCY_RMS_THRESHOLD,
                        )
                        commandWakeWord = null
                        updateNotif("我在听，请说…")
                        broadcast("灵犀", "我在，请说。")
                    }
                }
            }

            when (state) {
                State.LISTENING -> {
                    wakeAudioPreRoll.append(samples)
                    val hit = kws?.accept(samples)
                    if (hit != null && hit == chosen.id) {
                        val recognizer = asr
                        val stream = recognizer?.newStream()
                        asrStream = stream
                        val replayedChunks = if (recognizer != null && stream != null) {
                            wakeAudioPreRoll.replay { chunk -> recognizer.accept(stream, chunk) }
                        } else {
                            0
                        }
                        bestText = if (recognizer != null && stream != null) {
                            recognizer.text(stream).orEmpty().trim()
                        } else {
                            ""
                        }
                        val replayedMs = wakeAudioPreRoll.sampleCount * 1000L / SAMPLE_RATE
                        Log.i(
                            TAG,
                            "WAKE_ASR_PREROLL chunks=$replayedChunks duration_ms=$replayedMs " +
                                "stream_ready=${stream != null}",
                        )
                        wakeAudioPreRoll.clear()
                        captureStart = SystemClock.elapsedRealtime()
                        commandSpeechWindow = SpeechActivityWindow(
                            rmsThreshold = COMMAND_LATENCY_RMS_THRESHOLD,
                        )
                        commandWakeWord = chosen
                        state = State.CAPTURING
                        beep()
                        updateNotif("听到「${chosen.label}」，请说…")
                        broadcast("灵犀", "我在，请说。")
                    }
                }

                State.CAPTURING -> {
                    val s = asrStream
                    if (s == null) { state = State.LISTENING; continue }
                    asr?.accept(s, samples)
                    val text = asr?.text(s).orEmpty().trim()
                    if (text.length > bestText.length) bestText = text
                    val elapsedMs = SystemClock.elapsedRealtime() - captureStart
                    val speechRms = SpeechActivityWindow.rms(samples)
                    if (confirmCapturing) {
                        confirmSpeechWindow?.observe(
                            elapsedMs, speechRms, text
                        )
                    } else {
                        commandSpeechWindow?.observe(elapsedMs, speechRms, text)
                    }
                    val endpoint = asr?.isEndpoint(s) == true
                    val timedOut = elapsedMs > CAPTURE_MAX_MS
                    if (endpoint && text.isBlank() && bestText.isBlank() && !timedOut) {
                        asr?.reset(s)          // 开口前的假端点：清掉、继续等
                        continue
                    }
                    if (endpoint || timedOut) {
                        val finalText = if (bestText.length >= text.length) bestText else text
                        asr?.reset(s); s.release(); asrStream = null
                        bestText = ""
                        state = State.PROCESSING
                        if (confirmCapturing) {
                            // 多模态确认：endpoint 后继续看一小段时间，给"说完才点头/摇头"的 after 证据留窗口。
                            confirmCapturing = false
                            pendingSpokenConfirmText = finalText
                            val speechWindow = confirmSpeechWindow?.finish(elapsedMs)
                                ?: SpeechActivityWindow.Window(0L, elapsedMs, "missing_tracker")
                            confirmSpeechWindow = null
                            pendingConfirmSpeechStartSec = speechWindow.startMs / 1000.0
                            pendingConfirmSpeechEndSec = speechWindow.endMs / 1000.0
                            Log.i(TAG, "确认语音局部窗：[$pendingConfirmSpeechStartSec," +
                                "$pendingConfirmSpeechEndSec] source=${speechWindow.source} endpoint=${elapsedMs / 1000.0}")
                            val afterWindow = if (endpoint && !timedOut) CONFIRM_AFTER_WINDOW_MS else 0L
                            if (afterWindow > 0L) {
                                // M8/真实异步交互的明确时机提示：到这里语音窗已经冻结，随后蜂鸣和
                                // UI 文案只告诉用户“现在可以做 after 动作”，不会被计入语音证据。
                                updateNotif("已听到，继续看 2 秒等点头/摇头…")
                                beep()
                                broadcast("灵犀", "已听到，请现在点头或摇头（2 秒）。")
                            }
                            mainHandler.postDelayed({
                                val activeCapture = capture
                                if (activeCapture != null) activeCapture.stop()
                                else onConfirmMarks(emptyList())
                            }, afterWindow)
                        } else {
                            val speechWindow = commandSpeechWindow?.finish(elapsedMs)
                                ?: SpeechActivityWindow.Window(0L, elapsedMs, "missing_tracker")
                            commandSpeechWindow = null
                            val submittedText = commandWakeWord?.let {
                                WakeCommandNormalizer.stripLeadingWakeWord(finalText, it)
                            } ?: finalText
                            commandWakeWord = null
                            handleCommand(
                                submittedText,
                                captureStart + speechWindow.endMs,
                                speechWindow.source,
                            )
                        }
                    }
                }

                State.PROCESSING -> { /* 丢弃麦克风，等大脑+TTS 完成后 resume */ }
            }
        }

        runCatching { record.stop() }
        runCatching { record.release() }
        asrStream?.release()
    }

    /** 拿到指令文本 → 大脑 → 应答(TTS) + 真办事 → 念完恢复聆听。 */
    private fun handleCommand(
        text: String,
        speechEndElapsedMs: Long? = null,
        speechWindowSource: String = "missing",
    ) {
        if (text.isBlank()) {
            broadcast("灵犀", "没听清，再说一次试试～")
            speaker?.speak("没听清，再说一次试试") { resumeListening() } ?: resumeListening()
            return
        }
        broadcast("你", text)
        updateNotif("正在思考：$text")
        scope.launch {
            val reply = try {
                // T-16 v2：话语含指示代词("他/这个"…)→ 直接查无障碍服务持续维护的注视滚动缓冲区
                // （追踪常开时才有数据），不再现开摄像头等窗口；追踪未开/无障碍未连/看不稳 → null，
                // 纯语音路零回归。
                val touchAssist =
                    LingxiAccessibilityService.instance?.isGazeTouchAssistOn() == true
                val exactDirectGazeCommand = GazeUiActionPolicy.isDirectOpenCommand(text)
                val touchAssistFallback = touchAssist &&
                    GazeUiActionPolicy.isTouchAssistFallbackCommand(text)
                val directGazeCommand = exactDirectGazeCommand || touchAssistFallback
                val gaze = if (directGazeCommand || GazeDeixis.hasDeixis(text)) queryGazeVote() else null
                Log.i(
                    TAG,
                    "GAZE_UI_ROUTE direct=$directGazeCommand exact=$exactDirectGazeCommand " +
                        "fallback=$touchAssistFallback target_ready=${gaze != null} " +
                        "touch_assist=$touchAssist",
                )
                if (directGazeCommand) {
                    when {
                        gaze == null ->
                            "我还没有稳定看清你指的是哪一个，请看着目标再说一次“打开这个”。"
                        !GazeUiActionPolicy.isSafeTarget(gaze.target) ->
                            "这个目标可能产生重要操作，请直接说出名称并明确确认。"
                        else -> withContext(Dispatchers.Main) {
                            DeviceActionExecutor(this@KwsForegroundService).execute(
                                "click_ui_target",
                                mapOf("target" to gaze.target),
                            ).message
                        }
                    }
                } else {
                val apps = AppAutomator.installedAppsForBrain(this@KwsForegroundService)
                // 结合当前界面：被唤起时读前台 App 屏幕文本当上下文（灵犀自己在前台则为 null）
                val screen = LingxiAccessibilityService.currentScreen(packageName)
                PendingConfirmationStore.load(this@KwsForegroundService)?.let {
                    activePendingId = it.pendingId
                    activeConfirmationWindowId = it.windowId
                }
                val resp = brain.interact(InteractRequest(
                    text = text, apps = apps, screen = screen,
                    gaze_target = gaze?.target, vision = listOfNotNull(gaze?.mark),
                    pending_id = if (
                        activePendingId != null && ConfirmationEvidencePolicy.isExplicitConfirmationText(text)
                    ) activePendingId else null,
                    confirmation_window_id = if (
                        activePendingId != null && ConfirmationEvidencePolicy.isExplicitConfirmationText(text)
                    ) activeConfirmationWindowId else null,
                    confirmation_source = if (
                        activePendingId != null && ConfirmationEvidencePolicy.isExplicitConfirmationText(text)
                    ) if (gaze == null) "spoken" else "spoken_visual" else null,
                ))
                // 大脑回 pending_confirmation → 下一句走"多模态确认"（点头/摇头/手势 + 语音）
                awaitingConfirm = ConfirmationCapturePolicy.shouldAutoStart(
                    resp.status, resp.decision_reason,
                )
                if (awaitingConfirm) {
                    clearPendingConfirmationText()
                    activePendingId = resp.pending_id
                    activeConfirmationWindowId = resp.confirmation_window_id
                    PendingConfirmationStore.save(
                        this@KwsForegroundService,
                        resp.pending_id,
                        resp.confirmation_window_id,
                        PendingConfirmationOwner.KWS,
                    )
                } else {
                    clearPendingConfirmationText()
                    activePendingId = null
                    activeConfirmationWindowId = null
                    PendingConfirmationStore.clear(this@KwsForegroundService)
                }
                var spokenReply = resp.reply
                resp.device_action?.let { act ->
                    withContext(Dispatchers.Main) {
                        val r = DeviceActionExecutor(this@KwsForegroundService).execute(act.type, act.slots)
                        broadcast("执行", r.message)
                        if (act.type == "query_calendar") spokenReply = r.message
                    }
                }
                spokenReply
                }
            } catch (e: Exception) {
                Log.e(TAG, "调大脑失败", e); "网络或服务异常：${e.message}"
            }
            speechEndElapsedMs?.let { speechEnd ->
                val latencyMs = (SystemClock.elapsedRealtime() - speechEnd).coerceAtLeast(0L)
                Log.i(
                    TAG,
                    "E2E_RESPONSE latency_ms=$latencyMs anchor=speech_activity_end " +
                        "surface=reply_dispatch source=$speechWindowSource",
                )
            }
            broadcast("灵犀", reply)
            withContext(Dispatchers.Main) {
                speaker?.speak(reply) { resumeListening() } ?: resumeListening()
            }
        }
    }

    private fun hasCameraPermission() = ContextCompat.checkSelfPermission(
        this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED

    private data class GazeCaptureResult(val target: String, val mark: VisionMark)

    /** T-16 v2：注视追踪已常开且持续维护滚动缓冲区——这里只是"查询当前稳定看向谁"，
     *  不再现开摄像头/等待窗口。无障碍未连接、追踪未开（缓冲区天然为空）、或最近样本
     *  不够稳定 → null，调用方按纯语音路处理，不瞎猜（[GazeVote] 的"不确定就不猜"设计）。 */
    private fun queryGazeVote(): GazeCaptureResult? {
        val acc = LingxiAccessibilityService.instance ?: return null
        val vote = acc.currentGazeVote() ?: return null
        val mark = VisionMark("gaze", vote.target, vote.confidence.toDouble(), vote.t0Ms / 1000.0, vote.t1Ms / 1000.0)
        return GazeCaptureResult(vote.target, mark)
    }

    /** 确认环节开前置摄像头一个短窗口采点头/摇头/手势；起不来则降级为纯语音确认。 */
    private fun startConfirmCapture(timelineOriginMs: Long) {
        mainHandler.post {
            runCatching {
                val owner = ServiceLifecycleOwner().also { it.resume() }
                captureOwner = owner
                capture = ConflictVisionCapture(this, owner) { marks -> onConfirmMarks(marks) }
                    .also {
                        it.start(
                            CAPTURE_MAX_MS + CONFIRM_AFTER_WINDOW_MS + 500L,
                            timelineOriginMs,
                            activeConfirmationWindowId,
                        )
                    }
            }.onFailure {
                Log.e(TAG, "确认采集启动失败，降级纯语音确认", it)
                capture = null
            }
        }
    }

    /** 采集窗口结束（capture.onDone，主线程）：把点头/摇头/手势 + 口头应答一起发大脑做对齐+冲突消解。 */
    private fun onConfirmMarks(marks: List<VisionMark>) {
        runCatching { captureOwner?.destroy() }
        capture = null; captureOwner = null
        if (!running) return   // 服务正在停 → 不再发请求
        val spoken = pendingSpokenConfirmText.trim()
        val typed = pendingTypedConfirmText.trim()
        val resolved = TypedConfirmationHandoffPolicy.resolveText(typed, spoken)
        val evidence = ConfirmationEvidencePolicy.build(
            resolved.text,
            resolved.origin,
            marks,
        ) ?: run {
            clearPendingConfirmationText()
            val stored = PendingConfirmationStore.load(this)
            activePendingId = stored?.pendingId
            activeConfirmationWindowId = stored?.windowId
            awaitingConfirm = stored?.owner == PendingConfirmationOwner.KWS
            Log.i(
                TAG,
                "CONFIRM_SKIPPED reason=NO_EVIDENCE pending=${if (awaitingConfirm) "present" else "none"}",
            )
            broadcast(
                "多模态",
                if (awaitingConfirm) "没有检测到有效确认，待确认动作仍保留。"
                else "没有检测到有效确认，本次没有执行。",
            )
            resumeListening()
            return
        }
        val text = evidence.normalizedText
        val source = evidence.source
        clearPendingConfirmationText()
        if (marks.isNotEmpty()) {
            broadcast("多模态", "应答「$text」（看到 ${marks.joinToString("，") { "${it.kind}:${it.label}" }}）")
        }
        val visualEndSec = maxOf(1.0, marks.mapNotNull { it.t_end }.maxOrNull() ?: 1.0)
        val speechStartSec = if (resolved.origin == ConfirmationTextOrigin.SPOKEN) {
            pendingConfirmSpeechStartSec
        } else {
            0.0
        }
        val speechEndSec = if (resolved.origin == ConfirmationTextOrigin.SPOKEN) {
            pendingConfirmSpeechEndSec
        } else {
            visualEndSec
        }
        val relations = marks.joinToString(",") { mark ->
            val relation = when {
                (mark.t_end ?: speechEndSec) <= speechStartSec -> "BEFORE"
                (mark.t_start ?: speechStartSec) >= speechEndSec -> "AFTER"
                else -> "DURING"
            }
            "${mark.kind}:${mark.label}=$relation[${mark.t_start},${mark.t_end}]"
        }
        Log.i(TAG, "M7M8 speech=[$speechStartSec,$speechEndSec] vision={$relations}")
        if (relations.isNotBlank()) broadcast("多模态", "时间对齐：$relations")
        handleConfirmWithMarks(text, marks, speechStartSec, speechEndSec, source)
    }

    private fun handleConfirmWithMarks(
        text: String,
        marks: List<VisionMark>,
        speechStartSec: Double,
        speechEndSec: Double,
        confirmationSource: String,
    ) {
        updateNotif("正在处理确认…")
        scope.launch {
            val reply = try {
                val apps = AppAutomator.installedAppsForBrain(this@KwsForegroundService)
                val screen = LingxiAccessibilityService.currentScreen(packageName)
                val resp = brain.interact(InteractRequest(
                    text = text, vision = marks, apps = apps, screen = screen,
                    speech_t_start = speechStartSec, speech_t_end = speechEndSec,
                    pending_id = activePendingId,
                    confirmation_window_id = activeConfirmationWindowId,
                    confirmation_source = confirmationSource,
                ))
                // M7/M8 正式跑批的机器安全证据：时间关系正确不等于允许执行。
                // 只记录结构化决策元数据，不记录联系人、短信正文或生物特征。
                Log.i(
                    TAG,
                    "CONFIRM_RESULT intent=${resp.intent} status=${resp.status} " +
                        "action=${resp.device_action?.type ?: "none"} conflict=${resp.conflict}",
                )
                awaitingConfirm = ConfirmationCapturePolicy.shouldAutoStart(
                    resp.status, resp.decision_reason,
                )
                if (awaitingConfirm) {
                    activePendingId = resp.pending_id
                    activeConfirmationWindowId = resp.confirmation_window_id
                    PendingConfirmationStore.save(
                        this@KwsForegroundService,
                        resp.pending_id,
                        resp.confirmation_window_id,
                        PendingConfirmationOwner.KWS,
                    )
                } else {
                    activePendingId = null
                    activeConfirmationWindowId = null
                    PendingConfirmationStore.clear(this@KwsForegroundService)
                }
                if (resp.conflict && resp.conflict_resolution.isNotBlank()) {
                    broadcast("多模态", "语音与动作有冲突 → 已消解：${resp.conflict_resolution}")
                }
                var spokenReply = resp.reply
                resp.device_action?.let { act ->
                    withContext(Dispatchers.Main) {
                        val r = DeviceActionExecutor(this@KwsForegroundService).execute(act.type, act.slots)
                        broadcast("执行", r.message)
                        if (act.type == "query_calendar") spokenReply = r.message
                    }
                }
                spokenReply
            } catch (e: Exception) {
                Log.e(TAG, "确认调大脑失败", e); "网络或服务异常：${e.message}"
            }
            broadcast("灵犀", reply)
            withContext(Dispatchers.Main) {
                speaker?.speak(reply) { resumeListening() } ?: resumeListening()
            }
        }
    }

    private fun resumeListening() {
        if (awaitingConfirm) {
            // 两段确认：不必再喊唤醒词，直接进入"听确认"（CAPTURING 入口会同时开摄像头采点头/摇头）
            listenNowRequested = true
            updateNotif("请确认：点头同意 / 摇头取消")
            return
        }
        if (oneShot) { stopEverything(); return }   // 临时唤起：办完一句即停
        kws?.reset()
        state = State.LISTENING
        updateNotif(idleText())
    }

    private fun clearPendingConfirmationText() {
        pendingSpokenConfirmText = ""
        pendingTypedConfirmText = ""
    }

    /** Called synchronously from MainActivity in the same process; no text is logged. */
    private fun acceptTypedConfirmation(
        text: String,
        pendingId: String?,
        confirmationWindowId: String?,
    ): TypedConfirmationHandoffDecision {
        val stored = PendingConfirmationStore.load(this)
        val acceptingWindow = awaitingConfirm || confirmCapturing || capture != null
        val decision = TypedConfirmationHandoffPolicy.decide(
            text = text,
            requestedPendingId = pendingId,
            requestedWindowId = confirmationWindowId,
            storedBinding = stored,
            activePendingId = activePendingId,
            activeWindowId = activeConfirmationWindowId,
            serviceRunning = running,
            acceptingWindow = acceptingWindow,
        )
        if (decision == TypedConfirmationHandoffDecision.ACCEPT) {
            pendingTypedConfirmText = text.trim()
            Log.i(TAG, "TYPED_HANDOFF_ACCEPTED owner=kws pending=present window=open")
            updateNotif("已收到文字确认，正在融合当前手势窗口…")
            broadcast("灵犀", "已收到文字确认，正在和当前手势窗口一起判断。")
        } else {
            Log.i(TAG, "TYPED_HANDOFF_REJECTED reason=$decision")
        }
        return decision
    }

    private fun beep() = runCatching {
        if (tone == null) tone = ToneGenerator(AudioManager.STREAM_MUSIC, 80)
        tone?.startTone(ToneGenerator.TONE_PROP_BEEP, 150)
    }

    private fun stopEverything() {
        running = false
        awaitingConfirm = false; confirmCapturing = false; confirmSpeechWindow = null
        clearPendingConfirmationText()
        activePendingId = null; activeConfirmationWindowId = null
        PendingConfirmationStore.clear(this)
        mainHandler.post { runCatching { capture?.stop() }; runCatching { captureOwner?.destroy() }; capture = null; captureOwner = null }
        WakeWordStore.setEnabled(this, false)
        audioThread?.let { runCatching { it.join(800) } }
        audioThread = null
        runCatching { kws?.release() }
        runCatching { asr?.release() }
        runCatching { tone?.release() }
        kws = null; asr = null; tone = null
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        running = false
        isRunning = false
        if (activeInstance === this) activeInstance = null
        runCatching { capture?.stop() }
        runCatching { captureOwner?.destroy() }
        capture = null; captureOwner = null
        runCatching { tone?.release() }
        runCatching { speaker?.release() }
        speaker = null
        super.onDestroy()
    }

    // ---- 前台通知 ----
    private fun idleText() = "正在聆听唤醒词「${chosen.label}」"

    private fun startForegroundNotif(text: String) {
        val type = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
            ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE else 0
        ServiceCompat.startForeground(this, NOTIF_ID, buildNotif(text), type)
    }

    private fun updateNotif(text: String) {
        (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
            .notify(NOTIF_ID, buildNotif(text))
    }

    private fun buildNotif(text: String): Notification {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val stop = PendingIntent.getService(
            this, 1, Intent(this, KwsForegroundService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("灵犀 · 语音唤醒")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setOngoing(true)
            .setContentIntent(open)
            .addAction(android.R.drawable.ic_media_pause, "停止", stop)
            .build()
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID, "语音唤醒", NotificationManager.IMPORTANCE_LOW
            ).apply { description = "灵犀常驻聆听唤醒词" }
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .createNotificationChannel(ch)
        }
    }

    private fun broadcast(who: String, text: String) {
        sendBroadcast(Intent(ACTION_EVENT).apply {
            setPackage(packageName)
            putExtra(EXTRA_WHO, who)
            putExtra(EXTRA_TEXT, text)
        })
    }

    private fun broadcastEnergy(samples: FloatArray) {
        val now = System.currentTimeMillis()
        if (now - lastEnergyBroadcastMs < 80L || samples.isEmpty()) return
        lastEnergyBroadcastMs = now

        var sum = 0.0
        for (sample in samples) sum += (sample * sample).toDouble()
        val rms = sqrt(sum / samples.size).toFloat()
        val energy = ((rms - 0.01f) / 0.18f).coerceIn(0f, 1f)
        sendBroadcast(Intent(ACTION_EVENT).apply {
            setPackage(packageName)
            putExtra(EXTRA_KIND, KIND_ENERGY)
            putExtra(EXTRA_ENERGY, energy)
            putExtra(EXTRA_LISTENING, state != State.PROCESSING)
        })
    }

    companion object {
        private const val TAG = "LingxiKws"
        // 仅用于普通指令端到端延迟取证，不参与 ASR endpoint、确认窗口或设备动作。
        // 低端真机 VOICE_RECOGNITION 的归一化能量低于确认链保守阈值；0.012 仍高于
        // 首页能量可视化采用的 0.01 静音基线。多模态确认继续使用默认 0.025。
        private const val COMMAND_LATENCY_RMS_THRESHOLD = 0.012f
        private const val SAMPLE_RATE = 16000
        private const val CHUNK = 1600          // 100ms / 帧
        private const val WAKE_PREROLL_MS = 1800
        private const val CAPTURE_MAX_MS = 8000 // 唤醒后最长录 8s 指令
        // 真机诊断：1.5s 能采到 8.59° 明显低头，但来不及稳定采到抬回（pitchRev=0）。
        // 2s 与 HeadMotionDetector.MAX_EVENT_MS 上限一致，保留完整“低头→抬回”周期，
        // 不降低幅度/反转门槛，也不改变 AFTER 必须晚于已冻结语音窗的判定。
        private const val CONFIRM_AFTER_WINDOW_MS = 2000L
        private const val NOTIF_ID = 0x1ED9
        private const val CHANNEL_ID = "lingxi_kws"

        const val ACTION_STOP = "com.lingxi.mobile.KWS_STOP"
        const val ACTION_LISTEN_NOW = "com.lingxi.mobile.KWS_LISTEN_NOW"
        const val ACTION_EVENT = "com.lingxi.mobile.VOICE_EVENT"
        const val EXTRA_WHO = "who"
        const val EXTRA_TEXT = "text"
        const val EXTRA_KIND = "kind"
        const val EXTRA_ENERGY = "energy"
        const val EXTRA_LISTENING = "listening"
        const val KIND_ENERGY = "energy"

        /** 真实 Service 生命周期；不要用持久化“用户想开启”偏好冒充当前正在聆听。 */
        @Volatile
        var isRunning: Boolean = false
            private set

        @Volatile
        private var activeInstance: KwsForegroundService? = null

        fun handoffTypedConfirmation(
            text: String,
            pendingId: String?,
            confirmationWindowId: String?,
        ): TypedConfirmationHandoffDecision =
            activeInstance?.acceptTypedConfirmation(text, pendingId, confirmationWindowId)
                ?: TypedConfirmationHandoffDecision.WINDOW_CLOSED

        fun start(ctx: Context) {
            val i = Intent(ctx, KwsForegroundService::class.java)
            ContextCompat.startForegroundService(ctx, i)
        }

        /** 默认助手唤起（长按 Home）：跳过唤醒词，直接按住说话。 */
        fun listenNow(ctx: Context) {
            val i = Intent(ctx, KwsForegroundService::class.java).setAction(ACTION_LISTEN_NOW)
            ContextCompat.startForegroundService(ctx, i)
        }

        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, KwsForegroundService::class.java).setAction(ACTION_STOP))
        }
    }
}
