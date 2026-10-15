/******************** (C) COPYRIGHT 2009 www.armjishu.com ************************
* File Name          : date.h
* Author             : www.armjishu.com Team
* Version            : V1.0
* Date               : 12/1/2009
* Description        : 日期相关函数
*******************************************************************************/
#ifndef __DATE_H
#define __DATE_H

#include "main.h"

struct rtc_time 
{
	u32 tm_sec;
	u32 tm_min;
	u32 tm_hour;
	u32 tm_mday;
	u32 tm_mon;
	u32 tm_year;
	u32 tm_wday;
};
    
u8 GregorianDay(u8 yy, u8 mm, u8 dd);			// 计算公历时间星期
uint32_t mktimev(struct rtc_time tm);			// 转换时间戳, 北京时间转换
uint32_t mktimevGreenwich(struct rtc_time tm);	// 转换时间戳，格林威治时间转换
void to_tm(uint32_t tim, struct rtc_time * tm);	// 计算公历年月日时分秒

#endif 
