/************************************************************************
 * SSD1306 OLED 驱动（软件模拟 I2C）—— 实验一可选显示模块
 * 平台：STM32F103C8T6 + 标准外设库(SPL)
 *
 * 这套“软件模拟 I2C”就是信标固件 USER/ms5837.c 读温压传感器用的同一手法：
 * 不用硬件 I2C 外设，纯 GPIO 手写 起始/停止/发字节 时序。
 * 学会这一个，实验三读 MPU6050 直接复用。
 *
 * 接线：GND→GND  VCC→3.3V  SCL→PB8  SDA→PB9
 ************************************************************************/
#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

// 软件 I2C 引脚（统一引脚表：SCL=PB8, SDA=PB9）
#define OLED_SCL_PORT   GPIOB
#define OLED_SCL_PIN    GPIO_Pin_8
#define OLED_SDA_PORT   GPIOB
#define OLED_SDA_PIN    GPIO_Pin_9

void OLED_Init(void);                                           // 初始化 GPIO + SSD1306
void OLED_Clear(void);                                          // 清屏
void OLED_ShowChar(uint8_t x, uint8_t y, char chr);             // 显示一个 8x16 字符 (y 为页,0/2/4/6)
void OLED_ShowString(uint8_t x, uint8_t y, const char *str);    // 显示字符串
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len); // 显示无符号整数, len 位右对齐补空格

#endif
