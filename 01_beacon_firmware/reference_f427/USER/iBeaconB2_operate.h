#ifndef __iBeaconB2_operate_H
#define __iBeaconB2_operate_H

#include "main.h"

#if DEVICE_SUBTYPE/100 == iBeaconB2

extern bool m_bCodeRecv;
extern int  m_nCodeResult;			// 接收机解码输出
extern int  m_nCodeWDogCnt;			// 译码校验等待时间窗口

#endif

void iBeacon_InitDefaultParam(void);
int  iBeacon_SetMode(u32 wParam, u32 lParam);	// 设置工作模式
int  iBeacon_SetSampleRate(u8 uNew);			// 设置采样速率
int  iBeacon_SetSampleGain(u8 uNew);			// 设置信标增益
void iBeacon_SetRelayMode(bool bInit);			// 设置继电器工作模式
void iBeacon_SetRelay(u8 uCh, u8 uSec);			// 设置继电器控制状态
void iBeacon_SetEmitParam(void);				// 设置发射参数	

void iBeacon_OnEchoRtc(void);					// 响应RTC中断处理

int iBeacon_Decode_Debug(void* pStCmd, char* cmd);	// 译码并响应调试指令

void iBeacon_Echo_SetRelayInfo(void);				// 回应继电器设置参数
	
#endif	// __iBeaconB2_operate_H

