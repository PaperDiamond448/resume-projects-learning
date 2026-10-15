package com.lingxi.mobile.model

import java.util.UUID

/**
 * 大脑服务端 /interact 的请求/响应数据模型（与 brain/server.py 契约一一对应）。
 */

data class VisionMark(
    val kind: String,   // head_motion / gesture / expression
    val label: String,  // nod / shake / thumb_up / positive ...
    val confidence: Double = 0.9,
    // 该视觉事件在统一时间轴上的起止（秒）。端侧带上→大脑做真·模态异步对齐（overlap/relation）。
    val t_start: Double? = null,
    val t_end: Double? = null,
    // 高风险确认审计字段：事件绑定一次确认窗口，且只能消费一次。
    val event_id: String? = null,
    val window_id: String? = null,
    val stable_count: Int? = null,
    val localized: Boolean? = null,
    val quality: String? = null,
)

data class InteractRequest(
    val user_id: String = "android",
    val text: String,
    val vision: List<VisionMark> = emptyList(),
    val screen: Map<String, String>? = null,
    val gaze_target: String? = null,
    // 话语在统一时间轴上的起止（秒）。与 vision 同一时钟→大脑据此对齐语音与点头/手势事件。
    val speech_t_start: Double? = null,
    val speech_t_end: Double? = null,
    // 仅确认轮携带。普通新任务保持 null，避免被误解释成旧 pending 的确认回答。
    val pending_id: String? = null,
    val confirmation_window_id: String? = null,
    val confirmation_source: String? = null,
    // 本机【真实已装应用】清单，每项 "label\t包名"。大脑据此把 open_app 的口语名解析到真实包名
    // （治"b站→bilibili"等中英标签错位，且新装 App 无需手工维护别名表）。
    val apps: List<String> = emptyList(),
    // Cloud-2 HTTP幂等ID；每个逻辑POST只生成一次，和pending/window/event ID职责分离。
    val request_id: String = UUID.randomUUID().toString(),
)

/** 安卓端要在手机上真执行的动作；为 null 表示本轮不执行（pending/澄清/闲聊/查询已在 reply）。 */
data class DeviceAction(
    val type: String,                 // send_message / make_call / open_app / web_search ...
    val slots: Map<String, String> = emptyMap(),
)

// ③ GUI Agent · 分步计划：先拆成有序子步骤，逐步推进、做完一步不回头
data class AgentPlanRequest(
    val goal: String,
    val app: String = "",
    val skip_demo: Boolean = false,
    val request_id: String = UUID.randomUUID().toString(),
)

data class AgentPlanResponse(
    val steps: List<String> = emptyList(),
    val source: String = "",                 // "demo"=真人演示(确定性回放) / "guide" / "memory" / ""(LLM)
    val demo: List<DemoStep> = emptyList(),   // source=demo 时的元素级可回放步骤
)

// T-13 跨 App 编排：大脑只给协议和子任务，执行层后续逐 App 跑 run_agent + T-14 抽取 + 汇总。
data class MultiAppPlanRequest(
    val goal: String,
    val apps: List<String> = emptyList(),
    val request_id: String = UUID.randomUUID().toString(),
)

data class MultiAppTask(
    val id: String = "",
    val app: String = "",
    val goal: String = "",
    val steps: List<String> = emptyList(),
    val remember: Map<String, Any> = emptyMap(),
)

data class MultiAppPlanResponse(
    val workflow: Boolean = false,
    val source: String = "",
    val mode: String = "",
    val goal: String = "",
    val query: String = "",
    val route: Map<String, String> = emptyMap(),
    val tasks: List<MultiAppTask> = emptyList(),
    val merge: Map<String, Any> = emptyMap(),
    val final: Map<String, Any> = emptyMap(),
    val reason: String = "",
)

/** 教学模式演示的一步：真人点了哪个元素。回放时优先按 text 找，无 text 按 bounds 中心点；sig 校验是否在对的页。 */
data class DemoStep(
    val action: String = "tap",
    val text: String = "",
    val bounds: List<Int> = emptyList(),     // [left, top, right, bottom]（屏幕像素）
    val sig: String = "",                    // 这步执行前的页面文本签名
)

/** 教学模式上报：用户示范一次后，把元素级步骤存为可回放技能。隐私：只含文字/坐标/页面文本签名。 */
data class RecordDemoRequest(
    val app: String = "",
    val goal: String,
    val steps: List<DemoStep> = emptyList(),
    val request_id: String = UUID.randomUUID().toString(),
)

data class AgentTraceItem(
    val subgoal: String,
    val thought: String = "",
    val action: String = "",
    val target: String = "",
    val beforeHash: Int = 0,
    val afterHash: Int = 0,
    val done: Boolean = false,
    val reason: String = "",
)

// 自填充 App 技能库：run_agent 完成/失败后上报文本经验。只传步骤/动作历史，不传截图、音频、人脸。
data class AgentMemoryRecordRequest(
    val app: String = "",
    val goal: String,
    val steps: List<String> = emptyList(),
    val history: List<String> = emptyList(),
    val trace: List<AgentTraceItem> = emptyList(),
    val success: Boolean = false,
    val note: String = "",
    val request_id: String = UUID.randomUUID().toString(),
)

// ③ GUI Agent（边看边决策）单步请求/响应
data class AgentStepRequest(
    val goal: String,
    val screen: String,
    val history: List<String>,
    val subgoal: String = "",       // 当前要完成的子步骤（聚焦它；空=旧的整目标模式）
    val app: String = "",           // 当前 App 名→大脑注入该 App 攻略 hints
    val request_id: String = UUID.randomUUID().toString(),
)

data class AgentStep(
    val thought: String = "",        // 先想后做：模型一句话推理（仿 AgentCPM，显示给用户看"它在想什么"）
    val subgoal_done: Boolean = false, // 当前子步骤是否已达成→安卓推进到下一步、绝不回头
    val action: String = "none",    // tap / input / scroll / back / none
    val target: String = "",
    val reason: String = "",
)

// 路线C 视觉兜底（Set-of-Mark）：a11y 卡住时上传截图 + 元素框，大脑画编号→VLM 选号→回中心坐标。
/** 一个可点元素：编号 + 文字 + 屏幕坐标框 [left, top, right, bottom]（原图像素，与 tap 同坐标系）。 */
data class AgentVisualElement(
    val id: Int,
    val text: String,
    val bounds: List<Int>,
)

data class AgentVisualRequest(
    val goal: String,
    val history: List<String>,
    val screenshot: String,            // base64(PNG) 截图
    val elements: List<AgentVisualElement>,
    val app: String = "",              // 当前 App 名→大脑据此注入该 App 攻略 hints 进视觉 prompt（治"3B 视觉无 App 知识乱选卡"）
    val cloud_vision_consent: Boolean = false,
    val cloud_vision_policy: String = "",
    val screenshot_purpose: String = "",
    val screen_sensitivity: String = "unknown",
    val screenshot_sha256: String = "",
    val request_id: String = UUID.randomUUID().toString(),
)

/** 视觉单步结果：action=tap 时 (x,y)=选中元素中心（取自 a11y 原始 bounds，零缩放误差）。 */
data class VisualStep(
    val thought: String = "",          // 视觉模型一句话推理（哪个编号是直播间/歌单…为什么）
    val action: String = "done",       // tap / scroll / back / done
    val x: Int? = null,
    val y: Int? = null,
    val mark: Int? = null,
    val reason: String = "",
)

// T-14 结构化抽取：截图+候选元素 → VLM 只抽数值，代码比大小后返回 winner。
data class AgentExtractRequest(
    val goal: String,
    val screenshot: String,
    val elements: List<AgentVisualElement>,
    val metric: String = "",
    val higher_is_better: Boolean? = null,
    val app: String = "",
    val cloud_vision_consent: Boolean = false,
    val cloud_vision_policy: String = "",
    val screenshot_purpose: String = "",
    val screen_sensitivity: String = "unknown",
    val screenshot_sha256: String = "",
    val request_id: String = UUID.randomUUID().toString(),
)

data class AgentExtractItem(
    val mark: Int = 0,
    val title: String = "",
    val value_raw: Any? = null,
    val value: Double? = null,
)

data class AgentExtractResponse(
    val action: String = "done",
    val x: Int? = null,
    val y: Int? = null,
    val mark: Int? = null,
    val items: List<AgentExtractItem> = emptyList(),
    val reason: String = "",
    val model: String = "",
)

// P4-A 动作后反思：关键点击后验收是否真的到达目标页，避免进错页还继续执行。
data class AgentReflectRequest(
    val goal: String,
    val subgoal: String = "",
    val action: String = "",
    val target: String = "",
    val before_screen: String = "",
    val after_screen: String = "",
    val screenshot: String = "",       // base64(PNG) 动作后截图；可为空
    val app: String = "",
    val cloud_vision_consent: Boolean = false,
    val cloud_vision_policy: String = "",
    val screenshot_purpose: String = "",
    val screen_sensitivity: String = "unknown",
    val screenshot_sha256: String = "",
    val request_id: String = UUID.randomUUID().toString(),
)

data class AgentReflectResponse(
    val status: String = "UNSURE",     // SUCCESS / WRONG_PAGE / NO_CHANGE / UNSURE
    val thought: String = "",
    val reason: String = "",
    val model: String = "",
)

data class InteractResponse(
    val reply: String = "",
    val intent: String = "",
    val status: String = "",
    val confidence: Double = 0.0,
    val conflict: Boolean = false,
    val conflict_resolution: String = "",
    val need_clarification: Boolean = false,
    val clarify_question: String = "",
    val device_action: DeviceAction? = null,
    val pending_id: String? = null,
    val confirmation_window_id: String? = null,
    val decision_reason: String = "",
)
