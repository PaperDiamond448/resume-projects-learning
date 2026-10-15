# 海洋实验远程遥操作与教学平台

| 目录 | 内容 |
|---|---|
| python/ | 同一版本族的 Python 主控端与实验池从端 |
| web/ | Node.js 预约、认证和视频服务，端口 3002 |
| web_video_client/ | 旧版摄像头/FFmpeg 推流辅助端，独立参考 |

选 I 盘 OceanLab-Python 的完整主从代码，未混入接口不匹配的根目录旧启动文件。
摄像头曾实际接入；机械臂等是预留接口/对接模块，模拟反馈不能视为真实执行机构联调。

## Python 主端/从端

建议 Python 3.10+，本目录执行：

~~~text
python -m pip install -r requirements.txt
python start_master.py
python start_slave.py
~~~

主端默认 8000，附加端口 8887、8888，先检查占用。
python/remote/auth_config.json 已换成本地演示值：主密钥 demo-master-change-me，调试密钥 demo-debug-change-me。
演示密钥路径通过不等于 Web 预约验证通过，后者需沿 API 单独验证。

## Web 服务

需要 Node.js/npm，进入 web/：

~~~text
npm ci
python -m pip install cryptography
python generate_dev_certificate.py
npm start
~~~

访问 https://localhost:3002。开发证书为本机自签名；SQLite 数据库由服务初始化。
旧用户记录、数据库和证书私钥不上传，换设备后重新注册和预约。
旧穿透网址已改为 localhost；视频辅助端的 FFmpeg、摄像头名和 TLS 仍需按本机配置，不视为已验证的全链路发布包。

## 阅读顺序

python/main.py → gui/main_window.py → remote/server.py → client/client_app.py → remote/auth.py → web/server.js。
本次只做静态与语法检查，未重新连接摄像头、实验池或机械设备。保留的测试运行前应检查服务地址和设备要求。
