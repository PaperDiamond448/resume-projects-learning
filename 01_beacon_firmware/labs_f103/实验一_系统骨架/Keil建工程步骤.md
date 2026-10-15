# 在 Keil 里建立这个最小标准库工程（图文步骤）

> 文件我已经从标准库里挑好摆进本文件夹了，你只需在 Keil 里**新建工程 → 加文件 → 配几个选项**。
> 目录结构：
> - `CMSIS/`      —— 内核 + 器件 + 启动文件（core_cm3.c、system_stm32f10x.c、startup_stm32f10x_md.s 等）
> - `StdPeriph/`  —— 标准库驱动（inc 头文件 23 个，src 源文件 23 个）
> - `Config/`     —— stm32f10x_conf.h（配置）、stm32f10x_it.c/.h（中断，SysTick 已留空给 main.c）
> - 根目录        —— 你的 main.c、oled.c/.h、oledfont.h

---

## 第 1 步 · 新建工程

1. Keil 菜单 `Project → New uVision Project`。
2. 存到本文件夹 `F103\实验一_系统骨架\`，工程名填 `Lab1`，保存。
3. 弹出 **Select Device**：选 `STMicroelectronics → STM32F1 Series → STM32F103 → STM32F103C8`，确定。
   - ⚠️ 如果列表里**找不到 STM32F103C8**：说明缺 F1 器件包。开 Keil 的 **Pack Installer**，搜 `STM32F1`，装上 `Keil::STM32F1xx_DFP`（你电脑能上网，一键装）。装完重来本步。
4. 紧接着弹出 **Manage Run-Time Environment**：**什么都不勾，直接点 OK 关掉**（因为库文件我们手动加，不用它生成）。

## 第 2 步 · 建分组、加文件

在左侧 Project 窗口，右键 `Target1` → `Add Group`，建 4 个组：`CMSIS`、`StdPeriph`、`Config`、`User`。
然后右键每个组 → `Add Existing Files to Group...`，按下表加（**只加 .c 和 .s，.h 不用加**）：

| 组 | 加这些文件 | 位置 |
|---|---|---|
| CMSIS | `core_cm3.c`、`system_stm32f10x.c`、`startup_stm32f10x_md.s` | `CMSIS/`（加 .s 时把文件类型筛选改成 All files 或 Asm Source） |
| StdPeriph | `misc.c`、`stm32f10x_rcc.c`、`stm32f10x_gpio.c`、`stm32f10x_usart.c`、`stm32f10x_exti.c` | `StdPeriph/src/`（实验一只用这 5 个） |
| Config | `stm32f10x_it.c` | `Config/` |
| User | `main.c` | 根目录（`oled.c` 先别加，第一次编译跑通再加，见下） |

## 第 3 步 · 配置选项（点 魔术棒 Options for Target）

**① C/C++ 选项卡 → Define（预处理宏）**，填：
```
USE_STDPERIPH_DRIVER,STM32F10X_MD
```

**② C/C++ 选项卡 → Include Paths（头文件路径）**，点右边 `...` 把这 4 个目录都加上（用相对路径）：
```
.\CMSIS
.\StdPeriph\inc
.\Config
.\
```
（最后那个 `.\` 是本文件夹，让它能找到 oled.h）

**③ Target 选项卡：**
- 勾上 **Use MicroLIB**（否则 printf 不输出、卡死）。
- ARM Compiler 优先选 **Version 5 (AC5)** 最省事；只有 AC6 也能编，万一启动文件报汇编错，见末尾排错。

**④ Debug 选项卡：** 右上选你的下载器（你工程里有 JLink 记录，就选 `J-LINK / J-TRACE Cortex`；用 ST-Link 就选对应项），再进 Settings 里 Flash Download 确认有 STM32F10x 的编程算法。

## 第 4 步 · 编译

点 `Build`（F7）。**期望结果：0 Error(s), 0 Warning(s)**（可能有几个无害 warning，没 error 即可）。
然后接好下载器，点 `Download`（F8）下载到板子。

✅ 板载 LED(PC13) 开始 0.5s 闪 = 成功！再按你接在 PA0 的按键，闪烁变快/变慢。

## 第 5 步（可选）· 加上 OLED 显示

第一次编译跑通后，想用 OLED：
1. 把 `oled.c` 加进 `User` 组。
2. 按《接线_OLED补充.md》改 main.c（加 `#include "oled.h"`、`OLED_Init()` 和刷屏代码）。
3. 重新编译下载。

---

## 常见报错速查

| 报错 | 原因 / 解决 |
|---|---|
| `SysTick_Handler multiply defined` | Config/stm32f10x_it.c 里的 SysTick 没注释干净——我已注释，若仍报，确认你没加重复的 it.c |
| 找不到 `stm32f10x.h` / `stm32f10x_conf.h` | 第 3 步②的 Include Paths 没配全（少了 `.\CMSIS` 或 `.\Config`） |
| 一堆 `undefined symbol GPIO_Init / RCC_...` | StdPeriph 那 5 个 .c 没加进工程（第 2 步） |
| `printf` 不输出 / 程序卡死 | 没勾 Use MicroLIB（第 3 步③） |
| 启动文件 `startup_..._md.s` 汇编报错 | 用了 AC6。改用 AC5；或 Options→Asm 里把 startup 当 armasm 处理。最简单是装 AC5 编译器选上 |
| 编译过、下载后 LED 不闪 | 检查下载器选对没、Flash 算法选对没；或板子没插稳 |
| 找不到 STM32F103C8 器件 | 装 `Keil::STM32F1xx_DFP` 包（Pack Installer，需联网） |
