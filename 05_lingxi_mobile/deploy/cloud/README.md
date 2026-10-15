# Lingxi Brain · 本地安全容器与Railway Cloud-5 staging

当前状态：**Cloud-0～4已完成；Cloud-5 Railway服务器安全门、50条小样本`<5s`工程门和用户批准的拔USB Wi-Fi范围均已通过。部署`608e2211...f66b`=`SUCCESS`；同一Release通过天气、正确`11:37`报时和两句内冷笑话，证明不依赖ADB/PC Brain进程。蜂窝因设备无流量按用户决定未测试；Windows热点不证明电脑关机网络独立，故仍称VLM-disabled staging。精确`qwen2.5-vl-3b-instruct`既有自托管链可用但节点暂离线，不冒充截图GUI能力在线**。

`compose.local.yml`和`compose.secure-local.yml`继续只监听宿主机`127.0.0.1:8000`，供USB/adb reverse调试；`.railway/railway.ts`使用Railway当前IaC描述单服务、单副本、health、卷和非敏感门值，不自动创建公网域名，敏感值只用`preserve()`引用平台已有secret。`entrypoint.py`仅在Railway平台标识、显式批准、持久卷、单worker、业务secret和VLM-disabled全部成立时允许公网stage，随后从root降到UID999。Caddy示例仍未接入Compose，只作自管Linux备选。

## 文件

- `Dockerfile`：官方 Python slim、非 root、单进程；构建阶段执行 Cloud-2 契约，运行镜像不含 Android 源码。
- `Dockerfile.dockerignore`：排除缓存、构建产物、签名材料和 secret，只向临时验证阶段放行所需 Kotlin 文件。
- `requirements.lock.txt`：已验证的直接依赖版本。
- `compose.local.yml`：Cloud-1 兼容开发模式，仅限本机回环。
- `compose.secure-local.yml`：Cloud-2 必选安全模式；认证注册表以只读 secret 挂载，幂等 SQLite 位于持久卷。
- `entrypoint.py`：加载 file secret，强制阶段与安全模式匹配，并拒绝多 worker/未批准公网阶段。
- `manage_device_auth.py`：签发、轮换、吊销设备凭据；服务端只保存 SHA-256，不保存原始令牌。
- `audit_secure_runtime.py`：黑盒验证认证、主体、请求限制、一次性动作结果及跨重启防重放。
- `preflight.py`：无需 Docker 的静态安全检查。
- `Caddyfile.example`：后续 HTTPS 反代示例；当前不得上线。
- `RAILWAY.md`：Railway空项目、单卷、变量、上传、域名、真机验收与回滚顺序；真实secret不写入文档。
- 外层`.railway/railway.ts`/`.railwayignore`：当前Railway项目级IaC与上传排除表；嵌套Dockerfile通过`RAILWAY_DOCKERFILE_PATH`固定，真实secret只`preserve()`不落文件，也不会自动公开服务。旧`railway.json`已因平台退役时间表在首次staging前移除。

构建使用多阶段验证。临时`contract-verifier`现先跑Cloud静态预检，再运行`pending-action.3`29/29、standalone、限定Brain health、Cloud-2单元7/7/审计18/18、Cloud-3单元10/10/审计15/15、Cloud-5延迟契约18/18、Agent入口与独立预算7/7、入口策略18/18和Cloud-4网络审计11/11；最终镜像只继承通过标记，不含`/app/android`。

2026-08-20无缓存最终镜像digest=`sha256:f9b8dbceae63146f8ec7daaee70daae9b583d422bd74752d5370ab82a89622b2`。临时运行验证health、UID999和只读根均通过，Android源码、pyc和业务Key均为0；测试容器已删除。首轮干净构建曾发现Cloud-3审计暗中依赖主机DashScope Key，现已用显式离线假LLM修复，清空Key时仍15/15。

2026-08-22最新无缓存镜像digest=`sha256:969727e043a9e3e6ff4ffda535524b2b029389d22bd1951f661775ed79b419cf`；新增自托管VLM地址策略6/6与Cloud-4网络审计11/11。临时只读容器health、UID999、`pip check`通过，Android/pyc/业务Key均0，容器与专用卷已清理。

2026-08-22 Cloud-5 新IaC后的无缓存镜像=`sha256:ab873799dee2eaf89c313dbf9c5270bb6a1658a3f661ea5306ee5a71ad89cdbe`（Linux/amd64，138,631,273 bytes）。一次性Railway模拟以root挂卷启动，入口降权后的Uvicorn进程为UID999，动态PORT、`/live=200`、无认证`/ready=401`、注册表0600/UID999和运行镜像禁入文件0均通过，测试容器/卷已删除。

同日真实Railway服务器侧安全验收通过：IaC up to date，单活跃部署、500MB卷`READY`、域名`ACTIVE`且转发到Railway注入的`PORT=8080`。HTTPS最小存活、认证、生产docs关闭、canonical UUID、主体绑定、重放防护均通过，所有安全黑盒请求`device_action=null`；当前设备令牌短指纹=`4666273c20b8`，平台和卷只保存哈希。最终50条黑盒全部HTTP/语义/`<5s`/零动作通过。拔USB Release又通过Wi-Fi天气/报时/闲聊；准确结论仅为“不依赖ADB/PC Brain进程”，蜂窝未测、Windows热点不代表电脑关机网络独立。

经用户批准后，只在clean副本加入严格的移动端当前天气/时间确定性路由、任务确定性回执和4秒总墙钟硬截止；天气以3.5秒诚实截止、120秒成功缓存和“QWeather专属Host优先、否则高德Web Service”提供真实实况。一般纯聊天只有同时通过聊天线索、设备任务否定词与域路由三重闸才使用同一Qwen3-14B的紧凑单调用；出现短信、电话、提醒、导航等任务词仍回完整意图链。UTC容器报时修复版把`LINGXI_TIMEZONE=Asia/Shanghai`同时冻结在Railway IaC和入口门，查询时从UTC aware时刻转换；无缓存Linux/amd64镜像=`sha256:e8ea919766ad418ff471a03edbcc7a35045d0b5ae6b98eae6b688a6cdf7390ee`（138,642,673 bytes），18/18延迟契约、17/17入口策略、UTC→北京时间、零副作用和Android禁入均通过。Railway部署`608e2211...f66b`与公网认证报时复测通过。

2026-08-26修复云端Agent入口回归：普通问答继续维持4秒硬截止；明确点名酷狗、淘宝、京东等已知第三方应用且目标为搜索、播放、浏览或比价时，先由低风险白名单确定性建立`run_agent`，其后规划、单步决策与文本复核使用独立12秒单次预算。单纯打开应用仍走`open_app`，支付、购买、下单、购物车、发送和对外发布类指令不进入快捷入口；存在视觉证据时也保留完整融合链。精确3B节点离线的边界不变，故截图取价与视觉纠错仍不可宣称在线。

## 设备凭据

设备令牌不是 DashScope/QWeather/AMap Key。签发时原文只显示一次，之后注册表只保留哈希：

```text
python deploy/cloud/manage_device_auth.py \
  --file deploy/cloud/secrets/device_auth_registry.json \
  issue --subject <user-subject> --device-id <device-id>
```

同一命令的 `rotate`、`revoke`、`list` 子命令用于轮换、吊销和只读列举。不要把终端输出、注册表或任何 secret 提交进源码清单。

## secure-local 运行

在被 `.gitignore` 与镜像上下文排除的 `deploy/cloud/secrets/` 下准备：

- `device_auth_registry.json`
- `dashscope_api_key.txt`（中国区，供 Qwen3-14B 文本大脑使用）
- `qweather_key.txt`
- `amap_key.txt`

当前VLM默认关闭，不需要准备VLM secret。台式机或等价自托管节点恢复后，单独注入`LINGXI_VLM_API_KEY`（或其`_FILE`形式）并把`LINGXI_CLOUD_VLM_ENABLED`改为`true`；正式模型必须仍是`qwen2.5-vl-3b-instruct`。文本DashScope Key不得复用，且不会改用7B、第三方路由或其他替身模型。

随后从纯项目根执行：

```text
docker build -f deploy/cloud/Dockerfile -t lingxi-brain:cloud2-local .
docker compose -f deploy/cloud/compose.secure-local.yml \
  -p lingxi-cloud3-secure-local up -d --wait
```

公开 `GET /live` 只返回最小存活状态；`/ready`、`/health` 和所有业务接口都需要有效 Bearer。生产模式关闭 `/docs`、`/redoc` 与 OpenAPI。

黑盒审计通过环境变量临时读取测试令牌，不会打印令牌：

```text
set LINGXI_AUDIT_DEVICE_TOKEN=<temporary-device-token>
python deploy/cloud/audit_secure_runtime.py --mode full \
  --base-url http://127.0.0.1:8000
```

Android debug 可经 `adb reverse tcp:8000 tcp:8000` 使用本机 HTTP；release 连接策略只接受 HTTPS。设备令牌首次手动配对后由 Android Keystore AES-GCM 保护，界面只显示短哈希指纹，不回显令牌原文。

## Cloud-2 安全语义

- 除 `/live` 外全部认证；`/interact` 的 `user_id` 必须等于令牌绑定主体。
- 每个 JSON POST 必须带 canonical UUID `request_id`；命名空间为 `(subject, request_id)`，跨路径复用也拒绝。
- 请求在处理器前原子占位；响应只在幂等完成记录成功后释放。重放或复用返回 409 且 `device_action=null`。
- 注册表每次请求重读，吊销立即生效；令牌只做常量时间哈希比较。
- 默认请求体上限 8 MiB，只接受 JSON；错误媒体、超限、错令牌、错主体均 fail closed。
- 日志只记录路径、状态、延时、请求 ID 和主体/设备短哈希，不记录正文、号码、令牌、截图或生物特征。
- 状态未外置前固定单实例、单 worker；重启后的旧确认按 `ORPHAN` 拒绝，不恢复授权。

## Cloud-3 外部能力与隐私语义

- 云端截图同意与本机 MediaProjection 授权分离，默认关闭且可随时撤销。
- 每张图均绑定用途、策略版本、敏感度与 SHA-256；Android 和 Brain 双门校验，跨用途重放、摘要不符和非 PNG 均拒绝。
- 密码、验证码、支付、银行、证件和私聊等敏感页在手机本地硬停，不上传截图，也不退化为上传页面文字。
- 服务器 IP 不得冒充用户城市；天气缺城市时澄清，Key/服务失败时诚实不可用，不伪造天气。
- 云视觉固定精确 `qwen2.5-vl-3b-instruct`；既有LM Studio自托管链可用，当前节点离线时默认关闭。配置缺失或服务不可达时停止视觉动作，不执行设备操作。

## 强制边界

- 不把任何 secret 复制进镜像、APK、源码或日志。
- 不把端口映射改成 `8000:8000` 或 `0.0.0.0:8000:8000`。
- 不把部署阶段改成 production 来绕过启动门。
- 不增加 worker 或副本。
- Cloud-4最终真机与最新无缓存镜像复核已经完成；Cloud-5真实Railway服务只有在health、认证、重放、`<5s`性能和拔USB真机门全部通过后才可写成production。3B节点离线时只能发布明确标记的VLM-disabled档，不得冒充视觉能力可用。
