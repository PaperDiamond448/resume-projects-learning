#ifndef __HW_CONFIG_H
#define __HW_CONFIG_H

#include "main.h"

/************************************************************************/
/* 函数定义	*/
/************************************************************************/
void Init_All_Periph(void);

void SetSystemClock(uint32_t type, bool bShowMsg);	// 设置系统时钟，同时配置串口波特率	

#endif  /*__HW_CONFIG_H*/
