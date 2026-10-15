# 高风险待确认事务安全契约

> 版本：`pending-action.3`  
> 冻结日期：2026-08-07  
> 适用范围：独立项目目录内 Brain `/interact` 与 Android `device_action` 消费链。

## 1. 权威边界

- `D:\lingxi-mobile` 原目录是迁移前的只读参照，不因独立化整理而删、改或移动业务源码。
- `D:\lingxi-mobile\lingxi-project-clean` 是独立运行包及后续隔离验证的工作权威；它必须自包含，并通过本目录的清单和回归后才可交付。
- 两个目录不是可静默混用的“双主分支”。独立目录相对原目录的差异必须逐项声明；以后若在独立目录修改业务源码，必须另立任务、复测并更新 `SOURCE_MANIFEST.sha256`。
- 独立目录现已在用户授权下实施 B15：除既有 `not upgraded` 守卫外，还包含高风险 pending 状态机、确认来源与三类 ID、Android P0 封堵及视觉 shadow。原目录没有同步这些修复，继续只读。

## 2. 状态与适用动作

正式可建立高风险待确认事务的动作只有：

- `send_message`
- `make_call`

`send_email` 当前只打开系统邮件草稿，不建立待确认事务；不得因为常量中出现其名称就声称它已接入同一状态机。

高风险事务的正式决策状态为：

```text
NONE -> PENDING -> EXECUTED
               -> CANCELLED
               -> REFUSED
               -> EXPIRED
               -> INVALIDATED
               -> CLARIFY（唯一可保留 PENDING 的非终态）
```

`EXECUTED`、`CANCELLED`、`REFUSED`、`EXPIRED`、`INVALIDATED` 都是终态；同一事务不得再次消费。`CANCELLED` 是用户拒绝，`REFUSED` 是系统策略/能力拒绝，两者不能混记。

## 3. 冻结规则

1. **首次请求只建事务**：联系人、正文或电话对象齐全时建立 `PENDING`，本轮 `device_action=null`。
2. **事务参数不可原地修改**：待确认期间出现收件人、正文、号码等修改请求时，旧事务必须失效；用户需要重新说完整指令。不得静默保留旧参数后再允许一句“确认”执行。
3. **明确确认一次性消费**：仅接受“确认发送、确认执行、确认拨打、确认发出、可以发送、可以拨打、同意发送、同意拨打、发出去、发送吧、拨打吧、确认”等明确授权；成功后只下发一次原事务动作并清空 pending。
4. **明确口头拒绝优先**：出现“不要、不用、取消、别发、不发、不要打、算了、不确认”等明确拒绝时，立即取消、清空 pending、`device_action=null`；肯定视觉不能覆盖口头拒绝。
5. **双向冲突均不得执行**：
   - 口头拒绝 + 肯定视觉：取消；
   - 口头确认 + 否定视觉：取消并要求重新说完整指令；
   - 返回文案和 `conflict` 元数据必须说明真实原因，不得伪称“确认窗口超时”。
6. **含糊肯定只重问**：单字“要、好、嗯”或无法排除 ASR 截断的表达不构成授权；保持 pending、`device_action=null`，允许用户明确重答。
7. **新任务先使旧事务失效**：识别到不能解释为当前确认的肯定、拒绝、重试或上下文澄清的完整新意图时，必须先清空旧 pending，再处理新任务；“刚才说发给谁”等上下文查询只读取不可变快照，不修改也不机械清空 pending。
8. **过期、孤立和重复确认均不执行**：超过配置的 pending 生命周期、无 pending 的确认、以及已消费事务的再次确认都返回澄清，`device_action=null`。
9. **命名空间隔离**：GUI-agent 的 `_pending_app_task` 与高风险 `pending_action` 互不复活；已升级为 `run_agent` 的任务不得重新挂回旧 App pending。
10. **Android 只消费结构化动作**：Android 仅在 Brain 返回非空 `device_action` 时执行；大模型文案不构成授权，最终执行结果只由手机执行层回执产生。
11. **空证据不得发起确认请求**：Android 本地策略没有生成有效 `ConfirmationEvidence` 时，必须在端侧结束该次回调并恢复监听，不得向 Brain 发送 `text=""`、`confirmation_source="unknown"` 或携带旧 ID 的占位确认包。
12. **无 pending 的确认信封先于普通会话拒绝**：Brain 在不存在当前 pending 时，只要请求携带 `confirmation_source`、`pending_id` 或 `confirmation_window_id` 中任一确认信封字段，就必须在进入解析器、历史会话或大模型前 fail closed；已消费 ID 归因为 `REPLAY`，未知/矛盾来源归因为 `SOURCE_MISMATCH`，不得由对话历史重建短信或电话 pending。
13. **跨组件确认必须绑定所有者并只走一条请求**：Android 共享的 opaque binding 必须同时记录 `pending_id`、`confirmation_window_id` 与 `owner=activity/kws`。KWS 创建的 pending 只能由正在运行且持有同一未关闭窗口的 KWS 实例接收 typed 应答；Activity 正常发送入口和真机回归入口都必须先按 owner 分流。错 ID、旧版本未知 owner、服务已停或窗口已关一律不执行，也不得退回普通 typed 请求。成功交接只保存文字证据及其真实 `typed` 来源，待同窗视觉结束后组成唯一一条 `typed` 或 `typed_visual` 请求；不得先执行 typed、再发送一条旧 ID 的 vision-only 请求。
14. **来源元数据不得吞掉完整新任务**：KWS/Activity 在确认窗口中采到的 `spoken/typed/*_visual` 只说明证据来源，不等于用户在确认旧事务。若文本是明确、完整且不属于确认、拒绝、参数修改、重试或上下文澄清的新任务，Brain 必须先验证当前 pending/window/source 绑定；绑定有效时原子失效旧事务并按普通新任务路由，绑定错误仍 fail closed。视觉明确否定仍优先终止旧事务。

## 3.1 视觉单独授权决策（2026-08-03，结构已实现、生产授权仍为 shadow）

高置信、位于当前有效确认窗口、且明确绑定当前唯一 pending 的视觉肯定，可以单独授权短信或电话；它不是 Android 合成文本“确认”所代表的口头授权。

视觉单独授权还必须同时满足：

- pending 是完整、不可变、未过期的 `send_message` 或 `make_call` 快照，并拥有不可复用的 `pending_id`；
- 系统已针对该 pending 明确开启确认窗口；视觉事件在开窗后新产生、带起止时间、绑定同一 `pending_id`，且只消费一次；
- 事件来自 Android 本地感知白名单。当前候选仅 `nod`、`thumb_up`；`victory` 只有类别映射单测，没有独立视觉授权验证，暂不得作为高风险单模态授权手势；
- 视觉单独授权使用独立且严格高于普通辅助证据的授权门槛，不能复用普通识别阈值；具体数值只能从生产代码与验证数据冻结，不能凭空填写；
- 窗口内没有可信视觉否定、明确口头拒绝、参数修改、任务切换、事件跳变或质量降级；
- 无口头语义时标为 `vision_only`；确有可能改变授权语义的含糊口头内容时不得降格成“无语音”，只能澄清或使旧 pending 失效。

Android→Brain 显式传递互斥来源 `spoken/typed/vision_only/spoken_visual/typed_visual/unknown`。键入文本不得伪装成口头语音；字段缺失、来源矛盾或来源未知时，高风险动作不得执行。

KWS 与 Activity 只通过本机共享存储交换 opaque ID 和 owner，不共享联系人、短信正文、号码或生物特征。KWS 窗口内若同时出现 typed 与 spoken，应保留真实来源；任一明确拒绝压过另一通道的肯定，含义不确定的并发语音不得被 typed 肯定掩盖，必须 fail closed。

当前请求已携带 `pending_id/confirmation_window_id/confirmation_source`，视觉事件已携带 `event_id/window_id/stable_count/localized/quality` 并保留真实置信度。`nod/thumb_up` 的 vision-only 仍由 `vision_only_enabled=false` 与未配置授权阈值双重关闭；因此只能声称结构和 shadow 已接通，不能声称视觉单独授权已经上线。

## 4. 不可放宽项

- 模型分数、视觉肯定、用户画像或历史偏好都不能覆盖明确拒绝。
- 不能为了让历史 smoke 变绿而恢复“确认阶段直接改参数”的旧行为。
- 不能把“本轮没有外部动作”描述为“已发送、已拨打或已完成”。
- 视觉缺失、只有非授权类别、来源不明或授权门槛未满足时，不得由 Android 兜底合成“确认”并触发执行。
- 已取消、执行、过期或失效事务留下的迟到回调不得进入普通会话路由，也不得借助历史上下文复活旧动作。

## 5. 验证入口

```powershell
D:/python/python.exe -B brain/smoke_standalone.py
D:/python/python.exe -B brain/audit_pending_contract.py
```

第二条是契约审计器：全部通过才表示本契约在 Brain 侧闭合；发现失败时应保留失败，不得通过放宽断言掩盖。
