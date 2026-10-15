#include "Timer.h"
#include "PCLINK_operate.h"

#if USE_STM32H7XX
#include "STM32H7_operate.h"
#endif

bool m_bTim3Int = false;

u32	m_uPCLK1;	// PCLK1时钟频率
u32 m_uPCLK2;	// PCLK2时钟频率

#if USE_STM32H7XX
	TIM_HandleTypeDef m_htim2;
	TIM_HandleTypeDef m_htim3;
	TIM_HandleTypeDef m_htim4;
#endif

void TIMER2_Config(u16 HzPerSecond, bool bEnable)
{
	u32 uM;
	assert_param(HzPerSecond >= 1 && HzPerSecond <= 10000);
#if USE_STM32H7XX
	if ( bEnable )
	{
		TIM_ClockConfigTypeDef sClockSourceConfig = {0};
		TIM_SlaveConfigTypeDef sSlaveConfig = {0};
		TIM_MasterConfigTypeDef sMasterConfig = {0};

		uM = m_uPCLK1 / 1000000 * 200;								// 设置预分频周期为100uS
		m_htim2.Instance = TIM2;
		m_htim2.Init.CounterMode		= TIM_COUNTERMODE_UP;
		m_htim2.Init.Period 	 		= 10000 / HzPerSecond;		/* 自动重装载寄存器周期的值(计数值) */
		m_htim2.Init.Prescaler 	 		= (uM - 1);					/* 时钟预分频数 */
		m_htim2.Init.ClockDivision		= TIM_CLOCKDIVISION_DIV1;
		m_htim2.Init.AutoReloadPreload	= TIM_AUTORELOAD_PRELOAD_ENABLE;
		if (HAL_TIM_Base_Init(&m_htim2) != HAL_OK)
		{
			Error_Handler();
		}
		sClockSourceConfig.ClockSource	= TIM_CLOCKSOURCE_INTERNAL;
		if (HAL_TIM_ConfigClockSource(&m_htim2, &sClockSourceConfig) != HAL_OK)
		{
			Error_Handler();
		}
		sSlaveConfig.SlaveMode    		= TIM_SLAVEMODE_DISABLE;			// 禁用从模式
		sSlaveConfig.InputTrigger 		= TIM_TS_ITR0;
		if (HAL_TIM_SlaveConfigSynchro(&m_htim2, &sSlaveConfig) != HAL_OK)
		{
			Error_Handler();
		}
		sMasterConfig.MasterSlaveMode 		= TIM_MASTERSLAVEMODE_DISABLE;	// 禁用主模式
		sMasterConfig.MasterOutputTrigger 	= TIM_TRGO_UPDATE;
		if (HAL_TIMEx_MasterConfigSynchronization(&m_htim2, &sMasterConfig) != HAL_OK)
		{
			Error_Handler();
		}
		
		HAL_TIM_Base_Start_IT(&m_htim2);
	}
	else
	{
		HAL_TIM_Base_Stop_IT(&m_htim2);
	}
#else
	if ( bEnable )
	{
		TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 , ENABLE);

		TIM_DeInit(TIM2);//Deinitializes the TIMx peripheral registers to their default reset values

		uM = m_uPCLK1 / 1000000 * 200;									// 设置预分频周期为100uS
		TIM_TimeBaseStructure.TIM_Period 		= 10000 / HzPerSecond;	/* 自动重装载寄存器周期的值(计数值) */
		TIM_TimeBaseStructure.TIM_Prescaler 	= (uM - 1);				/* 时钟预分频数 */
		TIM_TimeBaseStructure.TIM_ClockDivision	= TIM_CKD_DIV1;			/* 采样分频 */
		TIM_TimeBaseStructure.TIM_CounterMode	= TIM_CounterMode_Up;	/* 向上计数模式 */
		TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
		
		TIM_ClearFlag(TIM2, TIM_FLAG_Update);					/* 清除溢出中断标志 */
		TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
		TIM_Cmd(TIM2, ENABLE);									/* 使能时钟 */
	}
	else
	{
		TIM_ClearFlag(TIM2, TIM_FLAG_Update);					/* 清除溢出中断标志 */
		TIM_ITConfig(TIM2, TIM_IT_Update, DISABLE);
		TIM_Cmd(TIM2, DISABLE);									/* 关闭时钟 */
	}
#endif
}	  

void TIMER3_Config(u16 HzPerSecond, bool bEnable)
{
	u32 uM;
	assert_param(HzPerSecond >= 1 && HzPerSecond <= 10000);
#if USE_STM32H7XX
	if ( bEnable )
	{
		TIM_ClockConfigTypeDef sClockSourceConfig = {0};
		TIM_SlaveConfigTypeDef sSlaveConfig = {0};
		TIM_MasterConfigTypeDef sMasterConfig = {0};

		uM = m_uPCLK1 / 1000000 * 200;								// 设置预分频周期为100uS
		m_htim3.Instance = TIM3;
		m_htim3.Init.CounterMode 		= TIM_COUNTERMODE_UP;
		m_htim3.Init.Period 	 		= 10000 / HzPerSecond;		/* 自动重装载寄存器周期的值(计数值) */
		m_htim3.Init.Prescaler 	 		= (uM - 1);					/* 时钟预分频数 */
		m_htim3.Init.ClockDivision     	= TIM_CLOCKDIVISION_DIV1;
		m_htim3.Init.AutoReloadPreload 	= TIM_AUTORELOAD_PRELOAD_ENABLE;
		if (HAL_TIM_Base_Init(&m_htim3) != HAL_OK)
		{
			Error_Handler();
		}
		sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
		if (HAL_TIM_ConfigClockSource(&m_htim3, &sClockSourceConfig) != HAL_OK)
		{
			Error_Handler();
		}
		sSlaveConfig.SlaveMode 			= TIM_SLAVEMODE_DISABLE;			// 禁用从模式
		sSlaveConfig.InputTrigger 		= TIM_TS_ITR0;
		if (HAL_TIM_SlaveConfigSynchro(&m_htim3, &sSlaveConfig) != HAL_OK)
		{
			Error_Handler();
		}
		sMasterConfig.MasterSlaveMode 		= TIM_MASTERSLAVEMODE_DISABLE;	// 禁用主模式
		sMasterConfig.MasterOutputTrigger 	= TIM_TRGO_RESET;
		if (HAL_TIMEx_MasterConfigSynchronization(&m_htim3, &sMasterConfig) != HAL_OK)
		{
			Error_Handler();
		}
		
		HAL_TIM_Base_Start(&m_htim3);
	}
	else
	{
		HAL_TIM_Base_Stop(&m_htim3);
	}
#else
	if ( bEnable )
	{
		TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3 , ENABLE);

		TIM_DeInit(TIM3);//Deinitializes the TIMx peripheral registers to their default reset values

		uM = m_uPCLK1 / 1000000 * 400;									// 设置预分频周期为100uS
		TIM_TimeBaseStructure.TIM_Period 		= 10000 / HzPerSecond;	/* 自动重装载寄存器周期的值(计数值) */
		TIM_TimeBaseStructure.TIM_Prescaler 	= (uM - 1);				/* 时钟预分频数 XXM */
		TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;			/* 采样分频 */
		TIM_TimeBaseStructure.TIM_CounterMode 	= TIM_CounterMode_Up;	/* 向上计数模式 */
		TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
		
		TIM_ClearFlag(TIM3, TIM_FLAG_Update);					/* 清除溢出中断标志 */
		TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
		TIM_Cmd(TIM3, ENABLE);									/* 使能时钟 */
	}
	else
	{
		TIM_ClearFlag(TIM3, TIM_FLAG_Update);					/* 清除溢出中断标志 */
		TIM_ITConfig(TIM3, TIM_IT_Update, DISABLE);
		TIM_Cmd(TIM3, DISABLE);									/* 关闭时钟 */
	}
#endif
	m_bTim3Int = false;
}

void TIMER3_Delay(Delay_Type nMode, const u16 nDelay)
{
	u32 uM;
#if USE_STM32H7XX
	TIM_ClockConfigTypeDef sClockSourceConfig = {0};
	TIM_SlaveConfigTypeDef sSlaveConfig = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};

	uM = m_uPCLK1 / 1000000 * 2;
	if (nMode == DELAY_MS)
	{
		m_htim3.Init.Period 	= nDelay*10;		/* 自动重装载寄存器周期的值(计数值) */
		m_htim3.Init.Prescaler  = (uM - 1)*100;		/* 时钟预分频数 XXM */
	}
	else
	{
		m_htim3.Init.Period 	= nDelay;			/* 自动重装载寄存器周期的值(计数值) */
		m_htim3.Init.Prescaler  = (uM - 1);			/* 时钟预分频数 XXM */
	}
	
	m_htim3.Instance = TIM3;
	m_htim3.Init.CounterMode 		= TIM_COUNTERMODE_UP;
	m_htim3.Init.ClockDivision 		= TIM_CLOCKDIVISION_DIV1;
	m_htim3.Init.AutoReloadPreload 	= TIM_AUTORELOAD_PRELOAD_ENABLE;
	if (HAL_TIM_Base_Init(&m_htim3) != HAL_OK)
	{
		Error_Handler();
	}
	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	if (HAL_TIM_ConfigClockSource(&m_htim3, &sClockSourceConfig) != HAL_OK)
	{
		Error_Handler();
	}
	sSlaveConfig.SlaveMode 			= TIM_SLAVEMODE_DISABLE;			// 禁用从模式
	sSlaveConfig.InputTrigger 		= TIM_TS_ITR0;
	if (HAL_TIM_SlaveConfigSynchro(&m_htim3, &sSlaveConfig) != HAL_OK)
	{
		Error_Handler();
	}
	sMasterConfig.MasterSlaveMode 		= TIM_MASTERSLAVEMODE_DISABLE;	// 禁用主模式
	sMasterConfig.MasterOutputTrigger	= TIM_TRGO_RESET;
	if (HAL_TIMEx_MasterConfigSynchronization(&m_htim3, &sMasterConfig) != HAL_OK)
	{
		Error_Handler();
	}
#else
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
	assert_param(nDelay <= 10000);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	TIM_DeInit(TIM3);//Deinitializes the TIMx peripheral registers to their default reset values

	uM = SystemCoreClock / 1000000 * 4;
	if (nMode == DELAY_MS)
	{
		TIM_TimeBaseStructure.TIM_Period 	= nDelay*10;	/* 自动重装载寄存器周期的值(计数值) */
		TIM_TimeBaseStructure.TIM_Prescaler = (uM - 1)*100;	/* 时钟预分频数 XXM */
	}
	else
	{
		TIM_TimeBaseStructure.TIM_Period 	= nDelay;		/* 自动重装载寄存器周期的值(计数值) */
		TIM_TimeBaseStructure.TIM_Prescaler = (uM - 1);		/* 时钟预分频数 XXM */
	}
	
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;		/* 采样分频 */
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;	/* 向上计数模式 */
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
	
	m_bTim3Int = false;
	TIM_ClearFlag(TIM3, TIM_FLAG_Update);					/* 清除溢出中断标志 */
	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
	TIM_Cmd(TIM3, ENABLE);									/* 使能时钟 */
#endif
	m_bTim3Int = false;
}

void TIMER4_Config(u16 HzPerSecond, bool bEnable)
{
	u32 uM;
	assert_param(HzPerSecond >= 1 && HzPerSecond <= 10000);
#if USE_STM32H7XX
	if ( bEnable )
	{
		TIM_ClockConfigTypeDef sClockSourceConfig = {0};
		TIM_SlaveConfigTypeDef sSlaveConfig = {0};
		TIM_MasterConfigTypeDef sMasterConfig = {0};

		uM = m_uPCLK1 / 1000000 * 200;								// 设置预分频周期为100uS
		m_htim4.Instance 				= TIM4;
		m_htim4.Init.CounterMode 		= TIM_COUNTERMODE_UP;
		m_htim4.Init.Period 	 		= 10000 / HzPerSecond;		/* 自动重装载寄存器周期的值(计数值) */
		m_htim4.Init.Prescaler 	 		= (uM - 1);					/* 时钟预分频数 */
		m_htim4.Init.ClockDivision 		= TIM_CLOCKDIVISION_DIV1;
		m_htim4.Init.AutoReloadPreload 	= TIM_AUTORELOAD_PRELOAD_ENABLE;
		if (HAL_TIM_Base_Init(&m_htim4) != HAL_OK)
		{
			Error_Handler();
		}
		sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
		if (HAL_TIM_ConfigClockSource(&m_htim4, &sClockSourceConfig) != HAL_OK)
		{
			Error_Handler();
		}
		sSlaveConfig.SlaveMode 				= TIM_SLAVEMODE_DISABLE;
		sSlaveConfig.InputTrigger 			= TIM_TS_ITR0;
		if (HAL_TIM_SlaveConfigSynchro(&m_htim4, &sSlaveConfig) != HAL_OK)
		{
			Error_Handler();
		}
		sMasterConfig.MasterSlaveMode 		= TIM_MASTERSLAVEMODE_DISABLE;
		sMasterConfig.MasterOutputTrigger 	= TIM_TRGO_UPDATE;
		if (HAL_TIMEx_MasterConfigSynchronization(&m_htim3, &sMasterConfig) != HAL_OK)
		{
			Error_Handler();
		}
		
		HAL_TIM_Base_Start_IT(&m_htim4);
	}
	else
	{
		HAL_TIM_Base_Stop_IT(&m_htim4);
	}
#else
	if ( bEnable )
	{
		TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

		TIM_DeInit(TIM4);//Deinitializes the TIMx peripheral registers to their default reset values

		uM = m_uPCLK1 / 1000000 * 400;									// 设置预分频周期为100uS
		TIM_TimeBaseStructure.TIM_Period 		= 10000 / HzPerSecond;	/* 自动重装载寄存器周期的值(计数值) */
		TIM_TimeBaseStructure.TIM_Prescaler 	= (uM - 1);				/* 时钟预分频数 */
		TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;			/* 采样分频 */
		TIM_TimeBaseStructure.TIM_CounterMode 	= TIM_CounterMode_Up;	/* 向上计数模式 */
		TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);
	
		TIM_ClearFlag(TIM4, TIM_FLAG_Update);					/* 清除溢出中断标志 */
		TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
		TIM_Cmd(TIM4, ENABLE);									/* 使能时钟 */
	}
	else
	{
		TIM_ClearFlag(TIM4, TIM_FLAG_Update);					/* 清除溢出中断标志 */
		TIM_ITConfig(TIM4, TIM_IT_Update, DISABLE);
		TIM_Cmd(TIM4, DISABLE);									/* 关闭时钟 */
	}
#endif
}

/**-------------------------------------------------------
* @函数名 SZ_STM32_SysTickInit
* @功能   初始化系统定时器SysTick
*         用户可以根据需要修改
* @参数   每秒中断次数
* @返回值 无
***------------------------------------------------------*/
void SysTick_Init(uint32_t HzPerSecond)
{
	/* HzPreSecond = 1000 to Setup SysTick Timer for 1 msec interrupts.
	------------------------------------------
	1. The SysTick_Config() function is a CMSIS function which configure:
	- The SysTick Reload register with value passed as function parameter.
	- Configure the SysTick IRQ priority to the lowest value (0x0F).
	- Reset the SysTick Counter register.
	- Configure the SysTick Counter clock source to be Core Clock Source (HCLK).
	- Enable the SysTick Interrupt.
	- Start the SysTick Counter.

	2. You can change the SysTick Clock source to be HCLK_Div8 by calling the
	SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8) just after the
	SysTick_Config() function call. The SysTick_CLKSourceConfig() is defined
	inside the misc.c file.

	3. You can change the SysTick IRQ priority by calling the
	NVIC_SetPriority(SysTick_IRQn,...) just after the SysTick_Config() function
	call. The NVIC_SetPriority() is defined inside the core_cm3.h file.

	4. To adjust the SysTick time base, use the following formula:

	Reload Value = SysTick Counter Clock (Hz) x  Desired Time base (s)
	- Reload Value is the parameter to be passed for SysTick_Config() function
	- Reload Value should not exceed 0xFFFFFF
	*/
	if (SysTick_Config(SystemCoreClock / HzPerSecond))
	{
		/* Capture error */
		while (1);
	}
}

void SysTick_DeInit(void)
{
	// 禁止系统定时器
	SysTick->CTRL  = (~SysTick_CTRL_CLKSOURCE_Msk) & 
                   (~SysTick_CTRL_TICKINT_Msk)   & 
                   (~SysTick_CTRL_ENABLE_Msk);     	/* Disable SysTick IRQ and SysTick Timer */
}

/************************************************************************/
// 由于延时函数和系统时钟配置有直接联系，因此在改变系统时钟配置时
// 通过参数传递改变延时函数的参数，保证延时时间的准确性
/************************************************************************/
void delay_ms(u16 nms)
{
	TIMER3_Delay(DELAY_MS, nms);
	while (m_bTim3Int == false);
}

void delay_us(u16 nus)
{
	TIMER3_Delay(DELAY_US, nus);
	while (m_bTim3Int == false);
}

void delay(u16 nms)
{
	// 为了避免中断嵌套，使用CPU空转进行延时
	u32 nCnt;
	u32 nTime = nms/1000;
	// 按照秒计数
	for (; nTime>0; nTime--)
	{
		u32 nCnt = SystemCoreClock / 5000 * 1000;
		for (; nCnt > 0; nCnt--) __NOP();
	}
	// 按照毫秒计数
	nCnt = SystemCoreClock / 5000 * (nms%1000);
	for (; nCnt > 0; nCnt--) __NOP();
	__NOP();
}

void delayus(u16 nus)
{
	// 为了避免中断嵌套，使用CPU空转进行延时
	u32 nCnt;
	if (nus/1000 > 0)
	{
		delay(nus/1000);	// 超过ms按照ms函数延时
	}
	
	nCnt = SystemCoreClock / 3000000 * (nus%1000);
	for (; nCnt > 0; nCnt--) __NOP();
	__NOP();
}


void OnTimer2Int(void)
{
	// 响应TIMER2定时器
#if DEVICE_TYPE == tDeviceIListenS	|| DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceILogger	|| \
	DEVICE_TYPE == tDeviceIPowerAmp
	Device_OnTimer2();	
#else
	// 终止定时器
	TIMER2_Config(0, false);
#endif
}

void OnTimer4Int(void)
{
	// 响应TIMER4定时器
#if DEVICE_TYPE == tDeviceIListenS	|| DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceILogger	|| \
	DEVICE_TYPE == tDeviceIPowerAmp
	Device_OnTimer4();	
#else
	// 终止定时器
	TIMER4_Config(0, false);
#endif
}

