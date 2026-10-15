# 水下测姿 LFM 信号检测算法移植

保留 MATLAB、可移植 C 算法、PC 测试入口和 STM32 接入参考。本模块输出检测峰、时延和幅值；姿态求解还需要几何模型与多通道处理。

## 入口

- lfm2_2.m、detpart.m、NL1.m：参考处理流程。
- firmware/algo/lfm_algo.c：滤波、降采样、FFT 相关、自适应门限、峰选择。
- firmware/host_test/main_host.c：PC 第 1 通道逐块测试。
- firmware/stm32/Core/：采集/串口/主循环接入参考，需适配实际板卡。
- signal_all_35_50.mat：原始信号，约 68 MB，保留以重建输入。
- result.txt、firmware/host_test/result_c.txt：已有参考输出。

## PC 复现

需要 Python 3、NumPy、SciPy、C99 编译器和 CMake。在本目录执行：

~~~text
python -m pip install -r requirements.txt
python firmware/host_test/prepare_input.py
cmake -S . -B build
cmake --build build --config Release
~~~

导出脚本生成 firmware/host_test/ch1_sig.u16 与 ch1_syn.u8，它们可再生成，因此不重复上传。
给生成的 lfm_host 依次传入两个输入和输出路径。Windows 多配置构建示例：

~~~text
build/Release/lfm_host.exe firmware/host_test/ch1_sig.u16 firmware/host_test/ch1_syn.u8 result_local.txt
~~~

Linux/单配置构建通常为 build/lfm_host。先核对行数、块号、通道与峰编号，再比较数值误差。

## STM32 范围

F407 可用来逐步学习移植，但需核对工作区、栈、DMA 缓冲与 SRAM 容量。先做 PC 对照，再评估实时运行。
firmware/stm32/ 不是已在当前 F407 上验证的完整采集工程。
docs/原移植说明.md 为历史记录，安装包与硬件适配应结合当前环境核对。
