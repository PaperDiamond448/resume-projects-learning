package com.lingxi.mobile.net

import com.google.gson.Gson
import com.lingxi.mobile.BuildConfig
import com.lingxi.mobile.model.AgentPlanRequest
import com.lingxi.mobile.model.AgentPlanResponse
import com.lingxi.mobile.model.AgentExtractRequest
import com.lingxi.mobile.model.AgentExtractResponse
import com.lingxi.mobile.model.AgentReflectRequest
import com.lingxi.mobile.model.AgentReflectResponse
import com.lingxi.mobile.model.AgentMemoryRecordRequest
import com.lingxi.mobile.model.AgentStep
import com.lingxi.mobile.model.AgentStepRequest
import com.lingxi.mobile.model.AgentTraceItem
import com.lingxi.mobile.model.AgentVisualElement
import com.lingxi.mobile.model.AgentVisualRequest
import com.lingxi.mobile.model.DemoStep
import com.lingxi.mobile.model.RecordDemoRequest
import com.lingxi.mobile.model.InteractRequest
import com.lingxi.mobile.model.InteractResponse
import com.lingxi.mobile.model.MultiAppPlanRequest
import com.lingxi.mobile.model.MultiAppPlanResponse
import com.lingxi.mobile.model.VisualStep
import com.lingxi.mobile.vision.CloudVisionEvidence
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException
import java.util.concurrent.TimeUnit

enum class BrainHealthStatus {
    AVAILABLE,
    AUTH_FAILED,
    SERVER_NOT_READY,
    HTTP_ERROR,
    NETWORK_ERROR,
    INVALID_CONFIGURATION,
}

data class BrainHealthResult(
    val status: BrainHealthStatus,
    val httpCode: Int? = null,
) {
    val available: Boolean get() = status == BrainHealthStatus.AVAILABLE

    fun userMessage(): String = when (status) {
        BrainHealthStatus.AVAILABLE -> "大脑认证成功 · secure-local可用"
        BrainHealthStatus.AUTH_FAILED -> "认证失败 · 请核对设备令牌或服务端吊销状态"
        BrainHealthStatus.SERVER_NOT_READY -> "认证通过 · 但大脑依赖尚未就绪"
        BrainHealthStatus.HTTP_ERROR -> "大脑返回异常状态${httpCode?.let { "（HTTP $it）" }.orEmpty()}"
        BrainHealthStatus.NETWORK_ERROR -> "网络连接失败 · 请检查地址、服务和USB映射"
        BrainHealthStatus.INVALID_CONFIGURATION -> "大脑地址或设备令牌配置无效"
    }
}

/**
 * 大脑服务端客户端 —— 调用 FastAPI 的 /interact。
 *
 * baseUrl 默认 10.0.2.2:8000：这是安卓【模拟器】访问宿主机 localhost 的特殊别名
 * （宿主机上 `python brain/server.py` 跑在 8000）。真机调试时改成开发机的局域网 IP。
 */
class BrainClient(
    private val baseUrlOverride: String? = null,
    private val tokenProvider: () -> String? = { BrainConnectionStore.readToken() },
) {

    private val gson = Gson()
    private fun client(readTimeoutSeconds: Long) = OkHttpClient.Builder()
        .addInterceptor { chain ->
            val request = chain.request().newBuilder()
                .header("X-Lingxi-Client-Version", BuildConfig.VERSION_NAME)
            tokenProvider()?.takeIf { BrainConnectionPolicy.validToken(it) }?.let {
                request.header("Authorization", "Bearer $it")
            }
            chain.proceed(request.build())
        }
        // <5s 红线：连接快超时；读取给云端大模型留够余量但不无限等
        .connectTimeout(3, TimeUnit.SECONDS)
        .readTimeout(readTimeoutSeconds, TimeUnit.SECONDS)
        .build()

    private val http = client(15)

    // ③ GUI-agent 的拆计划/单步/视觉调用是【异步后台】跑、不撞单轮 <5s →
    // 给更长读超时（真实复杂界面截图发给视觉模型、首帧冷启可能 >15s，治日志里的"视觉兜底失败:timeout"）。
    private val agentHttp = client(40)

    private fun url(path: String): String {
        val raw = baseUrlOverride ?: BrainConnectionStore.endpoint()
            ?: throw IllegalStateException("尚未配置云端大脑地址")
        val checked = BrainConnectionPolicy.validateEndpoint(raw, BuildConfig.DEBUG)
        require(checked.valid) { checked.reason }
        return checked.normalized + path
    }

    suspend fun interact(req: InteractRequest): InteractResponse = withContext(Dispatchers.IO) {
        val body = gson.toJson(req).toRequestBody(JSON)
        val request = Request.Builder()
            .url(url("/interact"))
            .post(body)
            .build()
        http.newCall(request).execute().use { resp ->
            val text = resp.body?.string().orEmpty()
            if (!resp.isSuccessful) {
                throw RuntimeException("大脑服务返回 ${resp.code}: $text")
            }
            gson.fromJson(text, InteractResponse::class.java)
        }
    }

    /** ③ 分步计划：把目标拆成有序子步骤（大脑一次性返回）。命中真人演示则 source=demo + 元素级步骤。
     *  失败返回空 steps→上层退回整目标。 */
    suspend fun agentPlan(goal: String, app: String, skipDemo: Boolean = false): AgentPlanResponse = withContext(Dispatchers.IO) {
        runCatching {
            val body = gson.toJson(AgentPlanRequest(goal, app, skipDemo)).toRequestBody(JSON)
            val request = Request.Builder().url(url("/agent_plan")).post(body).build()
            agentHttp.newCall(request).execute().use { resp ->
                gson.fromJson(resp.body?.string().orEmpty(), AgentPlanResponse::class.java)
            }
        }.getOrDefault(AgentPlanResponse())
    }

    /** T-13 跨 App 编排：拿到多个 App 子任务 + REMEMBER/merge 协议；执行层后续接入。 */
    suspend fun agentPlanMulti(goal: String, apps: List<String> = emptyList()): MultiAppPlanResponse = withContext(Dispatchers.IO) {
        runCatching {
            val body = gson.toJson(MultiAppPlanRequest(goal, apps)).toRequestBody(JSON)
            val request = Request.Builder().url(url("/agent_plan_multi")).post(body).build()
            agentHttp.newCall(request).execute().use { resp ->
                gson.fromJson(resp.body?.string().orEmpty(), MultiAppPlanResponse::class.java)
            }
        }.getOrDefault(MultiAppPlanResponse())
    }

    /** 教学模式：把用户示范的元素级步骤存为可回放技能。失败不影响主流程。 */
    suspend fun recordDemo(app: String, goal: String, steps: List<DemoStep>): Unit = withContext(Dispatchers.IO) {
        runCatching {
            val body = gson.toJson(RecordDemoRequest(app, goal, steps)).toRequestBody(JSON)
            val request = Request.Builder().url(url("/agent_memory/record_demo")).post(body).build()
            agentHttp.newCall(request).execute().close()
        }
    }

    /** 自填充 App 技能库：记录一次 run_agent 的文本经验。失败不影响主任务。 */
    suspend fun recordAgentMemory(
        app: String,
        goal: String,
        steps: List<String>,
        history: List<String>,
        trace: List<AgentTraceItem>,
        success: Boolean,
        note: String,
    ): Unit = withContext(Dispatchers.IO) {
        runCatching {
            val req = AgentMemoryRecordRequest(app, goal, steps, history, trace, success, note)
            val body = gson.toJson(req).toRequestBody(JSON)
            val request = Request.Builder().url(url("/agent_memory/record")).post(body).build()
            agentHttp.newCall(request).execute().close()
        }
    }

    /** ③ GUI Agent 单步决策：上传目标+【当前子步骤】+屏幕+历史+App名，拿回下一步动作 + 子步骤是否完成。
     *  app 字段 2026-07-03 补上——之前四个 agent 端点里唯独这个真正做决策的没有 App 知识注入。 */
    suspend fun agentStep(
        goal: String, subgoal: String, screen: String, history: List<String>, app: String = "",
    ): AgentStep =
        withContext(Dispatchers.IO) {
            val body = gson.toJson(AgentStepRequest(goal, screen, history, subgoal, app)).toRequestBody(JSON)
            val request = Request.Builder().url(url("/agent_step")).post(body).build()
            agentHttp.newCall(request).execute().use { resp ->
                gson.fromJson(resp.body?.string().orEmpty(), AgentStep::class.java)
            }
        }

    /** 路线C 视觉兜底：上传截图 + a11y 元素框 → 大脑 Set-of-Mark 选号 → 回中心坐标。 */
    suspend fun agentStepVisual(
        goal: String, history: List<String>, screenshotB64: String,
        elements: List<AgentVisualElement>, app: String = "",
        evidence: CloudVisionEvidence,
    ): VisualStep = withContext(Dispatchers.IO) {
        val req = AgentVisualRequest(
            goal = goal,
            history = history,
            screenshot = screenshotB64,
            elements = elements,
            app = app,
            cloud_vision_consent = evidence.consent,
            cloud_vision_policy = evidence.policyVersion,
            screenshot_purpose = evidence.purpose,
            screen_sensitivity = evidence.screenSensitivity,
            screenshot_sha256 = evidence.screenshotSha256,
        )
        val body = gson.toJson(req).toRequestBody(JSON)
        val request = Request.Builder().url(url("/agent_step_visual")).post(body).build()
        agentHttp.newCall(request).execute().use { resp ->
            gson.fromJson(resp.body?.string().orEmpty(), VisualStep::class.java)
        }
    }

    /** T-14 结构化抽取：VLM 抽数值、代码比大小，返回应点击的候选编号/坐标。 */
    suspend fun agentExtract(
        goal: String,
        screenshotB64: String,
        elements: List<AgentVisualElement>,
        metric: String = "",
        higherIsBetter: Boolean? = null,
        app: String = "",
        evidence: CloudVisionEvidence,
    ): AgentExtractResponse = withContext(Dispatchers.IO) {
        val req = AgentExtractRequest(
            goal = goal,
            screenshot = screenshotB64,
            elements = elements,
            metric = metric,
            higher_is_better = higherIsBetter,
            app = app,
            cloud_vision_consent = evidence.consent,
            cloud_vision_policy = evidence.policyVersion,
            screenshot_purpose = evidence.purpose,
            screen_sensitivity = evidence.screenSensitivity,
            screenshot_sha256 = evidence.screenshotSha256,
        )
        val body = gson.toJson(req).toRequestBody(JSON)
        val request = Request.Builder().url(url("/agent_extract")).post(body).build()
        agentHttp.newCall(request).execute().use { resp ->
            gson.fromJson(resp.body?.string().orEmpty(), AgentExtractResponse::class.java)
        }
    }

    /** P4-A 动作后反思：高风险点击后判断成功/错页/无变化/不确定。失败默认 UNSURE，不假成功。 */
    suspend fun agentReflect(
        goal: String,
        subgoal: String,
        action: String,
        target: String,
        beforeScreen: String,
        afterScreen: String,
        screenshotB64: String,
        app: String = "",
        evidence: CloudVisionEvidence? = null,
    ): AgentReflectResponse = withContext(Dispatchers.IO) {
        runCatching {
            val req = AgentReflectRequest(
                goal = goal,
                subgoal = subgoal,
                action = action,
                target = target,
                before_screen = beforeScreen,
                after_screen = afterScreen,
                screenshot = screenshotB64,
                app = app,
                cloud_vision_consent = evidence?.consent ?: false,
                cloud_vision_policy = evidence?.policyVersion.orEmpty(),
                screenshot_purpose = evidence?.purpose.orEmpty(),
                screen_sensitivity = evidence?.screenSensitivity ?: "unknown",
                screenshot_sha256 = evidence?.screenshotSha256.orEmpty(),
            )
            val body = gson.toJson(req).toRequestBody(JSON)
            val request = Request.Builder().url(url("/agent_reflect")).post(body).build()
            agentHttp.newCall(request).execute().use { resp ->
                gson.fromJson(resp.body?.string().orEmpty(), AgentReflectResponse::class.java)
            }
        }.getOrDefault(AgentReflectResponse())
    }

    suspend fun healthResult(): BrainHealthResult = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder().url(url("/health")).get().build()
            http.newCall(request).execute().use { response ->
                when (response.code) {
                    in 200..299 -> BrainHealthResult(BrainHealthStatus.AVAILABLE, response.code)
                    401, 403 -> BrainHealthResult(BrainHealthStatus.AUTH_FAILED, response.code)
                    503 -> BrainHealthResult(BrainHealthStatus.SERVER_NOT_READY, response.code)
                    else -> BrainHealthResult(BrainHealthStatus.HTTP_ERROR, response.code)
                }
            }
        } catch (_: IllegalArgumentException) {
            BrainHealthResult(BrainHealthStatus.INVALID_CONFIGURATION)
        } catch (_: IllegalStateException) {
            BrainHealthResult(BrainHealthStatus.INVALID_CONFIGURATION)
        } catch (_: IOException) {
            BrainHealthResult(BrainHealthStatus.NETWORK_ERROR)
        }
    }

    suspend fun health(): Boolean = healthResult().available

    companion object {
        // 127.0.0.1：真机经 `adb reverse tcp:8000 tcp:8000` 把手机 localhost→开发机大脑（USB 直连最稳）。
        // 模拟器旧值为 10.0.2.2；若走 WiFi/局域网，改成开发机的局域网 IP（如 192.168.x.x:8000）。
        const val DEFAULT_BASE_URL = "http://127.0.0.1:8000"
        private val JSON = "application/json; charset=utf-8".toMediaType()
    }
}
