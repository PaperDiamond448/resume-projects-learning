# 纯项目边界审计

整理原则：原 Android/Brain 与外部 PC 大脑源码不删不改；本目录只复制运行、构建和源码级验证所需内容。判断依据同时核对了 Gradle source set、Android Manifest/资源引用、Python AST import 闭包、配置/模型路径、运行文档、密钥与个人数据规则。

## 保留

- `android/app/src/main/`：全部 Kotlin、Manifest、布局/主题资源、KWS/ASR ONNX、MediaPipe 模型和 arm64 sherpa/onnxruntime 原生库。模型与原生库均被运行代码直接加载，不能裁剪。
- `android/app/src/debug/`：调试构建声明，保证现有调试链路一致。
- `android/app/src/test/`：项目本身的纯逻辑 JVM 回归测试。
- Android Gradle 配置与 ProGuard 配置；保留现有 Gradle 版本描述。
- `brain/server.py`、`visual.py`、`app_guides.py`、`app_memory.py`：生产服务与 GUI-agent 核心。
- `brain/data/app_demos.json`、`app_memory.json`、`app_skills.json`：教学回放、成功/失败经验与控件级技能，均被 `app_memory.py` 在运行时直接读取。
- `brain/smoke_server.py`、`smoke_visual.py`：与项目源码一起维护的冒烟验证。
- `brain/smoke_standalone.py`：不联网、不调用 14B/VLM 的独立性基础回归。
- `lingxi/`：从 PC 项目静态解析出的 29 文件最小依赖闭包，仅含移动 Brain 使用的 core/fusion/intent/llm/persona/response/runtime。
- `configs/config.yaml`：环境变量插值配置，无明文 API Key。
- `data/models/domain_router.pkl`：生产意图引擎默认启用的域路由模型；缺失会改变 OOS 守门行为，因此必须保留。
- `deploy/cloud/`：Cloud-1/2/3的Dockerfile、本机与secure-local Compose、设备凭据/隐私运行审计、依赖锁、入口安全门、静态预检、Railway IaC和Caddy示例；不包含任何真实secret。Cloud-5服务器门与拔USB Wi-Fi天气/报时/一般闲聊已通过，证明Release不依赖ADB或PC Brain进程；蜂窝因设备无流量按用户范围未测试，Windows热点也不证明电脑关机网络独立，因此仍保持staging而不称production。
- 模块 README、依赖清单和本目录的运行说明。

## 排除

- `提交/`、`sub/`、`docs/submission/`、根目录 Word/PPT/PDF、比赛要求和提交规范：竞赛材料，不是运行项目。
- 原 Mobile 的 `research/`、`.research/`、`data/`、`video/`、`eval/`、`docs/research/`：研究代码、数据集、跑批环境、证据与评测产物；不参与当前 Android/Brain 生产运行。独立目录新建的 `data/` 仅含生产域路由模型。
- PC 项目的 `apps/`、`src/`、`eval/`、`tests/`、`docs/`、928 MiB `data/` 研究/录制内容，以及 Paraformer、MediaPipe、VITS 等约 272 MiB PC 感知/TTS 模型：Android 已自行完成感知和 TTS，移动 Brain 不加载这些物料。
- `_staging/`、`tmp_chsims_cases/`、根目录抓屏 XML/PNG：临时加工和真机调试快照。
- `android/app/build/`、`android/.gradle/`、`android/.idea/`、`android/local.properties`：可再生构建/IDE状态或本机路径。
- `.secrets/`：正式签名密钥和密码配置；绝不复制。
- `deploy/cloud/secrets/`：云端运行密钥目录；只在部署主机临时提供并被 `.gitignore`/`Dockerfile.dockerignore` 排除。
- `brain/data/profiles/`、Brain 日志/备份、`__pycache__/`：用户画像或运行时生成内容。
- `brain/serve_qwen2vl.py`：T-05 实验用 UI-TARS/ShowUI 适配器，依赖文件不完整且不在当前 Qwen2.5-VL-3B-Instruct 生产链路。
- `ADBKeyboard.apk`、Pandoc 安装包、独立模型压缩包和旧 APK：本机工具/中间交付物，不是源码运行依赖。
- `AGENTS.md`、`CLAUDE.md`、`NEXT*.md`、`.claude/`、`.agents/`、`.codex/`：会话协作与项目管理状态，不进入纯产品目录。

## 隐私与完整性检查

- 首轮 Mobile 保留候选审计 123 个原始文件；独立化又对 PC `lingxi` 全包、配置和数据路径做静态闭包审计。
- 未发现嵌入式 API Key、11 位手机号或邮箱地址。
- 签名身份与个人画像均未进入本目录。
- 复制后通过逐文件 SHA-256 对照、Python 语法/冒烟测试和 Android 构建/单元测试验证；验证结果记录在本文件末尾。

## 验证结果（2026-07-31）

- 126 个从原目录保留的文件全部存在；其中 124 个源码/二进制资源与原文件 SHA-256 一致，差异仅为 `android/README.md` 和 `brain/README.md` 的断链/IP 说明修订。
- Python 源码逐文件无落盘语法编译：PASS。
- 三份 GUI-agent JSON 解析：PASS；`demos=1`、`entries=36`、`skills=112`。
- Brain `/health`：PASS；完整 `smoke_server.py` 已通过 health、reflect 降级、教学/trace 技能、extract 与多 App 计划等前置项，随后在 `test_pending_app_task_merge` 因 14B 本轮返回 `clarify=True` 触发既有非确定性断言而停止。该失败与精简无关（被测 Python/JSON 均与原件同哈希），本次不越界修改生产逻辑。
- Android `testDebugUnitTest assembleDebug`：PASS；10 个测试套件共 51 项，失败/错误/跳过均为 0；Debug APK 成功生成（99,427,613 bytes，SHA-256 `151894C297E609A76802B142453AE9691E7F0F3DE3488C21425A1D80B2B09FCF`）。验证后已删除新目录内 426 MiB 构建产物、1.8 MiB Gradle 缓存和空的运行时 profiles，保持源码目录干净。
- 最终项目文件哈希见 `SOURCE_MANIFEST.sha256`。

## 独立化迁移验证（2026-07-31）

- 从 PC 项目复制的 29 个 Python 文件、配置和域路由模型共 31 项，逐文件 SHA-256 与只读源完全一致；复制体约 7.36 MiB。
- `brain/server.py` 在本副本中默认从项目根加载随附核心；旧 `LINGXI_PC_PROJECT` 即使被故意指向不存在的 `Z:` 也不参与运行。可选 `LINGXI_CORE_PROJECT` 只供开发者显式替换核心。
- 独立探针确认 `CORE_PROJECT`、`lingxi.__file__`、`PROJECT_ROOT`、配置路径和全部已加载 `lingxi.*` 模块均位于本目录；域路由真实加载 31 域/16 OOS 域。
- 离线 `smoke_standalone.py`：PASS；覆盖 `/health`、确定性 `open_camera`、域路由装载和“不要＋点头”冲突预检。
- 真实 Qwen3-14B 无副作用请求“现在几点了”：`query_time/executed`，总延时 3.55s；进程未加载任何外部 PC 项目模块。
- 扩展 `smoke_server.py` 首先暴露“已升级 run_agent 却重挂 App pending”的真实旧漏洞；仅在本副本增加 `not upgraded` 守卫后该项通过。继续运行后停在历史测试期待“确认阶段修改收件人”与现行高风险守卫“只接受明确确认/取消”的策略冲突；不以迁移名义放宽安全策略。
- 独立化后 Android 再次 `testDebugUnitTest assembleDebug`：BUILD SUCCESSFUL；10 套件 51/51 通过，失败/错误/跳过均为 0。APK 验证产物 99,427,613 bytes、SHA-256 `E81C63D0884ED455F22A1A101E4A6CD2E325F9453EC7E626B81A448EAB4542C7`，随后连同 build/Gradle 缓存删除。
- 迁移前后聚合哈希复核：原 Mobile 受保护源码=`af93e0a3...0adf`、PC 核心/配置/路由模型=`7efe5662...ac3c`，两者均完全未变。
- 最终独立项目共 162 个文件、约 83.92 MiB；`SOURCE_MANIFEST.sha256` 含其余 161 个文件的 SHA-256，整表复核通过。

## B08 安全契约审计（2026-08-03）

- 只新增 `SAFETY_CONTRACT.md`、`B08_REGRESSION_REPORT.md` 与 `brain/audit_pending_contract.py`，并同步本 README/边界记录；没有修改原目录或本目录的 Android/Brain/随附核心业务源码。
- `smoke_standalone.py` 再次 PASS；独立契约审计为 12 项中 7 PASS、5 FAIL，失败均保留为待修复证据。
- 已成立：明确确认一次性消费、明确拒绝压过肯定视觉、含糊肯定零动作、重试零动作、显式计龄后的过期确认零动作、clean-only `run_agent` pending 修复、Android nullable `device_action` 消费闸。
- 待源码修复：口头确认与视觉否定被误报为超时；收件人/正文修改后旧 pending 未失效；新任务执行后旧短信仍悬挂；Brain 适配层与随附核心的短信/电话高风险集合不一致。
- 按 B08 停止条件，本轮不越界改业务源码；另立 B15 后才允许在独立目录修复，原目录继续保持不动。
- 当前独立目录共 165 个文件；`SOURCE_MANIFEST.sha256` 覆盖其余 164 个文件并已逐项复核，哈希失败、未列文件、陈旧条目均为 0。

## B15 状态机、真机安全门与 r3 KWS 收口（2026-08-04）

- 业务改动继续只存在于本独立目录；原目录四个受保护哨兵文件哈希未变。
- Brain 契约审计 `25/25`、Android JVM `63/63`、standalone smoke 与 debug 构建通过。
- 外层工程保存的 P01/MI 8 Lite 真机 canonical 16/16 格符合各自安全契约；这些是单人单设备工程证据，不进入本纯项目目录，也不冒充 human gold 或跨用户准确率。
- `vision_only_enabled=false`、`visual_authorize_threshold=null` 继续保持；当前单人手势分布不足以冻结授权阈值。
- r3 只修复 KWS 开关状态同步并补4项纯逻辑测试；不改语音模型、视觉阈值或确认状态机。
- `SOURCE_MANIFEST.sha256` 收口后覆盖 171 个项目本体文件；运行时画像、构建缓存和本机签名身份均继续排除。

## B15 取消后迟到确认竞态收口（2026-08-07）

- CALL-C02 证明 KWS 在 typed 取消后可能迟到发送空文本、`unknown` 来源和旧 ID；旧 Brain 守卫让该信封进入普通会话并重建 `make_call` pending。该轮零 `device_action`、无真实拨号，但违反终态不可复活契约。
- 修复只落在本独立目录：Android 无有效 `ConfirmationEvidence` 时本地短路；Brain 对无 pending 的任一确认信封在解析器/历史/大模型之前按 `REPLAY/SOURCE_MISMATCH/ORPHAN` 拒绝。
- 新增精确竞态回归后，修复前 `24/26`、修复后 `26/26`；Android JVM 仍为 `63/63`，standalone smoke、debug 与正式签名 release 构建通过。
- 外层 terminal-gold 采集在 canonical 16/24 暂停，失败 CALL-C02 原件保留且不计入 canonical；须在同一真机重放通过并建立新版本冻结后恢复。
- `SOURCE_MANIFEST.sha256` 仍覆盖 171 个项目本体文件；构建产物、运行时画像和本机签名身份继续排除。

## B15-B KWS typed 证据交接（2026-08-07）

- CALL-C03 的真实 `thumb_up` 与 typed“确认拨打”原先被拆成一条 typed 执行请求和一条旧 ID vision-only 重放请求；安全守卫阻止了第二次动作，但该路径不能形成目标 `typed_visual`，重复参与者操作无法解决。
- 修复仅落在独立 Android：共享 binding 新增 `owner=activity/kws`；MainActivity 正常输入和 debug 回归输入在普通 typed 路径前检查 owner；KWS 仅接收与当前活动 `pending_id + confirmation_window_id` 完全一致且窗口仍打开的显式应答，并在视觉采集结束后只提交一次融合请求。
- typed/spoken 来源不互相伪装：任一明确拒绝占优；并发语音若可能改变授权语义，来源降为 unknown 并 fail closed。跨组件只共享 opaque ID/owner，不共享联系人、正文、号码或生物特征。
- 改动没有触及 Brain 生产逻辑、模型、视觉阈值、动作执行器或原目录。纯策略新增 7 项测试；契约审计 `27/27`、Android JVM `70/70`、standalone smoke 与 debug 构建通过。
- 正式 r5 APK SHA=`80E474D2...686E91`已用既有 RSA-4096/v2 证书覆盖安装；同机非canonical修复门精确观察 `NEW_PENDING → typed_visual/executed/make_call` 两条请求，真实`thumb_up`为0.7973/16帧/DURING，动作一次、最终ID空，15秒无迟到设备请求，旧信封重放为`refused/REPLAY`。B15-B产品修复门关闭，但该预冻结轮不计canonical；CALL-C03/C04须在r5冻结后重采。
- 新增 2 个项目本体文件并更新既有实现/文档后，`SOURCE_MANIFEST.sha256` 将覆盖 173 个项目本体文件；构建产物、运行时画像和本机签名身份继续排除。

## B15-C 确认窗口内完整新任务（2026-08-07）

- r5 CALL-C09 证明来源元数据与意图语义被混淆：ASR 精确文本“查询明天杭州天气”携带当前 `spoken` 信封后被误报为 `AMBIGUOUS_CONFIRMATION`，旧电话 pending 未失效；该轮零动作但不能计 canonical。
- 修复仅落在独立 Brain：明确新任务先验证当前信封绑定，绑定有效才清旧 pending 并进入既有普通路由；错 pending/window/source 仍 fail closed，明确拒绝与视觉否定规则不变。
- 契约红测由 `28/29` 转为 `29/29`，standalone smoke、compileall、Android JVM `70/70` 和 debug 构建通过；真实 HTTP 重放得到 `query_weather/executed/NEW_TASK`、零动作、终态 ID 空，错 ID 反证仍为 `PENDING_ID_MISMATCH`。
- Android、APK、动作执行器、模型、视觉阈值与原目录均未改；最后一格须在新 r6 Brain 冻结后重采。

## Cloud-1 手机脱离开发电脑 · 本地容器门（2026-08-20）

- 用户目标是手机联网独立使用，不是把14B/3B模型塞入手机；完整方案位于外层`docs/research/cloud_brain_phase1.md`。
- 本轮只增加部署文件与说明，不改Brain/Android/随附核心业务逻辑；原目录三项源码哨兵保持不变。
- Compose只映射宿主机`127.0.0.1:8000`，不含Caddy公网服务；entrypoint对production阶段和多worker均硬拒绝，避免认证尚未完成时误上线。
- API Key只允许通过运行时file secret读取；`deploy/cloud/secrets/`、`.env`、签名密钥和运行时数据均不进入源码清单或镜像上下文。
- Docker Desktop/WSL 环境已完成真实 Linux/amd64 无缓存构建；最终镜像 digest=`sha256:aef6cd83ee6245cb83d023d23df8a3bc908e15e97b34156cdd23d0e76817c39e`。构建门为静态预检14/14、pending契约29/29、standalone PASS和明确限定的`smoke_server.test_health()` PASS；未把需要真实14B Key的完整`smoke_server.py main()`冒充离线全通过。
- 最终镜像以非root `lingxi`、单worker运行，不含`/app/android`、pycache/pyc、本机绝对路径、临时secret或业务Key；Compose已验证回环health、重启、只读根和持久卷。Cloud-2设备认证与通用幂等尚未实现，仍禁止公网启动。

## Cloud-2 设备认证、持久幂等与真机配对（2026-08-20）

- 业务改动仍只在本独立目录：Brain新增统一安全中间件、最小live/认证ready、设备凭据哈希注册表和SQLite一次性请求账本；Android新增动态endpoint、运行时Bearer、UUID request ID、Keystore AES-GCM与明确连接分类。原目录三项业务源码哨兵未变。
- secure模式除`GET /live`外全部认证，`/interact`主体必须与令牌绑定主体一致。重放、跨路径request ID复用、错主体、错令牌、非JSON和超限请求均fail closed且零动作；服务端不保存请求体或响应体。
- 静态预检21/21、Cloud-2单元7/7、安全审计18/18、pending契约29/29、Android JVM76/76、standalone、debug构建、黑盒11/11和跨真实容器重启重放均通过。最终镜像digest=`sha256:fadd6ac17ac8e8e0e1d474b0190ddbb0ade74b9ccde6edddc2a10d49a6335130`。
- MI 8 Lite同证书无损升级并通过Keystore密文保存、短指纹和Bearer health。第一次错误指纹安全拒绝；重填后服务端`GET /health 200`且无device action。透明配置框和错误吞并已一并修正并重新回归。
- 测试令牌、占位业务Key、容器、网络、专用测试卷、抓屏和USB reverse均已撤销/删除；这些运行材料不进入项目目录或源码清单。手机只保留已经吊销的密文，可在正式配对时覆盖。
- Cloud-3代码/离线隐私门已完成；精确`qwen2.5-vl-3b-instruct`的台式机LM Studio自托管链此前已跑通，当前只是该3B算力节点不在身边、暂时离线。Cloud-4平台禁明文代码/构建、MI 8 Lite同证书覆盖/包一致、无缓存容器和Release HTTP界面负测均已通过，HTTP输入在网络请求前被拒绝。该条保留Cloud-3/4关闭时的阶段边界；Cloud-5当前实况见本文件末尾，不把VLM-disabled公网staging冒充完全离线或视觉能力在线。

## Cloud-3 外部能力与云截图隐私门（2026-08-20）

- 业务改动继续只在本独立目录：天气无显式城市时澄清且不访问后端，Key失败不返回模拟天气；服务器IP不再替代用户位置。
- Android新增独立云截图同意、用途/敏感度/SHA证据；Brain在调用VLM前再次验证。旧客户端、跨用途重放、摘要不符、非PNG和敏感/未知页均fail closed。
- 密码、验证码、支付、银行、证件和私聊页在手机本地硬停，不上传截图或页面文字，也不进入GUI-agent恢复重试。MediaProjection本机授权不再被误当作云端上传同意。
- 云VLM仅允许精确`qwen2.5-vl-3b-instruct`，第三方路由和假Key回退已删除。文本14B Key不得复用为VLM凭据；Compose默认关闭VLM并保留台式机自托管OpenAI兼容入口，3B节点恢复后再显式开启，不要求区域云Key。
- Cloud-3单测10/10、离线审计15/15、自托管端点策略6/6通过；Cloud-4审计11/11、Android JVM16套件81/81、双构建、真机HTTP拒绝和无缓存镜像已关闭。Cloud-5选择Railway托管容器但保留USB本地调试：`.railway/railway.ts`、上传排除表、专用说明和fail-closed入口均已投入真实staging；平台退役的`railway.json`已在首次部署前移除。首轮性能反证后，最终实现包含4秒有界硬截止、云端禁同轮修复/二次闲聊、天气3.5秒截止与120秒成功缓存、QWeather专属Host/高德实况降级链，以及三重安全闸后的同模型紧凑纯聊天单调用。真机抓出的Railway UTC报时问题已用显式`Asia/Shanghai`转换和UTC容器回归修复。时区修复无缓存Linux/amd64镜像=`sha256:e8ea919766ad418ff471a03edbcc7a35045d0b5ae6b98eae6b688a6cdf7390ee`，延迟契约18/18、36/36预检、入口17/17以及容器时间/零副作用/Android禁入通过；Railway部署`608e2211...f66b`=`SUCCESS`、平台镜像`sha256:4dd6d4e2...53ac`。
- Cloud-5服务器侧真实安全验收：Railway单服务/单副本/500MB卷与HTTPS域名已运行，IaC归零；动态`PORT=8080`。`/live=200`且只有`status`，无/错令牌`/ready=401`，正确令牌`/ready=200`，`/docs=404`。canonical安全天气请求`200/device_action=null`；无效UUID、主体错配、原样重放、同ID改写和跨重部署重放均按契约拒绝且零动作，证明幂等SQLite随卷持久化。设备注册表仍只存哈希，当前短指纹=`4666273c20b8`；原始令牌仅有本机DPAPI密文交接，不在本目录、云端、日志或证据中。
- 首个公网版本的性能基线未通过：PC→Railway 5次正常天气请求的Brain总耗时=`5.90/5.69/5.15/6.79/5.98s`，额外拆分样本暴露`intent=14.39s`长尾；该失败证据原样保留。最终冻结版50条服务器黑盒分为同城天气、十城天气、报时、明确陪聊和一般闲聊各10条，HTTP、语义、Brain/公网wall `<5s`与零动作均50/50；一般闲聊从改造前2/5升至10/10，wall P50/P95/max=`1.654/1.955/2.080s`。拔USB Release的Wi-Fi天气、正确报时和一般闲聊均现场通过；蜂窝因设备无流量由用户明确不测，记为未测试而非通过。`vision_only=false`、空授权阈值和VLM-disabled不变。
