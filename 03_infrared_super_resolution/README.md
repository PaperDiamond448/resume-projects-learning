# 红外图像超分辨率重建

选第二阶段最终提交包的 HiT-SRF 灰度适配版本，保留训练、预处理、模型、推理与权重。
未沿用外层早期 README 的旧模型描述，学习入口不以排名组织。

- src/infer.py：推理入口；src/model_hit_sir_pretrained.py：模型封装。
- weights/model.pth：最终微调权重。
- pretrained/HiT-SRF-4x-DF2K.pth：官方预训练初始化权重，单独保存。
- docs/：项目说明、环境配置、第二阶段实验记录。

灰度输入复制成三通道，骨干输出再合成为灰度；属于对已有骨干的适配与训练。
原推理脚本有早期 RCAN 文案，实际加载逻辑以 load_model 和权重为准。

## 推理

建议独立 Python 环境，按 requirements.txt 与 CPU/CUDA 情况安装依赖。在本目录执行：

~~~text
python src/infer.py --input_dir input_lr --output_dir output_sr --weights weights/model.pth --no_tta
~~~

原任务默认输出 512×640，其他尺寸显式指定 --out_h、--out_w。
去掉 --no_tta 启用八方向测试时增强，耗时会增加。先用一张灰度 PNG 验证，再批量推理。
输入数据自行提供；未上传整套竞赛图片、批量结果与重复检查点。
本次用合成输入检查加载和推理，不据此推导真实测试集 PSNR/SSIM。
