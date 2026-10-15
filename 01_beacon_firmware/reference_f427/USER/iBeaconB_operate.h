#ifndef __iBeaconB_operate_H
#define __iBeaconB_operate_H

#include "main.h"

#if DEVICE_SUBTYPE == iBeaconB_V1
	#define MAX_EMIT_PARAM_GROUP		4
	#define MAX_DETECT_PARAM_GROUP		4				// 接收信号
	
	#define RECV_DATA_BLOCK				64				// 噪声阈值处理时划分的数据段数
	
	#define SYSCLK_ACQ					CLK_HSI96MHz	// 定义正常工作时的工作频率
	#define ACQ_REF_VOLTAGE				5.000f			// AD采集电压的转换系数
	
	#define FPGA_CLOCK					100000000.0f	// FPGA主频参数
	
	#define FPGA_INT_GPIO				GPIO_Pin_0
	#define FPGA_INT_GPIO_PORT			GPIOA
	#define FPGA_INT_RCC_AHB1Periph		RCC_AHB1Periph_GPIOA
	#define FPGA_INT_RCU_Periph			RCU_GPIOA
#endif

void iBeacon_InitDefaultParam(void);
void iBeacon_Initialize(void);					// 设备初始化
int  iBeacon_SetSampleRate(u8 uNew);			// 设置采样速率
int  iBeacon_SetSampleGain(u8 uNew);			// 设置信标增益
void iBeacon_SetEmitParam(void);				// 设置发射参数	

void iBeacon_OnEchoRtc(void);					// 响应RTC中断处理

int iBeacon_Decode_Debug(void* pStCmd, char* cmd);	// 译码并响应调试指令

int iBeacon_SetMode(u32 wParam, u32 lParam);	// 设置工作模式

#endif	// tDeviceIBeacon

