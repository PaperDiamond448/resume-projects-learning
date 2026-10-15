package com.lingxi.mobile.accessibility

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ApplicationInfo
import android.graphics.Path
import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.util.Base64
import android.util.Log
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo

/**
 * 灵犀无障碍服务 —— "操作任意 App 的手"。
 *
 * 系统授权后可读取任意前台 App 的屏幕内容、点击控件、往输入框填字、执行手势。
 * 上层把这些原子能力编排成"打开 App→搜联系人→输入→发送"即可驱动任意 App。
 *
 * debug 包内置一个 adb 可触发的命令接口（ACC_CMD broadcast），供回归与诊断使用。
 * release 包不注册该接收器，第三方 App 无法借广播驱动无障碍动作。
 */
class LingxiAccessibilityService : AccessibilityService() {

    private val debugCommandsEnabled: Boolean
        get() = (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0

    companion object {
        private const val TAG = "LingxiAcc"
        const val ACTION_CMD = "com.lingxi.mobile.ACC_CMD"
        const val ACTION_STATE_CHANGED = "com.lingxi.mobile.ACCESSIBILITY_STATE_CHANGED"
        private const val GAZE_BUFFER_MS = 2000L              // T-16 v2 滚动缓冲窗口
        private const val GAZE_CANDIDATE_REFRESH_MS = 300L    // 候选刷新节流
        private const val TOUCH_REACQUIRE_MS = 500L
        private const val TOUCH_CANDIDATE_REFRESH_MS = 1500L

        @Volatile
        var instance: LingxiAccessibilityService? = null
            private set

        val isConnected: Boolean get() = instance != null

        /**
         * 给大脑 /interact 的"当前界面上下文"：{app:前台包名, 可见文本:摘要}。
         * 无障碍未连 / 灵犀自己在前台 / 无可见文本 → 返回 null（不污染上下文、文本路零回归）。
         * 让赛题"结合当前界面理解意图"真正生效，且只传文本（守隐私 D5）。
         */
        fun currentScreen(selfPkg: String): Map<String, String>? =
            instance?.screenContextForBrain(selfPkg)
    }

    private val cmdReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val arg = intent.getStringExtra("arg").orEmpty()
            when (intent.getStringExtra("op")) {
                "read" -> Log.i(TAG, "SCREEN>>>\n" + readScreen())
                "click" -> Log.i(TAG, "click '$arg' => " + clickByText(arg))
                "settext" -> Log.i(TAG, "settext => " + setFocusedText(decodeB64(arg)))
                "tap" -> intent.getStringExtra("xy")?.split(",")?.let {
                    tap(it[0].toFloat(), it[1].toFloat()); Log.i(TAG, "tap $arg")
                }
                "back" -> Log.i(TAG, "back => " + pressBack())
                "home" -> Log.i(TAG, "home => " + pressHome())
                "automate" -> Log.i(TAG, "automate => " + com.lingxi.mobile.action.AppAutomator
                    .handle(this@LingxiAccessibilityService, decodeB64(arg)))
                "agent" -> Log.i(TAG, "agent => " + com.lingxi.mobile.action.AppAutomator
                    .runAgentGoal(decodeB64(arg)))
                "teach_start" -> startTeach(decodeB64(arg))   // arg=目标(b64)；进教学模式录真人操作
                "teach_stop" -> stopTeach()                   // 结束→存为可回放技能
                // T-05 grounding 离线集调试用：把当前屏幕的视觉候选(与真实 SoM 管线同源，
                // AppAutomator.visualCandidates() 也是包这一层)写成 JSON 文件（不走 logcat——
                // logcat 单条消息有长度上限，候选一多就被截断解析不出来）。同时绕开
                // uiautomator dump 在部分 MIUI 状态下静默写出空文件的问题：这条直接读 a11y
                // 树自己算候选，更贴近真实管线也更可靠。写到 getExternalFilesDir，adb pull
                // 不需要 run-as。
                "dump_visual" -> {
                    val json = clickableElementsForVisual()
                        .joinToString(",", prefix = "[", postfix = "]") { e ->
                            val t = e.text.replace("\\", "\\\\").replace("\"", "\\\"")
                            "{\"text\":\"$t\",\"bounds\":[${e.bounds.left},${e.bounds.top},${e.bounds.right},${e.bounds.bottom}],\"clickable\":${e.clickable}}"
                        }
                    val file = java.io.File(getExternalFilesDir(null), "grounding_dump.json")
                    file.writeText(json)
                    Log.i(TAG, "dump_visual => 写了 ${file.absolutePath}（${json.length} 字节）")
                }
                else -> Log.w(TAG, "未知命令: ${intent.getStringExtra("op")}")
            }
        }
    }

    override fun onServiceConnected() {
        super.onServiceConnected()
        instance = this
        if (debugCommandsEnabled) {
            val filter = IntentFilter(ACTION_CMD)
            if (Build.VERSION.SDK_INT >= 33) {
                registerReceiver(cmdReceiver, filter, Context.RECEIVER_EXPORTED)
            } else {
                @Suppress("UnspecifiedRegisterReceiverFlag")
                registerReceiver(cmdReceiver, filter)
            }
        }
        // T-16 v2：服务重连（MIUI 杀后台后用户重开无障碍）→ 恢复用户上次的注视追踪开/关意愿。
        applyGazeTrackingPreference()
        Log.i(TAG, "无障碍已连接，debug命令接口=${if (debugCommandsEnabled) "开启" else "关闭"}")
        notifyStateChanged()
    }

    // —— 教学模式（学真人操作）：录下用户手动点击的元素级步骤，供之后【确定性回放】——
    @Volatile
    private var teachMode = false
    private var teachGoal = ""
    @Volatile
    private var lastWindowClassName = ""
    private var lastScrollAt = 0L
    private var lastClickAt = 0L
    private val demoSteps =
        java.util.Collections.synchronizedList(mutableListOf<com.lingxi.mobile.model.DemoStep>())

    fun teaching(): Boolean = teachMode

    fun foregroundWindowClassName(): String = lastWindowClassName

    fun foregroundPackageName(): String =
        rootInActiveWindow?.packageName?.toString().orEmpty()

    fun startTeach(goal: String) {
        teachGoal = goal.trim()
        demoSteps.clear()
        teachMode = true
        Log.i(TAG, "教学开始: $teachGoal")
        com.lingxi.mobile.ui.OverlayProgress.update(
            this, "📚 教学中：$teachGoal\n请手动操作一遍，完成后结束教学")
    }

    fun stopTeach() {
        teachMode = false
        val steps = demoSteps.toList()
        Log.i(TAG, "教学结束: $teachGoal 共 ${steps.size} 步")
        com.lingxi.mobile.action.AppAutomator.saveDemo(this, teachGoal, steps)
    }

    // —— T-16 v2：注视追踪常开 + 悬浮反馈点 ——
    // 苹果式 UX：总开关打开时相机持续跑，屏幕上悬浮小圆点实时跟随注视点；语音链路说话时
    // 不再现开摄像头，直接查这里持续维护的滚动缓冲区。关掉开关=摄像头进程级别释放，不常开。
    private var gazeCamera: com.lingxi.mobile.vision.GazeCamera? = null
    private var gazeOwner: com.lingxi.mobile.voice.ServiceLifecycleOwner? = null
    private val gazeBuffer =
        java.util.Collections.synchronizedList(mutableListOf<Pair<Long, String?>>())
    @Volatile private var gazeCandidates: List<com.lingxi.mobile.vision.ScreenElement> = emptyList()
    @Volatile private var gazeCandidatesAt = 0L
    @Volatile private var gazeCandidateWindowClass = ""
    @Volatile private var gazeTouchAssistOn = false
    @Volatile private var touchAssistNx = 0.5f
    @Volatile private var touchAssistNy = 0.5f
    @Volatile private var touchAssistTarget: String? = null
    @Volatile private var touchAssistLastResolveAt = 0L
    private val touchAssistHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private val touchAssistSampler = object : Runnable {
        override fun run() {
            if (!gazeTouchAssistOn) return
            val now = android.os.SystemClock.elapsedRealtime()
            // 页面切换会主动清空旧目标，防止把上一页文字带到新页面。旧实现随后只复用 null，
            // 必须再次拖动圆点才会恢复；现在圆点静止时也会按节流周期自动重新解析当前位置。
            if (touchAssistTarget == null && now - touchAssistLastResolveAt >= TOUCH_REACQUIRE_MS) {
                touchAssistLastResolveAt = now
                touchAssistTarget = resolveGazeTarget(
                    touchAssistNx,
                    touchAssistNy,
                    true,
                    TOUCH_CANDIDATE_REFRESH_MS,
                )
                Log.i(TAG, "TOUCH_TARGET_REACQUIRE ready=${touchAssistTarget != null}")
            }
            appendGazeSample(touchAssistTarget)
            touchAssistHandler.postDelayed(this, 100L)
        }
    }

    fun isGazeTrackingOn(): Boolean = gazeCamera != null
    fun isGazeTouchAssistOn(): Boolean = gazeTouchAssistOn

    /** 按持久化偏好生效（服务重连时 / MainActivity 改完偏好后调用）。 */
    fun applyGazeTrackingPreference() {
        if (com.lingxi.mobile.vision.GazeTrackingStore.isEnabled(this)) startGazeTrackingInternal()
        else stopGazeTrackingInternal()
    }

    fun applyGazeTouchAssistPreference() {
        val requested = com.lingxi.mobile.vision.GazeTrackingStore.isTouchAssistEnabled(this)
        val enabled = requested && gazeCamera != null
        gazeTouchAssistOn = enabled
        touchAssistTarget = null
        touchAssistLastResolveAt = 0L
        touchAssistHandler.removeCallbacks(touchAssistSampler)
        com.lingxi.mobile.ui.GazeFeedbackOverlay.setTouchAssist(this, enabled) { nx, ny ->
            touchAssistNx = nx
            touchAssistNy = ny
            touchAssistLastResolveAt = android.os.SystemClock.elapsedRealtime()
            touchAssistTarget = resolveGazeTarget(nx, ny, true, TOUCH_CANDIDATE_REFRESH_MS)
            appendGazeSample(touchAssistTarget)
        }
        if (enabled) touchAssistHandler.post(touchAssistSampler)
        Log.i(TAG, "触控辅助演示=${if (enabled) "开启" else "关闭"}（输入来源=touch）")
        notifyStateChanged()
    }

    private fun startGazeTrackingInternal() {
        if (gazeCamera != null) return
        val owner = com.lingxi.mobile.voice.ServiceLifecycleOwner().also { it.resume() }
        gazeOwner = owner
        synchronized(gazeBuffer) { gazeBuffer.clear() }
        gazeCandidates = emptyList(); gazeCandidatesAt = 0L; gazeCandidateWindowClass = ""
        gazeCamera = com.lingxi.mobile.vision.GazeCamera(this, owner) { nx, ny, _, _, hyaw, hpitch, face, lm ->
            val posture = com.lingxi.mobile.vision.GazePosture.evaluate(lm, hyaw, hpitch, face)
            onGazeFrame(nx, ny, face && posture.ok)
        }.also { it.start() }
        com.lingxi.mobile.ui.GazeFeedbackOverlay.show(this)
        applyGazeTouchAssistPreference()
        Log.i(TAG, "注视追踪已开启（常驻，悬浮反馈点已挂）")
        notifyStateChanged()
    }

    private fun stopGazeTrackingInternal() {
        if (gazeCamera == null) return
        gazeTouchAssistOn = false
        touchAssistTarget = null
        touchAssistLastResolveAt = 0L
        touchAssistHandler.removeCallbacks(touchAssistSampler)
        gazeCamera?.stop(); gazeCamera = null
        gazeOwner?.destroy(); gazeOwner = null
        com.lingxi.mobile.ui.GazeFeedbackOverlay.hide()
        synchronized(gazeBuffer) { gazeBuffer.clear() }
        Log.i(TAG, "注视追踪已关闭，摄像头已释放")
        notifyStateChanged()
    }

    private fun notifyStateChanged() {
        sendBroadcast(Intent(ACTION_STATE_CHANGED).setPackage(packageName))
    }

    /** GazeCamera 每帧回调（已在主线程）：更新悬浮点 + 定期刷新候选 + 灌进滚动缓冲区。 */
    private fun onGazeFrame(nx: Float, ny: Float, face: Boolean) {
        if (gazeTouchAssistOn) return
        com.lingxi.mobile.ui.GazeFeedbackOverlay.update(nx, ny, face)
        recordGazePoint(nx, ny, face)
    }

    private fun recordGazePoint(nx: Float, ny: Float, valid: Boolean) {
        appendGazeSample(resolveGazeTarget(nx, ny, valid, GAZE_CANDIDATE_REFRESH_MS))
    }

    private fun resolveGazeTarget(
        nx: Float,
        ny: Float,
        valid: Boolean,
        refreshMs: Long,
    ): String? {
        val now = android.os.SystemClock.elapsedRealtime()
        // 真实注视按既有节流刷新；触控辅助只在拖动且页面/间隔发生变化时读取一次复杂 a11y 树，
        // 静止采样复用已锁定目标，避免淘宝等页面把主线程每 300ms 阻塞 1～2 秒。
        if (lastWindowClassName != gazeCandidateWindowClass || now - gazeCandidatesAt > refreshMs) {
            gazeCandidates = com.lingxi.mobile.vision.GazeDeixis.filterCandidates(elementsForGaze())
            gazeCandidatesAt = now
            gazeCandidateWindowClass = lastWindowClassName
        }
        return if (!valid) null else {
            val dm = resources.displayMetrics
            com.lingxi.mobile.vision.GazeDeixis
                .resolve(nx * dm.widthPixels, ny * dm.heightPixels, gazeCandidates)?.text
        }
    }

    private fun appendGazeSample(target: String?) {
        val now = android.os.SystemClock.elapsedRealtime()
        synchronized(gazeBuffer) {
            gazeBuffer.add(now to target)
            gazeBuffer.removeAll { now - it.first > GAZE_BUFFER_MS }
        }
    }

    /**
     * 供语音链路查询"最近 ~2 秒稳定在看谁"（[GazeVote] 众数投票逻辑不变，只是数据源从
     * "关键词触发现开窗口"改成"持续维护的滚动缓冲区")。追踪未开（缓冲区天然是空的）/
     * 最近样本不够稳定 → null，调用方按纯语音路处理，不瞎猜。
     */
    fun currentGazeVote(): com.lingxi.mobile.vision.GazeVote.Result? {
        val snapshot = synchronized(gazeBuffer) { gazeBuffer.toList() }
        if (snapshot.isEmpty()) return null
        val t0 = snapshot.first().first
        val samples = snapshot.map { com.lingxi.mobile.vision.GazeVote.Sample(it.first - t0, it.second) }
        return com.lingxi.mobile.vision.GazeVote.vote(samples)
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        if (event == null) return
        if (event.eventType == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) {
            val nextWindow = event.className?.toString().orEmpty()
            if (nextWindow != lastWindowClassName && gazeTouchAssistOn) {
                touchAssistTarget = null
                touchAssistLastResolveAt = 0L
                synchronized(gazeBuffer) { gazeBuffer.clear() }
            }
            lastWindowClassName = nextWindow
        }
        if (!teachMode) return
        val pkg = event.packageName?.toString().orEmpty()
        if (pkg == packageName) return            // 忽略点在灵犀自己界面上的操作
        // 桌面/状态栏(开App、下拉栏)不算任务步骤——回放是确定性开 App，别把开App动作录进去
        if (pkg.contains("launcher", true) || pkg == "com.miui.home" ||
            pkg.contains("systemui", true)) return
        when (event.eventType) {
            AccessibilityEvent.TYPE_VIEW_CLICKED -> recordClick(event)
            AccessibilityEvent.TYPE_VIEW_SCROLLED -> recordScroll()
            else -> {}
        }
    }

    private fun recordClick(event: AccessibilityEvent) {
        val src = event.source
        val r = Rect()
        src?.getBoundsInScreen(r)
        if (r.width() <= 0 || r.height() <= 0) {
            Log.w(TAG, "教学：点击元素无有效坐标，跳过这步")
            return
        }
        val text = (src?.text?.toString()?.takeIf { it.isNotBlank() }
            ?: src?.contentDescription?.toString()?.takeIf { it.isNotBlank() }
            ?: event.text?.joinToString(" ")?.takeIf { it.isNotBlank() }
            ?: "").trim().take(60)
        val sig = com.lingxi.mobile.action.DemoSig.signature(readScreen())  // 稳定签名（滤动态噪声）
        lastClickAt = System.currentTimeMillis()
        demoSteps.add(
            com.lingxi.mobile.model.DemoStep(
                "tap", text, listOf(r.left, r.top, r.right, r.bottom), sig))
        Log.i(TAG, "教学录步 ${demoSteps.size}: tap '$text' @${r.left},${r.top},${r.right},${r.bottom}")
        com.lingxi.mobile.ui.OverlayProgress.update(
            this, "📚 已录 ${demoSteps.size} 步：点 ${text.ifBlank { "卡片" }}")
    }

    /** 录滑动：一次手势会连发很多 SCROLLED→700ms 去抖合并为一步；紧跟点击后的零星滚动(<600ms)忽略。 */
    private fun recordScroll() {
        val now = System.currentTimeMillis()
        if (now - lastScrollAt < 700 || now - lastClickAt < 600) return
        lastScrollAt = now
        val sig = com.lingxi.mobile.action.DemoSig.signature(readScreen())
        demoSteps.add(com.lingxi.mobile.model.DemoStep("scroll", "", emptyList(), sig))
        Log.i(TAG, "教学录步 ${demoSteps.size}: scroll")
        com.lingxi.mobile.ui.OverlayProgress.update(this, "📚 已录 ${demoSteps.size} 步：下滑")
    }

    override fun onInterrupt() {}

    override fun onDestroy() {
        if (debugCommandsEnabled) runCatching { unregisterReceiver(cmdReceiver) }
        // 只释放摄像头/悬浮点这些进程内资源，不清持久化偏好——用户"开着"的意愿保留，
        // 下次服务重连（onServiceConnected→applyGazeTrackingPreference）会自动恢复。
        stopGazeTrackingInternal()
        instance = null
        notifyStateChanged()
        super.onDestroy()
    }

    private fun decodeB64(s: String): String =
        runCatching { String(Base64.decode(s, Base64.DEFAULT), Charsets.UTF_8) }.getOrDefault(s)

    // ---- 原子能力 ----

    /** 读取当前前台屏幕的全部可见文本（当前界面摘要）。 */
    fun readScreen(): String {
        val root = rootInActiveWindow ?: return ""
        val sb = StringBuilder()
        collectText(root, sb)
        return sb.toString().trim()
    }

    private fun collectText(node: AccessibilityNodeInfo?, sb: StringBuilder) {
        if (node == null) return
        node.text?.let { if (it.isNotBlank()) sb.append(it).append("  ") }
        node.contentDescription?.let { if (it.isNotBlank()) sb.append(it).append("  ") }
        for (i in 0 until node.childCount) collectText(node.getChild(i), sb)
    }

    /**
     * 当前屏幕上带文字的元素 + 屏幕坐标框（供注视指代融合：把粗注视吸附到联系人/卡片）。
     * 只取有 text/contentDescription 的节点（候选离散化），坐标用 getBoundsInScreen。
     */
    fun elementsForGaze(): List<com.lingxi.mobile.vision.ScreenElement> {
        val root = rootInActiveWindow ?: return emptyList()
        val out = ArrayList<com.lingxi.mobile.vision.ScreenElement>()
        collectElements(root, out)
        return out
    }

    private fun collectElements(
        node: AccessibilityNodeInfo?,
        out: MutableList<com.lingxi.mobile.vision.ScreenElement>,
    ) {
        if (node == null) return
        val label = node.text?.toString()?.takeIf { it.isNotBlank() }
            ?: node.contentDescription?.toString()?.takeIf { it.isNotBlank() }
        if (label != null) {
            val r = Rect()
            node.getBoundsInScreen(r)
            if (r.width() > 0 && r.height() > 0) {
                out.add(
                    com.lingxi.mobile.vision.ScreenElement(
                        text = label.trim(),
                        bounds = com.lingxi.mobile.vision.Bounds(r.left, r.top, r.right, r.bottom),
                        clickable = nodeOrAncestorClickable(node),
                    )
                )
            }
        }
        for (i in 0 until node.childCount) collectElements(node.getChild(i), out)
    }

    private fun nodeOrAncestorClickable(node: AccessibilityNodeInfo): Boolean {
        if (node.isClickable) return true
        var parent = node.parent
        var depth = 0
        while (parent != null && depth < 4) {
            if (parent.isClickable) return true
            parent = parent.parent
            depth++
        }
        return false
    }

    /**
     * 视觉兜底(Set-of-Mark)候选：除带文字的节点外，还收【可点击但无文字】的节点（如直播间缩略图卡、
     * 纯图标按钮）——纯文本候选(elementsForGaze)收不到它们，导致直播间卡片根本进不了编号、选不中。
     * label 取文字/描述，没有则留空（VLM 靠截图里框内的视觉外观判断）。
     */
    fun clickableElementsForVisual(): List<com.lingxi.mobile.vision.ScreenElement> {
        val root = rootInActiveWindow ?: return emptyList()
        val out = ArrayList<com.lingxi.mobile.vision.ScreenElement>()
        collectClickable(root, out)
        return out
    }

    private fun collectClickable(
        node: AccessibilityNodeInfo?,
        out: MutableList<com.lingxi.mobile.vision.ScreenElement>,
    ) {
        if (node == null) return
        val label = node.text?.toString()?.takeIf { it.isNotBlank() }
            ?: node.contentDescription?.toString()?.takeIf { it.isNotBlank() }
        if (node.isClickable || label != null) {
            val r = Rect()
            node.getBoundsInScreen(r)
            if (r.width() > 0 && r.height() > 0) {
                out.add(
                    com.lingxi.mobile.vision.ScreenElement(
                        text = label?.trim() ?: "",
                        bounds = com.lingxi.mobile.vision.Bounds(r.left, r.top, r.right, r.bottom),
                        clickable = node.isClickable,
                    )
                )
            }
        }
        for (i in 0 until node.childCount) collectClickable(node.getChild(i), out)
    }

    /** 见 companion.currentScreen 注释。可见文本截断到 500 字以控延时（<5s）。 */
    fun screenContextForBrain(selfPkg: String): Map<String, String>? {
        val root = rootInActiveWindow ?: return null
        val pkg = root.packageName?.toString().orEmpty()
        if (pkg.isBlank() || pkg == selfPkg) return null   // 灵犀自己在前台 → 不当上下文
        val sb = StringBuilder()
        collectText(root, sb)
        val text = sb.toString().trim().take(500)
        if (text.isBlank()) return null
        return mapOf("app" to pkg, "可见文本" to text)
    }

    /** 按文本找到节点并点击（找不到自身可点击则向上找可点击祖先）。
     *  关键：findAccessibilityNodeInfosByText 是【包含】匹配，点 "1" 会命中 "10"/"11"/"12"。
     *  故优先【精确相等】的节点（text 或 content-desc 正好等于 target），无精确匹配再退回包含。 */
    fun clickByText(text: String): Boolean {
        val root = rootInActiveWindow ?: return false
        val hits = root.findAccessibilityNodeInfosByText(text) ?: return false
        val exact = hits.filter {
            it.text?.toString() == text || it.contentDescription?.toString() == text
        }
        for (n in exact) if (performClick(n)) return true
        for (n in hits) if (performClick(n)) return true
        return false
    }

    /**
     * 注视/触控指代专用点击：优先 ACTION_CLICK（不受悬浮圆点遮挡），再短暂放行圆点区域后
     * 使用坐标手势兜底。日志只记录执行方式与返回值，不记录目标文字。
     */
    fun activateGazeTargetByText(text: String): Boolean {
        if (clickByText(text)) {
            Log.i(TAG, "GAZE_TARGET_ACTIVATE method=accessibility_action accepted=true")
            return true
        }
        if (gazeTouchAssistOn) {
            com.lingxi.mobile.ui.GazeFeedbackOverlay.temporarilyPassThrough()
        }
        val tapped = tapByText(text)
        Log.i(TAG, "GAZE_TARGET_ACTIVATE method=coordinate_fallback accepted=$tapped")
        return tapped
    }

    private val TITLE_SPLIT_RE = Regex("\\s*[-–—·|]\\s*")

    /** 按文本找到元素 → 在其【中心坐标手势点击】。比 performAction(CLICK) 更能触发卡片/列表项跳转
     *  （日志实测：b站直播卡 clickByText"点中"了却不跳转→改坐标点真实触摸最稳）。找不到退回 clickByText。
     *
     *  匹配次序（2026-07-03 真机连环实锤后重构）：
     *  ① 节点文本与原串完全相等 → ② 与拆分后第一段完全相等（模型常给"歌名 - 歌手"复合串，
     *     真实控件只露"歌名"一段）→ ③ 第一段包含匹配 → ④ 原串包含匹配 → ⑤ clickByText 兜底。
     *  包含匹配时在命中里选【节点文本最短】的——酷狗每日推荐页顶部推荐语里恰好含完整的
     *  『小半 - 陈粒』长简介，包含匹配先命中长简介、点了个不可点的文案区（这就是"识别到了
     *  却点不上"的真凶）；最短文本=最接近搜索串本身的那个控件，长简介天然出局。 */
    fun tapByText(text: String): Boolean {
        val root = rootInActiveWindow ?: return false
        val seg = text.split(TITLE_SPLIT_RE).firstOrNull()?.trim()
            ?.takeIf { it.isNotBlank() && it != text }
        val node = findTapCandidate(root, text, exact = true)
            ?: seg?.let { findTapCandidate(root, it, exact = true) }
            ?: seg?.let { findTapCandidate(root, it, exact = false) }
            ?: findTapCandidate(root, text, exact = false)
            ?: return clickByText(text)
        return tapNodeCardCenter(node) || clickByText(text)
    }

    /** 按 Android resource-id 点控件中心。用于 App 内稳定控件（如知乎顶部 query_container），
     *  避免只有空文本的关键容器被纯文本候选漏掉。 */
    fun tapByViewId(viewId: String): Boolean {
        val root = rootInActiveWindow ?: return false
        val hits = root.findAccessibilityNodeInfosByViewId(viewId).orEmpty()
        val node = hits.firstOrNull { it.isVisibleToUser } ?: hits.firstOrNull() ?: return false
        return performClick(node) || tapNodeCardCenter(node)
    }

    fun clickByViewId(viewId: String): Boolean {
        val root = rootInActiveWindow ?: return false
        val hits = root.findAccessibilityNodeInfosByViewId(viewId).orEmpty()
        val node = hits.firstOrNull { it.isVisibleToUser } ?: hits.firstOrNull() ?: return false
        return performClick(node)
    }

    fun isViewIdChecked(viewId: String): Boolean {
        val root = rootInActiveWindow ?: return false
        val hits = root.findAccessibilityNodeInfosByViewId(viewId).orEmpty()
        return hits.any { it.isVisibleToUser && it.isChecked }
    }

    fun tapFirstByViewIdPrefix(prefixes: List<String>, minTopPx: Int = 0): Boolean {
        val root = rootInActiveWindow ?: return false
        val candidates = ArrayList<AccessibilityNodeInfo>()
        collectNodesByViewIdPrefix(root, prefixes, minTopPx, candidates)
        val node = candidates
            .sortedWith(compareBy<AccessibilityNodeInfo> {
                Rect().also { r -> it.getBoundsInScreen(r) }.top
            }.thenBy {
                Rect().also { r -> it.getBoundsInScreen(r) }.left
            })
            .firstOrNull() ?: return false
        return tapNodeCardCenter(node) || performClick(node)
    }

    fun tapFirstCardTextByViewIdPrefix(prefixes: List<String>, minTopPx: Int = 0): Boolean {
        val root = rootInActiveWindow ?: return false
        val candidates = ArrayList<AccessibilityNodeInfo>()
        collectNodesByViewIdPrefix(root, prefixes, minTopPx, candidates)
        val card = candidates
            .sortedWith(compareBy<AccessibilityNodeInfo> {
                Rect().also { r -> it.getBoundsInScreen(r) }.top
            }.thenBy {
                Rect().also { r -> it.getBoundsInScreen(r) }.left
            })
            .firstOrNull() ?: return false
        val cardId = card.viewIdResourceName.orEmpty()
        val cardRect = Rect()
        card.getBoundsInScreen(cardRect)
        if (cardRect.width() > 0 && cardRect.height() > 0) {
            val x = cardRect.left + cardRect.width() * 0.30f
            val y = cardRect.top + cardRect.height() * 0.17f
            Log.i(TAG, "tap first card text area id=$cardId @${x.toInt()},${y.toInt()} bounds=$cardRect")
            tap(x, y)
            return true
        }
        val textTarget = findCardTextTarget(card, cardId)
        if (textTarget != null && tapRawNodeCenter(textTarget)) return true

        return false
    }

    fun tapFirstZhihuPostTextArea(prefixes: List<String>, minTopPx: Int = 0): Boolean {
        val root = rootInActiveWindow ?: return false
        val prefixCandidates = ArrayList<AccessibilityNodeInfo>()
        collectNodesByViewIdPrefix(root, prefixes, minTopPx, prefixCandidates)
        var postCandidates = prefixCandidates.filter { looksLikeZhihuPostCard(it) }
        if (postCandidates.isEmpty()) {
            val clickableCandidates = ArrayList<AccessibilityNodeInfo>()
            collectClickableZhihuPostCards(root, minTopPx, clickableCandidates)
            postCandidates = clickableCandidates.filter { looksLikeZhihuPostCard(it) }
        }
        val card = postCandidates
            .sortedWith(compareByDescending<AccessibilityNodeInfo> {
                zhihuPostCardScore(it)
            }.thenBy {
                Rect().also { r -> it.getBoundsInScreen(r) }.top
            }.thenBy {
                Rect().also { r -> it.getBoundsInScreen(r) }.left
            })
            .firstOrNull() ?: return false
        return tapZhihuCardTextArea(card)
    }

    private fun tapZhihuCardTextArea(card: AccessibilityNodeInfo): Boolean {
        val cardId = card.viewIdResourceName.orEmpty()
        val textTarget = findCardTextTarget(card, cardId)
        if (textTarget != null && tapRawNodeCenter(textTarget)) return true

        val r = Rect()
        card.getBoundsInScreen(r)
        if (r.width() <= 0 || r.height() <= 0) return false
        val x = r.left + r.width() * 0.30f
        val y = r.top + r.height() * 0.17f
        Log.i(TAG, "tap first zhihu post text area id=$cardId @${x.toInt()},${y.toInt()} bounds=$r")
        tap(x, y)
        return true
    }

    private fun collectClickableZhihuPostCards(
        node: AccessibilityNodeInfo?,
        minTopPx: Int,
        out: MutableList<AccessibilityNodeInfo>,
    ) {
        if (node == null) return
        if (node.isClickable && node.isVisibleToUser && node.isEnabled) {
            val r = Rect()
            node.getBoundsInScreen(r)
            if (r.width() > 0 && r.height() > 0 && r.top >= minTopPx) out.add(node)
        }
        for (i in 0 until node.childCount) collectClickableZhihuPostCards(node.getChild(i), minTopPx, out)
    }

    private fun looksLikeZhihuPostCard(node: AccessibilityNodeInfo): Boolean =
        zhihuPostCardScore(node) > 0

    private fun zhihuPostCardScore(node: AccessibilityNodeInfo): Int {
        val id = node.viewIdResourceName.orEmpty()
        if (id.contains("negativeFeedback", ignoreCase = true)) return 0
        val text = subtreeText(node)
        if (text.contains("知乎直答") || text.contains("展开更多")) return 0
        val number = """\d+(?:\.\d+)?\s*万?\s*"""
        val hasVote = Regex("${number}赞同").containsMatchIn(text)
        val hasComment = Regex("${number}评论").containsMatchIn(text)
        val hasView = Regex("${number}浏览").containsMatchIn(text)
        val hasAnswer = Regex("${number}回答").containsMatchIn(text)
        val hasFollow = Regex("${number}关注").containsMatchIn(text)
        val hasDate = Regex("(\\d{4}-\\d{1,2}-\\d{1,2}|\\d{1,2}-\\d{1,2})").containsMatchIn(text)
        val hasRelativeTime = Regex("(刚刚|昨天|前天|\\d+\\s*(秒|分钟|小时|天|周|个月|年)前)").containsMatchIn(text)

        if ((hasVote || hasComment) && (hasDate || hasRelativeTime || hasComment)) return 3
        if (hasVote || hasComment) return 2
        if (hasView && (hasAnswer || hasFollow)) return 1
        return 0
    }

    private fun subtreeText(node: AccessibilityNodeInfo?): String {
        if (node == null) return ""
        val sb = StringBuilder()
        collectText(node, sb)
        return sb.toString()
    }

    private fun findCardTextTarget(
        node: AccessibilityNodeInfo?,
        cardId: String,
    ): AccessibilityNodeInfo? {
        if (node == null) return null
        val id = node.viewIdResourceName.orEmpty()
        val text = node.text?.toString().orEmpty().trim()
        if (text.isNotBlank() && (id == "${cardId}:title" || id == "${cardId}:description")) {
            val r = Rect()
            node.getBoundsInScreen(r)
            if (r.width() > 0 && r.height() > 0) return node
        }
        for (i in 0 until node.childCount) {
            findCardTextTarget(node.getChild(i), cardId)?.let { return it }
        }
        return null
    }

    private fun tapRawNodeCenter(node: AccessibilityNodeInfo): Boolean {
        val r = Rect()
        node.getBoundsInScreen(r)
        return if (r.width() > 0 && r.height() > 0) {
            tap(r.exactCenterX(), r.exactCenterY())
            true
        } else false
    }

    private fun collectNodesByViewIdPrefix(
        node: AccessibilityNodeInfo?,
        prefixes: List<String>,
        minTopPx: Int,
        out: MutableList<AccessibilityNodeInfo>,
    ) {
        if (node == null) return
        val id = node.viewIdResourceName.orEmpty()
        if (prefixes.any { id.startsWith(it) || id.contains(it) } &&
            !id.contains("negativeFeedback", ignoreCase = true) &&
            node.isClickable &&
            node.isVisibleToUser &&
            node.isEnabled) {
            val r = Rect()
            node.getBoundsInScreen(r)
            if (r.width() > 0 && r.height() > 0 && r.top >= minTopPx) {
                out.add(node)
            }
        }
        for (i in 0 until node.childCount) collectNodesByViewIdPrefix(node.getChild(i), prefixes, minTopPx, out)
    }

    private fun findTapCandidate(
        root: AccessibilityNodeInfo, q: String, exact: Boolean,
    ): AccessibilityNodeInfo? {
        val hits = root.findAccessibilityNodeInfosByText(q)?.takeIf { it.isNotEmpty() } ?: return null
        return if (exact) hits.firstOrNull {
            it.text?.toString() == q || it.contentDescription?.toString() == q
        } else hits.minByOrNull {
            ((it.text ?: it.contentDescription)?.toString() ?: "").length
        }
    }

    /** 点【最近的可点击祖先(整张卡片)】的中心——比点标题文字更能触发跳转
     *  （日志实测：点 b站直播卡的标题文字进不去房间，得点卡片本身）。找不到可点祖先就退回点文字本身。 */
    private fun tapNodeCardCenter(node: AccessibilityNodeInfo): Boolean {
        var n: AccessibilityNodeInfo? = node
        var depth = 0
        while (n != null && !n.isClickable && depth < 6) { n = n.parent; depth++ }
        val hit = n?.takeIf { it.isClickable } ?: node
        val r = Rect()
        hit.getBoundsInScreen(r)
        return if (r.width() > 0 && r.height() > 0) {
            tap(r.exactCenterX(), r.exactCenterY()); true
        } else false
    }

    private fun performClick(node: AccessibilityNodeInfo?): Boolean {
        var n = node
        var depth = 0
        while (n != null && depth < 6) {
            if (n.isClickable && n.performAction(AccessibilityNodeInfo.ACTION_CLICK)) return true
            n = n.parent
            depth++
        }
        return false
    }

    /** 给当前聚焦的可编辑框填入文本。 */
    fun setFocusedText(text: String): Boolean {
        val focused = findFocusedEditable(rootInActiveWindow) ?: return false
        val args = Bundle().apply {
            putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, text)
        }
        return focused.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, args)
    }

    private fun findFocusedEditable(node: AccessibilityNodeInfo?): AccessibilityNodeInfo? {
        if (node == null) return null
        if (node.isEditable && node.isFocused) return node
        for (i in 0 until node.childCount) findFocusedEditable(node.getChild(i))?.let { return it }
        return null
    }

    /** 坐标点击（控件没有可点击属性时的兜底）。 */
    fun tap(x: Float, y: Float) {
        val path = Path().apply { moveTo(x, y) }
        val stroke = GestureDescription.StrokeDescription(path, 0, 60)
        dispatchGesture(GestureDescription.Builder().addStroke(stroke).build(), null, null)
    }

    fun pressBack() = performGlobalAction(GLOBAL_ACTION_BACK)
    fun pressHome() = performGlobalAction(GLOBAL_ACTION_HOME)

    /** 向下滚动（手势上滑=内容下翻）。补全 agent 动作空间——主页/列表的内容常在下面，够不着就点不到。 */
    fun scrollDown() {
        val dm = resources.displayMetrics
        val x = dm.widthPixels / 2f
        val path = Path().apply { moveTo(x, dm.heightPixels * 0.72f); lineTo(x, dm.heightPixels * 0.28f) }
        val stroke = GestureDescription.StrokeDescription(path, 0, 300)
        dispatchGesture(GestureDescription.Builder().addStroke(stroke).build(), null, null)
    }
}
