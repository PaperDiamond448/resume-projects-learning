# B08 · 独立包安全契约与回归一致性审计

> 日期：2026-08-07  
> 结论：**B08 自动化闭环保持关闭；B15 的取消后迟到回调竞态、typed→KWS 同窗交接及确认窗口内完整新任务路由均已通过对应修复门。** 修复仍只发生在独立目录，原目录业务源码继续未动。当前无模型契约审计 `29/29`、Android JVM `70/70`、standalone smoke、Python compileall 与 debug 构建通过。P01/MI 8 Lite terminal-gold canonical 为23/24；r5 CALL-C09 的安全失败原件保留，Brain-only 修复须升 r6 后重采最后一格。Android/APK、模型、视觉阈值与 `vision_only` 关闭状态均未改变。

## 1. 审计输入与权威声明

- 冻结契约：`SAFETY_CONTRACT.md`
- 独立契约审计器：`brain/audit_pending_contract.py`
- 原/独立扩展 smoke：两份 `brain/smoke_server.py` 当前逐字节相同。
- 原目录是迁移前只读参照；独立目录是独立运行包与后续隔离验证的工作权威。不得从两个目录混合取文件运行。
- 独立目录的 `brain/server.py` 除自包含加载改造和既有 `not upgraded` 守卫外，现包含 B15 高风险状态机；Android 独立源码包含来源/ID 契约与 P0 封堵。上述差异均未写回原目录。

## 2. 单一安全契约摘要

短信与电话只允许：首次建立不可变 pending；明确确认一次性消费；明确拒绝优先；任一否定冲突不执行；含糊肯定重问；参数修改、新任务、过期、重复或孤立确认使旧事务失效；Android 只消费非空 `device_action`。模型输出不得覆盖这些规则。

## 3. 旧 `smoke_server.py` 全量分类

分类只判断“断言是否仍代表当前产品契约”，不把依赖真实 API 的测试冒充离线确定性测试。

| 测试 | 分类 | 说明 |
|---|---|---|
| `test_health` | `VALID_CURRENT_CONTRACT` | 服务存活与配置加载 |
| `test_interact_query_time` | `VALID_CURRENT_CONTRACT` | 真实 14B 查询链，需 API |
| `test_multiturn_ellipsis_weather` | `VALID_CURRENT_CONTRACT` | 低风险多轮省略，需 API |
| `test_multiturn_correction_recipient` | `OBSOLETE_EXPECTATION` | 旧断言允许在高风险 pending 内改收件人；现冻结为不可变事务 |
| `test_multiturn_correction_content` | `OBSOLETE_EXPECTATION` | 旧断言允许在高风险 pending 内改正文；现冻结为不可变事务 |
| `test_multiturn_correction_reminder_time` | `VALID_CURRENT_CONTRACT` | 非高风险闹钟多轮修正，需 API |
| `test_interact_send_confirm` | `VALID_CURRENT_CONTRACT` | 两段式确认主路径，需 API |
| `test_agent_reflect_no_change` | `VALID_CURRENT_CONTRACT` | GUI-agent 无变化反思 |
| `test_agent_plan_not_contaminated_by_hints_example` | `VALID_CURRENT_CONTRACT` | 规划提示防污染，需 API |
| `test_reflect_with_screenshot_falls_back_to_text_llm` | `VALID_CURRENT_CONTRACT` | VLM 故障降级 |
| `test_agent_reflect_text_only_survives_vlm_outage` | `VALID_CURRENT_CONTRACT` | 文本反思降级，需 API |
| `test_demo_to_skill_docs` | `VALID_CURRENT_CONTRACT` | 教学记录转技能 |
| `test_trace_to_skill_and_avoid_docs` | `VALID_CURRENT_CONTRACT` | 轨迹转成功/避坑技能 |
| `test_extract_pick_parsing` | `VALID_CURRENT_CONTRACT` | 抽取解析 |
| `test_agent_extract_endpoint` | `VALID_CURRENT_CONTRACT` | 抽取接口 |
| `test_agent_plan_multi_shopping_compare` | `VALID_CURRENT_CONTRACT` | 多 App 购物规划 |
| `test_agent_plan_multi_travel_compare` | `VALID_CURRENT_CONTRACT` | 多 App 出行规划 |
| `test_pending_app_task_merge` | `REAL_REGRESSION` | 断言有效；原目录会重挂旧 pending，独立目录已有 clean-only 修复 |
| `test_pending_app_task_expiry` | `VALID_CURRENT_CONTRACT` | App 澄清 pending 老化 |
| `test_confirmation_retry_does_not_execute` | `VALID_CURRENT_CONTRACT` | 重开采集不授权 |
| `test_confirmation_copy_matches_machine_status` | `VALID_CURRENT_CONTRACT` | 文案与机器状态一致 |
| `test_orphan_confirmation_never_claims_completion` | `VALID_CURRENT_CONTRACT` | 孤立确认零动作 |
| `test_expired_high_risk_confirmation_never_claims_completion` | `VALID_CURRENT_CONTRACT` | 过期确认零动作；旧测试只测 helper，覆盖不足 |
| `test_deterministic_mobile_message_pending` | `VALID_CURRENT_CONTRACT` | 首轮稳定建 pending 且零动作 |
| `test_mobile_pending_resets_external_age` | `VALID_CURRENT_CONTRACT` | 新事务计龄归零 |
| `test_high_risk_refusal_blocks_after_nod` | `VALID_CURRENT_CONTRACT` | 明确拒绝优先于肯定视觉 |
| `test_high_risk_clipped_refusal_never_executes` | `VALID_CURRENT_CONTRACT` | ASR 截断含糊肯定只重问 |
| `test_high_risk_asr_refusal_variant_cancels` | `VALID_CURRENT_CONTRACT` | 拒绝同义表达取消 |

初审汇总：`VALID_CURRENT_CONTRACT=25`、`OBSOLETE_EXPECTATION=2`、`REAL_REGRESSION=1`、`REQUIRES_PRODUCT_DECISION=0`。B15 冻结来源/ID 后，旧 `test_interact_send_confirm` 的“无来源即可执行”和 `test_orphan_confirmation_never_claims_completion` 的旧 `status=clarify` 也成为历史接口断言；当前分类相应为 `VALID_CURRENT_CONTRACT=23`、`OBSOLETE_EXPECTATION=4`、`REAL_REGRESSION=1`。旧文件继续原样保留，不为全绿改写历史。

## 4. B15 修复前的新回归矩阵与本地结论（历史基线）

实际执行 `brain/audit_pending_contract.py`：`total=12`、`passed=7`、`failed=5`，进程按设计返回 1；失败不是测试环境或外部 API 故障，而是下表所列现行源码行为与冻结契约不一致。

| 场景 | 冻结预期 | 当前结论 |
|---|---|---|
| 明确确认 | 一次下发原 pending，随后清空 | 已成立 |
| 明确拒绝 | 清空、零动作 | 已成立 |
| 口头拒绝 + 视觉肯定 | 拒绝优先、零动作 | 已成立 |
| 口头确认 + 视觉否定 | 取消、准确标记冲突、零动作 | **不符合**：零动作成立，但被改写成“确认窗口超时”，`conflict=false` |
| ASR 截断“要” + 肯定视觉 | 重问、保留 pending、零动作 | 已成立 |
| 过期确认 | 清空、澄清、零动作 | 已成立（通过显式计龄触发） |
| 孤立/重复确认 | 澄清、零动作 | 已成立 |
| 改收件人 | 旧 pending 失效，要求完整重说 | **不符合**：旧收件人 pending 仍保留 |
| 改正文 | 旧 pending 失效，要求完整重说 | **不符合**：旧正文 pending 仍保留 |
| 新任务打断 | 先清旧 pending，再执行新任务 | **不符合**：相机已打开，但旧短信仍悬挂 |
| `run_agent` 旧 App pending | 升级后不得重挂 | 独立目录已成立；原目录仍缺 clean-only 守卫 |
| Brain/核心高风险集合 | 短信、电话口径一致 | **不符合**：适配层包含短信/电话，核心配置未包含，导致双向冲突走不同规则 |
| Android 消费 | 仅非空 `device_action` 执行 | 静态链成立；最终仍依赖 Brain 正确收口 |

## 5. B08 初审停止结论（历史）

这些不符合项都需要改 `brain/server.py`、随附核心配置或意图后置规则，已经超出 B08 首轮“只写文档、夹具和断言”的授权。因此：

- 没有修改原目录或独立目录的业务源码；
- 没有放宽明确拒绝安全规则；
- B08 保持 `BLOCKED`，不能写成通过；
- 另立 `B15 · 高风险 pending 状态机修复`，只在用户授权后修改独立目录，原目录继续不动。

## 6. 复现命令

在 `D:\lingxi-mobile\lingxi-project-clean` 运行：

```powershell
D:/python/python.exe -B brain/smoke_standalone.py
D:/python/python.exe -B brain/audit_pending_contract.py
```

第一条必须通过；第二条在 B15 修复后必须全部通过。当前结果分别为 `PASS` 与 `29/29 PASS`；Android JVM `70/70`、`assembleDebug` 同时通过，因此 B08 的自动化关闭条件继续满足。P01 单设备真机安全矩阵也已完成；视觉单独授权阈值因证据不足保持 `null`，不以降阈值制造放量结论。

## 7. 冻结哈希与清单结果

| 对象 | SHA-256 |
|---|---|
| 原 `brain/server.py` | `74CC7EFF7AB4840FEA5D0D5E5AEF91E183517740B5EB0EA345E6F1A26251BCE7` |
| 独立 `brain/server.py`（B15 + 确认信封/完整新任务守卫） | `A3BA9E76B35A47FC70A23E338B10E92698CE1EA3A4C88082228940C496C927E0` |
| 原/独立 `brain/smoke_server.py` | `EB63C119A5BE9EFF00A366F4B32242513FE778C0F2672F66AD594DACF3D198E2` |
| 原/独立 Android `DeviceActionExecutor.kt` | `B637A73F6FBC08F11E12C737FC214C333B5418F20676F9E43E2C566B660BABAB` |
| 原 Android `MainActivity.kt` | `1004C1697BE3DED76D7DD937688378800792ABD638DE56B25C50835424E02F6F` |
| 独立 Android `MainActivity.kt`（B15+r3 KWS状态同步） | `9C43A719654154B39CE71A40317A2A445C9FA3CF57CA579BB766A1ABBB489BBD` |
| 原 Android `KwsForegroundService.kt` | `C53EF6BD73244EA677A65B874C1617A5FB8A157A89F2F555B2FCEF2B6BA9BE24` |
| 独立 Android `KwsForegroundService.kt`（B15 + 空证据端侧短路） | `EC376500CA4A83645B1BF26F02B163939154AA69FCB837194956847C9AF89CC3` |
| `SAFETY_CONTRACT.md` | 以本轮重冻后的 `SOURCE_MANIFEST.sha256` 为准 |
| `brain/audit_pending_contract.py` | 以本轮重冻后的 `SOURCE_MANIFEST.sha256` 为准 |

`SOURCE_MANIFEST.sha256` 在 2026-08-07 重冻后覆盖 173 个项目本体文件（清单自身除外）；运行时 `brain/data/profiles/`、`__pycache__`、`.pyc`、`build/` 与 `.gradle/` 明确排除。逐项复核要求仍为哈希失败、未列源码和陈旧条目均为 0。

## 8. 视觉单独授权 A 决策与本地反查

外部评审已明确选择 A：在唯一未过期 pending、有效确认窗口、可信来源、独立授权阈值、时序绑定、一次性消费且无任何拒绝/修改证据时，视觉肯定可以单独授权；同时要求显式记录真实确认来源。该策略已写入契约，但本地现状尚不满足。

| 项目 | 当前本地事实 | 契约判定 |
|---|---|---|
| 请求来源字段 | Android/Brain `InteractRequest` 只有 `text/vision/speech_t_start/speech_t_end` 等字段，没有 `confirmation_source` | 缺失 |
| pending 绑定 | pending 没有 `pending_id/version`，视觉事件也不携带绑定 ID | 缺失 |
| UI 确认窗 | MainActivity 自动确认窗为 `3000ms` | 已有常量，尚未绑定 pending ID |
| KWS 确认窗 | 最长语音采集 `8000ms`，endpoint 后视觉延窗 `2000ms`；相机兜底总窗为 `8000+2000+500ms` | 已有两套时序，不能笼统写成单一 3 秒 |
| 手势筛选 | MediaPipe 原分数只要求 `>=0.5`，最高票至少2帧，随后上传置信度被固定写成 `0.9` | 不是独立授权阈值，且丢失真实分数 |
| 头动筛选 | 至少5样本、幅度≥7°、路径≥12°、至少1次反转、主轴比≥1.25；置信度公式约束到0.45～0.97 | 有识别门槛，但没有独立授权阈值 |
| 授权白名单证据 | 真机正式证据覆盖 `thumb_up`、`nod` 的时序/拒绝安全链；`victory` 只有映射单测 | `victory` 暂不得授权 |
| 事件生命周期 | 每次 `ConflictVisionCapture.start()` 清空样本/票数，`stop()` 单次回调并关闭采集 | 有窗口内一次采集，但无事件ID、pending绑定或跨请求防重放字段 |
| 用户/多人归属 | MediaPipe 配置只取1张脸、1只手，没有用户身份或多人干扰质量标记 | 不满足来源归属要求 |
| ASR质量 | Android 能在合成前区分空/非空文本，但请求没有 ASR 置信度或“无可用语义”字段 | 只能部分区分 |
| 键入确认 | MainActivity 允许窗口内键入“确认/不要” | 三值来源枚举不足，还需保真 `typed` |

### 新发现的 P0 安全缺口

KWS 的 `onConfirmMarks` 在无真实口头文本时，会依据视觉标签合成文本；但 `head/gesture` 都不命中时仍走 `else -> "确认"`。因此相机失败或没有稳定授权动作时，`marks=[]` 也可能被送成显式“确认”；只有 expression 标记时同样可能落入“确认”。MainActivity 虽会在 `marks.isEmpty()` 时停止，但 expression-only 仍存在同类兜底。

由于 Brain 当前不知道文本是 ASR 原话还是 Android 合成，这类输入可能沿显式确认路径消费高风险 pending。该问题不在原5项失败中，现追加为 B15 的 P0 回归项；本轮按用户要求只记录，不修改源码。

### B15 边界变化

B15 不再只是 Brain 内部5项修复，还必须覆盖 Android→Brain 来源保真、pending/事件绑定、视觉单独授权门槛和空/非授权视觉 fail-closed。正式修改前还需把 UI 键入来源纳入字段设计；不能用 `spoken` 冒充 typed，也不能继续用合成“确认”掩盖 `vision_only`。

## 9. B15 实施结果（更新至 2026-08-04）

- P0 已封堵：MainActivity 与 KWS 共用纯 Kotlin `ConfirmationEvidencePolicy`；空 marks、expression-only、`victory`、未知视觉均不能生成肯定确认。
- 来源已保真：`spoken/typed/vision_only/spoken_visual/typed_visual/unknown` 六值落入 Android→Brain 请求，`unknown`、缺失或矛盾 fail-closed。
- 三类 ID 已分责：Brain 生成并原子消费 `pending_id` 与 `confirmation_window_id`；Android 每个视觉事件生成 `event_id` 并绑定窗口。MainActivity/KWS 只共享 opaque ID，不落联系人、正文、号码或生物特征。
- 状态机已确定化：参数变化返回 `INVALIDATED`；完整新任务先清旧事务再处理；上下文查询只读快照；用户拒绝、系统拒绝、超时、失效、澄清分开归因；孤立与重放均零动作。
- 视觉证据不再固定写成 `0.9`：手势上传获胜类别的真实平均分、票数和时间范围；头动保留真实置信度、样本数和 localized 标记。
- `vision_only` 仍是 shadow：配置 `vision_only_enabled=false`、`visual_authorize_threshold=null`；没有真机正负样本和独立授权阈值前，合格点头/竖拇指也只记录不执行。可靠视觉否定仍可安全取消。
- 自动证据：`brain/audit_pending_contract.py=26/26`，包含双请求原子竞争只执行一次，以及取消后迟到 `unknown` 确认信封不得重建 pending；Android JVM=`63/63`，standalone smoke、`assembleDebug=PASS`。
- 真机安全矩阵：r2 签名 APK（SHA=`91E5C750...F1A9F3`）在 P01/MI 8 Lite 上完成 canonical 16/16 格；精确 API 交换证明冲突、拒绝、参数修改、新任务、轮次超时和重复确认均符合原因码/一次性消费契约，无非预期短信或电话动作。失败、污染、断线轮全部保留并从 canonical 选择中显式排除。
- 阈值决策：三次 `thumb_up` support 分数为 `0.7073/0.7357/0.7766`，无法支撑严格高于 `0.75` support 门的独立授权阈值；因此 `vision_only_enabled=false`、`visual_authorize_threshold=null` 保持不变。该结论不是识别率或跨用户安全证明。
- r3 KWS 状态同步：MainActivity 在派发 start/stop 前同步用户请求状态，并用纯逻辑策略处理持久化请求/真实 Service 四种组合；新增4项测试。r3 APK SHA=`BB2D5C4D...9DA28B3`、同一 RSA-4096 证书，更新安装后 KWS 前台服务持续超过33秒，UI 与真实生命周期一致；屏幕视觉、无障碍、KWS 三服务同时在线。
- 原目录未动：`brain/server.py`、`MainActivity.kt`、`KwsForegroundService.kt`、`Api.kt` 仍分别为 `74CC7EFF...51BCE7`、`1004C169...2F6F`、`C53EF6BD...9BE24`、`156DC0EB...0CE5`。

## 10. 2026-08-07 取消后迟到回调竞态与修复

P01-S02 的 CALL-C02 暴露了此前矩阵未覆盖的跨组件竞态：用户键入“不要拨打”后 Brain 已返回 `CANCELLED/EXPLICIT_TEXT_REJECTION` 并清空 pending，但 KWS 已启动的采集稍后仍以 `text=""/confirmation_source="unknown"` 和旧 ID 回调；Brain 的旧孤立守卫只识别显式确认文本，空文本进入普通对话路由后被历史上下文错误重建为新的 `make_call` pending。该轮没有 `device_action`，没有真实拨号，但违反终态不可复活契约。

独立目录已实施双端封堵：Android 在 `ConfirmationEvidencePolicy.build(...)` 返回空时本地短路并重新读取共享 pending 状态，完全不发请求；Brain 则把任一来源/ID 字段视为确认信封，在普通解析和大模型之前按 `REPLAY/SOURCE_MISMATCH/ORPHAN` 确定性拒绝。新增精确竞态回归后，修复前红证据为 `24/26`、修复后为 `26/26`。CALL-C02 失败原件保留但不进入 canonical；恢复采集前必须使用正式证书签名的新 APK 在同一真机重放“建 pending→文字取消→保持静止并等待迟到回调”。

## 11. 2026-08-07 B15-B：KWS pending 的 typed+visual 单请求交接

CALL-C03 r4 轮真实采到 `thumb_up`（support=`0.69249`、stable=17、DURING），typed“确认拨打”也只产生一次 `make_call` 并安全打开拨号盘；但 API 证据显示执行请求只有 `typed`，随后 KWS 把视觉另发为旧 ID `vision_only` 并被 `REPLAY`。这证明安全守卫有效，却也证明旧产品路径无法产出目标 `typed_visual`，该轮按 `SAFE_TARGET_NOT_MET_PRODUCT_PATH_UNREACHABLE` 保存且不计 canonical。

最小修复只改 Android 确认编排：`PendingConfirmationStore` 新增 `owner=activity/kws`；MainActivity 的用户输入和 adb 回归输入都在普通 typed 路径之前分流；`TypedConfirmationHandoffPolicy` 要求 stored/requested/active 的 pending 和 window 完全一致、KWS 服务运行且窗口打开；KWS 保存 typed/spoken 两类真实证据并在同窗视觉回调后只构造一次来源保真的请求。明确拒绝跨通道占优，含糊并发语音不能被 typed 肯定覆盖。Brain、模型、视觉阈值和动作执行器未改。

新增 7 项纯策略测试和 1 项静态契约项后，本地结果为契约 `27/27`、Android JVM `70/70`、standalone smoke 与 debug/release 构建通过。正式 r5 APK SHA=`80E474D2...686E91`与真机base.apk一致；非canonical修复门精确形成`NEW_PENDING → typed_visual/executed/make_call`两条设备请求，真实`thumb_up`为0.7973/16帧/DURING，动作一次、响应ID全空，继续等待15秒无迟到请求，主动重放旧信封返回`refused/REPLAY/device_action=null/IDs=null`。因此B15-B产品修复门关闭；修复轮不计canonical，CALL-C03/C04只在r5冻结后恢复。

## 12. 2026-08-07 B15-C：确认窗口内完整新任务路由

r5 CALL-C09 的 ASR 精确收到“查询明天杭州天气”，请求携带当前电话 pending/window 和 `confirmation_source=spoken`、无视觉；旧 Brain 因把来源字段直接当成确认意图，错误返回 `pending_confirmation/AMBIGUOUS_CONFIRMATION` 并保留旧事务。该轮零设备动作、无真实拨号，但未满足“新任务先失效旧事务”的契约，故原件保留且不计 canonical。

最小修复只改独立 Brain：先确定文本是否为明确完整新任务，再校验 pending/window/source 信封；绑定有效时清旧事务并走既有普通路由，绑定错误仍保持 pending 并返回具体 mismatch。新增真实 C09 回归及错 ID 反证后，红测 `28/29` 转为 `29/29`；standalone smoke、Python compileall、Android JVM `70/70` 与 debug 构建通过。重启后的真实 HTTP 重放为 `NEW_PENDING → query_weather/executed/NEW_TASK`，天气回复正常、两步均无设备动作、最终 ID 空；错 pending ID 仍为 `PENDING_ID_MISMATCH`，随后显式取消清理。Android/APK、动作执行器、模型、阈值和原目录均未改。
