
/************************************************************************/
// 2026.02.23 修改：增加STM32H7操作
// 2026.03.22 修改：如果 RTC 后备寄存器的定义有改动，则需要更改后备寄存器的校验值
/************************************************************************/

#include "RTC_operate.h"

#if USE_DS1302
#include "DS1302_operate.h"
#elif USE_DS3231
#include "DS3231_operate.h"
#endif

#if USE_STM32H7XX
#include "STM32H7_operate.h"
#endif

// 如果 RTC 后备寄存器的定义有改动，则需要更改后备寄存器的校验值
#define RTC_BKRAM_CRC_VALUE     (uint32_t)0x0100

#if USE_STM32F1XX || USE_STM32F4XX
struct rtc_time			m_stSystemtime;		// 设置系统时间的时间结构体
#elif USE_STM32H7XX
RTC_HandleTypeDef 		m_hRtc;
struct rtc_time			m_stSystemtime;		// 设置系统时间的时间结构体
#endif

bool m_bRtcInit = false;					// RTC时钟是否根据GPS接收数据初始化
u32 m_nRtcYMD, m_nRtcHMS, m_nRtcWeek;		// 当前RTC时间的参数，随FreshTime更新
u32 m_nRtcYMDb;								// 标志当前建立文件夹的日期，用于判断是否需要建立新的文件夹
/************************************************************************/
// 不常用和非开放函数
#if USE_STM32F1XX || USE_STM32F4XX || USE_STM32H7XX
u32 GetYMD(struct rtc_time tm);
u32 GetHMS(struct rtc_time tm);
#endif
/************************************************************************/

void RTC_Initialize( void )
{
	// 本函数是供系统调用，以判断RTC硬件是否配置完善
	uint32_t mmm;
/*---------------------------------------------*/
#if USE_STM32F1XX
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);	/* RTC Configuration */
	PWR_BackupAccessCmd(ENABLE);	/* Allow access to BKP Domain */
	// BKP_DeInit();				/* Reset Backup Domain */
	
#if RTC_CLOCK_SOURCE_LSI
	RCC_LSICmd(ENABLE);										/* Enable the LSI OSC */ 
	while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET){};	// 等待LSI时钟启动
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI); 				// 使用LSI提供RTC时钟

#else
	RCC_LSEConfig(RCC_LSE_ON);								/* Enable the LSE OSC */
	while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET){};	// 等待LSE时钟启动 
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);					// 使用LSE提供RTC时钟
#endif

	RCC_RTCCLKCmd(ENABLE);			/* Enable the RTC Clock */
	RTC_WaitForSynchro();			/* Wait for RTC APB registers synchronisation */
	
	mmm = BKP_ReadBackupRegister(BkReg_ParamInitCheck);
	if (mmm != RTC_BKRAM_CRC_VALUE)
	{
		/* 每次操作前要等待RTC的上一个操作完成 */
		RTC_WaitForLastTask(); RTC_ITConfig(RTC_IT_SEC, DISABLE);	/* Disable the RTC Second interrupt */
		RTC_WaitForLastTask(); RTC_SetPrescaler(32767); 			/* RTC period = RTCCLK/RTC_PR = (32.768 KHz)/(32767+1) */
		RTC_WaitForLastTask(); 
		RTC_UpdateTime2(RTC_DEFAULT_YMD, RTC_DEFAULT_HMS);			// 设置默认时间，避免出现1970年的初始时间的问题
		RTC_SetCounter(mktimev(m_stSystemtime));					/* 修改当前RTC计数寄存器内容 */
		RTC_WaitForLastTask();				
		BKP_WriteBackupRegister(BkReg_ParamInitCheck, RTC_BKRAM_CRC_VALUE);
	}
	else
	{
		if (RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET)	/* Check if the Power On Reset flag is set */
		{
		//	printf(" Power On Reset occurred....\n");
		}
		/* Check if the Pin Reset flag is set */
		else if (RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET)
		{
		//	printf(" External Reset occurred....\n");
		}
		/*等待寄存器同步*/
		RTC_WaitForSynchro();
	}
	RCC_ClearFlag();				/* Clear reset flags */
#endif	// USE_STM32F1XX
/*---------------------------------------------*/
#if USE_STM32F4XX
    RTC_InitTypeDef 	RTC_InitStructure;
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);		/* Enable the PWR clock */
	PWR_BackupAccessCmd(ENABLE);							/* Allow access to BKP Domain */

#if RTC_CLOCK_SOURCE_LSI
	RCC_LSICmd(ENABLE);										/* Enable the LSI OSC */ 
	while(RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET){};	/* Wait till LSI is ready */  
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);					/* Select the RTC Clock Source */
#else
	RCC_LSEConfig(RCC_LSE_ON);								/* Enable the LSE OSC */
	while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET){};	/* Wait till LSE is ready */ 
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);					/* Select the RTC Clock Source */
#endif /* RTC_CLOCK_SOURCE_LSI */

	RCC_RTCCLKCmd(ENABLE);			/* Enable the RTC Clock */
	RTC_WaitForSynchro();			/* Wait for RTC APB registers synchronisation */
	
	mmm = RTC_ReadBackupRegister(BkReg_ParamInitCheck);
	if(mmm != RTC_BKRAM_CRC_VALUE)
	{  
		/* Configure the RTC data register and RTC prescaler */
		RTC_InitStructure.RTC_SynchPrediv  = 0xFF;				// RTC同步分频系数(0~7FFF)
		RTC_InitStructure.RTC_AsynchPrediv = 0x7F;				// RTC异步分频系数(1~0X7F)
		RTC_InitStructure.RTC_HourFormat   = RTC_HourFormat_24;	// RTC设置为,24小时格式
		if (RTC_Init(&RTC_InitStructure) == ERROR)				/* Check on RTC init */
			printf(" RTC Prescaler Config failed! \n");
		RTC_UpdateTime2(RTC_DEFAULT_YMD, RTC_DEFAULT_HMS);		// 设置默认时间，避免出现1970年的初始时间的问题	
		RTC_WriteBackupRegister(BkReg_ParamInitCheck, RTC_BKRAM_CRC_VALUE);
	}
	else
	{
		if (RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET)		/* Check if the Power On Reset flag is set */
		{
		//	printf(" Power On Reset occurred....\n");
		}
		/* Check if the Pin Reset flag is set */
		else if (RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET)
		{
		//	printf(" External Reset occurred....\n");
		}
	}
#endif	// USE_STM32F4XX
/*---------------------------------------------*/
#if USE_STM32H7XX
	uint32_t tickstart = HAL_GetTick();	/* Get Start Tick*/
	/* 步骤1：取消RTC备份区域写保护（STM32H7必须先解锁，否则无法操作RTC/备份域） */
	HAL_PWR_EnableBkUpAccess();			// 取消备份区域写保护
	
	 /* 步骤2：配置RTC时钟源（LSI/LSE二选一，含超时检测） */
#if RTC_CLOCK_SOURCE_LSI
	/* 配置时钟源为LSI：使能LSI + 失能LSE */
	__HAL_RCC_LSI_ENABLE();				/* 使能内部低速晶振(LSI，约32kHz) */
	while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == 0U)	/* Wait till LSI is ready */
	{
		if ((HAL_GetTick() - tickstart) > LSI_TIMEOUT_VALUE) break;
	}
	
	__HAL_RCC_LSE_CONFIG(RCC_LSE_OFF);	/* 关闭外部低速晶振(LSE) */
	while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != 0U)
	{
		if ((HAL_GetTick() - tickstart) > LSI_TIMEOUT_VALUE) break;
	}
	/* 将RTC时钟源配置为LSI */
    __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
#else 
	/* 配置时钟源为LSE：配置驱动能力 + 使能LSE + 失能LSI */
	__HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);	
	__HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
	/* Wait till LSE is ready */
	while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == 0U)
	{
		if ((HAL_GetTick() - tickstart) > RCC_LSE_TIMEOUT_VALUE) break;
	}
    
	__HAL_RCC_LSI_DISABLE();			/* Disable the Internal Low Speed oscillator (LSI). */
	while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) != 0U)	/* Wait till LSI is ready */
	{
		if ((HAL_GetTick() - tickstart) > LSI_TIMEOUT_VALUE) break;
	}
	/* 将RTC时钟源配置为LSE */
    __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
#endif
	/* 步骤3：使能RTC时钟（STM32H7需手动使能RTC外设时钟） */
	 __HAL_RCC_RTC_ENABLE();
	
	/** Initialize RTC Only */
	m_hRtc.Instance = RTC;
	m_hRtc.Init.HourFormat 		= RTC_HOURFORMAT_24;
	m_hRtc.Init.AsynchPrediv 	= 127;
	m_hRtc.Init.SynchPrediv 	= 255;
	m_hRtc.Init.OutPut 			= RTC_OUTPUT_DISABLE;
	m_hRtc.Init.OutPutPolarity 	= RTC_OUTPUT_POLARITY_HIGH;
	m_hRtc.Init.OutPutType 		= RTC_OUTPUT_TYPE_OPENDRAIN;
	m_hRtc.Init.OutPutRemap 	= RTC_OUTPUT_REMAP_NONE;
	if (HAL_RTC_Init(&m_hRtc) != HAL_OK)
	{
		Error_Handler();
		printf(" RTC Prescaler Config failed! \n");
	}
	
	mmm = RTC_BackRegRead(BkReg_ParamInitCheck);
	if(mmm != RTC_BKRAM_CRC_VALUE)
	{  
		/* Configure the time register */
		// 设置默认时间，避免出现1970年的初始时间的问题	
		RTC_UpdateTime2(RTC_DEFAULT_YMD, RTC_DEFAULT_HMS);		
		RTC_BackRegWrite(BkReg_ParamInitCheck, RTC_BKRAM_CRC_VALUE);
	}
#endif	// USE_STM32H7XX
	
#if USE_DS1302
	Ds1302_Init();						// 初始化DS1302
	Ds1302UpdateRTC();					// 读取DS1302的时间数据并更新RTC
	m_nRtcYMD = m_stDsTime.uYMD;
	m_nRtcHMS = m_stDsTime.uHMS;
#elif USE_DS3231
	Ds3231_Init();						// 初始化DS3231
	Ds3231UpdateRTC();					// 读取DS3231的时间数据并更新RTC
	m_nRtcYMD = m_stDsTime.uYMD;
	m_nRtcHMS = m_stDsTime.uHMS;
#endif
	RTC_WakeupEn(true);					// 使能秒脉冲中断
	RTC_FreshTime();
}

/**
  * @brief  Returns the time entered by user, using Hyperterminal.
  * @param  h :hour(0-23)
			m :minutes(0-59)
			s :second(0-59)
			yy:year(00-99) Based on 1980
			mm:month(1-12)
			dd:date(1-31)
			ww:weekday(1-7)
  * @retval None
  */
void RTC_UpdateTime( u8 h, u8 m, u8 s, u8 yy, u8 mm, u8 dd)
{
#if USE_STM32F1XX
	struct rtc_time tm;
	
	// 设置RTC时钟
	tm.tm_hour = h > 23 ? 0 : h;
	tm.tm_min  = m > 59 ? 0 : m;
	tm.tm_sec  = s > 59 ? 0 : s;
	
	tm.tm_mday = dd;
	tm.tm_mon  = mm > 12 ? 1 : mm;
	tm.tm_year = yy;
	if (tm.tm_year < 100) tm.tm_year += 2000;		// 千年
	tm.tm_wday = GregorianDay(yy, mm, dd);			// 自行计算星期
	
	
	/* Enable PWR and BKP clocks */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
	/* Allow access to BKP Domain */
	PWR_BackupAccessCmd(ENABLE);

	/* Wait until last write operation on RTC registers has finished */
	RTC_WaitForLastTask();
	/* 修改当前RTC计数寄存器内容 */
	RTC_SetCounter(mktimev(tm));
	/* Wait until last write operation on RTC registers has finished */
	RTC_WaitForLastTask();
	
#elif USE_STM32F4XX
	RTC_DateTypeDef 	RTC_DateStructure;
	RTC_TimeTypeDef 	RTC_TimeStructure;

	RTC_DateStructure.RTC_Date  = dd > 31 ? 1 : dd;
	RTC_DateStructure.RTC_Month = mm > 12 ? 1 : mm;
	RTC_DateStructure.RTC_Year  = yy > 99 ? 1 : yy;
	RTC_DateStructure.RTC_WeekDay = GregorianDay(yy, mm, dd);	// 自行计算星期
	
	/* Configure the RTC date register */
	if(RTC_SetDate(RTC_Format_BIN, &RTC_DateStructure) == ERROR)
	{
		printf(" RTC Set Date failed. \n");	return;
	}
	
	// 设置RTC时钟
	RTC_TimeStructure.RTC_Hours = h > 23 ? 0 : h;
	RTC_TimeStructure.RTC_Minutes = m > 59 ? 0 : m;
	RTC_TimeStructure.RTC_Seconds = s > 59 ? 0 : s;
	RTC_TimeStructure.RTC_H12 = h >= 12 && h <= 23 ? RTC_H12_PM : RTC_H12_AM;

	if(RTC_SetTime(RTC_Format_BIN, &RTC_TimeStructure) == ERROR)
	{
		printf(" RTC Set Time failed. \n");	return;
	} 
	
#elif USE_STM32H7XX
	RTC_DateTypeDef 	RTC_DateStructure;
	RTC_TimeTypeDef 	RTC_TimeStructure;

	RTC_DateStructure.Date  = dd > 31 ? 1 : dd;
	RTC_DateStructure.Month = mm > 12 ? 1 : mm;
	RTC_DateStructure.Year  = yy > 99 ? 1 : yy;
	RTC_DateStructure.WeekDay = GregorianDay(yy, mm, dd);	// 自行计算星期
	
	/* Configure the RTC date register */
	if(HAL_RTC_SetDate(&m_hRtc, &RTC_DateStructure, RTC_FORMAT_BIN) == ERROR)
	{
		printf(" RTC Set Date failed. \n");	return;
	}
	
	// 设置RTC时钟
	RTC_TimeStructure.Hours = h > 23 ? 0 : h;
	RTC_TimeStructure.Minutes = m > 59 ? 0 : m;
	RTC_TimeStructure.Seconds = s > 59 ? 0 : s;
	RTC_TimeStructure.TimeFormat = h >= 12 && h <= 23 ? RTC_HOURFORMAT12_PM : RTC_HOURFORMAT12_AM;
	
	RTC_TimeStructure.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;		// 此处两个参数必须赋初值
	RTC_TimeStructure.StoreOperation = RTC_STOREOPERATION_RESET;
	RTC_TimeStructure.SubSeconds = 0;

	if(HAL_RTC_SetTime(&m_hRtc, &RTC_TimeStructure, RTC_FORMAT_BIN) == ERROR)
	{
		printf(" RTC Set Time failed. \n");	return;
	} 
#endif
	
	RTC_FreshTime();
}

void RTC_UpdateTime2(int nYMD, int nHMS)
{
	// 根据北京时间设置RTC时间，RTC的内部计数值是格林威治时间
	u8 h, m, s, dd, mm, yy;
	h = GetU8Hour(nHMS);
	m = GetU8Minute(nHMS);
	s = GetU8Second(nHMS);
	yy = GetU8Year(nYMD);
	mm = GetU8Month(nYMD);
	dd = GetU8Day(nYMD);
	
	RTC_UpdateTime(h, m, s, yy, mm, dd);
}

void RTC_UpdateTime3(u32 uGpsWeek, float fGpsSec)
{
	// 根据电罗经输出的国标时间设置RTC时间，RTC的内部计数值是格林威治时间
	// GPS周-周内秒到年月日系统的转换
	// gpsWeek：GPS周；
	// gpsWIS：gps周内秒 604800 = 7*24*60*60 (一周的秒数)
	u32 timeBegin, difFromBegin;
	timeBegin = RTC_Greenwich2Cnt(19800106, 0);
	difFromBegin = uGpsWeek * 604800 + (u32)fGpsSec;		// 罗经的格式中，时间是按照格林威治时间 1980年1月6日，0时
	RTC_Cnt2Systime(timeBegin + difFromBegin);				// 计算格林威治时间的秒数，然后转换为北京时间
	RTC_UpdateTime2(m_nRtcYMD, m_nRtcHMS);
}

#if USE_STM32F1XX || USE_STM32F4XX || USE_STM32H7XX
u32 GetYMD(struct rtc_time tm)
{
	u32 lnTime = tm.tm_year*10000 + tm.tm_mon*100 + tm.tm_mday;
	return lnTime;
}

u32 GetHMS(struct rtc_time tm)
{
	u32 lnTime = tm.tm_hour*10000 + tm.tm_min*100 + tm.tm_sec;
	return lnTime;
}
#endif

void RTC_FreshTime(void)
{
#if USE_STM32F1XX
	uint32_t BJ_TimeVar;
	/* 把标准时间定义的定时器的值转换为北京时间*/
	BJ_TimeVar = RTC_GetCounter() + 8 * 3600;
	to_tm(BJ_TimeVar, &m_stSystemtime);
	
	m_nRtcYMD = GetYMD(m_stSystemtime);
	m_nRtcHMS = GetHMS(m_stSystemtime);
	m_nRtcWeek = m_stSystemtime.tm_wday;
	
#elif USE_STM32F4XX
	RTC_TimeTypeDef		stRtcTime;
	RTC_DateTypeDef 	stRtcDate;
    RTC_GetTime(RTC_Format_BIN, &stRtcTime);
	RTC_GetDate(RTC_Format_BIN, &stRtcDate);
	
	m_stSystemtime.tm_hour 	= stRtcTime.RTC_Hours;
	m_stSystemtime.tm_min	= stRtcTime.RTC_Minutes;
	m_stSystemtime.tm_sec	= stRtcTime.RTC_Seconds;
	m_stSystemtime.tm_year	= stRtcDate.RTC_Year < 100 ? stRtcDate.RTC_Year+2000 : stRtcDate.RTC_Year;
	m_stSystemtime.tm_mon	= stRtcDate.RTC_Month;
	m_stSystemtime.tm_mday	= stRtcDate.RTC_Date;
	m_stSystemtime.tm_wday  = stRtcDate.RTC_WeekDay;
	
	m_nRtcYMD = GetYMD(m_stSystemtime);
	m_nRtcHMS = GetHMS(m_stSystemtime);
	m_nRtcWeek = m_stSystemtime.tm_wday;
	
#elif USE_STM32H7XX
	RTC_TimeTypeDef		stRtcTime;
	RTC_DateTypeDef 	stRtcDate;
    HAL_RTC_GetTime(&m_hRtc, &stRtcTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&m_hRtc, &stRtcDate, RTC_FORMAT_BIN);
	
	m_stSystemtime.tm_hour 	= stRtcTime.Hours;
	m_stSystemtime.tm_min	= stRtcTime.Minutes;
	m_stSystemtime.tm_sec	= stRtcTime.Seconds;
	m_stSystemtime.tm_year	= stRtcDate.Year < 100 ? stRtcDate.Year+2000 : stRtcDate.Year;
	m_stSystemtime.tm_mon	= stRtcDate.Month;
	m_stSystemtime.tm_mday	= stRtcDate.Date;
	m_stSystemtime.tm_wday  = stRtcDate.WeekDay;
	
	m_nRtcYMD = GetYMD(m_stSystemtime);
	m_nRtcHMS = GetHMS(m_stSystemtime);
	m_nRtcWeek = m_stSystemtime.tm_wday;
#endif
}

void RTC_WakeupEn(bool bEn)
{
	// 使能秒脉冲中断
	if ( bEn )
	{
#if USE_STM32F1XX
		RTC_ITConfig(RTC_IT_SEC, ENABLE);
#elif USE_STM32F4XX
		RTC_WakeUpCmd(DISABLE);									// 关闭WAKE UP
		RTC_WakeUpClockConfig(RTC_WakeUpClock_CK_SPRE_16bits);	// 唤醒时钟选择
		RTC_SetWakeUpCounter(0);								// 设置WAKE UP自动重装载寄存器
		
		EXTI_ClearITPendingBit(EXTI_Line22);	// 清除LINE22上的中断标志位 
		RTC_ClearFlag(RTC_FLAG_WUTF);			// 清除RTC WAKE UP的中断标志
		RTC_ITConfig(RTC_IT_WUT, ENABLE);		// 开启WAKE UP 定时器中断
		RTC_WakeUpCmd(ENABLE);					// 开启WAKE UP 定时器　
#elif USE_STM32H7XX
		// 使能秒中断（RTC_SEC中断）
		if (HAL_RTCEx_SetWakeUpTimer_IT(&m_hRtc, 0, RTC_WAKEUPCLOCK_CK_SPRE_16BITS) != HAL_OK)
				Error_Handler();
		// 清除秒中断标志位（避免一上电就触发中断）
		// 这里必须选清除wakeup中断标志位，否则可能触发不了中断
		__HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&m_hRtc, RTC_FLAG_WUTF);		
#endif	
	}
	else
	{
#if USE_STM32F1XX
		RTC_ITConfig(RTC_IT_SEC, DISABLE);
#elif USE_STM32F4XX
		RTC_ITConfig(RTC_IT_WUT, DISABLE);	
		RTC_WakeUpCmd(DISABLE);	
#elif USE_STM32H7XX
		HAL_NVIC_DisableIRQ(RTC_WKUP_IRQn);
		HAL_RTCEx_DeactivateWakeUpTimer(&m_hRtc);
		__HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&m_hRtc, RTC_FLAG_WUTF);	
#endif	
	}
}

/*
 * 函数名：RTC_GetFattime
 * 描述  ：得到文件Calendar格式的建立日期
 * 输出  ：Time供ff.c中的get_fattime()调用
 * 调用  ：外部调用
 */	
u32 RTC_GetFattime(void)
{
	u32 Time = 0;
	u32 temp = 0;

#if USE_STM32F1XX || USE_STM32F4XX || USE_STM32H7XX
	temp = m_stSystemtime.tm_year - 1980;
	Time |= temp << 25;
	temp = m_stSystemtime.tm_mon;
	Time |= temp << 21;
	temp = m_stSystemtime.tm_mday;
	Time |= temp << 16;
	temp = m_stSystemtime.tm_hour;
	Time |= temp << 11;
	temp = m_stSystemtime.tm_min;
	Time |= temp << 5;
	temp = m_stSystemtime.tm_sec;
	Time |= temp >> 1;
#endif
	
	return	Time;
}

u8 RTC_GetWDAY(int nYMD)
{
	// 根据时间结算星期
	u8 nY, nM, nD;
	nY = GetU8Year(nYMD);
	nM = GetU8Month(nYMD);
	nD = GetU8Day(nYMD);
	return GregorianDay(nY, nM, nD);
}

u32 RTC_GetSystimeCnt(void)
{
	// 获得当前时间计算的秒数值
	return RTC_YMDHMS2Cnt(m_nRtcYMD, m_nRtcHMS);
}

u32 RTC_YMDHMS2Cnt(u32 uYMD, u32 uHMS)
{
	// 将年月日，时分秒值合并计算秒数，输入时间为北京时间
	// 例子：20240101 80000
	struct rtc_time tm;
	tm.tm_hour 	= (u32)GetU8Hour(uHMS);
	tm.tm_min 	= (u32)GetU8Minute(uHMS);
	tm.tm_sec 	= (u32)GetU8Second(uHMS);
	tm.tm_year 	= uYMD/10000;
	tm.tm_mon 	= (u32)GetU8Month(uYMD);
	tm.tm_mday 	= (u32)GetU8Day(uYMD);
	return mktimev(tm);
}

u32 RTC_Greenwich2Cnt(u32 uYMD, u32 uHMS)
{
	// 将年月日，时分秒值合并计算秒数，输入时间为GPS的格林尼治时间
	// 例子：20240101 00000
	struct rtc_time tm;
	tm.tm_hour 	= (u32)GetU8Hour(uHMS);
	tm.tm_min 	= (u32)GetU8Minute(uHMS);
	tm.tm_sec 	= (u32)GetU8Second(uHMS);
	tm.tm_year 	= uYMD/10000;
	tm.tm_mon 	= (u32)GetU8Month(uYMD);
	tm.tm_mday 	= (u32)GetU8Day(uYMD);
	return mktimevGreenwich(tm);
}

u32  RTC_Cnt2YMD(u32 uCnt)
{
	// 将秒数转换为北京时间年月日值
	struct rtc_time tm;
	to_tm(uCnt + 8*3600, &tm);
	return tm.tm_year*10000 + tm.tm_mon*100 + tm.tm_mday;
}

u32  RTC_Cnt2HMS(u32 uCnt)
{
	// 将秒数转换为北京时间时分秒值
	struct rtc_time tm;
	to_tm(uCnt + 8*3600, &tm);
	return tm.tm_hour*10000 + tm.tm_min*100 + tm.tm_sec;
}

void RTC_Cnt2Systime(u32 uCnt)
{
	// 将秒数转换为北京时间对应的 struct rtc_time	m_stSystemtime;
	struct rtc_time tm;
	to_tm(uCnt + 8*3600, &tm);

	m_stSystemtime.tm_year	= tm.tm_year;
	m_stSystemtime.tm_mon	= tm.tm_mon;
	m_stSystemtime.tm_mday	= tm.tm_mday;
	m_stSystemtime.tm_hour	= tm.tm_hour;
	m_stSystemtime.tm_min	= tm.tm_min;
	m_stSystemtime.tm_sec	= tm.tm_sec;
	m_stSystemtime.tm_wday	= tm.tm_wday;

	m_nRtcWeek = tm.tm_wday;
	m_nRtcYMD = GetYMD(m_stSystemtime);
	m_nRtcHMS = GetHMS(m_stSystemtime);
}

u8 GetU8Year(long lnTime)
{
	// 年只取后两位
	u8 result = (u8)((lnTime/10000)%100);
	return result;
}

u8 GetU8Month(long lnTime)
{
	u8 result = (u8)((lnTime%10000)/100);
	if (result < 1 || result > 12)
	{
		result = 0;
	}
	return result;
}

u8 GetU8Day(long lnTime)
{
	u8 result = (u8)(lnTime%100);
	if (result < 1 || result > 31)
	{
		result = 0;
	}
	return result;
}

u8 GetU8Hour(long lnTime)
{
	u8 result = (u8)(lnTime/10000);
	if (result >= 24)
	{
		result = 0;
	}
	return result;
}

u8 GetU8Minute(long lnTime)
{
	u8 result = (u8)((lnTime%10000)/100);
	if (result > 59)
	{
		result = 0;
	}
	return result;
}

u8 GetU8Second(long lnTime)
{
	u8 result = (u8)(lnTime%100);
	if (result > 59)
	{
		result = 0;
	}
	return result;
}

#if USE_STM32F4XX || USE_STM32H7XX
void RTC_Set_AlarmA(int nYMD, int nHMS)
{
	//设置闹钟时间(按星期闹铃,24小时制)
	//week:星期几(1~7) @ref  RTC_Alarm_Definitions
	//hour,min,sec:小时,分钟,秒钟
	u8 nWeek, nH, nM, nS;
	nWeek = RTC_GetWDAY(nYMD);
	nH = GetU8Hour(nHMS);
	nM = GetU8Minute(nHMS);
	nS = GetU8Second(nHMS);

	// 配置RTC时钟的定时中断
	RTC_Set_AlarmA2(nWeek, nH, nM, nS);
}

void RTC_Set_AlarmA2(u8 week, u8 hour, u8 min, u8 sec)
{
	//设置闹钟时间(按星期闹铃,24小时制)
	//week:星期几(1~7) @ref  RTC_Alarm_Definitions
	//hour,min,sec:小时,分钟,秒钟
#if USE_STM32F4XX
	RTC_AlarmTypeDef	RTC_AlarmTypeInitStructure;
	RTC_TimeTypeDef		RTC_TimeTypeInitStructure;
	RTC_AlarmCmd(RTC_Alarm_A, DISABLE);				//关闭闹钟A 

	RTC_TimeTypeInitStructure.RTC_Hours = hour;		//小时
	RTC_TimeTypeInitStructure.RTC_Minutes = min;	//分钟
	RTC_TimeTypeInitStructure.RTC_Seconds = sec;	//秒
	RTC_TimeTypeInitStructure.RTC_H12 = RTC_H12_AM;

	RTC_AlarmTypeInitStructure.RTC_AlarmDateWeekDay = week;									//星期
	RTC_AlarmTypeInitStructure.RTC_AlarmDateWeekDaySel = RTC_AlarmDateWeekDaySel_WeekDay;	//按星期闹
	RTC_AlarmTypeInitStructure.RTC_AlarmMask = RTC_AlarmMask_None;							//精确匹配星期，时分秒
	RTC_AlarmTypeInitStructure.RTC_AlarmTime = RTC_TimeTypeInitStructure;
	RTC_SetAlarm(RTC_Format_BIN, RTC_Alarm_A, &RTC_AlarmTypeInitStructure);

	RTC_ClearITPendingBit(RTC_IT_ALRA);		//清除RTC闹钟A的标志
	EXTI_ClearITPendingBit(EXTI_Line17);	//清除LINE17上的中断标志位 

	RTC_ClearFlag(RTC_FLAG_ALRAF);			//清除中断标志
	RTC_ITConfig(RTC_IT_ALRA, ENABLE);		//开启闹钟A中断
	RTC_AlarmCmd(RTC_Alarm_A, ENABLE);		//开启闹钟A 
#endif	// USE_STM32F4XX
}

void RTC_Set_AlarmB(u16 min)
{
	// 按照参数给定的延迟分钟数设定闹铃
	
}

#endif	// USE_STM32F4XX || USE_STM32H7XX

// 使用后备存储器保存重要参数
void RTC_BackMemRead(uint16_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead)
{
#if USE_STM32F4XX
	// 从后备存储器中读取数据
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);		/* Enable the PWR clock */
	PWR_BackupAccessCmd(ENABLE);							/* Allow access to RTC */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_BKPSRAM, ENABLE); // 使能备份SRAM时钟
	PWR_BackupRegulatorCmd(ENABLE);							// 启用备份SRAM稳压器
	
	if(pBuffer == NULL) return;												// 无效的地址
	if(NumToRead==0 || ReadAddr+NumToRead >= BACKUP_SRAM_SIZE) return;		// 无效的数量，限制范围，只有4KB
	memcpy(pBuffer, (u8 *)BKPSRAM_BASE+ReadAddr, NumToRead);
#elif USE_STM32H7XX
	// 从后备存储器中读取数据
	if(pBuffer == NULL) return;												// 无效的地址
	if(NumToRead==0 || ReadAddr+NumToRead >= BACKUP_SRAM_SIZE) return;		// 无效的数量，限制范围，只有4KB
	memcpy(pBuffer, (u8 *)D3_BKPSRAM_BASE+ReadAddr, NumToRead);
#endif
}

void RTC_BackMemWrite(uint16_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite)
{
#if USE_STM32F4XX
	// 向后备存储器中写入数据
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);		/* Enable the PWR clock */
	PWR_BackupAccessCmd(ENABLE);							/* Allow access to RTC */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_BKPSRAM, ENABLE); // 使能备份SRAM时钟
	PWR_BackupRegulatorCmd(ENABLE);							// 启用备份SRAM稳压器
	
	if(pBuffer == NULL) return;												// 无效的地址
	if(NumToWrite==0 || WriteAddr+NumToWrite >= BACKUP_SRAM_SIZE) return;	// 无效的数量，限制范围，只有4KB
	memcpy((u8 *)BKPSRAM_BASE+WriteAddr, pBuffer, NumToWrite);
#elif USE_STM32H7XX
	if(pBuffer == NULL) return;												// 无效的地址
	if(NumToWrite==0 || WriteAddr+NumToWrite >= BACKUP_SRAM_SIZE) return;	// 无效的数量，限制范围，只有4KB
	memcpy((u8 *)D3_BKPSRAM_BASE+WriteAddr, pBuffer, NumToWrite);
#endif
}

u8 RTC_BackMemReadByte(uint16_t ReadAddr)
{
	// 从后备存储器中读取数据
#if USE_STM32F4XX
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);		/* Enable the PWR clock */
	PWR_BackupAccessCmd(ENABLE);							/* Allow access to RTC */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_BKPSRAM, ENABLE); // 使能备份SRAM时钟
	PWR_BackupRegulatorCmd(ENABLE);							// 启用备份SRAM稳压器

	if(ReadAddr >= BACKUP_SRAM_SIZE) return 0;				// 无效的数量，限制范围，只有4KB
	return *((u8 *)BKPSRAM_BASE+ReadAddr);					// 返回数据
#elif USE_STM32H7XX
	if(ReadAddr >= BACKUP_SRAM_SIZE) return 0;				// 无效的数量，限制范围，只有4KB
	return *((u8 *)D3_BKPSRAM_BASE+ReadAddr);				// 返回数据
#else
	return 0;
#endif
}

u8 RTC_BackMemWriteByte(uint16_t WriteAddr, u8 data)
{
	// 向后备存储器中写入数据，成功 返回1， 失败 返回0
#if USE_STM32F4XX
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);		/* Enable the PWR clock */
	PWR_BackupAccessCmd(ENABLE);							/* Allow access to RTC */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_BKPSRAM, ENABLE); // 使能备份SRAM时钟
	PWR_BackupRegulatorCmd(ENABLE);							// 启用备份SRAM稳压器
	
	if(WriteAddr >= BACKUP_SRAM_SIZE) return 0;				// 无效的数量，限制范围，只有4KB
	*((u8 *)BKPSRAM_BASE+WriteAddr) = data; return 1;		// 写入数据
#elif USE_STM32H7XX
	if(WriteAddr >= BACKUP_SRAM_SIZE) return 0;				// 无效的数量，限制范围，只有4KB
	*((u8 *)D3_BKPSRAM_BASE+WriteAddr) = data; return 1;	// 写入数据
#else
	return 0;
#endif
}

u32 RTC_BackRegRead(uint32_t nReg)
{
	// 从后备寄存器中读取数据
#if USE_STM32F1XX
	return BKP_ReadBackupRegister(nReg);
#elif USE_STM32F4XX
	return RTC_ReadBackupRegister(nReg);
#elif USE_STM32H7XX
	return HAL_RTCEx_BKUPRead(&m_hRtc, nReg);
#else
	return 0;
#endif
}

u32 RTC_BackRegWrite(uint32_t nReg, u32 data)
{
	// 向后备寄存器中写入数据，成功 返回1， 失败 返回0
#if USE_STM32F1XX
	BKP_WriteBackupRegister(nReg, data); return 1;
#elif USE_STM32F4XX
	RTC_WriteBackupRegister(nReg, data); return 1;
#elif USE_STM32H7XX
	HAL_RTCEx_BKUPWrite(&m_hRtc, nReg, data); return 1;
#else
	return 0;
#endif
}

u32 GetRemainderTime(u32 uCurTime, u32 uDstTime)
{
	// 计算到指定时间的剩余时间值，单位：秒
	if ( uDstTime >= uCurTime ) 
		return uDstTime - uCurTime;
	return 0;
}

u32 GetRemainderTimeCeil(u32 uCurTime)
{
	// 计算到当前时间最近的以小时凑整时刻剩余秒数
	u32 uDstTime = (( uCurTime/3600 ) + 1)*3600;
	return GetRemainderTime(uCurTime, uDstTime);
}

void GetCurrTimeCeil(u32 uCnt, int* uYMD, int* uHMS)
{
	// 计算当前时间最近的以小时凑整的年月日，时分秒
	if ( uCnt%3600 != 0 )
	{
		u32 uT = uCnt%3600 +3600;
		*uYMD = (int)RTC_Cnt2YMD(uT);
		*uHMS = (int)RTC_Cnt2HMS(uT);
	}
	else
	{
		*uYMD = (int)RTC_Cnt2YMD(uCnt);
		*uHMS = (int)RTC_Cnt2HMS(uCnt);
	}
}

