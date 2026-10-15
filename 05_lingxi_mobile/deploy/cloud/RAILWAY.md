# Lingxi Brain · Railway Cloud-5 staging

本档用于把已经通过 Cloud-0～4 的 Brain 部署到 Railway 托管容器。它不会替代 USB 本地调试：Debug APK 仍可经 `adb reverse` 连接电脑；Release APK 使用 Railway 自动签发的 HTTPS 地址，手机联网即可访问。

## 当前边界

- 这是 Cloud-5 **staging**，不是尚未验收的 production 声明。
- 固定单服务、新加坡区单副本、单 worker；挂载卷后 Railway 本身也不允许多 replicas。首次创建时若平台默认落到其他区，只能在卷仍无业务数据时经IaC plan迁到`asia-southeast1-eqsg3a`。
- 精确 `qwen2.5-vl-3b-instruct` 的既有自托管节点当前离线，因此 `LINGXI_CLOUD_VLM_ENABLED=false` 必须保持不变；本档不换模型、不冒充 GUI 截图能力在线。
- Railway 自动注入 `PORT`，应用只监听 `0.0.0.0:$PORT`；`/live` 是唯一公开无认证探针。
- Railway 持久卷挂载到 `/app/brain/data`，保存设备注册表、幂等 SQLite 和本机画像。设备注册表只含令牌 SHA-256，不含令牌原文。
- Railway 的卷初始属于 root；平台变量 `RAILWAY_RUN_UID=0` 只让入口完成卷校验和 ownership 修复。入口随后强制降权到镜像内 UID 999，再启动 Uvicorn。
- 业务 Key 只通过 Railway Variables 注入，不写入源码、镜像、APK、日志或本文件。
- Railway 已宣布旧 `railway.json` / `railway.toml` 于 2026-12-01 停止读取；本项目在首次 staging 前已迁到 `.railway/railway.ts`。IaC 管理单服务、单副本、health 和卷，敏感变量仅用 `preserve()` 保留平台值；嵌套 Dockerfile 由 `RAILWAY_DOCKERFILE_PATH=/deploy/cloud/Dockerfile` 明确选择。

## 2026-08-22 真实staging状态

- Railway服务器侧安全门与小样本性能工程门均已通过：IaC up to date、单活跃部署、500MB卷`READY`、HTTPS域名`ACTIVE`；VLM仍关闭。真机抓出的UTC报时失败已通过显式`LINGXI_TIMEZONE=Asia/Shanghai`和UTC容器回归修复；部署`608e2211...f66b`=`SUCCESS`、平台镜像`sha256:4dd6d4e2...53ac`，公网报时与北京时间误差0分钟、wall=`1.265s`、零动作。既有50条固定黑盒全部HTTP/语义/`<5s`/零动作通过；一般闲聊wall P95=`1.955s`，冷缓存天气真实后端19/20，后端波动按契约诚实降级。最终候选已包含4秒硬截止、云端禁同轮修复/二次闲聊、天气3.5秒截止/成功缓存、高德实况降级链和安全纯聊天紧凑单调用，并通过36/36预检、18/18延迟契约、17/17入口策略和无缓存Linux运行模拟。该N=10/组小样本不是总体正式P95，也不是Android端到端结论。
- 首次域名创建若手工写`--port 8000`会与平台实际注入的动态`PORT=8080`错配并返回502。当前域名已依据启动日志改为转发8080；后续不得凭本地默认端口猜测，必须以部署日志中的Uvicorn监听端口为准。
- `/live=200`且仅返回`status`；无/错令牌`/ready=401`、正确令牌`/ready=200`、`/docs=404`。
- 无效UUID、主体错配、原样重放、同ID改写和跨真实重部署重放分别得到`INVALID_REQUEST_ID / SUBJECT_MISMATCH / IDEMPOTENT_REPLAY / REQUEST_ID_REUSE / IDEMPOTENT_REPLAY`，均`device_action=null`。
- 当前手机凭据短指纹=`4666273c20b8`；原始令牌只保存在clean目录外的Windows DPAPI密文交接中。手机配对成功后才能删除该本机密文。
- 服务器安全与小样本性能通过不自动等于全部手机网络已验收。当前Release已在断USB/PC Brain后通过Wi-Fi天气、报时和闲聊；蜂窝因设备无流量按用户决定未测试，必须在声明中保留，不得冒充通过。

## 首次部署顺序

1. 注册并登录 Railway，建立空项目和空服务；先不要生成公网域名。当前 staging 服务名固定为 `lingxi-brain-staging`。
2. 给服务附加一个 volume，mount path 必须精确为 `/app/brain/data`。不要启用 replicas 或 serverless/sleep；`.railway/railway.ts`必须与平台现状做无破坏性 `railway config plan` 后才 apply。
3. 在本机被 `.gitignore` 排除的 `deploy/cloud/secrets/` 中，用 `manage_device_auth.py issue` 签发一枚全新的设备凭据。令牌原文只保存到手机，注册表只保留哈希。
4. 把注册表 JSON 做标准 Base64，作为 `LINGXI_DEVICE_AUTH_REGISTRY_B64` 写入 Railway Variables。它是 hash-only 启动种子；入口会原子写入持久卷，绝不把令牌原文写入云端注册表。
5. 在 Railway Variables 配置下面的必需项。真实值只能在 Railway 控制台或受保护的 CLI 会话中输入，不得发到聊天、命令日志或源码：

```text
LINGXI_DEPLOYMENT_STAGE=railway
LINGXI_SECURITY_MODE=required
LINGXI_PUBLIC_DEPLOYMENT_APPROVED=railway-cloud5-vlm-disabled
LINGXI_TIMEZONE=Asia/Shanghai
LINGXI_WORKERS=1
LINGXI_LLM_REQUEST_TIMEOUT_SECONDS=4.5
LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS=12.0
LINGXI_MAX_REQUEST_BYTES=8388608
LINGXI_DEVICE_AUTH_FILE=/app/brain/data/security/device_auth_registry.json
LINGXI_IDEMPOTENCY_DB=/app/brain/data/security/idempotency.sqlite3
LINGXI_MOBILE_PROFILE_DIR=/app/brain/data/profiles
LINGXI_CLOUD_VLM_ENABLED=false
LINGXI_VLM_MODEL=qwen2.5-vl-3b-instruct
RAILWAY_DOCKERFILE_PATH=/deploy/cloud/Dockerfile
RAILWAY_RUN_UID=0
DASHSCOPE_API_KEY=<Railway secret>
QWEATHER_KEY=<Railway secret>
AMAP_KEY=<Railway secret>
LINGXI_DEVICE_AUTH_REGISTRY_B64=<hash-only registry base64>
```

`PORT`、`RAILWAY_PROJECT_ID`、`RAILWAY_ENVIRONMENT_ID`、`RAILWAY_SERVICE_ID`、`RAILWAY_DEPLOYMENT_ID` 和 `RAILWAY_VOLUME_MOUNT_PATH` 必须由 Railway 注入，不手工伪造。入口会核验它们。

普通问答使用4.5秒硬截止；只有已通过“明确点名的已知第三方应用＋安全应用内动作”白名单入口的 Agent 规划、单步决策和动作复核使用12秒单次预算。支付、下单、发送和对外发布类目标不进入该快捷入口；两个预算都由启动门精确核验，不能在平台控制台静默改写。4.5秒是针对Railway到模型服务尾延时的临时止血值，完整手机单轮仍须单独测量，不据此自动宣称全部样本低于5秒。

6. 先运行 `railway config plan`；只允许看到既定服务、500 MiB单卷、单副本、`/live`与非敏感门值的预期变更，不允许服务/卷/变量删除。确认后执行 `railway config apply`，再从 `lingxi-project-clean/` 作为上传根执行 `railway up`。Railway CLI 会上传当前 clean 副本；`.railwayignore` 和 `.gitignore` 会排除本机 secret、签名材料、`node_modules`、构建缓存与运行数据。
7. 只有构建、`/live` 健康门、UID 999 和持久状态检查全部通过后，才运行 `railway domain` 生成 `https://*.up.railway.app` 地址。
8. 先用错令牌、无令牌、重复 request ID、重启重放做负测，再把新地址和同一枚设备令牌配置到 Release APK。
9. 服务器工程门关闭后，删除 `adb reverse`、关闭笔记本 Brain，在手机蜂窝网络和普通 Wi-Fi 各做完整回归与正式延迟采样。未通过前保留 USB 路径，不把 staging 写成“已经完全脱离电脑”。

## 回滚

- 客户端把 Brain 配置切回原 HTTPS staging 地址或在 Debug APK 中恢复本地 `adb reverse`；Release 不允许降级到 HTTP。
- Railway 部署失败时不生成域名；已上线版本失败时回滚到上一镜像，设备令牌不变。
- 若注册表、幂等数据库或卷不可读，服务必须停止，不能转成无认证模式或清空防重放账本继续运行。
- 3B节点恢复前不得把 `LINGXI_CLOUD_VLM_ENABLED` 改为 `true`；恢复后仍需另做精确模型、HTTPS端点、独立令牌和真机隐私门验收。
