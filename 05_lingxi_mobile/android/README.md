# 灵犀 Mobile · 安卓端（android）

原生 Kotlin 客户端。当前形态=**端侧采集/执行层 + 全局助手壳**：本地采集语音、读屏和截图授权，执行系统 Intent/API 与无障碍动作，把文本化证据和必要的屏幕视觉请求交给大脑服务端决策。

## 当前能力
- **🎙 语音唤醒全链路**：喊中文唤醒词（你好灵犀 / 小灵同学 / 灵犀灵犀，首启可选）→ 端侧 KWS 唤醒 → 端侧 ASR 转写 → 大脑 `/interact` → 应答 + 系统 TTS 播报。**音频不出端、只文本进大脑**（隐私边界 D5）。
  - 注：曾试双语「嘿 Siri」，英文短词识别率不稳，已改纯中文（拼音路最稳）。
  - KWS=sherpa-onnx 双语 `kws-zipformer-zh-en`(int8，~5MB)；ASR=`streaming-zipformer-small-ctc-zh`(~26MB)；TTS=安卓系统 `TextToSpeech`。
  - 实现：前台服务 `voice/KwsForegroundService` 常驻麦克风跑状态机（聆听→唤醒→ASR→大脑→TTS→回聆听）。
- **🤖 默认助手角色（对标 Siri 全局唤起）**：注册了数字助理"三件套"（`assist/` VoiceInteractionService + SessionService + Session + RecognitionService 占位）。设为系统默认助理后，**长按 Home 唤起 → 直接按住说话**（跳过唤醒词，复用端侧 ASR→大脑→TTS），且获得**后台启动 Activity 豁免**（device_action 在息屏/后台也能落地）。
  - ⚠️ MIUI 拦 `adb settings put secure` → 需**手动**设默认：MIUI 设置 → 应用设置 → 默认应用（或"系统应用助手"）→ 数字助理 → 选「灵犀 Mobile」。
- 文本输入（调试/无网兜底）→ 调大脑 `/interact` → 显示应答。
- 依大脑下发的 `device_action` **真办事**：发短信 / 打电话 / 开应用 / 网页搜索 / 导航 / 邮件 / 提醒。
  - 发短信/打电话：通讯录解析"妈妈"→号码后直发/直拨；无权限或查不到则降级为打开短信/拨号 App（不误发）。
- 发短信/打电话的"是否执行"由**大脑两段式确认流**把关（confirm 后才回填 device_action）。
- **系统确定性能力**：相机、闹钟列表、计时器/倒计时、日历、WiFi/蓝牙/音量/显示/电池/无障碍设置直达、手电筒。
- **无障碍执行层**：`LingxiAccessibilityService` 读屏、点按、手势、输入、回退/主页，并作为 GUI-agent 的执行手。进度浮窗使用 `TYPE_ACCESSIBILITY_OVERLAY`，不再依赖 MIUI 容易拦截的悬浮窗权限。
- **屏幕视觉与云端截图分权**：`ScreenCaptureService`的MediaProjection只代表本机可截图，不等于允许上传。云端截图另设默认关闭、明示开启且可撤销的同意；上传前本地绑定用途、检查敏感页并生成PNG SHA-256，Brain再校验。SoM仍采用“无障碍元素框+编号截图+VLM选号+无障碍落点”。
- **GUI-agent 与教学回放**：`AppAutomator` 支持分步计划、视觉兜底、动作后反思、错误恢复、真人教学录制/回放。真人示教优先级高于手写攻略和 LLM；示教只存文字、bounds、页面签名，不存截图/音频/人脸。
- **🎭 多模态冲突消解 / 时间对齐（赛题题眼）**：发短信/打电话的确认环节可在端侧采点头/摇头、拇指↑↓和弱表情信号，连同口头/打字证据及统一时间轴送大脑。现行安全契约是任一可靠来源明确拒绝就`CANCELLED`并清空pending；真正含糊才`CLARIFY`。视觉单独授权结构已保留但生产开关仍关闭、阈值仍为空。前摄图像不出端，只上报语义标签、真实分数和时间戳；这与GUI-agent“屏幕截图需另行同意”是两条完全不同的数据链。

## 当前缺口
- 多模态冲突消解已完成单设备脚本化真机回归；跨设备、跨 ROM 与多人独立数据仍需继续验证。
- `onHandleAssist` 的当前屏幕上下文还需要做成正式体验：长按唤起时应稳定把屏幕摘要、App、焦点对象带入大脑。
- App 类任务还缺一键真机回归和成功率统计；B站/音乐/便签/微信/淘宝等应逐步形成可复测任务集。
- 教学入口仍偏调试（输入框“教学:xxx/结束教学”），后续应做成正式 UI。
- 唤醒词任意自定义、MediaPipe 手势/表情/眼动精度仍是后续增强项。

## sherpa-onnx 集成（语音唤醒/ASR）
- native `.so`（仅 arm64-v8a）在 `app/src/main/jniLibs/`，vendoring 的 6 个 kotlin-api `.kt` 在 `app/src/main/java/com/k2fsa/sherpa/onnx/`，模型在 `app/src/main/assets/{kws-zh-en,asr-ctc-zh}/`。
- 来源：sherpa-onnx v1.13.3 release（`.so` 与 `.kt` 版本对齐）+ KWS/ASR 预训练模型 GitHub release。`gradle` 已设 `abiFilters arm64-v8a` + `noCompress onnx`。
- 唤醒词 token 在 `assets/kws-zh-en/keywords.txt`（中文拼音 + 英文 CMU 音素，`@后是显示名`，可加 `:boost #threshold`）。

## 构建（笔记本工具链已就绪，命令行 gradle）
```
JAVA_HOME=D:\Android\jbr GRADLE_USER_HOME=D:\AndroidDev\gradle-home ANDROID_HOME=D:\AndroidDev\Sdk \
  D:\AndroidDev\gradle-home\wrapper\dists\gradle-8.9-bin\*\gradle-8.9\bin\gradle.bat -p android assembleDebug
```
产物：`app/build/outputs/apk/debug/app-debug.apk`（约 71MB，含 sherpa-onnx native + KWS/ASR 模型）。

## 运行（真机 MI 8 Lite，serial 2687da22）
1. 大脑：`$env:NO_PROXY='*'; python brain/server.py`（注意把 QWEATHER_KEY/AMAP_KEY/DASHSCOPE_API_KEY 注入进程环境）。
2. 连大脑：USB + `adb -s 2687da22 reverse tcp:8000 tcp:8000`（手机 localhost:8000 → 电脑大脑；`BrainClient` 默认 `127.0.0.1:8000`）。
3. 装：`adb -s 2687da22 install -r app-debug.apk`。**麦克风权限 MIUI 拦 adb grant → app 内运行时点允许**。
4. **语音唤醒测试**：开 app → 首启选唤醒词(中文) → 点「🎙 唤醒：开」(授麦克风) → 喊「你好灵犀」(或所选词) → 提示音 → 说指令(如"今天天气") → 应答 + TTS。
5. 文本兜底：输入"给妈妈发短信说我晚点回家" → 弹确认 → 回"确认" → 落地发短信。

## 包结构
```
com.lingxi.mobile
├── MainActivity.kt              ← 聊天界面 + 唤醒开关/选词 + 串联大脑/执行器
├── model/Api.kt                ← /interact、agent、visual、reflect、demo 数据模型
├── net/BrainClient.kt          ← OkHttp 调大脑
├── voice/                      ← 🎙 语音唤醒全链路
│   ├── WakeWord.kt              ← 唤醒词预设 + 持久化(WakeWordStore)
│   ├── SherpaKws.kt             ← 端侧 KWS 引擎封装(KeywordSpotter)
│   ├── SherpaAsr.kt             ← 端侧 ASR 引擎封装(OnlineRecognizer)
│   ├── Speaker.kt               ← 系统 TextToSpeech 播报
│   └── KwsForegroundService.kt  ← 前台服务:常驻麦克风状态机(聆听→唤醒→ASR→大脑→TTS) + listenNow(助手唤起)
├── assist/                     ← 🤖 默认助手"三件套"(数字助理角色)
│   ├── LingxiInteractionService.kt        ← VoiceInteractionService 入口
│   ├── LingxiInteractionSessionService.kt ← 会话工厂
│   ├── LingxiInteractionSession.kt        ← 唤起即按住说话(onShow→listenNow) + 读屏(onHandleAssist)
│   └── LingxiRecognitionService.kt        ← RecognitionService 占位(满足声明)
├── accessibility/              ← 无障碍读屏、点击、输入、教学事件采集
├── vision/                     ← MediaProjection 屏幕视觉截图
├── ui/                         ← 进度浮窗与状态提示
└── action/
    ├── DeviceActionExecutor.kt ← device_action → 真实手机操作
    └── AppAutomator.kt         ← GUI-agent、教学回放、视觉兜底、反思恢复
```
