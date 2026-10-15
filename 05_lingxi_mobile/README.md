# 灵犀 Mobile：Android 多模态语音助手

选自包含的 lingxi-project-clean 版本，终端、大脑、核心模块、配置和必要模型一并保存，不依赖另一处旧 PC 源码。

- android/：终端语音/视觉输入、界面与设备动作。
- brain/：FastAPI 服务与编排。
- lingxi/core/：多模态帧、规则、域路由。
- configs/config.yaml、data/models/domain_router.pkl：配置与路由模型。
- deploy/cloud/：云端部署参考；docs/：范围与交互约束。

必要 ONNX/MediaPipe 模型与 JNI .so 已保留。APK、签名、SDK 本机路径、真人操作历史排除。
brain/data/app_*.example.json 提供空白结构，实际 app_*.json 在本地生成并忽略，手写攻略保留，新的操作经验由本地运行产生。

## 离线检查与服务

建议 Python 3.10+，在本目录执行：

~~~text
python -m pip install -r brain/requirements.txt
python -B brain/smoke_standalone.py
python -B brain/audit_pending_contract.py
~~~

这两个检查使用临时用户目录/模拟响应，不调用真实云模型或手机动作。
启动真实服务前按 .env.example 设置自己的 API 密钥及 VLM 地址，然后：

~~~text
python brain/server.py --port 8000
~~~

示例只说明变量；在 PowerShell 设置环境变量，或按启动方式加载 .env，不假定复制文件即可自动生效。

## Android

需要 Android Studio、JDK 17、SDK 34，最低 Android 8 / API 26。
补齐 Gradle 8.9 wrapper；打开 android/，生成本机 local.properties 指向 SDK，不提交该文件。

~~~text
cd android
gradlew.bat assembleDebug
~~~

Linux/macOS 用 sh gradlew assembleDebug。首次构建需下载 Gradle/Maven 依赖。
原生库主要面向 arm64，模拟器 ABI 需另行核对；调试包无需原发布签名。
手机权限、无障碍、厂商后台策略和服务地址按新设备配置，本次未重做真机动作与完整 Android 打包。
先读 docs/PROJECT_SCOPE.md 与 docs/SAFETY_CONTRACT.md，再沿 brain/server.py → 核心帧/路由 → Android 请求/动作学习。
