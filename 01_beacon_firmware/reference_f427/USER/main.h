/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "stdbool.h"

#ifndef BYTE
#define BYTE uint8_t //BYTE 就是 uint8_t 的别名，u32 就是 uint32_t 的别名
#endif
/*// 不用宏
uint8_t data[100];
uint32_t timestamp;

// 用宏
BYTE data[100];
u32 timestamp;*/	 
#ifndef u32
#define u32 uint32_t 
#endif

#define GET_FILE_NAME(file) (strrchr(file, '/') ? strrchr(file, '/') + 1 : (strrchr(file, '\\') ? strrchr(file, '\\') + 1 : file))
#define PRINT_FILE_LINE()  printf(" File：%s, Line：%05d:\r\n", GET_FILE_NAME(__FILE__), __LINE__)

#define USE_STM32F1XX		0		// 当前设备使用的ARM类型
#define USE_STM32F4XX		1
#define USE_STM32H7XX		0
#define USE_GD32F4XX		0

#if USE_STM32F1XX					// DARMSTM.DLL -pSTM32F103RE	
#include "hw_config.h"
#include "stm32f10x.h"				// $$Device:STM32F103RE$SVD\STM32F103xx.svd
#include "system_stm32f10x.h"
	 
#elif USE_STM32F4XX					// DARMSTM.DLL -pSTM32F427VG
#include "hw_config.h"
#include "stm32f4xx.h"
#include "system_stm32f4xx.h"

#elif USE_STM32H7XX
#include "hw_config.h"
#include "stm32h743xx.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_conf.h"

#define u8	uint8_t
#define u16	uint16_t
#define u32	uint32_t
#endif

#include "Timer.h"
#include "malloc.h"

enum SYSTEM_CLK_TYPE//系统时钟枚举
{
#if USE_STM32F1XX
	CLK_SLEEP = 1,		// 1
	CLK_HSE,			// 设置低功耗模式时勿用此参数
	CLK_1MHz,
	CLK_8MHz,
	CLK_24MHz,
	CLK_48MHz,
	CLK_64MHz,
	CLK_HSI,			// 设置低功耗模式时勿用此参数
	CLK_HSI48MHz,
	CLK_HSI64MHz,
#elif USE_STM32F4XX//列出这块芯片支持的所有时钟频率
	CLK_SLEEP = 1,	// 1
	CLK_HSE,		// 2	
	CLK_24MHz,		// 3
	CLK_48MHz,		// 4
	CLK_64MHz,		// 5	
	CLK_96MHz,		// 6
	CLK_128MHz,		// 7		
	CLK_144MHz,		// 8	
	CLK_160MHz,		// 9
	CLK_180MHz,		// 10
	CLK_192MHz,		// 11	
	CLK_HSI = 100,	// 100	
	CLK_HSI48MHz,	// 101
	CLK_HSI64MHz,	// 102
	CLK_HSI96MHz,	// 103
	CLK_HSI128MHz,	// 104
	CLK_HSI160MHz,	// 
#elif USE_STM32H7XX
	CLK_SLEEP = 1,	
	CLK_HSE,		
	CLK_64MHz,	
	CLK_96MHz,	
	CLK_128MHz,	
	CLK_192MHz,	
	CLK_256MHz,
	CLK_HSI = 100,
	CLK_HSI64MHz,
	CLK_HSI96MHz,
	CLK_HSI128MHz,	
	CLK_HSI192MHz,	
	CLK_HSI256MHz,
#endif
};
//罗盘宏定义
#define	COMPASS_TCM2		1		// PNI TCM2
#define	COMPASS_TCM3		2		// PNI TCM3
#define	COMPASS_TCM6		3		// PNI TCM6
#define COMPASS_DCM250B		4		// 瑞芬科技DCM250B
#define COMPASS_LCAxx		5		// 瑞芬科技LCAxx系列倾角传感器
#define IMU100C_PLUS		6		// MEMS陀螺组合导航系统
#define COMPASS_MHT_C330	7 		// MHT-C330,邈航科技有限公司
#define COMPASS_MCC_602		8		// MCC602，710所国产电子罗盘

// 选择所使用的电子罗盘型号
#define COMPASS_TYPE		0		// 选择所使用的器件

#define TD_MS5837			1
#define TD_MPM3801			2
//当前只开了内部Flash存储
#define USE_FULL_ASSERT		0		// 定义是否使用 assert_param 函数
#define USE_NET				0		// 定义使用网络接口
#define USE_USB				0		// 定义使用USB接口
#define USE_FATFS			0		// 定义是否使用Fatfs文件操作
#define USE_FILE			0		// 使用文件作为数据存储器保存配置参数
#define USE_EEPROM			0		// 使用外挂EEPROM作为数据存储器保存配置参数
#define USE_BACKMEM			0		// 使用后备存储器作为数据存储器保存配置参数
#define USE_SPIFLASH		0		// 使用SPI接口外挂存储器，保存文件系统数据表
#define USE_STMFLASH		1		// 使用STM内部FLASH存储器，保存配置参数
#define USE_COMM_DMA		0		// 进行文件系统数据上传时，是否使用串口的DMA模式，适用于串口模式的数据传输
#define USE_IWDG			0		// 采集过程中是否使用独立看门狗
#define USE_HSE				0		// 定义是否使用片外HSE时钟
#define USE_RTC_LSE			1		// 定义是否使用片外LSE晶振
#define USE_DS1302 			0 		// 定义是否使用DS1302时钟芯片（补偿RTC LSE的精度不足）
#define USE_DS3231			0		// 定义是否使用DS3231时钟芯片（补偿RTC LSE的精度不足）
#define USE_FAST_UPLOAD		0		// 使用快速文件数据上传模式

#define USE_LCD				0		// 0，系统不连接LCD
#define USE_GPS				0		// 0，系统不连接GPS
#define USE_ATOMIC			0		// 0，系统未设置原子钟
#define USE_COMPASS			0		// 0, 系统未设置电子罗盘
#define USE_TD_SENSOR		0		// 0, 系统未设置TD温深传感器

#define USE_STM8_PWCTRL		0		// 定义使用 STM8 电源主控
#define USE_STM8_LSE		0		// 定义使用 STM8 时，STM8使用LSE时钟提高计时精度，并为后续增加新的功能准备
#define STM8_COMM			COMM6	// 定义 STM8 主控串口
#define STM8_COMM_BAUD		9600	// 定义 STM8 主控串口波特率
#define DEBUG_PRINTF		0		// 定义在调试时，是否使能调试信息串口打印输出功能, 1->打印输出
#define DEBUG_PW_DOWN		1		// 定义在调试时，停止采集时是否关闭采集电路电源, 1->关闭电源， 0->不关闭电源

//串口分配 上位机用串口2，debug用串口6，原子钟也是6
#define PCLINK_Comm			COMM2	// 上位机连接串口定义，至少要定义一个串口，否则网口连通时将无缓冲区使用
#define PCLINK_CommBaud		115200	// 上位机连接串口波特率定义
#define DEBUG_Comm			COMM6	// Debug信息输出串口
#define DEBUG_Comm_Baud		57600	// Debug串口波特率
#define ATOMIC_Comm			COMM6	// 铷原子钟连接串口定义
#define ATOMIC_Comm_Baud	57600	// 铷原子钟连接串口波特率

#define USE_IAP_UPDATE		0			// 使能在线升级
#define USER_FLASH_FIRST_PAGE_ADDRESS 	0x08010000 /* Only as example see comment */
#define USER_FLASH_LAST_PAGE_ADDRESS  	0x080E0000
#define USER_FLASH_END_ADDRESS        	0x080FFFFF  

typedef struct//RTC计数器结构体
{
  	u16 uAcqIdle;					// 在采集过程中，通过该值检查FPGA是否正常工作
	u16 uPPSIdle;					// 同步模式下，秒脉冲启动后的监测定时器	 秒脉冲监测
	u16 uAcqRstCnt;					// 采集复位计数
	u16 uWatchDog;					// 看门狗空闲计数器
	u16 uPCLinkIdle;				// 上位机连接空闲计数器
}RTC_COUNTER_TypeDef;
/*uPCLinkIdle 一直在涨  →  上位机一直没发消息  →  可能断连了
uAcqIdle    一直在涨  →  FPGA一直没响应     →  采集可能卡死了*/
extern bool m_bInitDone;			// 系统初始化完成标志 初始化完成了没
extern bool m_bForceLoad;			// 强制加载系统参数
extern bool m_bForceStore;			// 强制保存系统参数
extern bool m_bForceInitParam;		// 强制系统参数重新初始化
extern bool m_bForceInitRtcEx;		// 强制从外部RTC更新系统时钟
extern bool m_bStartStatus;			// 系统运行状态
extern u32	m_uCpuIdleStep;			// CPU 空闲率计数器步进值
extern u32  m_nSysClkTest;			// 系统时钟频率配置值（用于时钟切换）
extern u32 	m_nSysClkType;			// 当前设置的时钟配置
extern float m_fCpuIdle;	// CPU空闲率（百分比，0~100%）
extern RTC_COUNTER_TypeDef m_hRtcCnt;
//这些中断回调是硬件触发的，串口收到数据、定时器到点，硬件自动调用对应的函数。
void LedShine(void);				// 闪灯控制
void OnEchoRTC(void);				// 响应RTC时钟中断
void OnEchoComm1(void);				// 响应串口1中断
void OnEchoComm2(void);				// 响应串口2中断
void OnEchoComm3(void);				// 响应串口3中断
void OnEchoComm6(void);				// 响应串口6中断
void OnEchoPCLINK(void* pObj);		// 响应计算机命令

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/*main.h
  ├── 选芯片型号      →  决定用哪套寄存器
  ├── 功能开关        →  决定编译哪些模块
  ├── 串口分配        →  决定谁用哪个口
  ├── Flash地址       →  决定程序住哪里
  ├── 全局状态变量    →  各模块共享的状态
  └── 中断回调声明    →  硬件事件的处理入口*/