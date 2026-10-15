# 灵犀 Mobile · 大脑服务端（brain）

把本项目随附的 `lingxi` 核心管线包成 HTTP 服务，供安卓端调用。
默认从项目根目录加载 `lingxi/`、`configs/config.yaml` 和域路由模型，不再依赖 `D:\华为比赛`。本服务不只是 `/interact` 包装层，还承担手机 GUI-agent 的规划、视觉选择、动作反思、App 技能库和教学记忆。

## 设计要点
- 人脸、原始音频和前摄确认图像始终端侧闭环；一般交互只上传ASR文本与视觉标签摘要。GUI-agent屏幕截图是单独的数据边界：仅在用户明示开启云端截图、当前用途绑定、页面本地判为非敏感且PNG摘要一致时才可发送，Brain还会二次校验。
- 服务端跑意图理解→冲突消解→个性化应答，`terminal=mock`：**只决策、不真执行**。
- **真正的设备动作由安卓端落地**，依据服务端返回的归一化字段 `device_action`。
- 适配层把大脑的两段式确认流（发消息/打电话先 pending、confirm 后 executed）
  归一化成统一 `device_action`：安卓端只需判断它是否为空决定要不要在手机上执行。
- 待确认动作有确定性安全护栏：纯「再来一次/重试/没看清/没识别到」只保留 pending 并要求重新确认，绝不下发 `device_action`。
- App 内多步任务走 `agent_plan → agent_step/agent_step_visual → agent_reflect`：文本大脑负责拆步骤和轻量决策，视觉模型只做 SoM 编号选择/状态反思，坐标落地交给安卓无障碍。
- App 能力沉淀在 `app_guides.py`、`data/app_demos.json`、`data/app_memory.json`、`data/app_skills.json`：手写攻略、真人教学、成功经验和失败避坑会共同注入 planning / visual / reflection。

## 运行
```powershell
# API Key 已持久化到 User 环境变量 DASHSCOPE_API_KEY（不落盘项目）
$env:NO_PROXY='*'
$env:LINGXI_VLM_BASE_URL='http://<VLM主机IP>:1234/v1'
$env:LINGXI_VLM_MODEL='qwen2.5-vl-3b-instruct'
$env:LINGXI_VLM_TIMEOUT='30'
python brain/server.py                  # 0.0.0.0:8000
python brain/server.py --port 8000
```

真机走 USB 调试时，安卓端默认访问手机自己的 `127.0.0.1:8000`，必须先建反向转发：
```powershell
D:\AndroidDev\Sdk\platform-tools\adb.exe -s 2687da22 reverse tcp:8000 tcp:8000
D:\AndroidDev\Sdk\platform-tools\adb.exe -s 2687da22 reverse --list
```
若手机提示“大脑连接异常”，先查电脑 `/health`，再查 `adb reverse --list`；拔插 USB、重启 adb、重装调试时这条转发常会丢。

## 冒烟测试（不占端口，TestClient 直连）
```powershell
python -B brain/smoke_standalone.py
python brain/smoke_server.py
```
`smoke_standalone.py` 离线验证随附核心、配置、域路由、`/health`、系统快捷与冲突预检均不依赖外部 PC 源码。`smoke_server.py` 还包含真实 14B 调用和历史行为断言，需要有效 Key 与网络。

## 接口

### `GET /health`
不调 LLM 的存活检查。返回 `{status, llm_provider, active_sessions}`。

### `POST /interact`
请求：
```json
{
  "user_id": "default",
  "text": "给妈妈发短信说我晚点回家",
  "vision": [{"kind": "head_motion", "label": "nod", "confidence": 0.92}],
  "screen": {"app": "短信"},
  "gaze_target": "张三"
}
```
- `vision[].kind`: `head_motion` | `gesture` | `expression`
- `vision[].label`: `nod`/`shake` | `thumb_up`/`thumb_down`/`open_palm`/`pointing`… | `positive`/`negative`/`neutral`
- `vision[].t_start`/`t_end` + `speech_t_start`/`speech_t_end`（秒，统一时间轴，可选）：端侧带上→大脑做**真·模态异步对齐**
  （`lingxi/fusion/aligner.py` 算 overlap_ratio 与 before/during/after），并把冲突预检（`conflict.py` 语音极性×视觉极性）
  注入意图 LLM 终裁，响应回 `conflict`/`conflict_resolution`。缺省退回固定占位窗（纯文本/旧端零回归）。
  实测：确认轮发「不要」+`head_motion:nod` → `conflict=true`、消解=点头压过口头否定、回填 `send_message` 真执行。

响应：
```json
{
  "reply": "已准备好通过短信给妈妈发送「我晚点回家」，请确认是否发送。",
  "intent": "send_message",
  "slots": {"recipient": "妈妈", "content": "我晚点回家", "app": "短信"},
  "status": "pending_confirmation",
  "confidence": 0.95,
  "conflict": false,
  "conflict_resolution": "",
  "need_clarification": false,
  "clarify_question": "",
  "card": {"type": "confirm", "title": "...", "fields": [...], "actions": [...]},
  "device_action": null,
  "latency": {"intent": 2.3, "reply": 0.6, "total": 2.9}
}
```

**安卓端动作约定**：只看 `device_action`——
- 为 `null`：不在手机上执行（pending 等确认 / 澄清 / 闲聊 / 查询类结果已在 reply）。
- 非 `null`：`{type, slots}` 即要真执行的设备动作，例：
  `{"type":"send_message","slots":{"recipient":"妈妈","content":"我晚点回家","app":"短信"}}`。

两段式时序：发短信首轮 `status=pending_confirmation, device_action=null`（弹确认 UI）；
用户说"确认/发吧/点头" → 次轮 `intent=confirm, status=executed`，服务端回填
`device_action={type:send_message, slots:{...}}` → 安卓端此时才真发。

### GUI-agent / App 技能接口

- `POST /agent_plan`：把 `{app, goal, screen}` 拆成有序子步骤。优先级：真人教学 demo > 手写攻略 recipe > 成功经验 memory/skill docs > LLM 拆计划。
- `POST /agent_step`：在当前无障碍读屏文本下给出下一步原子动作，支持 `subgoal_done`。
- `POST /agent_step_visual`：接收截图 + a11y 元素框 + app/goal/subgoal，注入 App hints/技能文档后让 ≤3B VLM 选择编号；VLM 不通时返回 `action="error"`，安卓必须诚实收手。
- `POST /agent_reflect`：动作后验收，返回 `SUCCESS / WRONG_PAGE / NO_CHANGE / UNSURE`，用于安卓恢复策略。
- `GET /agent_memory`、`POST /agent_memory/record`：记录 run_agent 成功/失败、trace 与备注。
- `GET /agent_demos`、`POST /agent_memory/record_demo`：真人教学轨迹读写。
- `GET /agent_skills`、`POST /agent_skills/rebuild`、`POST /agent_skills/rebuild_trace`：调试/迁移控件级技能文档。

截图接口要求`cloud_vision_consent=true`、`cloud_vision_policy=cloud-screen.1`、与端点匹配的`screenshot_purpose`、`screen_sensitivity=clear`和PNG的`screenshot_sha256`。缺字段、旧客户端、跨用途重放、敏感/未知页、摘要不符或非PNG都在调用VLM前拒绝。云VLM模型固定精确`qwen2.5-vl-3b-instruct`；配置缺失或服务失败不会回退第三方模型，也不会返回设备动作。

VLM 地址以运行环境为准，换网或重连后应先实测 `/v1/models`，不要假定局域网 IP 固定。改 `brain/` 模块后需要重启大脑进程，Python import 有缓存。
