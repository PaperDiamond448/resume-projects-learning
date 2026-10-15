# 实验一附加 · 把心跳信息显示到 OLED 屏

你那块 4 脚（GND/VCC/SCL/SDA）的就是标准 **I2C SSD1306 OLED**，可以把串口打印的内容同步显示到屏上。
**额外收获**：这里用的“软件模拟 I2C”就是信标固件 `ms5837.c` 读温压传感器的同一套手法——提前把实验三的 I2C 功课练了。

## 1. 接线（4 根线）

| OLED 引脚 | 接到 F103 | 说明 |
|---|---|---|
| GND | GND | 共地 |
| VCC | **3.3V** | 模块一般 3.3~5V 都行，用 3.3V 最稳 |
| SCL | **PB8** | 软件 I2C 时钟 |
| SDA | **PB9** | 软件 I2C 数据 |

> 模块板载有上拉电阻，所以驱动里 SCL/SDA 配成开漏输出，不用额外接电阻。

## 2. 把这 3 个文件加进 Keil 工程

- `oled.c` —— 加入工程参与编译（右键 Source Group → Add Existing Files）
- `oled.h`、`oledfont.h` —— 和 `oled.c` 放同一目录即可（已生成，字库是用 Consolas 渲染的，保证正确）

## 3. 改 `main.c`（只加 4 处，已标好位置）

**① 文件顶部加头文件：**
```c
#include "oled.h"
```

**② `main()` 里，`BSP_KeyEXTI_Init();` 之后、`printf("...boot ok...")` 附近，加初始化：**
```c
    OLED_Init();
    OLED_ShowString(0, 0, "F103 Lab1");
    OLED_ShowString(0, 2, "booting...");
```

**③ 把 `Task_HeartBeat()` 里的 `printf(...)` 那一段，改成同时刷屏。**
在 `printf(...)` 后面补上：
```c
        OLED_ShowString(0, 0, "t:");    OLED_ShowNum(24, 0, g_msTick/1000, 5);
        OLED_ShowString(0, 2, "CPU:");  OLED_ShowNum(40, 2, use, 3);
        OLED_ShowString(72,2, "%");
        OLED_ShowString(0, 4, "LED:");  OLED_ShowNum(40, 4, g_blinkMs, 4);
        OLED_ShowString(72,4, "ms");
```
（OLED 用 8x16 字体，一行占 2 页，所以 y 取 0、2、4、6，最多 4 行）

**④（可选）`Task_OnKey()` 里切换闪烁后，也刷一下屏，立刻看到变化——其实③每秒会自动刷新，不加也行。**

## 4. 验收

上电后 OLED 上出现并每秒刷新：
```
t:    12
CPU:   2 %
LED: 500 ms
```
串口和屏幕内容一致 = 成功。按键改变 LED 周期时，屏上 `LED:` 那行的数字会跟着变。

## 5. 如果你的屏是 0.91 寸（128x32，比较扁的那种）

驱动默认按 0.96 寸 128x64 写的。128x32 只需把 `oled.c` 的 `OLED_Init()` 里这两行改一下：
```c
    OLED_WR_Byte(0xA8, 0); OLED_WR_Byte(0x1F, 0);   // 多路复用改 1/32
    OLED_WR_Byte(0xDA, 0); OLED_WR_Byte(0x02, 0);   // COM 引脚配置改 0x02
```
128x32 只有 4 页，所以最多显示 2 行 8x16 文字（y 用 0 和 2）。

## 6. 屏不亮 / 花屏怎么查（按顺序）

1. **完全不亮**：先确认 VCC、GND 没接反；多数 SSD1306 地址是 `0x78`，少数是 `0x7A`——把 `oled.c` 里 `#define OLED_ADDR 0x78` 改成 `0x7A` 试试。
2. **花屏/错位**：八成是 128x32 的屏用了 128x64 的参数，按第 5 节改。
3. **整屏雪花不刷新**：检查 SCL/SDA 有没有接反（PB8=SCL、PB9=SDA），或杜邦线虚接。
4. 串口照常打印但屏不动：说明主程序在跑，问题一定在 OLED 接线或地址——回到第 1 步。
