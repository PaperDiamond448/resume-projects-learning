# 五个简历项目学习合集

个人私有学习与跨设备备份仓库。按当前简历中的五个项目整理，每个项目保留一套明确的代码入口、必要依赖和学习材料。

| 目录 | 项目 | 内容 |
|---|---|---|
| [01_beacon_firmware](01_beacon_firmware/) | 水下短基线信标固件 | F427 参考固件、F103 练习、F407 点灯工程 |
| [02_lfm_detection](02_lfm_detection/) | 水下测姿 LFM 信号检测算法移植 | MATLAB → 可移植 C → PC 对照验证 |
| [03_infrared_super_resolution](03_infrared_super_resolution/) | 红外图像超分辨率重建 | 最终 HiT-SRF 代码与权重 |
| [04_oceanlab_teleoperation](04_oceanlab_teleoperation/) | 海洋实验远程遥操作与教学平台 | Python 主端/从端 + Web 预约服务 |
| [05_lingxi_mobile](05_lingxi_mobile/) | 灵犀 Mobile：Android 多模态语音助手 | Android 终端 + Python 大脑服务 |

## 换设备继续学习

1. 登录有此私有仓库访问权限的 GitHub 账号，克隆整个仓库。模型和信号文件已保留，不依赖原电脑的盘符。
2. 进入对应项目，先读各自 README，再安装依赖。Python 项目建议分别创建虚拟环境。
3. 先运行离线检查，再连接开发板、摄像头或手机。API 密钥、Android SDK 路径、设备编号和本地证书需要在新设备配置。
4. 修改后提交和推送，即可在另一台设备同步；生成目录和本地配置已有忽略规则。

内容约 189 MB，主要二进制是必要模型、原生运行库和 LFM 原始信号。使用普通 Git，无 Git LFS 或子模块。

## 导航

- [学习路线](docs/学习路线.md)
- [选版和整理说明](docs/整理说明.md)
- [实际验证记录](docs/验证记录.md)
- [第三方来源与许可](THIRD_PARTY_NOTICES.md)
- [文件校验清单](docs/文件校验清单.sha256)

信标和 LFM 来源于课题组工程，另三个是已有个人项目。阅读、练习和实物复现的进度应分别记录；存在某项代码不代表相应实物联调已经完成。
