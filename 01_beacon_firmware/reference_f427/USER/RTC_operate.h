
/************************************************************************/
// 2026.03.12 修改：增加 RTC_DEFAULT_CNT 宏定义
/************************************************************************/

#ifndef __RTC_OPERATE_H
#define __RTC_OPERATE_H

#include "main.h"
#include "date.h"
#include "calendar.h"

/************************************************************************/
/* RTC parameter define                                                 */
/************************************************************************/
#if defined(USE_RTC_LSE) && USE_RTC_LSE
#define RTC_CLOCK_SOURCE_LSI	0
#else
#define RTC_CLOCK_SOURCE_LSI	1
#endif

#define RTC_DEFAULT_YMD		20260101u
#define RTC_DEFAULT_HMS		80000u
#define RTC_DEFAULT_CNT		RTC_YMDHMS2Cnt(RTC_DEFAULT_YMD, RTC_DEFAULT_HMS)	// 系统默认时间对应的时间值

#if USE_STM32F1XX
	#define BkReg_ParamInitCheck	BKP_DR1
	#define BkReg_FlashUpdateCheck	BKP_DR2
	#define BkReg_SleepStatCheck	BKP_DR3
#elif USE_STM32F4XX || USE_STM32H7XX
	#define BACKUP_SRAM_SIZE		4096
	#define BkReg_ParamInitCheck	RTC_BKP_DR0
	#define BkReg_SleepStatCheck	RTC_BKP_DR1
	#define BkReg_FlashUpdateCheck	RTC_BKP_DR2
	#define BkReg_PpsSyncStatCheck	RTC_BKP_DR3		// 时钟同步状态保存
	#define BkReg_PwLinkInitCheck	RTC_BKP_DR4		// 上电保持连接初始化状态保存
	#define BkReg_PwDelayInitCheck	RTC_BKP_DR5		// 上电延迟启动初始化状态保存
	#define BkReg_PwOffInitCheck	RTC_BKP_DR6		// 自动启动初始化状态保存
	#define BkReg_PwOnTimeCnt		RTC_BKP_DR7		// 记录上电时刻对应的时间计数值
	#define BkReg_PwOffTimeCnt		RTC_BKP_DR8		// 记录下电时刻对应的时间计数值
	#define BkReg_PwAcqTimeCnt		RTC_BKP_DR9		// 记录间歇采集时刻对应的时间计数值
#endif

#if USE_STM32F1XX || USE_STM32F4XX
extern struct rtc_time		m_stSystemtime;
#elif USE_STM32H7XX
extern struct rtc_time		m_stSystemtime;
extern RTC_HandleTypeDef	m_hRtc;
#endif

extern bool m_bRtcInit;
extern u32 m_nRtcYMD, m_nRtcHMS, m_nRtcWeek;
extern u32 m_nRtcYMDb;						// 标志当前建立文件夹的日期，用于判断是否需要建立新的文件夹

void RTC_Initialize(void);
void RTC_UpdateTime(u8 h, u8 m, u8 s, u8 yy, u8 mm, u8 dd);	// 根据北京时间设置RTC时钟
void RTC_UpdateTime2(int nYMD, int nHMS);					// 根据北京时间设置RTC时钟
void RTC_UpdateTime3(u32 uGpsWeek, float fGpsSec);			// 根据电罗经输出的国标时间设置RTC时间

void RTC_FreshTime(void);   				// 刷新 m_nRtcYMD m_nRtcHMS m_nRtcWeek值
void RTC_WakeupEn(bool bEn);				// 使能秒脉冲中断

u8  RTC_GetWDAY(int nYMD);					// 根据时间解算星期
u32 RTC_GetFattime(void);					// 生成FATFS所需的文件时间
u32 RTC_GetSystimeCnt(void);				// 获得当前时间计算的秒数值
u32 RTC_YMDHMS2Cnt(u32 uYMD, u32 uHMS);		// 将年月日，时分秒值合并计算秒数，输入时间为北京时间
u32 RTC_Greenwich2Cnt(u32 uYMD, u32 uHMS);	// 将年月日，时分秒值合并计算秒数，输入时间为GPS的格林尼治时间
u32 RTC_Cnt2YMD(u32 uCnt);					// 将秒数转换为北京时间年月日值
u32 RTC_Cnt2HMS(u32 uCnt);					// 将秒数转换为北京时间时分秒值
void RTC_Cnt2Systime(u32 uCnt);				// 将秒数转换为北京时间对应的 (struct rtc_time)	m_stSystemtime;

// 将年月日星期时分秒转换成单字节表示的时间
u8 GetU8Year(long lnTime);
u8 GetU8Month(long lnTime);
u8 GetU8Day(long lnTime);
u8 GetU8Hour(long lnTime);
u8 GetU8Minute(long lnTime);
u8 GetU8Second(long lnTime);

#if USE_STM32F4XX || USE_STM32H7XX
void RTC_Set_AlarmA(int nYMD, int nHMS);
void RTC_Set_AlarmA2(u8 week, u8 hour, u8 min, u8 sec);			// 设置闹钟时间(按星期闹铃,24小时制)
void RTC_Set_AlarmB(u16 min);									// 按照参数给定的延迟分钟数设定闹铃
#endif

// 使用后备存储器保存重要参数
void RTC_BackMemRead(uint16_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead);		// 从后备存储器中读取数据
void RTC_BackMemWrite(uint16_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite);	// 向后备存储器中写入数据
u8 RTC_BackMemReadByte(uint16_t ReadAddr);						// 从后备存储器中读取数据
u8 RTC_BackMemWriteByte(uint16_t WriteAddr, u8 data);			// 向后备存储器中写入数据，成功 返回1， 失败 返回0

u32 RTC_BackRegRead(uint32_t nReg);								// 从后备寄存器中读取数据
u32 RTC_BackRegWrite(uint32_t nReg, u32 data);					// 向后备寄存器中写入数据，成功 返回1， 失败 返回0

// 
u32 GetRemainderTime(u32 uCurTime, u32 uDstTime);				// 计算到指定时间的剩余时间值，单位：秒
u32 GetRemainderTimeCeil(u32 uCurTime);							// 计算到当前时间最近的以小时凑整时刻剩余秒数
void GetCurrTimeCeil(u32 uCnt, int* uYMD, int* uHMS);			// 计算当前时间最近的以小时凑整的年月日，时分秒
#endif



