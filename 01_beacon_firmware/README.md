# 水下短基线信标固件

| 目录 | 用途 | 目标 |
|---|---|---|
| reference_f427/ | 课题组信标参考固件 | STM32F427VG + 专用硬件 |
| labs_f103/ | 六个分步学习实验 | STM32F103，按各实验说明操作 |
| labs_f407/01_PC0指示灯/ | 当前 F407 板入门验证 | STM32F407VG，PC0 指示灯 |

F407 点灯是学习起点。原信标依赖 FPGA、模拟电路等外设，不能直接下载 F427 工程就视为完整复现。

## 先跑 F407

使用 Keil µVision 5、Arm Compiler 6、STM32F4xx_DFP 3.1.1。
打开 labs_f407/01_PC0指示灯/F407_PC0_Blink.uvprojx，按同目录“先看这里.md”连接 SWD，选择自己的 ST-Link。
程序使用 HSI 16 MHz、SysTick 1 ms，每 500 ms 翻转 PC0，完整亮灭周期为 1 秒。
观察 g_milliseconds、g_led_toggle_count，区分节拍、翻转间隔和周期。

## 读原固件的顺序

1. USER/main.c：初始化、主循环与标志。
2. USER/stm32f4xx_it.c → Comm_operate.c：串口中断、逐字节接收、组帧。
3. OnEchoComm2 → OnEchoPCLINK → PCLINK_DecodeCmd：地址过滤与命令分发。
4. PCLink_operate.c：参数修改与 Flash 保存。部分路径使用 m_bForceStore，设置地址等路径仍直接保存。
5. [串口第一课](docs/串口通信学习.md)：画状态表，分析分段、噪声、超长输入。

当前参考配置 USE_IWDG=0、USE_TD_SENSOR=0、USE_STMFLASH=1。存在驱动不等于当前启用。
docs/reference_notes_original.md 是历史解释，配置与行为以源码为准。

## 构建范围

reference_f427/iListen.uvprojx 使用旧标准外设库与 ARMCC 5 配置，必要 CMSIS/DSP 源码、头文件和静态库已保留。
整理只修正复制件里的 USER 路径大小写、无效包含目录，并去掉 F407 调试选项的本机探针编号。
本次未对原信标重编译、下载专用硬件或做声学实验。
