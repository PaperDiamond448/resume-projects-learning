package com.lingxi.mobile.action

import android.content.Context
import android.content.Intent
import android.util.Base64
import android.util.Log
import com.google.gson.Gson
import com.lingxi.mobile.MainActivity
import com.lingxi.mobile.accessibility.LingxiAccessibilityService
import com.lingxi.mobile.data.MessageCard
import com.lingxi.mobile.model.AgentReflectResponse
import com.lingxi.mobile.model.AgentTraceItem
import com.lingxi.mobile.model.AgentVisualElement
import com.lingxi.mobile.model.DemoStep
import com.lingxi.mobile.model.MultiAppPlanResponse
import com.lingxi.mobile.model.MultiAppTask
import com.lingxi.mobile.net.BrainClient
import com.lingxi.mobile.ui.OverlayProgress
import com.lingxi.mobile.vision.ScreenCaptureService
import com.lingxi.mobile.vision.ScreenElement
import com.lingxi.mobile.vision.CloudVisionConsentStore
import com.lingxi.mobile.vision.CloudVisionPolicy
import com.lingxi.mobile.voice.KwsForegroundService
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.io.File

/**
 * 应用自动化编排 —— 把无障碍原子能力（读屏/点击/输入）编排成"打开 App→多步操作"。
 *
 * 这是"大脑决策 → 无障碍执行"的落点：大脑出 open_app{app_name}，安卓端在此
 *  - 先看是否命中【预设编排脚本】（如"省电模式"=开设置→点电池→点省电）→ 无障碍自动跑；
 *  - 否则按【中文名→包名】映射或 App 标签匹配，真打开任意已装 App。
 *
 * 每步用 clickWhenReady 智能等待目标文本出现再点击（应对界面加载延迟），比固定 sleep 稳。
 */
object AppAutomator {

    private data class TapPoint(val x: Float, val y: Float, val reason: String)

    private const val TAG = "AppAutomator"
    // 无障碍节点操作在主线程更稳；协程 delay 是挂起非阻塞，不会卡 UI
    private val scope = CoroutineScope(Dispatchers.Main)

    /** T-04 交易类硬守卫：屏幕文本命中支付/密码类字样 → 代码级立即硬停，不再问大脑、不再问视觉。
     *  比 prompt 约束硬——LLM 温度噪声/视觉误判都可能被绕过，字符串匹配挡在动作执行之前不会。
     *  只挡"真要付钱/输密码"这一下，不挡"提交订单"这类确认页本身（D 层任务就是要停在那页汇报）。 */
    private val PAYMENT_GUARD_RE = Regex(
        "支付宝支付|微信支付|立即支付|确认支付|去支付|支付密码|输入密码|请输入密码|" +
        "银行卡密码|支付验证码|短信验证码|指纹支付|刷脸支付|人脸支付|确认付款|立即付款|" +
        "扣款|钱包密码|交易密码"
    )

    private fun paymentGuardHit(screen: String): Boolean = PAYMENT_GUARD_RE.containsMatchIn(screen)

    /** 常见"跳过/关闭"类按钮的【精确】文案——开屏广告、App启动后延迟弹出的插屏广告都靠这类按钮收场。
     *  只做精确匹配（不做包含匹配），避免误点正文里恰好出现"取消"两字的无关按钮。
     *  2026-07-03 用户实测反馈：①b站开屏广告"有时"没等到——旧逻辑只在刚打开App那一刻检查一次，
     *  ②酷狗"开屏广告之外，进去之后还有一个App内广告"——插屏广告常常是进主界面几秒后才弹出，
     *  单次检查窗口必然错过。改用【主动找按钮点掉】+【在每一步循环开头都重新检查一次】，
     *  而不是只在启动那一刻被动等固定时长。
     *  🐛 T-06 真机回归实测(2026-07-03)：b站的跳过按钮文案带倒计时数字("跳过 1"/"跳过3")，
     *  精确匹配"跳过"两个字原样必然搜不中——退化成只能靠被动等固定时长扛过去（能跑通但慢，
     *  遇到更长的广告倒计时就会顶到预算上限）。放宽"跳过"这一条允许后面跟数字/秒数。 */
    private val DISMISSIBLE_POPUP_RE = Regex(
        "^(跳过\\s*\\d*\\s*s?|跳过广告|关闭|关闭广告|×|✕|✖|知道了|我知道了|稍后再说|以后再说|暂不|" +
        "暂不开启|下次再说|不用了|残忍关闭|暂不提醒|我再想想)$"
    )

    /** 屏上有没有明显的"跳过/关闭"类按钮，有就直接点掉——比死等广告倒计时快，
     *  也能应付"进了主界面才弹"的插屏广告（只要每步循环都会重新调用这个检查）。 */
    private fun dismissInterstitialIfPresent(acc: LingxiAccessibilityService): Boolean {
        val els = acc.elementsForGaze()
        val hit = els.firstOrNull { DISMISSIBLE_POPUP_RE.matches(it.text.trim()) } ?: return false
        val cx = (hit.bounds.left + hit.bounds.right) / 2f
        val cy = (hit.bounds.top + hit.bounds.bottom) / 2f
        acc.tap(cx, cy)
        return true
    }

    /** 循环开头调用：连续尝试点掉插屏广告（最多几次，防止死循环），点完的成功次数供调用方决定要不要提示。 */
    private suspend fun clearInterstitials(acc: LingxiAccessibilityService, maxTries: Int = 3): Int {
        var cleared = 0
        while (cleared < maxTries && dismissInterstitialIfPresent(acc)) {
            cleared++
            delay(800)
        }
        return cleared
    }

    /** 常见 App 中文名 → 包名（命中则直接精确打开，绕过英文 label 匹配问题）。 */
    private val PKG = mapOf(
        "设置" to "com.android.settings",
        "时钟" to "com.google.android.deskclock",
        "通讯录" to "com.google.android.contacts",
        "联系人" to "com.google.android.contacts",
        "短信" to "com.google.android.apps.messaging",
        "信息" to "com.google.android.apps.messaging",
        "微信" to "com.tencent.mm",
        "QQ" to "com.tencent.mobileqq",
        "支付宝" to "com.eg.android.AlipayGphone",
        "淘宝" to "com.taobao.taobao",
        "京东" to "com.jingdong.app.mall",
        "拼多多" to "com.xunmeng.pinduoduo",
        "携程" to "ctrip.android.view",
        "去哪儿" to "com.Qunar",
        "抖音" to "com.ss.android.ugc.aweme",
        "浏览器" to "com.android.chrome",
        "Chrome" to "com.android.chrome",
    )

    /** 中文/口语叫法 → 候选标签关键词（英文 label 常见）。在【真实已装列表】里按这些词找，
     *  治"相册≠Photos"这类中英标签错位——系统据此真正"知道设备上有什么 app"。 */
    private val ALIASES = mapOf(
        "相册" to listOf("photos", "gallery", "图库", "相片"),
        "图库" to listOf("photos", "gallery"),
        "照片" to listOf("photos", "gallery"),
        "浏览器" to listOf("chrome", "browser"),
        "邮件" to listOf("gmail", "email", "mail", "邮箱"),
        "邮箱" to listOf("gmail", "email", "mail"),
        "地图" to listOf("maps", "高德", "百度地图"),
        "日历" to listOf("calendar"),
        "计算器" to listOf("calculator"),
        "相机" to listOf("camera"),
        "音乐" to listOf("music", "yt music"),
        "视频" to listOf("youtube", "video"),
        "文件" to listOf("files", "文件管理"),
        "商店" to listOf("play store", "store", "market"),
        "应用商店" to listOf("play store", "store"),
        "电话" to listOf("phone", "dialer", "拨号"),
        "拨号" to listOf("phone", "dialer"),
        // 国民 App 口语名 → 标签/包名关键词（标签常为英文，口语名与之零字符重叠，必须靠别名）
        "哔哩哔哩" to listOf("bilibili", "bili"),
        "b站" to listOf("bilibili", "bili"),
        "哔哩" to listOf("bilibili", "bili"),
        "微信" to listOf("wechat", "weixin", "tencent.mm"),
        "支付宝" to listOf("alipay", "支付宝"),
        "淘宝" to listOf("taobao", "淘宝"),
        "京东" to listOf("jd", "jingdong", "京东"),
        "拼多多" to listOf("pinduoduo", "拼多多"),
        "携程" to listOf("ctrip", "携程"),
        "智行" to listOf("zhixing", "suanya", "智行"),
        "去哪儿" to listOf("qunar", "去哪儿"),
        "抖音" to listOf("douyin", "抖音", "aweme"),
        "微博" to listOf("weibo", "微博"),
        "知乎" to listOf("zhihu", "知乎"),
        "小红书" to listOf("xiaohongshu", "小红书"),
        "网易云" to listOf("netease", "cloudmusic", "网易云"),
        "网易云音乐" to listOf("netease", "cloudmusic"),
        "钉钉" to listOf("dingtalk", "钉钉"),
        "美团" to listOf("meituan", "美团"),
        "高德" to listOf("amap", "autonavi", "高德"),
        "百度地图" to listOf("baidu", "百度地图"),
    )

    /** 预设编排脚本：指令关键词 → 无障碍多步操作（模拟器可演示的样例）。 */
    private val SCRIPTS: Map<String, suspend (Context) -> String> = mapOf(
        "省电模式" to { ctx -> batterySaver(ctx) },
        "省电" to { ctx -> batterySaver(ctx) },
    )

    /** 大脑 open_app 的统一入口。返回给用户的回执（编排异步执行，先回"正在操作"）。 */
    fun handle(ctx: Context, name: String): String {
        SCRIPTS[name]?.let { script ->
            if (!LingxiAccessibilityService.isConnected)
                return "需要先开启「灵犀无障碍」才能自动操作 $name"
            scope.launch { script(ctx) }
            return "好的，正在自动操作：$name"
        }
        val pkg = resolvePackage(ctx, name)
        if (pkg != null && launchPackage(ctx, pkg)) return "已打开 $name"
        // 找不到：诚实把设备上【真实能打开】的常见 app 列给用户，而不是干巴巴一句"没找到"
        val have = installedLaunchers(ctx).map { it.first }.distinct()
        Log.i(TAG, "未匹配「$name」；设备已装 launcher(${have.size}): $have")
        return "没找到「$name」。这台设备上我能打开的有：" + have.take(12).joinToString("、")
    }

    // ==== 范式③：GUI Agent（大脑边看屏边决策，能做没预设脚本的任意任务）====

    private val brain = BrainClient()

    private const val MAX_STEPS_PER_SUBGOAL = 8
    private const val MAX_RECOVERY_EVENTS_PER_SUBGOAL = 4

    /** 用户点浮窗「✕ 结束」→ 置取消标志，runAgent 每步检查后硬停（治人工介入反扰 AI）。 */
    @Volatile
    private var cancelRequested = false

    /** 视觉模型后端不可用（没连上/鉴权失败/截图坏）→ 这步立即诚实收手，绝不靠"装完成"蒙混（治演示忘开 VLM 时假成功）。 */
    @Volatile
    private var visualBackendDown = false

    /** 当前任务操作的 App 名→传给视觉端点，大脑据此注入该 App 攻略 hints 进视觉 prompt（让 3B 视觉带 App 知识选卡）。 */
    @Volatile
    private var currentApp = ""

    private enum class RecoveryDecision { RETRY, STOP }

    private data class RecoveryState(
        var wrongPage: Int = 0,
        var noChange: Int = 0,
        var unsure: Int = 0,
        var total: Int = 0,
        var reopened: Boolean = false,
    )

    private data class DemoReplayResult(
        val success: Boolean,
        val allowReplan: Boolean = true,
        val failedStep: Int = 0,
        val label: String = "",
        val reason: String = "",
        val screen: String = "",
        val history: List<String> = emptyList(),
        val trace: List<AgentTraceItem> = emptyList(),
    )

    private data class MultiAppRememberResult(
        val app: String,
        val key: String,
        val success: Boolean,
        val title: String = "",
        val valueRaw: String = "",
        val value: Double? = null,
        val screenshotPath: String = "",
        val reason: String = "",
        /** resolvePackage() 已经解析出的包名，供聊天卡片点击后直接 resume 到前台（不重新搜索）。 */
        val pkg: String = "",
    )

    fun cancelAgent() { cancelRequested = true }

    /** 上一个 runAgent 协程的 Job——同一时刻只允许一个任务跑，新任务来了先硬取消旧的。 */
    private var currentJob: Job? = null

    /** 🐛 T-06 真机回归实测(2026-07-03，用户揪出)：连续两个任务之间旧任务还没跑完、新任务
     *  就已经通过下一条 DEBUG_SUBMIT/run_agent 启动——`scope.launch` 每次都开一个新协程，
     *  旧协程没有任何取消机制，两个 runAgent 并发跑在同一个 object 单例上，共享
     *  `cancelRequested`/`currentApp`/`visualBackendDown` 这些实例级可变状态，互相踩踏
     *  （真机日志实锤：知乎任务开始时，酷狗任务的"视觉候选(0)"循环还在打日志）。
     *  修法：每次开新任务前，先硬取消上一个 Job（协程级取消，不只是设标志位等它自己检查），
     *  保证任意时刻只有一个 runAgent 在跑。 */
    fun runAgentGoal(goal: String, app: String? = null): String {
        if (!LingxiAccessibilityService.isConnected) return "需要先开启无障碍"
        currentJob?.cancel()
        // 注：视觉任务"要先开屏幕视觉"的守卫移到 runAgent 里、放在【真人演示】检查之后——
        // 因为确定性回放(纯 a11y 点击)根本不需要屏幕视觉，有演示就该直接回放，别被守卫挡掉。
        currentJob = scope.launch { runAgent(goal, app) }
        return "好的，我来边看边操作：$goal"
    }

    /**
     * GUI Agent · 分步计划执行（治反应式 agent 来回瞎点/倒退，用户拍板的 #4）：
     *  1) 确定性开 App；2) 大脑把目标拆成有序【子步骤】；3) 逐子步骤推进——每步内反应式做事，
     *     直到大脑判 `subgoal_done` 才进下一步、**做完一步绝不回头**；每步卡住→视觉兜底(聚焦该步)。
     *  护栏：每子步骤步数上限、停滞检测、用户随时「✕ 结束」硬停。
     */
    private suspend fun runAgent(goal: String, app: String? = null) {
        val acc = LingxiAccessibilityService.instance ?: return
        cancelRequested = false
        OverlayProgress.startSession { cancelAgent() }
        try {
            progress(acc, "开始：$goal")
            progress(acc, "状态检查：无障碍已开，屏幕视觉${if (ScreenCaptureService.isReady) "已开" else "未开"}")

            val multiPlan = maybePlanMultiApp(goal, app)
            if (multiPlan != null) {
                runMultiAppWorkflow(acc, goal, multiPlan)
                return
            }

            // 1) 确定性开 App（远比从灵犀界面认路可靠）
            val opened = app?.takeIf { it.isNotBlank() }?.let { openAppNamed(acc, it) }
                ?: openAppInGoal(acc, goal)
            if (opened != null) {
                progress(acc, "已打开 $opened")
                waitForAppSettled(acc)
            }
            if (cancelRequested) { progress(acc, "已结束"); return }

            // 2) 拆计划（失败退回整目标）；确定性滤掉"打开App/进主界面"废步骤——App 已确定性打开，
            //    留着会让 agent 在空步骤里抽风(点直播→又back回主界面，用户实测到的倒退 bug)。
            val agentApp = app ?: opened ?: ""
            currentApp = agentApp   // 供 tryVisualStep 把 App 名带给视觉端点 → 注入该 App 攻略 hints
            val planResp = brain.agentPlan(goal, agentApp)
            // 0) 命中【真人演示】→ 确定性回放（最可靠，纯 a11y 点击、不需要屏幕视觉，彻底跳过 3B 视觉选不准）
            if (planResp.source == "demo" && planResp.demo.isNotEmpty()) {
                val replay = replayDemo(acc, goal, planResp.demo)
                if (replay.success || !replay.allowReplan || cancelRequested) return

                brain.recordAgentMemory(
                    agentApp,
                    goal,
                    listOf("demo_replay"),
                    replay.history,
                    replay.trace,
                    false,
                    "demo replay failed at step ${replay.failedStep}: ${replay.reason}",
                )
                progress(acc, "教学路径这次没走通，带着失败现场重新规划")
                val recoveryGoal = buildDemoRecoveryGoal(goal, replay)
                val recoveryPlanResp = brain.agentPlan(recoveryGoal, agentApp, skipDemo = true)
                val recoveryPlan = recoveryPlanResp.steps
                    .filterNot { isOpenAppStep(it) }
                    .ifEmpty { listOf(recoveryGoal) }
                Log.i(TAG, "教学回放失败后重规划(${recoveryPlan.size}步): $recoveryPlan")
                val recoveryOk = executePlan(
                    acc = acc,
                    app = agentApp,
                    goal = goal,
                    plan = recoveryPlan,
                    history = replay.history.toMutableList(),
                    trace = replay.trace.toMutableList(),
                    successPrefix = "✅ 完成（教学失效后重新规划）：",
                )
                if (recoveryOk) sendResultCard(acc, agentApp, goal)
                return
            }
            // 没有演示，且是要分辨卡片的视觉任务→屏幕视觉没开就诚实收手（别开 App 乱试）
            if (isLikelyVisualGoal(goal) && !ScreenCaptureService.isReady) {
                progress(acc, "这个任务要分辨视频/直播间/歌曲卡片，请先点「屏幕视觉」授权；我不会看不见时乱点。")
                return
            }
            val plan = planResp.steps
                .filterNot { isOpenAppStep(it) }.ifEmpty { listOf(goal) }
            Log.i(TAG, "计划(${plan.size}步): $plan")

            // 3) 逐子步骤推进，单调不回头
            val ok = executePlan(
                acc = acc,
                app = agentApp,
                goal = goal,
                plan = plan,
                history = mutableListOf(),
                trace = mutableListOf(),
            )
            if (ok) sendResultCard(acc, agentApp, goal)
        } finally {
            OverlayProgress.endSession()
        }
    }

    // ==== 教学模式（学真人操作）：保存演示 + 确定性回放 ====

    private suspend fun maybePlanMultiApp(goal: String, app: String?): MultiAppPlanResponse? {
        val appVal = app?.trim().orEmpty()
        // 🐛 2026-07-05 用户真机揪出：比价类("帮我比一下淘宝和京东…")大脑会把 app 槽塞成一个复合字符串
        //    "淘宝+京东"。原逻辑"只要 app 非空就 return null"→直接跳过跨App工作流；随后 resolvePackage
        //    对 "淘宝+京东" 走双向包含匹配（"淘宝+京东".contains("淘宝")）命中淘宝包名→只开淘宝当单App跑，
        //    比价从没执行。修法：app 槽是复合值(含 + / 、 、和/与…) 视为跨App任务；否则单一App+目标不像
        //    比价才保持单App，避免误触发。
        val compoundApp = appVal.isNotBlank() && Regex("[+、,，/]|和|与|跟").containsMatchIn(appVal)
        if (!compoundApp && !looksLikeMultiAppWorkflowGoal(goal)) return null
        val installedLabels = LingxiAccessibilityService.instance
            ?.let { installedLaunchers(it).map { pair -> pair.first } }
            .orEmpty()
        val plan = brain.agentPlanMulti(goal, installedLabels)
        if (!plan.workflow || plan.tasks.size < 2) return null
        return plan
    }

    private fun looksLikeMultiAppWorkflowGoal(goal: String): Boolean {
        val t = goal.replace(" ", "")
        val compare = listOf("比价", "哪家便宜", "哪个便宜", "最便宜", "最低价", "价格")
            .any { t.contains(it) }
        if (!compare) return false
        val shoppingApps = listOf("淘宝", "京东", "拼多多").count { t.contains(it) }
        val travelApps = listOf("携程", "智行", "去哪儿").count { t.contains(it) }
        val shoppingDomain = listOf("商品", "首屏", "电脑", "键盘", "手机", "ThinkPad").any { t.contains(it) }
        val travelDomain = listOf("机票", "高铁", "火车票", "车票").any { t.contains(it) }
        return shoppingApps >= 2 || travelApps >= 2 || (shoppingDomain && compare) || (travelDomain && compare)
    }

    private suspend fun runMultiAppWorkflow(
        acc: LingxiAccessibilityService,
        goal: String,
        plan: MultiAppPlanResponse,
    ): Boolean {
        if (!ScreenCaptureService.isReady) {
            progress(acc, "跨 App 比较需要截图抽取价格/数值，请先点「屏幕视觉」授权。")
            return false
        }
        progress(acc, "开始跨 App 工作流：${plan.mode.ifBlank { "比较" }}，共 ${plan.tasks.size} 个 App")
        val results = mutableListOf<MultiAppRememberResult>()
        val allTrace = mutableListOf<AgentTraceItem>()

        for ((idx, task) in plan.tasks.withIndex()) {
            if (cancelRequested) {
                progress(acc, "已结束")
                return false
            }
            currentApp = ""
            progress(acc, "跨 App ${idx + 1}/${plan.tasks.size}：打开 ${task.app}")
            val expectedPkg = resolvePackage(acc, task.app)
            val expectedPkgNonNull = expectedPkg.orEmpty()
            val opened = expectedPkgNonNull.isNotBlank() && launchPackage(acc, expectedPkgNonNull)
            if (!opened) {
                val r = MultiAppRememberResult(
                    app = task.app,
                    key = rememberString(task, "key").ifBlank { task.id },
                    success = false,
                    reason = "未安装或未找到 App",
                )
                results.add(r)
                progress(acc, "${task.app} 没打开，继续下一个 App")
                continue
            }
            waitForAppSettled(acc)
            if (!isExpectedAppForeground(acc, expectedPkgNonNull)) {
                val actualPkg = acc.foregroundPackageName()
                results.add(MultiAppRememberResult(
                    app = task.app,
                    key = rememberString(task, "key").ifBlank { task.id },
                    success = false,
                    reason = "打开后前台不是目标 App：$actualPkg",
                ))
                progress(acc, "${task.app} 没有切到前台（当前 $actualPkg），跳过，避免串 App 乱点")
                continue
            }
            currentApp = task.app

            val history = mutableListOf<String>()
            val trace = mutableListOf<AgentTraceItem>()
            val executableSteps = task.steps.filterNot { isRememberStep(it) }
                .ifEmpty { listOf(task.goal) }
            val ok = executePlan(
                acc = acc,
                app = task.app,
                goal = task.goal,
                plan = executableSteps,
                history = history,
                trace = trace,
                successPrefix = "已到达${task.app}结果页：",
            )
            allTrace.addAll(trace)
            if (!ok) {
                results.add(MultiAppRememberResult(
                    app = task.app,
                    key = rememberString(task, "key").ifBlank { task.id },
                    success = false,
                    reason = "子任务未完成到可抽取结果页",
                ))
                progress(acc, "${task.app} 没拿到稳定结果，继续下一个 App")
                continue
            }

            val extracted = extractRememberForTask(acc, task, goal, allTrace)
            results.add(if (extracted.success) extracted.copy(pkg = expectedPkgNonNull) else extracted)
            progress(acc, formatRememberProgress(extracted))
        }

        val summary = summarizeMultiAppResults(plan, results)
        progress(acc, summary)
        val compareCards = results.filter { it.success && it.pkg.isNotBlank() }.map {
            MessageCard(
                packageName = it.pkg,
                title = "${it.app} · ${it.title.ifBlank { "已找到结果" }}".take(40),
                subtitle = it.valueRaw.ifBlank { it.value?.toString().orEmpty() },
                thumbnailPath = it.screenshotPath.ifBlank { null },
            )
        }
        sendAgentReport(acc, "跨 App 汇报", buildMultiAppReport(plan, results), compareCards)
        Log.i(TAG, "跨 App 结果: $results")
        return results.any { it.success }
    }

    private suspend fun extractRememberForTask(
        acc: LingxiAccessibilityService,
        task: MultiAppTask,
        originalGoal: String,
        trace: MutableList<AgentTraceItem>,
    ): MultiAppRememberResult {
        val key = rememberString(task, "key").ifBlank { task.id.ifBlank { task.app } }
        if (clearInterstitials(acc) > 0) progress(acc, "跳过了广告/弹窗，准备抽取")
        progress(acc, "${task.app}：等待结果加载并抽取")
        waitForScreenSettled(acc, budgetMs = 5000)
        delay(1200)
        val beforeScreen = acc.readScreen().take(1400)
        val beforeHash = beforeScreen.hashCode()
        if (paymentGuardHit(beforeScreen)) {
            return MultiAppRememberResult(task.app, key, false, reason = "支付/密码页面已硬停止")
        }
        val privacy = CloudVisionPolicy.decide(
            consentEnabled = CloudVisionConsentStore.isEnabled(acc),
            screenText = beforeScreen,
            purpose = CloudVisionPolicy.PURPOSE_STRUCTURED_EXTRACT,
            appName = task.app,
        )
        if (!privacy.allowed) {
            return MultiAppRememberResult(task.app, key, false, reason = privacy.userMessage)
        }
        val png = ScreenCaptureService.captureOnce()
            ?: return MultiAppRememberResult(task.app, key, false, reason = "屏幕视觉未返回截图")
        val screenshotPath = saveEvidencePng(acc, task.app, key, png)
        val els = visualCandidates(acc, rememberString(task, "extract_goal").ifBlank { task.goal })
            .mapIndexed { i, e ->
                AgentVisualElement(
                    i + 1,
                    e.text,
                    listOf(e.bounds.left, e.bounds.top, e.bounds.right, e.bounds.bottom),
                )
            }
        if (els.isEmpty()) {
            return MultiAppRememberResult(task.app, key, false, reason = "没有可抽取候选项")
        }
        val extractGoal = rememberString(task, "extract_goal").ifBlank { task.goal }
        val metric = rememberString(task, "metric").ifBlank { inferExtractMetric(extractGoal) }
        val higher = rememberBoolean(task, "higher_is_better") ?: inferExtractHigherIsBetter(extractGoal)
        val b64 = Base64.encodeToString(png, Base64.NO_WRAP)
        val evidence = CloudVisionPolicy.evidence(
            png,
            CloudVisionPolicy.PURPOSE_STRUCTURED_EXTRACT,
        )
        val r = try {
            brain.agentExtract(extractGoal, b64, els, metric, higher, task.app, evidence)
        } catch (e: Exception) {
            Log.w(TAG, "跨 App 抽取失败: ${e.message}")
            return MultiAppRememberResult(task.app, key, false, reason = e.message ?: "抽取异常")
        }
        trace.add(AgentTraceItem(
            subgoal = "REMEMBER $key",
            thought = "跨 App 结构化抽取 ${metric.ifBlank { "数值" }}",
            action = "extract:remember",
            target = r.reason,
            beforeHash = beforeHash,
            afterHash = acc.readScreen().take(1400).hashCode(),
            done = r.action != "error" && r.items.any { it.value != null },
            reason = "mark=${r.mark}",
        ))
        if (r.action == "error") {
            visualBackendDown = true
            return MultiAppRememberResult(task.app, key, false, reason = r.reason.ifBlank { "视觉模型错误" })
        }
        if (r.action == "blocked") {
            return MultiAppRememberResult(task.app, key, false, reason = r.reason.ifBlank { "云端截图被阻止" })
        }
        val comparable = r.items.filter { it.value != null }
        if (comparable.isEmpty()) {
            return MultiAppRememberResult(task.app, key, false, reason = r.reason.ifBlank { "没有抽到可比较数值" })
        }
        val winner = comparable.firstOrNull { it.mark == r.mark } ?: if (higher == true) {
            comparable.maxByOrNull { it.value ?: Double.NEGATIVE_INFINITY }
        } else {
            comparable.minByOrNull { it.value ?: Double.POSITIVE_INFINITY }
        } ?: comparable.first()
        return MultiAppRememberResult(
            app = task.app,
            key = key,
            success = true,
            title = winner.title,
            valueRaw = winner.value_raw?.toString().orEmpty(),
            value = winner.value,
            screenshotPath = screenshotPath,
            reason = r.reason.ifBlank { originalGoal },
        )
    }

    private fun isRememberStep(step: String): Boolean =
        step.trimStart().startsWith("REMEMBER", ignoreCase = true)

    private fun rememberString(task: MultiAppTask, field: String): String =
        task.remember[field]?.toString().orEmpty()

    private fun rememberBoolean(task: MultiAppTask, field: String): Boolean? {
        val v = task.remember[field] ?: return null
        return when (v) {
            is Boolean -> v
            is String -> v.equals("true", ignoreCase = true)
            else -> null
        }
    }

    private fun formatRememberProgress(r: MultiAppRememberResult): String {
        if (!r.success) return "${r.app}：抽取失败（${r.reason}）"
        val label = r.valueRaw.ifBlank { r.value?.toString().orEmpty() }
        val title = r.title.take(18)
        return "${r.app}：记住 ${label.ifBlank { "一个结果" }}${if (title.isNotBlank()) "（$title）" else ""}"
    }

    private fun summarizeMultiAppResults(
        plan: MultiAppPlanResponse,
        results: List<MultiAppRememberResult>,
    ): String {
        val ok = results.filter { it.success && it.value != null }
        if (ok.isEmpty()) {
            val failed = results.joinToString("；") { "${it.app}:${it.reason.ifBlank { "失败" }}" }
            return "跨 App 比较没拿到可比较数字：$failed"
        }
        val strategy = plan.merge["strategy"]?.toString().orEmpty()
        val pickMin = strategy.equals("min", ignoreCase = true) || plan.merge["metric"]?.toString() == "price"
        val best = if (pickMin) ok.minByOrNull { it.value ?: Double.POSITIVE_INFINITY }
            else ok.maxByOrNull { it.value ?: Double.NEGATIVE_INFINITY }
        val details = results.joinToString("；") {
            if (it.success) "${it.app}=${it.valueRaw.ifBlank { it.value?.toString().orEmpty() }}"
            else "${it.app}=失败(${it.reason})"
        }
        return if (best != null) {
            "✅ 跨 App 比较完成：${best.app} 目前最符合（${best.valueRaw.ifBlank { best.value?.toString().orEmpty() }}）。$details"
        } else {
            "跨 App 比较完成，但没有可排序结果。$details"
        }
    }

    private fun buildMultiAppReport(
        plan: MultiAppPlanResponse,
        results: List<MultiAppRememberResult>,
    ): String {
        val ok = results.filter { it.success && it.value != null }
        val strategy = plan.merge["strategy"]?.toString().orEmpty()
        val pickMin = strategy.equals("min", ignoreCase = true) || plan.merge["metric"]?.toString() == "price"
        val best = if (pickMin) ok.minByOrNull { it.value ?: Double.POSITIVE_INFINITY }
            else ok.maxByOrNull { it.value ?: Double.NEGATIVE_INFINITY }

        val lines = mutableListOf<String>()
        lines += "任务：${plan.goal.ifBlank { "跨 App 比较" }}"
        if (best != null) {
            val bestValue = best.valueRaw.ifBlank { best.value?.toString().orEmpty() }
            lines += "结论：${best.app} 当前最符合要求，抽到的数值是 $bestValue"
        } else {
            lines += "结论：这次没有拿到可排序的有效数值"
        }
        lines += "证据："
        results.forEach { r ->
            if (r.success) {
                val value = r.valueRaw.ifBlank { r.value?.toString().orEmpty() }.ifBlank { "已抽取" }
                val title = r.title.ifBlank { "未抽到标题" }
                val shot = if (r.screenshotPath.isNotBlank()) "，截图=${r.screenshotPath}" else ""
                lines += "- ${r.app}: $value，$title$shot"
            } else {
                lines += "- ${r.app}: 抽取失败（${r.reason.ifBlank { "未知原因" }}）"
            }
        }
        val failed = results.filterNot { it.success }
        if (failed.isNotEmpty()) {
            lines += "备注：失败项没有编造结果，本次结论只基于成功抽取的 App。"
        }
        return lines.joinToString("\n")
    }

    private suspend fun isExpectedAppForeground(
        acc: LingxiAccessibilityService,
        expectedPkg: String,
        timeoutMs: Long = 3500,
    ): Boolean {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            val actual = acc.foregroundPackageName()
            if (actual == expectedPkg) return true
            delay(250)
        }
        Log.w(TAG, "前台 App 不匹配 expected=$expectedPkg actual=${acc.foregroundPackageName()}")
        return false
    }

    private fun saveEvidencePng(ctx: Context, app: String, key: String, png: ByteArray): String {
        return runCatching {
            val dir = File(ctx.filesDir, "agent_reports").apply { mkdirs() }
            val safeApp = app.replace(Regex("[^A-Za-z0-9_\\u4e00-\\u9fa5-]"), "_")
            val safeKey = key.replace(Regex("[^A-Za-z0-9_\\u4e00-\\u9fa5-]"), "_")
            val file = File(dir, "${System.currentTimeMillis()}_${safeApp}_${safeKey}.png")
            file.writeBytes(png)
            file.absolutePath
        }.getOrElse {
            Log.w(TAG, "保存证据截图失败: ${it.message}")
            ""
        }
    }

    private val reportGson = Gson()

    private fun sendAgentReport(ctx: Context, title: String, report: String, cards: List<MessageCard> = emptyList()) {
        if (report.isBlank()) return
        Log.i(TAG, "Agent 报告: $report" + if (cards.isNotEmpty()) "（附 ${cards.size} 张卡片）" else "")
        ctx.startActivity(Intent(ctx, MainActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
            putExtra(MainActivity.EXTRA_AGENT_REPORT_TITLE, title)
            putExtra(MainActivity.EXTRA_AGENT_REPORT_TEXT, report)
            if (cards.isNotEmpty()) putExtra(MainActivity.EXTRA_AGENT_REPORT_CARDS, reportGson.toJson(cards))
        })
    }

    /** 单 App 任务成功后，额外补一张可点回跳的结果卡——纯增量，不改 executePlan/runAgent 原有文字播报。 */
    private suspend fun sendResultCard(ctx: Context, appLabel: String, goal: String) {
        val pkg = resolvePackage(ctx, appLabel) ?: return
        val screenshotPath = ScreenCaptureService.captureOnce()
            ?.let { saveEvidencePng(ctx, appLabel, "result", it) }
            ?.ifBlank { null }
        sendAgentReport(
            ctx, "任务完成",
            "已完成「$goal」，点击卡片可直接跳回「$appLabel」刚才停留的页面。",
            listOf(MessageCard(pkg, "$appLabel · ${goal.take(20)}", "已找到并停在结果页", screenshotPath)),
        )
    }

    private suspend fun executePlan(
        acc: LingxiAccessibilityService,
        app: String,
        goal: String,
        plan: List<String>,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
        successPrefix: String = "✅ 完成：",
    ): Boolean {
        for ((idx, subgoal) in plan.withIndex()) {
            if (cancelRequested) {
                progress(acc, "已结束")
                return false
            }
            progress(acc, "步骤 ${idx + 1}/${plan.size}：$subgoal")
            val ok = runSubgoal(acc, goal, subgoal, history, trace)
            if (cancelRequested) {
                progress(acc, "已结束")
                return false
            }
            if (!ok) {
                brain.recordAgentMemory(app, goal, plan, history, trace, false, "failed at: $subgoal")
                progress(acc, "「$subgoal」这步没能完成，先停下（不乱点）")
                return false
            }
        }
        brain.recordAgentMemory(app, goal, plan, history, trace, true, "completed")
        progress(acc, "$successPrefix$goal")
        return true
    }

    private fun buildDemoRecoveryGoal(goal: String, replay: DemoReplayResult): String {
        val screenHint = replay.screen.replace(Regex("\\s+"), " ").take(300)
        return "继续完成原任务：$goal。刚才按真人教学回放到第${replay.failedStep}步「${replay.label}」失败，原因：${replay.reason}。当前屏幕文本：$screenHint。请不要重复同一条教学回放路径，从当前页面重新规划可执行步骤。"
    }

    /** 无障碍服务录完真人演示后调用：上报大脑存为可回放技能。隐私：只传文字/坐标/页面签名。 */
    fun saveDemo(ctx: Context, goal: String, steps: List<DemoStep>) {
        if (goal.isBlank() || steps.isEmpty()) {
            OverlayProgress.update(ctx, "没录到可用步骤，教学未保存")
            return
        }
        scope.launch {
            brain.recordDemo("", goal, steps)   // app 留空：大脑按 goal 匹配（教学只知包名、回放传 App 名）
            OverlayProgress.update(ctx, "✅ 已学会「$goal」：${steps.size} 步，下次直接回放")
            Log.i(TAG, "已保存真人演示: $goal (${steps.size} 步)")
        }
    }

    /** 确定性回放：逐步——先校验当前页是否匹配该步签名(治"页面变样还硬点")→按文字/坐标点→等跳转。 */
    private suspend fun replayDemo(
        acc: LingxiAccessibilityService,
        goal: String,
        steps: List<DemoStep>,
    ): DemoReplayResult {
        progress(acc, "📚 按你教的路径操作：$goal（${steps.size} 步）")
        val history = mutableListOf<String>()
        val trace = mutableListOf<AgentTraceItem>()
        for ((i, step) in steps.withIndex()) {
            if (cancelRequested) {
                progress(acc, "已结束")
                return DemoReplayResult(false, allowReplan = false, history = history, trace = trace)
            }

            // 滑动步：直接下滑（教学时录的滑动），翻出下面的内容
            if (step.action == "scroll") {
                progress(acc, "步骤 ${i + 1}/${steps.size}：下滑")
                val beforeHash = acc.readScreen().take(1400).hashCode()
                acc.scrollDown()
                delay(1500)
                val afterHash = acc.readScreen().take(1400).hashCode()
                history.add("demo_scroll:${i + 1}")
                trace.add(AgentTraceItem(
                    subgoal = "真人教学回放",
                    action = "demo:scroll",
                    target = "scroll",
                    beforeHash = beforeHash,
                    afterHash = afterHash,
                    done = beforeHash != afterHash,
                    reason = "教学滑动步骤",
                ))
                continue
            }

            // 每步点击前先清一遍插屏广告——不止开局那一下，回放过程中随时可能弹出（如切页触发的广告位）。
            if (clearInterstitials(acc) > 0) progress(acc, "跳过了广告/弹窗，继续按教学步骤")

            // 点击前：稳定签名只当【提示】（动态页签名不可靠，不靠它硬停，免得误伤好回放）
            val before = acc.readScreen()
            if (paymentGuardHit(before)) {
                progress(acc, "⚠ 检测到支付/密码页面，为安全立即停止操作，请自行确认")
                return DemoReplayResult(
                    success = false,
                    allowReplan = false,
                    failedStep = i + 1,
                    label = "支付/密码页",
                    reason = "payment guard",
                    screen = before.take(1400),
                    history = history,
                    trace = trace,
                )
            }
            val overlap = DemoSig.overlap(before, step.sig)
            if (overlap < 0.2f) {
                progress(acc, "（第 ${i + 1} 步页面和教学略有不同，仍按你教的位置点）")
                Log.w(TAG, "回放签名偏差 step${i + 1} overlap=$overlap")
            }

            val label = step.text.ifBlank { "卡片" }
            progress(acc, "步骤 ${i + 1}/${steps.size}：点「$label」")
            val beforeHash = before.take(1400).hashCode()
            val ok = if (step.text.isNotBlank())
                acc.tapByText(step.text) || tapBoundsCenter(acc, step.bounds)
            else tapBoundsCenter(acc, step.bounds)
            if (!ok) {
                progress(acc, "第 ${i + 1} 步没点到「$label」，准备重新规划")
                history.add("demo_tap_failed:$label")
                trace.add(AgentTraceItem(
                    subgoal = "真人教学回放",
                    action = "demo:tap",
                    target = label,
                    beforeHash = beforeHash,
                    afterHash = beforeHash,
                    done = false,
                    reason = "未点到教学元素",
                ))
                return DemoReplayResult(
                    success = false,
                    failedStep = i + 1,
                    label = label,
                    reason = "未点到教学元素",
                    screen = before.take(1400),
                    history = history,
                    trace = trace,
                )
            }
            delay(1800)   // 等页面跳转

            // 自纠错（BacktrackAgent 式前后状态对比）：点完屏幕【完全没变】=没点中/页面不对→诚实停，
            // 绝不硬撑往下走、绝不假装完成。动态页(弹幕/自动播放)前后必然不同→不会误停。
            val after = acc.readScreen()
            val afterHash = after.take(1400).hashCode()
            history.add("demo_tap:$label")
            trace.add(AgentTraceItem(
                subgoal = "真人教学回放",
                action = "demo:tap",
                target = label,
                beforeHash = beforeHash,
                afterHash = afterHash,
                done = before != after,
                reason = "教学点击步骤",
            ))
            if (before == after) {
                progress(acc, "⚠ 第 ${i + 1} 步点了「$label」屏幕没反应，准备重新规划")
                Log.w(TAG, "回放无变化 step${i + 1}")
                return DemoReplayResult(
                    success = false,
                    failedStep = i + 1,
                    label = label,
                    reason = "点击后屏幕无变化",
                    screen = after.take(1400),
                    history = history,
                    trace = trace,
                )
            }
        }
        progress(acc, "✅ 完成（按你教的路径）：$goal")
        return DemoReplayResult(true, history = history, trace = trace)
    }

    /** 按元素 bounds 中心做手势点击（无文字的卡片走这条）。 */
    private fun tapBoundsCenter(acc: LingxiAccessibilityService, b: List<Int>): Boolean {
        if (b.size != 4) return false
        val cx = (b[0] + b[2]) / 2f
        val cy = (b[1] + b[3]) / 2f
        if (cx <= 0f && cy <= 0f) return false
        acc.tap(cx, cy)
        return true
    }

    /** 在【一个子步骤】内反应式推进，直到 subgoal_done 或耗尽预算。完成 true、卡死 false。 */
    private suspend fun runSubgoal(
        acc: LingxiAccessibilityService, goal: String, subgoal: String,
        history: MutableList<String>, trace: MutableList<AgentTraceItem>,
    ): Boolean {
        var lastAct = ""; var lastHash = 0; var stalled = 0; var visionFails = 0; var decisionFails = 0
        val recovery = RecoveryState()
        visualBackendDown = false   // 每步重置；视觉后端挂掉时立即诚实收手
        runDeterministicAppSubgoal(acc, goal, subgoal, history, trace)?.let { return it }
        if (isStructuredExtractPickSubgoal(subgoal)) {
            return runStructuredExtractPickSubgoal(acc, goal, subgoal, history, trace)
        }
        // 这一步是不是"挑一个视觉项"(找/第一个 直播间/视频/歌…)？是→视觉优先，别让文本把搜索框/标签当内容瞎点
        val visualPick = isVisualPickSubgoal(subgoal)
        stepLoop@ for (i in 1..MAX_STEPS_PER_SUBGOAL) {
            if (cancelRequested) return false
            delay(800)
            // 每次决策前先清一遍插屏广告——治"进了主界面才弹"的延迟广告，不止靠启动时那一次检查。
            if (clearInterstitials(acc) > 0) progress(acc, "跳过了广告/弹窗，继续")
            val screenFull = acc.readScreen()
            if (paymentGuardHit(screenFull)) {
                progress(acc, "⚠ 检测到支付/密码页面，为安全立即停止操作，请自行确认")
                return false
            }
            val screen = screenFull.take(1400)
            val s = try {
                brain.agentStep(goal, subgoal, screen, history, currentApp)
            } catch (e: Exception) {
                Log.e(TAG, "决策失败: ${e.message}"); progress(acc, "操作中断：${e.message}"); return false
            }
            if (cancelRequested) return false   // 用户点了✕ → 不再执行这步决策
            if (s.thought.isNotBlank()) progress(acc, "💭 ${s.thought}")
            val beforeHash = screen.hashCode()
            if (s.action == "error") {
                // 🐛 bug1(学长 review)：大脑决策失败(超时/JSON坏)【绝不】当"这步完成"推进——
                //    重试一次，仍失败诚实收手（与视觉后端挂掉 visualBackendDown 同款处理）。
                decisionFails++
                if (decisionFails >= 2) {
                    progress(acc, "⚠ 大脑决策失败（${s.reason}），这步先停下，不硬推进")
                    return false
                }
                progress(acc, "决策没成，重试一次…"); delay(600); continue@stepLoop
            }
            decisionFails = 0   // 成功拿到决策 → 归零
            if (s.subgoal_done) {
                trace.add(AgentTraceItem(
                    subgoal = subgoal,
                    thought = s.thought,
                    action = "none",
                    beforeHash = beforeHash,
                    afterHash = beforeHash,
                    done = true,
                    reason = s.reason,
                ))
                progress(acc, "✓ 这步完成"); return true
            }   // 达成→推进下一步

            val act = "${s.action}:${s.target}"
            // 大模型已经从文本里读出具体目标、且这个目标真的在当前屏上——这是比"猜"更硬的证据，
            // 视觉该给这种已验证的文本匹配让路，不该无脑覆盖。2026-07-03 真机实测抓到：酷狗"点第一首歌"
            // 这类子步骤，模型已正确读出歌名(如"小半")、歌名也确实在屏上，但 visualPick 静态判定
            // "挑歌=一律要视觉"，把明明能直接点的一步硬憋停在"屏幕视觉没开"——视觉该是文本不confident
            // 时的兜底，不是所有"挑歌/挑视频"字面的一票否决。
            val hasConfidentTextTarget = s.action == "tap" && s.target.isNotBlank() &&
                screen.contains(s.target, ignoreCase = true)
            // 触发视觉：①这步是"挑视觉项"(直播间/视频/歌…)且模型没能给出已验证的文本目标→视觉优先
            //          ②大模型主动甩给视觉(action=vision) ③同动作连发卡住 ④tap 的词不在当前屏(瞎猜)
            val needVisual = (visualPick && !hasConfidentTextTarget) || s.action == "vision" ||
                (act == lastAct && s.action != "none") ||
                (s.action == "tap" && s.target.isNotBlank() && !screen.contains(s.target, ignoreCase = true))
            if (needVisual) {
                if (!ScreenCaptureService.isReady) {
                    progress(acc, "⚠ 屏幕视觉没开，无法可靠分辨这类卡片")
                    history.add("visual_not_ready")
                    trace.add(AgentTraceItem(
                        subgoal = subgoal,
                        thought = s.thought,
                        action = "vision_not_ready",
                        target = s.target,
                        beforeHash = beforeHash,
                        afterHash = beforeHash,
                        reason = "screen vision off",
                    ))
                    // 视觉选择题(直播间/视频/歌曲卡片)没有截图时宁可停下，也不点搜索框/广告/标签栏冒充完成。
                    if (visualPick || s.action == "vision") return false
                } else {
                    progress(acc, "👁 看一眼屏幕分辨…")
                    val v = tryVisualStep(acc, subgoal, history)
                    if (cancelRequested) return false   // 视觉调用期间点了✕ → 立刻停
                    if (visualBackendDown) {            // VLM 后端没连上 → 这步无法可靠完成，诚实收手（不空转、不假成功）
                        progress(acc, "⚠ 视觉模型没连上，这步没法完成（检查台式机视觉服务），先停下")
                        return false
                    }
                    if (v.isNotEmpty()) {
                        delay(700)
                        val afterScreen = acc.readScreen().take(1400)
                        val afterHash = afterScreen.hashCode()
                        trace.add(AgentTraceItem(
                            subgoal = subgoal,
                            thought = s.thought,
                            action = "vision:$v",
                            beforeHash = beforeHash,
                            afterHash = afterHash,
                            reason = s.reason,
                        ))
                        history.add("v:$v"); lastAct = "v"; lastHash = 0; visionFails = 0
                        if (visualPick && v == "tap") {
                            val r = reflectAfterAction(
                                acc = acc,
                                goal = goal,
                                subgoal = subgoal,
                                action = "vision:tap",
                                target = "视觉候选",
                                beforeScreen = screen,
                                afterScreen = afterScreen,
                            )
                            when (r.status.uppercase()) {
                                "SUCCESS" -> {
                                    progress(acc, "✓ 反思确认：这步已到目标页")
                                    return true
                                }
                                else -> {
                                    val decision = recoverAfterReflection(
                                        acc = acc,
                                        subgoal = subgoal,
                                        reflect = r,
                                        recovery = recovery,
                                        history = history,
                                        trace = trace,
                                    )
                                    if (decision == RecoveryDecision.STOP) return false
                                    continue@stepLoop
                                }
                            }
                        }
                        continue
                    }
                    if (++visionFails >= 3) { Log.w(TAG, "视觉连续失败，放弃本步"); return false }  // 防超时空转
                    // 视觉没选出可执行动作：换一屏再看，不再误导成"没看清"。
                    progress(acc, "视觉没选准，下滑换一屏"); acc.scrollDown()
                    delay(500)
                    trace.add(AgentTraceItem(
                        subgoal = subgoal,
                        thought = s.thought,
                        action = "scroll",
                        beforeHash = beforeHash,
                        afterHash = acc.readScreen().take(1400).hashCode(),
                        reason = "visual_retry",
                    ))
                    history.add("scroll"); lastAct = "scroll"; continue
                }
            }
            lastAct = act

            Log.i(TAG, "[$subgoal] ${s.action} '${s.target}' (${s.reason})")
            when (s.action) {
                "tap" -> { progress(acc, "点击「${s.target}」"); acc.tapByText(s.target) }   // 坐标点击，能进卡片
                "input" -> { progress(acc, "输入「${s.target}」"); acc.setFocusedText(s.target) }
                "scroll" -> { progress(acc, "下滑找内容"); acc.scrollDown() }
                "back" -> { progress(acc, "返回"); acc.pressBack() }
                else -> {}   // none = 等待/无操作；vision 已在上面处理
            }
            // 停滞检测（本子步骤内）：连续没变化→上视觉；视觉也没辙→这步卡死收手(不回头)
            // 🐛 真机实测(2026-07-03，用户连续三次揪出)：tap 后固定延时(500ms，后改成"跟点击前比一次没变
            // 再多等1.2s")都不够稳——网络型内容(歌单/搜索结果)加载中途会先变成"加载中"这类过渡态，
            // 跟点击前一比"哈希不同"就误以为已经加载完，其实还没到最终内容，反思照样误判。
            // 真正稳的判据不是"变没变"，是"连续两次读到的文本一样"（内容不再刷新了）——
            // tap 用这个来等，其余动作(scroll/input/none)本来就是本地即时状态，仍用固定 500ms。
            val afterScreen = if (s.action == "tap") waitForScreenSettled(acc) else run {
                delay(500); acc.readScreen().take(1400)
            }
            val h2 = afterScreen.hashCode()
            // 🐛 bug5(学长 review)：history 带【结果信号】——让"点了没变化"成模型可读事实(治重复点同一元素，
            //    不再全靠 prompt 央求)。none 无动作不加结果。
            history.add(if (s.action == "none") act else "$act ${if (h2 != beforeHash) "→变了" else "→没变化"}")
            if (history.size > 10) history.removeAt(0)
            trace.add(AgentTraceItem(
                subgoal = subgoal,
                thought = s.thought,
                action = s.action,
                target = s.target,
                beforeHash = beforeHash,
                afterHash = h2,
                reason = s.reason,
            ))
            if (shouldReflectAfterAction(goal, subgoal, s.action)) {
                val r = reflectAfterAction(
                    acc = acc,
                    goal = goal,
                    subgoal = subgoal,
                    action = s.action,
                    target = s.target,
                    beforeScreen = screen,
                    afterScreen = afterScreen,
                )
                when (r.status.uppercase()) {
                    "SUCCESS" -> {
                        progress(acc, "✓ 反思确认：这步完成")
                        return true
                    }
                    else -> {
                        val decision = recoverAfterReflection(
                            acc = acc,
                            subgoal = subgoal,
                            reflect = r,
                            recovery = recovery,
                            history = history,
                            trace = trace,
                        )
                        stalled = 0
                        lastHash = 0
                        if (decision == RecoveryDecision.STOP) return false
                        continue@stepLoop
                    }
                }
            }
            if (h2 == lastHash && s.action != "input" && s.action != "none") {
                if (++stalled >= 3) {
                    if (tryVisualStep(acc, subgoal, history).isEmpty()) return false
                    stalled = 0
                }
            } else stalled = 0
            lastHash = h2
        }
        return tryVisualStep(acc, subgoal, history).isNotEmpty()   // 预算耗尽，最后搏一次视觉
    }

    /** T-14：比较型选择（播放量最高/评分最高/最便宜等）优先走结构化抽取。
     *  VLM 只抽每个编号的数值，winner 由大脑代码比较；安卓只负责截图、候选、点击 winner。 */
    private suspend fun runStructuredExtractPickSubgoal(
        acc: LingxiAccessibilityService,
        goal: String,
        subgoal: String,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
    ): Boolean {
        if (!ScreenCaptureService.isReady) {
            progress(acc, "这一步要比较数字/价格/评分，请先点「屏幕视觉」授权；我不会凭空猜。")
            history.add("extract_not_ready")
            return false
        }
        val recovery = RecoveryState()
        for (attempt in 1..3) {
            if (cancelRequested) return false
            if (clearInterstitials(acc) > 0) progress(acc, "跳过了广告/弹窗，继续")
            val beforeScreen = acc.readScreen().take(1400)
            if (paymentGuardHit(beforeScreen)) {
                progress(acc, "⚠ 检测到支付/密码页面，为安全立即停止操作，请自行确认")
                return false
            }
            val privacy = CloudVisionPolicy.decide(
                consentEnabled = CloudVisionConsentStore.isEnabled(acc),
                screenText = beforeScreen,
                purpose = CloudVisionPolicy.PURPOSE_STRUCTURED_EXTRACT,
                appName = currentApp,
            )
            if (!privacy.allowed) {
                progress(acc, "⚠ ${privacy.userMessage}")
                history.add("cloud_vision_blocked:${privacy.code}")
                return false
            }
            val png = ScreenCaptureService.captureOnce()
            if (png == null) {
                progress(acc, "屏幕视觉暂时没抓到画面，等一下")
                delay(500)
                continue
            }
            val els = visualCandidates(acc, subgoal)
                .mapIndexed { i, e ->
                    AgentVisualElement(
                        i + 1, e.text,
                        listOf(e.bounds.left, e.bounds.top, e.bounds.right, e.bounds.bottom)
                    )
                }
            if (els.isEmpty()) {
                progress(acc, "没找到可比较的候选项，下滑看看")
                acc.scrollDown()
                delay(900)
                continue
            }

            progress(acc, "🔢 抽取并比较候选项数值")
            val b64 = Base64.encodeToString(png, Base64.NO_WRAP)
            val evidence = CloudVisionPolicy.evidence(
                png,
                CloudVisionPolicy.PURPOSE_STRUCTURED_EXTRACT,
            )
            val metric = inferExtractMetric(subgoal)
            val higher = inferExtractHigherIsBetter(subgoal)
            val r = try {
                brain.agentExtract(subgoal, b64, els, metric, higher, currentApp, evidence)
            } catch (e: Exception) {
                Log.w(TAG, "结构化抽取失败: ${e.message}")
                progress(acc, "结构化抽取失败：${e.message}")
                return false
            }
            Log.i(TAG, "结构化抽取: action=${r.action} mark=${r.mark} reason=${r.reason} items=${r.items}")
            progress(acc, "🔢 ${r.reason.ifBlank { "已比较候选项" }}")
            if (r.action == "error") {
                visualBackendDown = true
                progress(acc, "⚠ 视觉模型没连上，没法可靠比较数值，先停下")
                return false
            }
            if (r.action == "blocked") {
                progress(acc, "⚠ 云端截图安全门已阻止本次上传（${r.reason}）")
                return false
            }
            if (r.action == "tap" && r.x != null && r.y != null) {
                val beforeHash = beforeScreen.hashCode()
                acc.tap(r.x.toFloat(), r.y.toFloat())
                val afterScreen = waitForScreenSettled(acc)
                val afterHash = afterScreen.hashCode()
                history.add("extract_tap:${r.mark}:${r.reason}")
                trace.add(AgentTraceItem(
                    subgoal = subgoal,
                    thought = "结构化抽取比较${metric.ifBlank { "数值" }}",
                    action = "extract:tap",
                    target = r.reason,
                    beforeHash = beforeHash,
                    afterHash = afterHash,
                    reason = "mark=${r.mark}",
                ))
                val reflect = reflectAfterAction(
                    acc = acc,
                    goal = goal,
                    subgoal = subgoal,
                    action = "extract:tap",
                    target = r.reason,
                    beforeScreen = beforeScreen,
                    afterScreen = afterScreen,
                )
                if (reflect.status.uppercase() == "SUCCESS") {
                    progress(acc, "✓ 已点开比较结果中最符合的一项")
                    return true
                }
                val decision = recoverAfterReflection(acc, subgoal, reflect, recovery, history, trace)
                if (decision == RecoveryDecision.STOP) return false
                continue
            }

            progress(acc, "这一屏没抽到可比较数值，下滑换一屏")
            acc.scrollDown()
            delay(900)
        }
        progress(acc, "没能稳定抽到可比较结果，先停下")
        return false
    }

    private fun isStructuredExtractPickSubgoal(subgoal: String): Boolean {
        val t = subgoal.replace(" ", "")
        if ("REMEMBER" in subgoal || "记录" in t || "汇总" in t || "确认" in t) return false
        val compare = listOf("最高", "最多", "最便宜", "最低", "哪家便宜", "哪个便宜", "价格低")
            .any { t.contains(it) }
        val metric = listOf("播放量", "点赞", "赞", "收藏", "评分", "销量", "价格", "价", "便宜")
            .any { t.contains(it) }
        val wantsOpen = listOf("点击", "点开", "打开", "进入", "播放", "收藏", "点赞")
            .any { t.contains(it) }
        return compare && metric && wantsOpen
    }

    private fun inferExtractMetric(subgoal: String): String = when {
        "播放" in subgoal || "播放量" in subgoal -> "播放量"
        "评分" in subgoal -> "评分"
        "销量" in subgoal -> "销量"
        "点赞" in subgoal || "赞" in subgoal -> "点赞数"
        "收藏" in subgoal -> "收藏数"
        "价格" in subgoal || "便宜" in subgoal || "价" in subgoal -> "价格"
        else -> ""
    }

    private fun inferExtractHigherIsBetter(subgoal: String): Boolean? {
        val t = subgoal.replace(" ", "")
        if ("最便宜" in t || "哪家便宜" in t || "哪个便宜" in t || "最低" in t || "价格低" in t) return false
        if ("最高" in t || "最多" in t || "销量" in t || "播放量" in t || "评分" in t || "点赞" in t) return true
        return null
    }

    /** App 专用确定性小动作：把已知高频坑从 LLM 决策里拿出来，少给它在关键入口处"神游"的空间。 */
    private suspend fun runDeterministicAppSubgoal(
        acc: LingxiAccessibilityService,
        goal: String,
        subgoal: String,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
    ): Boolean? {
        val appName = currentApp.lowercase()
        if (("知乎" in currentApp || "zhihu" in appName) && isZhihuOpenFirstResultSubgoal(subgoal)) {
            return runZhihuOpenFirstResultSubgoal(acc, subgoal, history, trace)
        }
        if (("知乎" in currentApp || "zhihu" in appName) && isSearchSubmitSubgoal(subgoal)) {
            return runZhihuSearchSubgoal(acc, goal, subgoal, history, trace)
        }
        if (("淘宝" in currentApp || "taobao" in appName) && isTaobaoSearchEntrySubgoal(subgoal)) {
            return runTaobaoSearchEntrySubgoal(acc, subgoal, history, trace)
        }
        if (("淘宝" in currentApp || "taobao" in appName) && isShoppingOpenFirstResultSubgoal(subgoal)) {
            return runShoppingOpenFirstResultSubgoal(acc, subgoal, history, trace, "淘宝")
        }
        if (("淘宝" in currentApp || "taobao" in appName) && isShoppingSearchSubgoal(subgoal)) {
            return runTaobaoSearchSubgoal(acc, goal, subgoal, history, trace)
        }
        if (("京东" in currentApp || "jd" in appName || "jingdong" in appName) && isShoppingOpenFirstResultSubgoal(subgoal)) {
            return runShoppingOpenFirstResultSubgoal(acc, subgoal, history, trace, "京东")
        }
        if (("京东" in currentApp || "jd" in appName || "jingdong" in appName) && isShoppingSearchSubgoal(subgoal)) {
            return runJdSearchSubgoal(acc, goal, subgoal, history, trace)
        }
        return null
    }

    private fun isSearchSubmitSubgoal(subgoal: String): Boolean {
        val t = subgoal.replace(" ", "")
        if ("搜索结果页" in t || "点击第一个" in t || "点开第一个" in t || "第一个回答" in t || "第一个回复" in t) return false
        return "输入" in t && ("搜" in t || "搜索框" in t || "搜索栏" in t || "并搜索" in t)
    }

    private fun isShoppingSearchSubgoal(subgoal: String): Boolean {
        val t = subgoal.replace(" ", "")
        if ("搜索结果页" in t || "结果页" in t || "REMEMBER" in subgoal || "抽取" in t || "记录" in t) return false
        if ("点击第一个" in t || "点开第一个" in t || "加入购物车" in t || "立即购买" in t) return false
        return isSearchSubmitSubgoal(subgoal) || Regex("^(?:搜索|搜一下|搜)[「\\\"“']?.+").containsMatchIn(t)
    }

    private fun isTaobaoSearchEntrySubgoal(subgoal: String): Boolean {
        val t = subgoal.replace(" ", "")
        if ("输入" in t || "完成搜索" in t || "结果" in t) return false
        val searchArea = "搜索栏" in t || "搜索框" in t || "热搜词" in t || "推荐词" in t
        val open = "点击" in t || "点开" in t || "进入" in t || "激活" in t
        return searchArea && open
    }

    private fun isShoppingOpenFirstResultSubgoal(subgoal: String): Boolean {
        val t = subgoal.replace(" ", "")
        if ("结果页" in t || "搜索结果页" in t || "REMEMBER" in subgoal || "记录" in t || "汇总" in t || "抽取" in t) return false
        if (listOf("最便宜", "最高", "最多", "评分最高", "销量最高", "比价", "哪家便宜").any { t.contains(it) }) return false
        if ("确认" in t || "详情页" in t || "已打开" in t) return false
        val first = "第一个" in t || "第一条" in t || "首个" in t || "第1个" in t
        val result = "商品" in t || "宝贝" in t || "链接" in t || "结果" in t || "卡片" in t || "视频" in t
        val open = "点击" in t || "点开" in t || "打开" in t || "进入" in t
        return first && result && (open || result)
    }

    private fun isZhihuOpenFirstResultSubgoal(subgoal: String): Boolean {
        val t = subgoal.replace(" ", "")
        if ("确认" in t || "已打开" in t || "详情页" in t) return false
        val first = "第一个" in t || "第一条" in t || "首个" in t
        val result = "回答" in t || "回复" in t || "结果" in t || "内容" in t || "卡片" in t
        val open = "点击" in t || "点开" in t || "打开" in t || result
        return first && result && open
    }

    private suspend fun runZhihuOpenFirstResultSubgoal(
        acc: LingxiAccessibilityService,
        subgoal: String,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
    ): Boolean {
        val beforeScreen = acc.readScreen()
        val beforeHash = beforeScreen.take(1400).hashCode()
        progress(acc, "知乎结果页：点击第一条结果")
        val minTop = (acc.resources.displayMetrics.heightPixels * 0.15f).toInt()
        val prefixes = listOf(
            "Answer:Card:OpenUrl:content_card:",
            "Post:Card:OpenUrl:content_card:",
            "Article:Card:OpenUrl:content_card:",
        )
        var clicked = false
        val deadline = System.currentTimeMillis() + 6000
        while (System.currentTimeMillis() < deadline && !clicked) {
            clicked = acc.tapFirstZhihuPostTextArea(prefixes, minTopPx = minTop)
            if (!clicked) delay(600)
        }
        if (!clicked) {
            progress(acc, "知乎结果页：没找到带赞同/评论/浏览/回答/时间标记的帖子卡片，先停下避免点知乎直答")
        }
        val afterScreen = waitForScreenSettled(acc, budgetMs = 5000)
        val changed = afterScreen != beforeScreen
        history.add(if (clicked) "zhihu_open_first_result" else "zhihu_open_first_result_failed")
        trace.add(AgentTraceItem(
            subgoal = subgoal,
            thought = "知乎搜索结果页点击第一条带赞同/评论/浏览/回答/时间标记的帖子卡片，避开知乎直答、顶部搜索栏和标签",
            action = "deterministic:zhihu_open_first_result",
            target = "first_result_card",
            beforeHash = beforeHash,
            afterHash = afterScreen.take(1400).hashCode(),
            done = clicked && changed,
            reason = if (clicked) "点击第一条结果卡片" else "未找到结果卡片",
        ))
        return clicked && changed
    }

    private suspend fun runZhihuSearchSubgoal(
        acc: LingxiAccessibilityService,
        goal: String,
        subgoal: String,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
    ): Boolean {
        val query = extractSearchQuery(goal, subgoal)
        if (query.isNullOrBlank()) return false
        if (clearInterstitials(acc) > 0) progress(acc, "跳过了广告/弹窗，继续搜索")

        progress(acc, "知乎搜索：点顶部左侧搜索框")
        val beforeHash = acc.readScreen().take(1400).hashCode()
        if (!acc.setFocusedText(query)) {
            tapZhihuTopLeftSearchField(acc)
            if (!waitAndSetFocusedText(acc, query)) {
                history.add("zhihu_search_focus_failed")
                trace.add(AgentTraceItem(
                    subgoal = subgoal,
                    action = "deterministic:zhihu_search",
                    target = query,
                    beforeHash = beforeHash,
                    afterHash = acc.readScreen().take(1400).hashCode(),
                    reason = "未聚焦输入框",
                ))
                return false
            }
        }

        progress(acc, "知乎搜索：输入「$query」并提交")
        if (acc.isViewIdChecked("com.zhihu.android:id/ai_switch")) {
            progress(acc, "知乎搜索：关闭 AI 搜索")
            acc.tapByViewId("com.zhihu.android:id/ai_switch")
            delay(300)
        }
        delay(500)
        val submitted = tapZhihuTopRightSearchButton(acc)
        if (!submitted) {
            progress(acc, "知乎搜索：没找到可点击的搜索按钮，先停下避免误点浮窗")
            history.add("zhihu_search_submit_failed")
            trace.add(AgentTraceItem(
                subgoal = subgoal,
                action = "deterministic:zhihu_search",
                target = query,
                beforeHash = beforeHash,
                afterHash = acc.readScreen().take(1400).hashCode(),
                done = false,
                reason = "未找到搜索按钮 resource-id",
            ))
            return false
        }
        val afterScreen = waitForZhihuSearchResultsReady(acc, query)
        val afterHash = afterScreen.hashCode()
        val submittedOk = isZhihuSearchResultScreen(afterScreen, query)
        history.add("zhihu_search:$query")
        trace.add(AgentTraceItem(
            subgoal = subgoal,
            thought = "知乎搜索框使用确定性路径，避开AI搜索和热搜词",
            action = "deterministic:zhihu_search",
            target = query,
            beforeHash = beforeHash,
            afterHash = afterHash,
            done = submittedOk,
            reason = "搜索提交",
        ))
        return submittedOk
    }

    private suspend fun runTaobaoSearchEntrySubgoal(
        acc: LingxiAccessibilityService,
        subgoal: String,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
    ): Boolean {
        if (clearInterstitials(acc) > 0) progress(acc, "跳过了广告/弹窗，继续搜索")
        val beforeScreen = acc.readScreen()
        val beforeHash = beforeScreen.take(1400).hashCode()
        progress(acc, "淘宝搜索：点左侧搜索框")
        val clicked = tapTaobaoSearchField(acc)
        val afterScreen = if (clicked) waitForScreenSettled(acc, budgetMs = 2500) else beforeScreen
        val changed = afterScreen != beforeScreen
        history.add(if (clicked && changed) "taobao_search_entry" else "taobao_search_entry_failed")
        trace.add(AgentTraceItem(
            subgoal = subgoal,
            thought = "淘宝首页顶部左侧搜索框才是输入入口，右侧搜索按钮会搜索推荐词",
            action = "deterministic:taobao_search_entry",
            target = "搜索栏",
            beforeHash = beforeHash,
            afterHash = afterScreen.take(1400).hashCode(),
            done = clicked && changed,
            reason = if (clicked) "点击淘宝左侧搜索框" else "未找到淘宝搜索框",
        ))
        return clicked && changed
    }

    private suspend fun runTaobaoSearchSubgoal(
        acc: LingxiAccessibilityService,
        goal: String,
        subgoal: String,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
    ): Boolean {
        val query = extractSearchQuery(goal, subgoal)
        if (query.isNullOrBlank()) return false
        // 🐛 2026-07-05 用户真机揪出：淘宝记忆计划里"点击搜索栏前的热搜词进入搜索输入页"这条前导步
        //    含"输入"+"搜"被判成搜索，和真正的搜索步都跑 runTaobaoSearchSubgoal → 同一关键词搜两次
        //    （"任务栏又出现一次搜索我的世界"）。幂等：已在【本次 run 里】搜过这个关键词就跳过，不重复输入+提交。
        // 🐛 2026-07-05 同日修复回归（用户真机再次揪出）：第一版幂等检查改成直接读【当前屏幕】判断
        //    "是不是已经在结果页"——但京东首页恰好搜索框留有上次浏览的关键词提示("我的世界矿车")+
        //    首页大量促销卡片自带 ¥ 价格，同样满足 isJdSearchResultScreen 的宽松文本判据，导致还没真正
        //    输入/提交就被误判"已经在结果页"直接跳过搜索，下一步就点了首页促销卡片当成"第一个商品"。
        //    改法：不再看任意时刻的屏幕内容，只认【本次 run 的 history】——只有当"这个 run 自己之前
        //    已经真的执行过这个关键词的搜索"才幂等跳过，杜绝被无关的历史/首页状态误伤。
        if (history.any { it == "taobao_search:$query" || it == "taobao_search_already:$query" }) {
            history.add("taobao_search_already:$query")
            progress(acc, "本次已经搜过「$query」，跳过重复搜索")
            return true
        }
        if (clearInterstitials(acc) > 0) progress(acc, "跳过了广告/弹窗，继续搜索")

        val beforeHash = acc.readScreen().take(1400).hashCode()
        progress(acc, "淘宝搜索：输入「$query」并提交")
        if (!acc.setFocusedText(query)) {
            tapTaobaoSearchField(acc)
            if (!waitAndSetFocusedText(acc, query)) {
                history.add("taobao_search_focus_failed")
                trace.add(AgentTraceItem(
                    subgoal = subgoal,
                    action = "deterministic:taobao_search",
                    target = query,
                    beforeHash = beforeHash,
                    afterHash = acc.readScreen().take(1400).hashCode(),
                    done = false,
                    reason = "未聚焦输入框",
                ))
                return false
            }
        }

        delay(500)
        val submitted = tapTaobaoTopRightSearchButton(acc)
        if (!submitted) {
            progress(acc, "淘宝搜索：没找到顶部搜索按钮，先停下避免搜推荐词")
            history.add("taobao_search_submit_failed")
            trace.add(AgentTraceItem(
                subgoal = subgoal,
                action = "deterministic:taobao_search",
                target = query,
                beforeHash = beforeHash,
                afterHash = acc.readScreen().take(1400).hashCode(),
                done = false,
                reason = "未找到顶部搜索按钮",
            ))
            return false
        }

        val afterScreen = waitForTaobaoSearchResultsReady(acc, query)
        val submittedOk = isTaobaoSearchResultScreen(afterScreen, query)
        history.add("taobao_search:$query")
        trace.add(AgentTraceItem(
            subgoal = subgoal,
            thought = "淘宝搜索使用确定性路径：先输入关键词，再点顶部右侧搜索",
            action = "deterministic:taobao_search",
            target = query,
            beforeHash = beforeHash,
            afterHash = afterScreen.hashCode(),
            done = submittedOk,
            reason = "搜索提交",
        ))
        return submittedOk
    }

    private suspend fun runShoppingOpenFirstResultSubgoal(
        acc: LingxiAccessibilityService,
        subgoal: String,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
        appLabel: String,
    ): Boolean {
        val beforeScreen = acc.readScreen().take(1800)
        val beforeHash = beforeScreen.hashCode()
        val itemLabel = if ("视频" in subgoal) "第一个结果" else "第一个商品"
        progress(acc, "${appLabel}结果页：点击$itemLabel")

        val dm = acc.resources.displayMetrics
        val candidates = shoppingFirstResultCandidates(acc, appLabel)
        val allClickable = acc.clickableElementsForVisual()
        val taps = buildShoppingFirstResultTapPoints(candidates, allClickable, dm.widthPixels, dm.heightPixels)
        var chosenReason = "未找到候选"
        var finalScreen = beforeScreen
        var changed = false

        for (tap in taps) {
            chosenReason = tap.reason
            acc.tap(tap.x, tap.y)
            val afterScreen = waitForScreenSettled(acc, budgetMs = 4500, pollMs = 700)
            if (afterScreen != beforeScreen) {
                finalScreen = afterScreen
                changed = true
                break
            }
        }

        history.add(if (changed) "${appLabel.lowercase()}_open_first_result" else "${appLabel.lowercase()}_open_first_result_failed")
        trace.add(AgentTraceItem(
            subgoal = subgoal,
            thought = "$appLabel 搜索结果页只做单任务的'点第一个'：优先点首条商品卡的标题/价格区域，不复用比价那条停在结果页的路径",
            action = "deterministic:${appLabel.lowercase()}_open_first_result",
            target = itemLabel,
            beforeHash = beforeHash,
            afterHash = finalScreen.hashCode(),
            done = changed,
            reason = chosenReason,
        ))
        if (!changed) {
            progress(acc, "${appLabel}结果页：点了首条候选还是没进详情，先停下避免乱点")
        }
        return changed
    }

    private suspend fun runJdSearchSubgoal(
        acc: LingxiAccessibilityService,
        goal: String,
        subgoal: String,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
    ): Boolean {
        val query = extractSearchQuery(goal, subgoal)
        if (query.isNullOrBlank()) return false
        // 幂等（同淘宝，同一处 2026-07-05 回归修复）：只认【本次 run 的 history】里是否已经真的
        // 搜过这个关键词，不再看当前屏幕内容——京东首页搜索框会留上次浏览关键词的提示文字
        // （如"我的世界矿车"）+ 首页促销卡片自带 ¥ 价格，会让基于屏幕文本的判据在首页就误判"已在结果页"，
        // 于是没真正输入/提交就被跳过，下一步误点首页促销卡当"第一个商品"。
        if (history.any { it == "jd_search:$query" || it == "jd_search_already:$query" }) {
            history.add("jd_search_already:$query")
            progress(acc, "本次已经搜过「$query」，跳过重复搜索")
            return true
        }
        if (clearInterstitials(acc) > 0) progress(acc, "跳过了广告/弹窗，继续搜索")

        val beforeHash = acc.readScreen().take(1400).hashCode()
        progress(acc, "京东搜索：点顶部热词搜索框，输入「$query」")
        if (!focusAndSetJdSearchText(acc, query)) {
            history.add("jd_search_focus_failed")
            trace.add(AgentTraceItem(
                subgoal = subgoal,
                action = "deterministic:jd_search",
                target = query,
                beforeHash = beforeHash,
                afterHash = acc.readScreen().take(1400).hashCode(),
                done = false,
                reason = "未聚焦京东搜索框",
            ))
            return false
        }

        delay(500)
        val submitted = tapJdTopRightSearchButton(acc)
        if (!submitted) {
            progress(acc, "京东搜索：没找到顶部右侧搜索按钮，先停下避免点到浮窗取消")
            history.add("jd_search_submit_failed")
            trace.add(AgentTraceItem(
                subgoal = subgoal,
                action = "deterministic:jd_search",
                target = query,
                beforeHash = beforeHash,
                afterHash = acc.readScreen().take(1400).hashCode(),
                done = false,
                reason = "未找到顶部搜索按钮",
            ))
            return false
        }

        val afterScreen = waitForJdSearchResultsReady(acc, query)
        val submittedOk = isJdSearchResultScreen(afterScreen, query)
        history.add("jd_search:$query")
        trace.add(AgentTraceItem(
            subgoal = subgoal,
            thought = "京东搜索使用确定性路径：先点顶部热词搜索框，再点京东页面右上搜索；不碰浮窗取消按钮",
            action = "deterministic:jd_search",
            target = query,
            beforeHash = beforeHash,
            afterHash = afterScreen.hashCode(),
            done = submittedOk,
            reason = "搜索提交",
        ))
        return submittedOk
    }

    private fun isZhihuSearchResultScreen(screen: String, query: String): Boolean {
        if (screen.contains("综合") || screen.contains("内容") || screen.contains("用户")) return true
        if (screen.contains("回答") || screen.contains("赞同") || screen.contains("评论")) return true
        val suggestionOnly = screen.contains("${query}好的地方") ||
            screen.contains("${query}图片") ||
            screen.contains("${query}作文")
        return screen.contains(query) && !suggestionOnly && !screen.contains("AI 搜索")
    }

    private suspend fun waitForZhihuSearchResultsReady(
        acc: LingxiAccessibilityService,
        query: String,
        timeoutMs: Long = 8000,
    ): String {
        progress(acc, "知乎搜索：等待结果内容加载")
        val deadline = System.currentTimeMillis() + timeoutMs
        var last = ""
        var stableHits = 0
        while (System.currentTimeMillis() < deadline) {
            delay(700)
            val screen = acc.readScreen().take(1800)
            val isResultPage = isZhihuSearchResultScreen(screen, query)
            val hasPostMeta = hasZhihuPostMeta(screen)
            val hasDirectAnswer = screen.contains("知乎直答") || screen.contains("展开更多")
            if (isResultPage && hasPostMeta) return screen
            if (isResultPage && hasDirectAnswer) {
                stableHits = if (screen == last) stableHits + 1 else 0
                if (stableHits >= 2) return screen
            }
            last = screen
        }
        return waitForScreenSettled(acc, budgetMs = 2000)
    }

    private fun hasZhihuPostMeta(text: String): Boolean {
        val number = """\d+(?:\.\d+)?\s*万?\s*"""
        val hasVote = Regex("${number}赞同").containsMatchIn(text)
        val hasComment = Regex("${number}评论").containsMatchIn(text)
        val hasView = Regex("${number}浏览").containsMatchIn(text)
        val hasAnswer = Regex("${number}回答").containsMatchIn(text)
        val hasFollow = Regex("${number}关注").containsMatchIn(text)
        val hasDate = Regex("(\\d{4}-\\d{1,2}-\\d{1,2}|\\d{1,2}-\\d{1,2})").containsMatchIn(text)
        val hasRelativeTime = Regex("(刚刚|昨天|前天|\\d+\\s*(秒|分钟|小时|天|周|个月|年)前)").containsMatchIn(text)
        return ((hasVote || hasComment) && (hasDate || hasRelativeTime || hasComment)) ||
            (hasView && (hasAnswer || hasFollow))
    }

    private fun isTaobaoSearchResultScreen(screen: String, query: String): Boolean {
        if (!screen.contains(query)) return false
        return listOf("综合", "销量", "店铺", "筛选", "宝贝", "商品", "发货", "包邮", "¥")
            .any { screen.contains(it) }
    }

    private fun isJdSearchResultScreen(screen: String, query: String): Boolean {
        if (!screen.contains(query)) return false
        return listOf("综合", "销量", "筛选", "京东物流", "自营", "评价", "店铺", "¥", "￥")
            .any { screen.contains(it) }
    }

    private suspend fun waitForTaobaoSearchResultsReady(
        acc: LingxiAccessibilityService,
        query: String,
        timeoutMs: Long = 8000,
    ): String {
        progress(acc, "淘宝搜索：等待结果内容加载")
        val deadline = System.currentTimeMillis() + timeoutMs
        var last = ""
        var stableHits = 0
        while (System.currentTimeMillis() < deadline) {
            delay(700)
            val screen = acc.readScreen().take(1800)
            if (isTaobaoSearchResultScreen(screen, query)) return screen
            stableHits = if (screen == last) stableHits + 1 else 0
            if (stableHits >= 2 && screen.contains(query)) return screen
            last = screen
        }
        return waitForScreenSettled(acc, budgetMs = 2000)
    }

    private suspend fun waitForJdSearchResultsReady(
        acc: LingxiAccessibilityService,
        query: String,
        timeoutMs: Long = 12000,
    ): String {
        progress(acc, "京东搜索：等待商品结果加载")
        val deadline = System.currentTimeMillis() + timeoutMs
        var last = ""
        var stableHits = 0
        while (System.currentTimeMillis() < deadline) {
            delay(800)
            val screen = acc.readScreen().take(1800)
            if (isJdSearchResultScreen(screen, query)) return screen
            stableHits = if (screen == last) stableHits + 1 else 0
            if (stableHits >= 2 && screen.contains(query)) return screen
            last = screen
        }
        return waitForScreenSettled(acc, budgetMs = 2500)
    }

    private suspend fun waitAndSetFocusedText(
        acc: LingxiAccessibilityService,
        text: String,
        timeoutMs: Long = 2600,
    ): Boolean {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            delay(250)
            if (acc.setFocusedText(text)) return true
        }
        return false
    }

    private suspend fun focusAndSetJdSearchText(
        acc: LingxiAccessibilityService,
        query: String,
    ): Boolean {
        if (acc.setFocusedText(query)) return true
        val dm = acc.resources.displayMetrics
        val used = HashSet<String>()
        val points = ArrayList<TapPoint>()
        val field = acc.elementsForGaze()
            .filter {
                val cy = (it.bounds.top + it.bounds.bottom) / 2f
                val cx = (it.bounds.left + it.bounds.right) / 2f
                val text = it.text.trim()
                cy < dm.heightPixels * 0.22f &&
                    cx < dm.widthPixels * 0.78f &&
                    (text.contains("搜索") || text.contains("search", ignoreCase = true) ||
                        text.contains("oop", ignoreCase = true) || text.contains("热") ||
                        (it.bounds.right - it.bounds.left) > dm.widthPixels * 0.42f)
            }
            .sortedBy { it.bounds.left }
            .firstOrNull()
        if (field != null) {
            val x = (field.bounds.left + field.bounds.right) / 2f
            val y = (field.bounds.top + field.bounds.bottom) / 2f
            points += TapPoint(x, y, "a11y 搜索框中心")
        }
        points += listOf(
            TapPoint(dm.widthPixels * 0.46f, dm.heightPixels * 0.11f, "顶部搜索框中间"),
            TapPoint(dm.widthPixels * 0.32f, dm.heightPixels * 0.11f, "顶部搜索框左半区"),
            TapPoint(dm.widthPixels * 0.60f, dm.heightPixels * 0.11f, "顶部搜索框右半区"),
            TapPoint(dm.widthPixels * 0.46f, dm.heightPixels * 0.095f, "顶部搜索框偏上"),
        )
        for (tap in points) {
            val key = "${tap.x.toInt()}_${tap.y.toInt()}"
            if (!used.add(key)) continue
            acc.tap(tap.x, tap.y)
            if (waitAndSetFocusedText(acc, query, timeoutMs = 1500)) return true
        }
        return false
    }

    private fun extractSearchQuery(goal: String, subgoal: String): String? {
        val quoted = Regex("[\"'“”‘’「」]([^\"'“”‘’「」]{1,60})[\"'“”‘’「」]")
        for (source in listOf(subgoal, goal)) {
            quoted.find(source)?.groupValues?.getOrNull(1)?.trim()?.takeIf { it.isNotBlank() }?.let { return it }
        }
        for (source in listOf(goal, subgoal)) {
            val compact = source.replace(Regex("\\s+"), "")
            val m = Regex("(?:搜索|搜一下|搜)(.+?)(?:[，,。.]?(?:并且|并|然后|再|接着|之后|打开|点开|点击|第一个|第一条|回答|回复)|$)").find(compact)
            val raw = m?.groupValues?.getOrNull(1)?.trim('，', '。', ',', '.', ' ', '「', '」') ?: ""
            val q = raw
                .replace(Regex("^(在?知乎上?|知乎)"), "")
                .replace(Regex("^(顶部左侧)?搜索(框|栏)?输入"), "")
                .replace(Regex("^输入"), "")
                .replace(Regex("[，,。.]?(然后|并且|并|再|接着|之后).*$"), "")
                .trim()
            if (q.isNotBlank()) return q
        }
        return null
    }

    private fun tapZhihuTopLeftSearchField(acc: LingxiAccessibilityService) {
        if (acc.tapByViewId("com.zhihu.android:id/query_container")) return
        if (acc.tapByViewId("com.zhihu.android:id/text_switcher")) return
        val dm = acc.resources.displayMetrics
        val searchButton = acc.elementsForGaze()
            .filter {
                it.text.trim() == "搜索" &&
                    (it.bounds.top + it.bounds.bottom) / 2f < dm.heightPixels * 0.22f
            }
            .maxByOrNull { (it.bounds.left + it.bounds.right) / 2f }
        val y = searchButton?.let { (it.bounds.top + it.bounds.bottom) / 2f } ?: (dm.heightPixels * 0.06f)
        val rightEdge = searchButton?.bounds?.left?.toFloat() ?: (dm.widthPixels * 0.72f)
        val x = ((dm.widthPixels * 0.08f) + rightEdge) / 2f
        acc.tap(x.coerceIn(dm.widthPixels * 0.18f, dm.widthPixels * 0.48f), y.coerceAtMost(dm.heightPixels * 0.07f))
    }

    private fun tapZhihuTopRightSearchButton(acc: LingxiAccessibilityService): Boolean {
        if (acc.clickByViewId("com.zhihu.android:id/search_button")) return true
        if (acc.clickByViewId("com.zhihu.android:id/search_text")) return true
        return false
    }

    private fun tapTaobaoSearchField(acc: LingxiAccessibilityService): Boolean {
        val dm = acc.resources.displayMetrics
        val field = acc.elementsForGaze()
            .filter {
                it.text.trim() == "搜索栏" &&
                    (it.bounds.top + it.bounds.bottom) / 2f < dm.heightPixels * 0.22f
            }
            .minByOrNull { (it.bounds.left + it.bounds.right) / 2f }
        if (field != null) {
            val x = (field.bounds.left + field.bounds.right) / 2f
            val y = (field.bounds.top + field.bounds.bottom) / 2f
            acc.tap(x, y)
            return true
        }
        acc.tap(dm.widthPixels * 0.44f, dm.heightPixels * 0.12f)
        return true
    }

    private fun tapTaobaoTopRightSearchButton(acc: LingxiAccessibilityService): Boolean {
        val dm = acc.resources.displayMetrics
        val button = acc.elementsForGaze()
            .filter {
                it.text.trim() == "搜索" &&
                    (it.bounds.left + it.bounds.right) / 2f > dm.widthPixels * 0.62f &&
                    (it.bounds.top + it.bounds.bottom) / 2f < dm.heightPixels * 0.25f
            }
            .maxByOrNull { (it.bounds.left + it.bounds.right) / 2f }
            ?: return false
        val x = (button.bounds.left + button.bounds.right) / 2f
        val y = (button.bounds.top + button.bounds.bottom) / 2f
        acc.tap(x, y)
        return true
    }

    private fun tapJdSearchField(acc: LingxiAccessibilityService): Boolean {
        val dm = acc.resources.displayMetrics
        val field = acc.elementsForGaze()
            .filter {
                val cy = (it.bounds.top + it.bounds.bottom) / 2f
                val cx = (it.bounds.left + it.bounds.right) / 2f
                val text = it.text.trim()
                cy < dm.heightPixels * 0.20f &&
                    cx < dm.widthPixels * 0.78f &&
                    (text.contains("搜索") || text.contains("search", ignoreCase = true) ||
                        text.contains("oop") || text.contains("热搜") ||
                        (it.bounds.right - it.bounds.left) > dm.widthPixels * 0.42f)
            }
            .minByOrNull { (it.bounds.left + it.bounds.right) / 2f }
        if (field != null) {
            val x = (field.bounds.left + field.bounds.right) / 2f
            val y = (field.bounds.top + field.bounds.bottom) / 2f
            acc.tap(x, y)
            return true
        }
        acc.tap(dm.widthPixels * 0.43f, dm.heightPixels * 0.085f)
        return true
    }

    private fun tapJdTopRightSearchButton(acc: LingxiAccessibilityService): Boolean {
        val dm = acc.resources.displayMetrics
        val button = acc.elementsForGaze()
            .filter {
                it.text.trim() == "搜索" &&
                    (it.bounds.left + it.bounds.right) / 2f > dm.widthPixels * 0.68f &&
                    (it.bounds.top + it.bounds.bottom) / 2f < dm.heightPixels * 0.18f
            }
            .maxByOrNull { (it.bounds.left + it.bounds.right) / 2f }
        if (button != null) {
            val x = (button.bounds.left + button.bounds.right) / 2f
            val y = (button.bounds.top + button.bounds.bottom) / 2f
            acc.tap(x, y)
            return true
        }
        // 京东搜索按钮在页面顶部右侧，限定 y 很靠上，避开灵犀浮窗里的取消按钮。
        acc.tap(dm.widthPixels * 0.88f, dm.heightPixels * 0.085f)
        return true
    }

    private fun shoppingFirstResultCandidates(
        acc: LingxiAccessibilityService,
        appLabel: String,
    ): List<ScreenElement> {
        val dm = acc.resources.displayMetrics
        val screenW = dm.widthPixels
        val screenH = dm.heightPixels
        val skipWords = listOf(
            "综合", "销量", "全部", "天猫", "店铺", "闪购", "筛选", "品牌", "连接方式", "佩戴方式",
            "续航时间", "功能", "推荐", "首页", "购物车", "我的", "消息", "搜索", "AI助手"
        )
        // 🐛 2026-07-05 用户真机截图揪出：京东在结果页顶部塞一条"¥300 无门槛…倒计时…仅可购买…筛选商品"
        //    优惠券横幅，正好落在候选波段最上方，被"最靠上"排序当成"第一个商品"点进去（点到券条不是商品）。
        //    它文案很长、逃过 skipWords 的 length<=10 限制。这里按券条专有词过滤，不受长度限制（这些词绝不会
        //    出现在正常商品卡上；"到手价"是真商品词，特意不列入）。
        val promoWords = listOf(
            "无门槛", "优惠券", "领券", "立减", "满减", "补贴", "凑单", "倒计时", "仅可购买"
        )
        val seen = HashSet<String>()
        return acc.clickableElementsForVisual()
            .asSequence()
            .filter { it.clickable }
            .filter {
                val b = it.bounds
                val bw = b.right - b.left
                val bh = b.bottom - b.top
                val cx = (b.left + b.right) / 2f
                val cy = (b.top + b.bottom) / 2f
                if (bw <= 0 || bh <= 0) return@filter false
                if (cy < screenH * 0.24f || cy > screenH * 0.84f) return@filter false
                if (bw * bh < 7_000) return@filter false
                if (bw > screenW * 0.96f && bh > screenH * 0.24f) return@filter false
                if (bw > screenW * 0.86f && bh > screenH * 0.34f) return@filter false
                val text = it.text.trim()
                if (text.isNotBlank() && skipWords.any { w -> text == w || text.contains(w) && text.length <= 10 }) return@filter false
                // 促销/优惠券横幅（不论长短一律排除，治点到京东顶部券条当成第一个商品）
                if (text.isNotBlank() && promoWords.any { w -> text.contains(w) }) return@filter false
                // 又宽又扁的横幅（券条/活动条）不是商品卡：商品行/卡都有一定高度，券条只有一行
                if (bw > screenW * 0.70f && bh < screenH * 0.07f) return@filter false
                if (cx > screenW * 0.94f && bw < screenW * 0.18f) return@filter false
                true
            }
            .filter {
                val b = it.bounds
                seen.add("${b.left / 20}_${b.top / 20}_${b.right / 20}_${b.bottom / 20}")
            }
            .sortedWith(
                compareBy<ScreenElement> { it.bounds.top }
                    .thenBy { it.bounds.left }
                    .thenByDescending { shoppingCandidateScore(it, appLabel, screenW) }
            )
            .take(8)
            .toList()
    }

    private fun shoppingCandidateScore(
        e: ScreenElement,
        appLabel: String,
        screenW: Int,
    ): Int {
        val b = e.bounds
        val bw = b.right - b.left
        val bh = b.bottom - b.top
        val text = e.text.trim()
        var score = 0
        if (bw > screenW * 0.72f && bh > 180) score += 4
        if (bw in (screenW * 0.28f).toInt()..(screenW * 0.58f).toInt() && bh > 240) score += 4
        if (text.contains("¥") || text.contains("￥")) score += 3
        if (text.contains("付款") || text.contains("评价") || text.contains("包邮") || text.contains("自营")) score += 2
        if (appLabel == "淘宝" && text.contains("天猫")) score += 1
        if (appLabel == "京东" && text.contains("京东")) score += 1
        return score
    }

    private fun buildShoppingFirstResultTapPoints(
        candidates: List<ScreenElement>,
        allClickable: List<ScreenElement>,
        screenW: Int,
        screenH: Int,
    ): List<TapPoint> {
        val points = ArrayList<TapPoint>()
        val seen = HashSet<String>()
        val listLayout = candidates.any {
            val bw = it.bounds.right - it.bounds.left
            val bh = it.bounds.bottom - it.bounds.top
            bw > screenW * 0.72f && bh > 180
        }

        // 🐛 2026-07-05 用户真机揪出（京东切到双列大图模式后复现）：商品卡内部常嵌套自己的
        // 可点子元素（如"游戏软件榜第7名"这类排行榜链接），猜的百分比落点如果恰好砸在这类
        // 子元素上，点击会被它截胡跳到不相关页面——而"点击后屏幕变了"这个宽松判据会把这次
        // 误跳也当成"已进详情页"成功（真机实测：整个 run 报"✅完成"，实际停在排行榜页）。
        // 修法：候选点位如果落在【别的、比该商品卡小的、带文字的可点子元素】范围内就跳过，
        // 把"点卡片本体"和"点卡片内某个具体链接"分开，不让后者冒充前者。
        fun hitsNestedLink(x: Float, y: Float, card: ScreenElement): Boolean {
            val cardArea = (card.bounds.right - card.bounds.left).toLong() *
                (card.bounds.bottom - card.bounds.top)
            return allClickable.any { other ->
                if (other.bounds == card.bounds || other.text.isBlank()) return@any false
                val ob = other.bounds
                val otherArea = (ob.right - ob.left).toLong() * (ob.bottom - ob.top)
                otherArea < cardArea && x >= ob.left && x <= ob.right && y >= ob.top && y <= ob.bottom
            }
        }

        for (c in candidates) {
            val b = c.bounds
            val bw = (b.right - b.left).toFloat()
            val bh = (b.bottom - b.top).toFloat()
            val candidatePoints = if (bw > screenW * 0.72f) {
                listOf(
                    TapPoint(b.left + bw * 0.68f, b.top + bh * 0.30f, "首条列表文字区"),
                    TapPoint(b.left + bw * 0.60f, b.top + bh * 0.52f, "首条列表中部"),
                )
            } else {
                listOf(
                    TapPoint(b.left + bw * 0.36f, b.top + bh * 0.74f, "左上第一张商品卡文字区"),
                    TapPoint(b.left + bw * 0.34f, b.top + bh * 0.52f, "左上第一张商品卡中部"),
                )
            }
            for (tap in candidatePoints) {
                if (hitsNestedLink(tap.x, tap.y, c)) continue
                val key = "${tap.x.toInt()}_${tap.y.toInt()}"
                if (seen.add(key)) points += tap
            }
        }

        val fallbacks = if (listLayout) {
            listOf(
                TapPoint(screenW * 0.66f, screenH * 0.38f, "列表版兜底：首条商品标题区"),
                TapPoint(screenW * 0.56f, screenH * 0.48f, "列表版兜底：首条商品中部"),
            )
        } else {
            listOf(
                TapPoint(screenW * 0.28f, screenH * 0.45f, "双列版兜底：左上第一张商品"),
                TapPoint(screenW * 0.28f, screenH * 0.60f, "双列版兜底：左上第一张商品文字区"),
            )
        }
        for (tap in fallbacks) {
            val key = "${tap.x.toInt()}_${tap.y.toInt()}"
            if (seen.add(key)) points += tap
        }
        return points
    }

    /** 高风险动作才做反思：页面跳转/卡片选择/直播视频类任务，避免每个普通按钮都多调 VLM。 */
    private fun shouldReflectAfterAction(goal: String, subgoal: String, action: String): Boolean {
        if (action != "tap" && !action.startsWith("vision")) return false
        val text = "$goal $subgoal"
        val risky = listOf("直播", "视频", "播放", "卡片", "栏目", "频道", "分区", "第一个", "最新", "歌曲", "歌单")
            .any { text.contains(it) }
        return risky
    }

    /** 动作后验收。失败默认 UNSURE；UNSURE 不推进，避免把不确定当成功。 */
    private suspend fun reflectAfterAction(
        acc: LingxiAccessibilityService,
        goal: String,
        subgoal: String,
        action: String,
        target: String,
        beforeScreen: String,
        afterScreen: String,
    ): AgentReflectResponse {
        if (beforeScreen == afterScreen) {
            return AgentReflectResponse(status = "NO_CHANGE", thought = "动作后屏幕未变化", reason = "无变化")
        }
        if (CloudVisionPolicy.classifySensitivity(afterScreen, currentApp) == "sensitive") {
            progress(acc, "⚠ 检测到敏感页面，停止操作且不上传截图或读屏内容")
            return AgentReflectResponse(
                status = "UNSURE",
                thought = "敏感页本地硬停止",
                reason = "SENSITIVE_SCREEN_BLOCKED_LOCAL",
                model = "local-privacy-gate",
            )
        }
        progress(acc, "🔎 验收刚才这一步")
        var screenshot = ""
        var cloudEvidence: com.lingxi.mobile.vision.CloudVisionEvidence? = null
        if (ScreenCaptureService.isReady) {
            val privacy = CloudVisionPolicy.decide(
                consentEnabled = CloudVisionConsentStore.isEnabled(acc),
                screenText = afterScreen,
                purpose = CloudVisionPolicy.PURPOSE_ACTION_REFLECT,
                appName = currentApp,
            )
            if (privacy.allowed) {
                ScreenCaptureService.captureOnce()?.let { png ->
                    screenshot = Base64.encodeToString(png, Base64.NO_WRAP)
                    cloudEvidence = CloudVisionPolicy.evidence(
                        png,
                        CloudVisionPolicy.PURPOSE_ACTION_REFLECT,
                    )
                }
            } else {
                Log.i(TAG, "反思截图未上传: ${privacy.code}；降级读屏文本")
            }
        }
        val r = brain.agentReflect(
            goal = goal,
            subgoal = subgoal,
            action = action,
            target = target,
            beforeScreen = beforeScreen,
            afterScreen = afterScreen,
            screenshotB64 = screenshot,
            app = currentApp,
            evidence = cloudEvidence,
        )
        if (r.thought.isNotBlank()) progress(acc, "🔎 ${r.thought}")
        Log.i(TAG, "动作后反思: ${r.status} (${r.reason})")
        return r
    }

    /** P4-B 错误恢复：把 P4-A 的反思结果变成有上限的恢复动作，而不是继续在脏页面里硬跑。 */
    private suspend fun recoverAfterReflection(
        acc: LingxiAccessibilityService,
        subgoal: String,
        reflect: AgentReflectResponse,
        recovery: RecoveryState,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
    ): RecoveryDecision {
        recovery.total += 1
        val status = reflect.status.uppercase()
        val reason = reflect.reason.ifBlank { status }
        if (reason.startsWith("SENSITIVE_SCREEN_BLOCKED")) {
            progress(acc, "⚠ 敏感页面已停止操作，不再恢复或重试")
            recordRecoveryTrace(acc, subgoal, "recover:privacy-stop", reason, trace)
            history.add("recover_privacy_stop")
            return RecoveryDecision.STOP
        }
        if (recovery.total > MAX_RECOVERY_EVENTS_PER_SUBGOAL) {
            progress(acc, "⚠ 多次纠错仍没恢复，先停下")
            recordRecoveryTrace(acc, subgoal, "recover:stop", reason, trace)
            history.add("recover_stop:$status")
            return RecoveryDecision.STOP
        }

        return when (status) {
            "WRONG_PAGE" -> {
                recovery.wrongPage += 1
                if (recovery.wrongPage == 1) {
                    progress(acc, "⚠ 走错页：$reason，返回上一层")
                    history.add("recover_back_wrong_page")
                    acc.pressBack()
                    delay(1400)
                    recordRecoveryTrace(acc, subgoal, "recover:back", reason, trace)
                    RecoveryDecision.RETRY
                } else if (!recovery.reopened && reopenCurrentApp(acc, subgoal, reason, history, trace)) {
                    recovery.reopened = true
                    RecoveryDecision.RETRY
                } else {
                    progress(acc, "⚠ 错页恢复失败，先停下")
                    history.add("recover_failed_wrong_page")
                    recordRecoveryTrace(acc, subgoal, "recover:stop", reason, trace)
                    RecoveryDecision.STOP
                }
            }

            "NO_CHANGE" -> {
                recovery.noChange += 1
                when (recovery.noChange) {
                    1 -> {
                        progress(acc, "⚠ 点击没反应，等一下再判断")
                        history.add("recover_wait_no_change")
                        delay(1300)
                        recordRecoveryTrace(acc, subgoal, "recover:wait", reason, trace)
                        RecoveryDecision.RETRY
                    }
                    2 -> {
                        progress(acc, "⚠ 当前区域没反应，下滑换一屏")
                        history.add("recover_scroll_no_change")
                        acc.scrollDown()
                        delay(900)
                        recordRecoveryTrace(acc, subgoal, "recover:scroll", reason, trace)
                        RecoveryDecision.RETRY
                    }
                    else -> {
                        if (!recovery.reopened && reopenCurrentApp(acc, subgoal, reason, history, trace)) {
                            recovery.reopened = true
                            RecoveryDecision.RETRY
                        } else {
                            progress(acc, "⚠ 连续无反应，先停下")
                            history.add("recover_failed_no_change")
                            recordRecoveryTrace(acc, subgoal, "recover:stop", reason, trace)
                            RecoveryDecision.STOP
                        }
                    }
                }
            }

            else -> {
                recovery.unsure += 1
                if (recovery.unsure <= 2) {
                    progress(acc, "反思不确定，重新观察当前页")
                    history.add("recover_observe_unsure")
                    delay(900)
                    recordRecoveryTrace(acc, subgoal, "recover:observe", reason, trace)
                    RecoveryDecision.RETRY
                } else {
                    progress(acc, "⚠ 多次无法确认页面状态，先停下")
                    history.add("recover_failed_unsure")
                    recordRecoveryTrace(acc, subgoal, "recover:stop", reason, trace)
                    RecoveryDecision.STOP
                }
            }
        }
    }

    private suspend fun reopenCurrentApp(
        acc: LingxiAccessibilityService,
        subgoal: String,
        reason: String,
        history: MutableList<String>,
        trace: MutableList<AgentTraceItem>,
    ): Boolean {
        val appName = currentApp.takeIf { it.isNotBlank() } ?: return false
        progress(acc, "重新打开 $appName，回到稳定入口")
        val ok = openAppNamed(acc, appName) != null
        history.add(if (ok) "recover_reopen_app" else "recover_reopen_failed")
        if (!ok) return false
        waitForAppSettled(acc)
        recordRecoveryTrace(acc, subgoal, "recover:reopen_app", reason, trace)
        return true
    }

    private fun recordRecoveryTrace(
        acc: LingxiAccessibilityService,
        subgoal: String,
        action: String,
        reason: String,
        trace: MutableList<AgentTraceItem>,
    ) {
        val h = acc.readScreen().take(1400).hashCode()
        trace.add(AgentTraceItem(
            subgoal = subgoal,
            action = action,
            beforeHash = h,
            afterHash = h,
            reason = reason,
        ))
    }

    /** 这步是否"挑一个视觉项"——需靠看缩略图/外观分辨该点哪个(直播间/视频/歌曲卡片)，文本靠不住→视觉优先。
     *  纯导航步(进栏目/标签)无视觉名词→走文本；确认/验证步→文本判状态(是否已在房间/在播放)，不挑卡片。 */
    /** 去掉子步骤文本里的备注性插入语"（别点成…那是…）"——这类避坑提示常顺带提到"歌/视频"等
     *  视觉名词纯粹是为了说"别点错成那个"，不代表这步本身要挑视觉项。2026-07-03 真机实测抓到：
     *  酷狗 recipe 步骤含"…那是哼唱找歌"，误触发视觉判定，把本可纯文本 tap 解决的一步硬憋停在
     *  "屏幕视觉没开"——只看主干、不看插入语，才是这条启发式该有的粒度。 */
    private fun stripAsides(text: String): String = text.replace(Regex("[（(][^）)]*[）)]"), "")

    private fun isVisualPickSubgoal(subgoal: String): Boolean {
        val trunk = stripAsides(subgoal)
        if (trunk.contains("确认")) return false
        val pickVerb = listOf("找", "选", "点", "进", "第一", "第二", "第三", "第", "打开", "播放")
            .any { trunk.contains(it) }
        val visualNoun = listOf("直播间", "直播房间", "房间", "视频", "歌曲", "歌单", "歌", "作品", "封面", "主播", "影片", "节目")
            .any { trunk.contains(it) }
        return pickVerb && visualNoun
    }

    /** 任务整体是否大概率需要视觉判别。用于启动前拦一下，避免明知没开屏幕视觉还打开 App 乱试。 */
    private fun isLikelyVisualGoal(goal: String): Boolean {
        val pickVerb = listOf("找", "选", "点", "进", "第一", "第二", "第三", "第", "最新", "打开", "播放", "看")
            .any { goal.contains(it) }
        val visualNoun = listOf("直播间", "直播房间", "房间", "视频", "歌曲", "歌单", "歌", "作品", "封面", "主播", "影片", "节目")
            .any { goal.contains(it) }
        return pickVerb && visualNoun
    }

    /** 是否"打开App/进主界面"废步骤——App 已被确定性打开，这种步骤留着会让 agent 抽风(进了又back回去)。 */
    private fun isOpenAppStep(step: String): Boolean {
        val openVerb = listOf("打开", "进入", "启动", "回到").any { step.contains(it) }
        val appNoun = listOf("App", "app", "应用", "主界面", "主页", "首页").any { step.contains(it) }
        return openVerb && appNoun
    }

    /** 打开 App 后等待启动页/开屏广告过去，避免第一步点到广告页或推荐页。 */
    /** 点击后等内容真正"稳定"下来再读——不是等固定时长，也不是只跟点击前比一次，而是【连续两次
     *  读到的文本一样】才算稳（内容不再刷新了）。网络型内容(歌单/搜索结果)加载中途常先变成过渡态
     *  (加载中/骨架屏)，单次哈希比较会把"变了"误判成"加载完了"；轮询到真正不再变化，或到预算上限
     *  为止(避免死等)。2026-07-03 真机反复实测暴露：这一步不稳，后面的动作后反思跟着一起错判。 */
    private suspend fun waitForScreenSettled(
        acc: LingxiAccessibilityService, budgetMs: Long = 3000, pollMs: Long = 500,
    ): String {
        delay(pollMs)
        var last = acc.readScreen().take(1400)
        val deadline = System.currentTimeMillis() + budgetMs
        while (System.currentTimeMillis() < deadline) {
            delay(pollMs)
            val cur = acc.readScreen().take(1400)
            if (cur == last) return cur
            last = cur
        }
        return last
    }

    private suspend fun waitForAppSettled(acc: LingxiAccessibilityService) {
        progress(acc, "等待 App 启动完成")
        delay(3500)
        // 8s 硬上限(3.5+4.5)对酷狗等开屏广告较长的 App 不够——真机实测(2026-07-03)广告没等完
        // 就开始决策，第一步直接对着广告页瞎判。放宽到 3.5+12=15.5s，仍有界不会无限等。
        val deadline = System.currentTimeMillis() + 12_000
        var stillAd = false
        while (System.currentTimeMillis() < deadline) {
            if (dismissInterstitialIfPresent(acc)) {
                progress(acc, "跳过一个广告/弹窗")
                delay(800)
                continue
            }
            val screen = acc.readScreen()
            val windowClass = acc.foregroundWindowClassName()
            val hasLaunchAdActivity = windowClass.contains("LaunchAd", ignoreCase = true) ||
                windowClass.contains("Splash", ignoreCase = true)
            val hasSplashAd = listOf("跳过", "广告", "赞助", "立即打开", "立即下载").any {
                screen.contains(it, ignoreCase = true)
            } || hasLaunchAdActivity
            stillAd = hasSplashAd
            if (!hasSplashAd) break
            if (hasLaunchAdActivity) {
                progress(acc, "检测到开屏广告页，等待/尝试跳过")
                val dm = acc.resources.displayMetrics
                acc.tap(dm.widthPixels * 0.88f, dm.heightPixels * 0.08f)
            } else {
                progress(acc, "检测到开屏广告，稍等")
            }
            delay(1000)
        }
        if (stillAd) progress(acc, "⚠ 开屏广告等待超时，可能仍在广告页")
        // 🐛 用户实测反馈(2026-07-03)：不少App(酷狗)在开屏广告之后、进主界面几秒才弹第二个插屏广告——
        // 上面那个循环已经 break 退出，必须再单独等一拍、专门抓这类"延迟插屏"，不能假设"没广告字样=安全了"。
        delay(1500)
        if (dismissInterstitialIfPresent(acc)) {
            progress(acc, "跳过一个延迟弹出的广告/弹窗")
            delay(1000)
            clearInterstitials(acc)
        }
    }

    /**
     * 路线C 视觉兜底（Set-of-Mark）：a11y 文本认不出点哪时，截屏 + 元素框 → 大脑画编号 → ≤3B 视觉
     * 模型选号 → 落地。返回已执行动作("tap/scroll/back/done")；视觉未授权/失败/无元素返回 ""（退回文本兜底）。
     * 坐标取自 a11y 原始 bounds，tap 落点零缩放误差。隐私：只截界面像素，生物特征不经此路。
     */
    private suspend fun tryVisualStep(
        acc: LingxiAccessibilityService, goal: String, history: List<String>,
    ): String {
        if (!ScreenCaptureService.isReady) return ""          // 用户没开屏幕视觉 → 不走视觉
        val screen = acc.readScreen().take(3000)
        if (paymentGuardHit(screen)) {
            progress(acc, "⚠ 检测到支付/密码页面，为安全立即停止操作")
            return ""
        }
        val privacy = CloudVisionPolicy.decide(
            consentEnabled = CloudVisionConsentStore.isEnabled(acc),
            screenText = screen,
            purpose = CloudVisionPolicy.PURPOSE_VISUAL_PICK,
            appName = currentApp,
        )
        if (!privacy.allowed) {
            progress(acc, "⚠ ${privacy.userMessage}")
            Log.i(TAG, "视觉截图未上传: ${privacy.code}")
            return ""
        }
        val png = ScreenCaptureService.captureOnce() ?: return ""
        val els = visualCandidates(acc, goal)
            .mapIndexed { i, e ->
                AgentVisualElement(
                    i + 1, e.text,
                    listOf(e.bounds.left, e.bounds.top, e.bounds.right, e.bounds.bottom)
                )
            }
        if (els.isEmpty()) return ""
        val b64 = Base64.encodeToString(png, Base64.NO_WRAP)
        val evidence = CloudVisionPolicy.evidence(png, CloudVisionPolicy.PURPOSE_VISUAL_PICK)
        val v = try {
            brain.agentStepVisual(goal, history, b64, els, currentApp, evidence)
        } catch (e: Exception) {
            Log.w(TAG, "视觉兜底失败: ${e.message}"); return ""
        }
        progress(acc, "👁 ${v.thought.ifBlank { v.reason.ifBlank { "看了眼屏幕" } }}")
        Log.i(TAG, "视觉兜底: ${v.action} mark=${v.mark} (${v.x},${v.y})")
        when (v.action) {
            "tap" -> { if (v.x == null || v.y == null) return ""; acc.tap(v.x.toFloat(), v.y.toFloat()) }
            "scroll" -> acc.scrollDown()
            "back" -> acc.pressBack()
            "error" -> { visualBackendDown = true; return "" }  // VLM 后端挂了 → 标记，调用方立即收手（不当完成、不空转重试）
            "blocked" -> {
                progress(acc, "⚠ 云端截图安全门已阻止本次上传（${v.reason}）")
                return ""
            }
            // done/未知：视觉没给出可执行动作 → 返回空当"没选准"，绝不当成"已完成"（治 VLM 失败被当 done 假成功）
            else -> return ""
        }
        return v.action
    }

    /**
     * P4② 视觉候选过滤：VLM 只在干净候选里做选择题。
     * 过滤整屏容器、顶部搜索框、底部导航、广告/推广、小图标；再优先给大卡片/列表项编号。
     */
    private fun visualCandidates(acc: LingxiAccessibilityService, goal: String): List<ScreenElement> {
        val dm = acc.resources.displayMetrics
        val w = dm.widthPixels
        val h = dm.heightPixels
        val seen = HashSet<String>()
        val raw = acc.clickableElementsForVisual()
            .filter { (it.clickable || it.text.isNotBlank()) && isUsefulVisualCandidate(it, w, h) }
        val pool = raw.filter { it.clickable }.ifEmpty { raw }
        val ranked = pool
            .filter {
                seen.add("${it.bounds.left / 24}_${it.bounds.top / 24}_${it.bounds.right / 24}_${it.bounds.bottom / 24}")
            }
            .sortedWith(
                compareByDescending<ScreenElement> { visualCandidateScore(it, goal, w, h) }
                    .thenBy { it.bounds.top }
                    .thenBy { it.bounds.left }
            )
            .take(16)
        Log.i(TAG, "视觉候选(${ranked.size}): " + ranked.mapIndexed { i, e ->
            val b = e.bounds
            "${i + 1}:${e.text.take(12)}@${b.left},${b.top},${b.right},${b.bottom}/s=${visualCandidateScore(e, goal, w, h)}"
        }.joinToString(" | "))
        return ranked
    }

    private fun isUsefulVisualCandidate(e: ScreenElement, screenW: Int, screenH: Int): Boolean {
        val b = e.bounds
        val bw = b.right - b.left
        val bh = b.bottom - b.top
        if (bw <= 0 || bh <= 0) return false
        if (b.area() >= screenW.toLong() * screenH.toLong() * 4 / 10) return false
        if (bw * bh < 1800) return false
        val cy = (b.top + b.bottom) / 2f
        if (cy < screenH * 0.08f || cy > screenH * 0.94f) return false

        val text = e.text.trim()
        val lower = text.lowercase()
        val navWords = listOf("首页", "我的", "消息", "动态", "会员", "推荐", "发现", "搜索", "历史", "下载")
        val adWords = listOf("广告", "推广", "赞助", "立即下载", "打开看看", "跳过")
        if (adWords.any { text.contains(it) }) return false

        val looksLikeTopSearch = cy < screenH * 0.20f &&
            (text.contains("搜索") || lower.contains("search") || (bw > screenW * 0.45f && bh < 140))
        if (looksLikeTopSearch) return false

        val looksLikeBottomNav = cy > screenH * 0.80f &&
            (navWords.any { text == it || text.contains(it) } || bh < 150)
        if (looksLikeBottomNav) return false

        val tinyIconOnly = text.isBlank() && (bw < 72 || bh < 72)
        if (tinyIconOnly) return false
        return true
    }

    private fun visualCandidateScore(e: ScreenElement, goal: String, screenW: Int, screenH: Int): Int {
        val b = e.bounds
        val bw = b.right - b.left
        val bh = b.bottom - b.top
        val cy = (b.top + b.bottom) / 2f
        var score = 0
        if (bw >= screenW * 0.35f && bh >= 110) score += 6
        if (bw >= screenW * 0.55f && bh >= 150) score += 4
        if (cy in (screenH * 0.16f)..(screenH * 0.78f)) score += 2
        if (e.clickable) score += 1

        val text = e.text
        val positive = listOf("直播", "正在直播", "在线", "视频", "播放", "每日推荐", "日推", "歌单", "作品")
        if (positive.any { goal.contains(it) && text.contains(it) }) score += 5
        if (text.contains("搜索") || text.contains("广告") || text.contains("推广")) score -= 8
        return score
    }

    /** 从任务原话里认出要操作的 App（命中已知名/别名）并打开它。返回打开的 App 名，没命中返回 null。 */
    /** 直接打开指定名字的 App（大脑 app 槽位走这条，最稳）。 */
    private fun openAppNamed(ctx: Context, name: String): String? {
        val pkg = resolvePackage(ctx, name) ?: return null
        return if (launchPackage(ctx, pkg)) name else null
    }

    /** 兜底：大脑没给 app 槽位时从 goal 里猜要开的 App——优先具体品牌名(b站/淘宝)，
     *  避开"视频/音乐"等泛指类目词(治"看籽岷的视频帮我打开b站"误开成小米视频)。 */
    private fun openAppInGoal(ctx: Context, goal: String): String? {
        val hit = (PKG.keys + ALIASES.keys)
            .filter { goal.contains(it, ignoreCase = true) }
            .minWithOrNull(compareBy({ if (it in GENERIC_APP_WORDS) 1 else 0 },  // 具体品牌名优先
                { goal.indexOf(it, ignoreCase = true) }, { -it.length }))
            ?: return null
        return openAppNamed(ctx, hit)
    }

    /** 泛指类目词（非具体 App 品牌）——从 goal 猜 App 时降权，避免"X的视频"被当成视频App。 */
    private val GENERIC_APP_WORDS = setOf(
        "视频", "音乐", "地图", "浏览器", "邮件", "邮箱", "相册", "图库", "照片",
        "日历", "计算器", "相机", "文件", "商店", "应用商店", "电话", "拨号",
    )

    /** 操作进度广播给 UI（MainActivity 的 voiceReceiver 会显示），让多步操作"看得见"。 */
    private fun progress(ctx: Context, msg: String) {
        Log.i(TAG, "进度: $msg")
        OverlayProgress.update(ctx, msg)   // 悬浮窗：浮在被操作 App 上层实时显示（看得见的操作）
        runCatching {
            ctx.sendBroadcast(Intent(KwsForegroundService.ACTION_EVENT).apply {
                setPackage(ctx.packageName)
                putExtra(KwsForegroundService.EXTRA_WHO, "灵犀·操作")
                putExtra(KwsForegroundService.EXTRA_TEXT, msg)
            })
        }
    }

    // ---- 编排脚本样例：打开省电模式（开设置→点电池→点省电）----
    private suspend fun batterySaver(ctx: Context): String {
        Log.i(TAG, "batterySaver 开始：打开设置")
        launchPackage(ctx, "com.android.settings")
        delay(1000)  // 留出设置进程启动时间，再开始找控件
        if (!clickWhenReady("Battery")) return "没进到电池设置"
        delay(500)
        if (!clickWhenReady("Battery Saver")) return "到了电池页但没点到省电"
        delay(600)
        val on = clickWhenReady("Use Battery Saver")  // 关键最后一步：拨动开关真正开启
        Log.i(TAG, "拨动 Use Battery Saver 开关 = $on")
        return if (on) "已开启省电模式" else "进了省电页但没拨到开关"
    }

    // ---- 工具 ----

    /** 等目标文本在当前屏出现后点击它，最多等 timeoutMs。 */
    private suspend fun clickWhenReady(text: String, timeoutMs: Long = 8000): Boolean {
        val acc = LingxiAccessibilityService.instance ?: run {
            Log.w(TAG, "无障碍未连接"); return false
        }
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (acc.readScreen().contains(text, ignoreCase = true) && acc.clickByText(text)) return true
            delay(300)
        }
        return false
    }

    private fun launchPackage(ctx: Context, pkg: String): Boolean {
        val intent = ctx.packageManager.getLaunchIntentForPackage(pkg)
            ?.apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) } ?: return false
        return runCatching { ctx.startActivity(intent); true }.getOrDefault(false)
    }

    /** 多级解析 app 名 → 包名：①已知包名表(且确已装) ②真实标签精确 ③标签双向包含 ④中文别名扩展。 */
    private fun resolvePackage(ctx: Context, name: String): String? {
        PKG[name]?.let { if (isInstalled(ctx, it)) return it }
        val apps = installedLaunchers(ctx)
        val q = name.trim().lowercase()
        if (q.isEmpty()) return null
        // ② 标签精确（忽略大小写）
        apps.firstOrNull { it.first.equals(name.trim(), ignoreCase = true) }?.let { return it.second }
        // ③ 标签双向包含（label 含 query，或 query 含 label 且 label 不太短防误命中）
        apps.firstOrNull {
            val l = it.first.lowercase()
            l.contains(q) || (l.length >= 2 && q.contains(l))
        }?.let { return it.second }
        // ④ 中文别名 → 候选关键词 → 真实已装列表里找（治"相册→Photos"、"b站→bilibili"）；
        //    关键词同时比对【标签】与【包名】（如 b站→pkg tv.danmaku.bili 含 "bili"），更稳。
        val keys = ALIASES[name.trim()] ?: ALIASES.entries.firstOrNull { name.contains(it.key) }?.value
        keys?.forEach { k ->
            apps.firstOrNull {
                it.first.contains(k, ignoreCase = true) || it.second.contains(k, ignoreCase = true)
            }?.let { return it.second }
        }
        return null
    }

    /** 上报给大脑的已装清单：每项 "label\t包名"。大脑据此把口语名解析到真实包名(③)。 */
    fun installedAppsForBrain(ctx: Context): List<String> =
        installedLaunchers(ctx).map { "${it.first}\t${it.second}" }

    /** 按包名直接启动（大脑已解析出 package 时走这条，最稳，绕过所有名字匹配）。 */
    fun launchByPackage(ctx: Context, pkg: String): Boolean =
        if (isInstalled(ctx, pkg)) launchPackage(ctx, pkg) else false

    /** 枚举设备上【真实已装】的可启动 app：List<(label, 包名)>。manifest 已声明 LAUNCHER queries 故可见全部。 */
    private fun installedLaunchers(ctx: Context): List<Pair<String, String>> {
        val pm = ctx.packageManager
        val main = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        return pm.queryIntentActivities(main, 0).map {
            it.loadLabel(pm).toString() to it.activityInfo.packageName
        }
    }

    private fun isInstalled(ctx: Context, pkg: String): Boolean =
        ctx.packageManager.getLaunchIntentForPackage(pkg) != null
}
