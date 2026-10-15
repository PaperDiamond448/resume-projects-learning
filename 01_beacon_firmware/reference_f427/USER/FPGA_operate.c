
#include "FPGA_operate.h"
#include "PCLINK_operate.h"

bool m_bFpgaInitRdy		= false;	// FPGA接口初始化状态
u16 m_nFpgaInt  = ACQ_PROC_NULL;	// FPGA数据中断传输结果状态标志
u16 m_nFpgaIntB = ACQ_PROC_NULL;	// FPGA数据中断传输结果状态标志B 
u16 m_uFpgaIntReg = 0;				// FPGA中断寄存器参数

void FPGA_IntInit(bool bInit)
{
	// FPGA初始化中断引脚
#if USE_STM32F4XX
	EXTI_InitTypeDef EXTI_InitStructure;
    
	if ( bInit )
	{
		RCC_AHB1PeriphClockCmd(FPGA_INT_RCC_AHB1Periph, ENABLE);
		GPIO_SetIn(FPGA_INT_GPIO_PORT, FPGA_INT_GPIO, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_UP);

		SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource0);
		EXTI_InitStructure.EXTI_Line = EXTI_Line0;
		EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
		EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
		EXTI_InitStructure.EXTI_LineCmd = ENABLE;
		EXTI_Init(&EXTI_InitStructure); 
	}
	else
	{
		GPIO_SetIn(FPGA_INT_GPIO_PORT, FPGA_INT_GPIO, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_DOWN);
		
		SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource0);
		EXTI_InitStructure.EXTI_Line = EXTI_Line0;
		EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
		EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
		EXTI_InitStructure.EXTI_LineCmd = DISABLE;
		EXTI_Init(&EXTI_InitStructure); 
	} 
#endif
	FPGA_IntEn(false);		// 初始化后先禁止FPGA中断
}

void FPGA_IntEn(bool bEn)
{
	// 使能或者禁止FPGA中断
	// 设置0级-2号中断响应FPGA中断
	// 设置1级-0号中断响应存储中断处理
	// 两个中断不能同级，否则会造成中断不能嵌套
#if USE_STM32F4XX
	NVIC_InitTypeDef 	NVIC_InitStructure;
	EXTI_ClearITPendingBit(EXTI_Line0);		// 清除中断0
	
	if (bEn)
	{	
		NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;	// 先占优先级
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;			// 从优先级
		NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;				// 初始化使能中断
		NVIC_Init(&NVIC_InitStructure);
	}
	else
	{
		NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;	// 先占优先级
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;			// 从优先级
		NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;			// 初始化使能中断
		NVIC_Init(&NVIC_InitStructure);
	}
#endif
}

void FPGA_FMC_Init(bool bInit)
{
     // 根据参数配置ARM与FPGA连接的FMC接口设置
#if USE_STM32F4XX && USE_FMC_CMD_PORT
	// 初始化FSMC接口
	// 1.使用的芯片STM32F42X
	// 2.用FSMC连接FPGA，都是采用16位的数据接口，工作模式为异步的PSRAM模式
	// 片选和CPU的88引脚即PD7（FMC_NE1）相连接
	// 地址 0x6000 0000 - 0x63FF FFFF
	FMC_NORSRAMInitTypeDef  FMC_NORSRAMInitStructure;
	FMC_NORSRAMTimingInitTypeDef  p;

	if ( bInit )
	{
		RCC_AHB3PeriphClockCmd(RCC_AHB3Periph_FMC, ENABLE);
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA|RCC_AHB1Periph_GPIOB|RCC_AHB1Periph_GPIOC|RCC_AHB1Periph_GPIOD|RCC_AHB1Periph_GPIOE, ENABLE);

		GPIO_PinAFConfig(GPIOB, GPIO_PinSource7, GPIO_AF_FMC);
		GPIO_SetFunc(GPIOB, GPIO_Pin_7, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_NOPULL);

		GPIO_PinAFConfig(GPIOD, GPIO_PinSource0,  GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOD, GPIO_PinSource1,  GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOD, GPIO_PinSource4,  GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOD, GPIO_PinSource5,  GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOD, GPIO_PinSource7,  GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOD, GPIO_PinSource8,  GPIO_AF_FMC); 
		GPIO_PinAFConfig(GPIOD, GPIO_PinSource9,  GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOD, GPIO_PinSource10, GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOD, GPIO_PinSource14, GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOD, GPIO_PinSource15, GPIO_AF_FMC);

		GPIO_SetFunc(GPIOD, 
			GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_14 | GPIO_Pin_15, 
			GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_NOPULL);

		GPIO_PinAFConfig(GPIOE, GPIO_PinSource7 ,  GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOE, GPIO_PinSource8 ,  GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOE, GPIO_PinSource9 ,  GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOE, GPIO_PinSource10 , GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOE, GPIO_PinSource11 , GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOE, GPIO_PinSource12 , GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOE, GPIO_PinSource13 , GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOE, GPIO_PinSource14 , GPIO_AF_FMC);
		GPIO_PinAFConfig(GPIOE, GPIO_PinSource15 , GPIO_AF_FMC);

		GPIO_SetFunc(GPIOE, 
			GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15, 
			GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_NOPULL);

		/*-- FSMC Configuration ------------------------------------------------------*/
		p.FMC_AddressSetupTime 	= 6;
		p.FMC_AddressHoldTime 	= 12;
		p.FMC_DataSetupTime 	= 6;
		p.FMC_BusTurnAroundDuration = 0;
		p.FMC_CLKDivision 		= 0;	// don't care
		p.FMC_DataLatency 		= 0;	// don't care
		p.FMC_AccessMode 		= FMC_AccessMode_A;

		FMC_NORSRAMInitStructure.FMC_Bank 				= FMC_Bank1_NORSRAM1;
		FMC_NORSRAMInitStructure.FMC_DataAddressMux 	= FMC_DataAddressMux_Enable;
		FMC_NORSRAMInitStructure.FMC_MemoryType 		= FMC_MemoryType_PSRAM;
		FMC_NORSRAMInitStructure.FMC_MemoryDataWidth 	= FMC_NORSRAM_MemoryDataWidth_16b;
		FMC_NORSRAMInitStructure.FMC_BurstAccessMode 	= FMC_BurstAccessMode_Disable;
		FMC_NORSRAMInitStructure.FMC_AsynchronousWait 	= FMC_AsynchronousWait_Disable;  
		FMC_NORSRAMInitStructure.FMC_WaitSignalPolarity = FMC_WaitSignalPolarity_Low;
		FMC_NORSRAMInitStructure.FMC_WrapMode 			= FMC_WrapMode_Disable;
		FMC_NORSRAMInitStructure.FMC_WaitSignalActive 	= FMC_WaitSignalActive_BeforeWaitState;
		FMC_NORSRAMInitStructure.FMC_WriteOperation 	= FMC_WriteOperation_Enable;
		FMC_NORSRAMInitStructure.FMC_WaitSignal 		= FMC_WaitSignal_Disable;
		FMC_NORSRAMInitStructure.FMC_ExtendedMode 		= FMC_ExtendedMode_Disable;
		FMC_NORSRAMInitStructure.FMC_WriteBurst 		= FMC_WriteBurst_Disable;
		FMC_NORSRAMInitStructure.FMC_ContinousClock		= FMC_CClock_SyncOnly;
		FMC_NORSRAMInitStructure.FMC_ReadWriteTimingStruct = &p;
		FMC_NORSRAMInitStructure.FMC_WriteTimingStruct 	= &p;

		FMC_NORSRAMInit(&FMC_NORSRAMInitStructure); 

		/*!< Enable FSMC Bank1_SRAM1 Bank */
		FMC_NORSRAMCmd(FMC_Bank1_NORSRAM1, ENABLE); 
	}
	else
	{
		RCC_AHB3PeriphClockCmd(RCC_AHB3Periph_FMC, DISABLE);
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, DISABLE);		// 只能关闭 PortE 的时钟

		GPIO_SetIn(GPIOB, GPIO_Pin_7, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_NOPULL);

		GPIO_SetIn(GPIOD, 
			GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_14 | GPIO_Pin_15, 
			GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_NOPULL);

		GPIO_SetIn(GPIOE, 
			GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15, 
			GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_NOPULL);

		/*!< Enable FSMC Bank1_SRAM1 Bank */
		FMC_NORSRAMCmd(FMC_Bank1_NORSRAM1, DISABLE); 
	}
#endif	
}

void FPGA_ClkConfig(bool bInit)
{
	// 利用Timer配置FPGA时钟
#if USE_STM32F4XX
	if (bInit == true)
	{
		TIM_TimeBaseInitTypeDef TimeBaseStructure;
		TIM_OCInitTypeDef  		OCInitStructure;
	#if DEVICE_SUBTYPE/100 == iBeaconR
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);	// TIM开时钟
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB , ENABLE);	// GPIO开时钟

		GPIO_PinAFConfig(GPIOB, GPIO_PinSource0, GPIO_AF_TIM3);
		GPIO_SetFunc(GPIOB, GPIO_Pin_0, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_NOPULL);

		TIM_DeInit(TIM3);
		
		if( m_nSysClkType == CLK_160MHz || m_nSysClkType == CLK_HSI160MHz)
			TimeBaseStructure.TIM_Period = 0x0004;				// 160MHz主频下，设置PCLK2为80M, 分频系数为 (TIM_Period+1) = 16MHz
		else if( m_nSysClkType == CLK_96MHz  || m_nSysClkType == CLK_HSI96MHz )
			TimeBaseStructure.TIM_Period = 0x0001;				// 96MHz主频下，设置PCLK2为48M, 分频系数为 (TIM_Period+1)  = 24MHz

		TimeBaseStructure.TIM_Prescaler = 0x0;					// 设置时钟频率除数的预分频值
		TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;		// 设置时钟分割
		TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;	// 向上计数模式
		TIM_TimeBaseInit(TIM3, &TimeBaseStructure);

		/* PWM1 Mode configuration: Channel3 */
		OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
		OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
		
		if( m_nSysClkType == CLK_160MHz || m_nSysClkType == CLK_HSI160MHz)
			OCInitStructure.TIM_Pulse = 0x0002;					// 占空比2:3
		else if( m_nSysClkType == CLK_96MHz || m_nSysClkType == CLK_HSI96MHz )
			OCInitStructure.TIM_Pulse = 0x0001;					// 占空比1:1
		
		OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
		TIM_OC3Init(TIM3, &OCInitStructure);
		TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
		TIM_ARRPreloadConfig(TIM3, ENABLE);

		/* TIM3 enable counter */
		TIM_Cmd(TIM3, ENABLE);
		
	#elif DEVICE_SUBTYPE/100 == iBeaconR2	
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);	// TIM开时钟
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA , ENABLE);	// GPIO开时钟
		
		GPIO_PinAFConfig(GPIOA, GPIO_PinSource8, GPIO_AF_TIM1);
		GPIO_SetFunc(GPIOA, GPIO_Pin_8, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_NOPULL);
		
		TIM_DeInit(TIM1);
		
		if( m_nSysClkType == CLK_160MHz || m_nSysClkType == CLK_HSI160MHz )
			TimeBaseStructure.TIM_Period = 0x0009;				// 160MHz主频下，设置PCLK2为80M, 分频系数为 PCLK2*2/(TIM_Period+1) = 16MHz
		else if( m_nSysClkType == CLK_96MHz  || m_nSysClkType == CLK_HSI96MHz )
			TimeBaseStructure.TIM_Period = 0x0003;				// 96MHz主频下，设置PCLK2为48M,  分频系数为 PCLK2*2/(TIM_Period+1)  = 24MHz

		TimeBaseStructure.TIM_Prescaler = 0x0;					// 设置时钟频率除数的预分频值
		TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;		// 设置时钟分割
		TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;	// 向上计数模式
		TIM_TimeBaseInit(TIM1, &TimeBaseStructure);

		/* PWM1 Mode configuration: Channel1 */
		OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
		OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
		TIM1->BDTR |= 1<<15;									// 此寄存器位必须置位才能有输出
		TIM1->CR2   = 0;
		
		if( m_nSysClkType == CLK_160MHz || m_nSysClkType == CLK_HSI160MHz)
			OCInitStructure.TIM_Pulse = 0x0005;					// 占空比5:5
		else if( m_nSysClkType == CLK_96MHz  || m_nSysClkType == CLK_HSI96MHz )
			OCInitStructure.TIM_Pulse = 0x0002;					// 占空比1:1
		
		OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
		TIM_OC1Init(TIM1, &OCInitStructure);
		TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
		TIM_ARRPreloadConfig(TIM1, ENABLE);

		/* TIM1 enable counter */
		TIM_Cmd(TIM1, ENABLE);
	#else
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);	// TIM开时钟
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB , ENABLE);	// GPIO开时钟

		GPIO_PinAFConfig(GPIOB, GPIO_PinSource0, GPIO_AF_TIM3);
		GPIO_SetFunc(GPIOB, GPIO_Pin_0, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_NOPULL);

		TIM_DeInit(TIM3);
		
		if( m_nSysClkType == CLK_160MHz || m_nSysClkType == CLK_HSI160MHz)
			TimeBaseStructure.TIM_Period = 0x0004;				// 160MHz主频下，设置PCLK2为80M, 分频系数为 (TIM_Period+1) = 16MHz
		else if( m_nSysClkType == CLK_192MHz )
			TimeBaseStructure.TIM_Period = 0x0005;				// 192MHz主频下，设置PCLK2为96M, 分频系数为 (TIM_Period+1) = 16MHz
		else if( m_nSysClkType == CLK_96MHz  || m_nSysClkType == CLK_HSI96MHz )
			TimeBaseStructure.TIM_Period = 0x0002;				// 96MHz主频下， 设置PCLK2为48M, 分频系数为 (TIM_Period+1) = 16MHz

		TimeBaseStructure.TIM_Prescaler = 0x0;					// 设置时钟频率除数的预分频值
		TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;		// 设置时钟分割
		TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;	// 向上计数模式
		TIM_TimeBaseInit(TIM3, &TimeBaseStructure);

		/* PWM1 Mode configuration: Channel3 */
		OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
		OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
		
		if( m_nSysClkType == CLK_160MHz || m_nSysClkType == CLK_HSI160MHz)
			OCInitStructure.TIM_Pulse = 0x0002;					// 占空比2:3
		else if( m_nSysClkType == CLK_192MHz )
			OCInitStructure.TIM_Pulse = 0x0003;					// 占空比3:3
		else if( m_nSysClkType == CLK_96MHz || m_nSysClkType == CLK_HSI96MHz )
			OCInitStructure.TIM_Pulse = 0x0001;					// 占空比1:1
		
		OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
		TIM_OC3Init(TIM3, &OCInitStructure);
		TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
		TIM_ARRPreloadConfig(TIM3, ENABLE);

		/* TIM3 enable counter */
		TIM_Cmd(TIM3, ENABLE);
	#endif
	}
	else
	{
	#if DEVICE_SUBTYPE/100 == iBeaconR2
		/* TIM1 enable counter */
		TIM_Cmd(TIM1, DISABLE);
		TIM_DeInit(TIM1);

		RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, DISABLE);	//TIM关时钟
		GPIO_SetOut(GPIOA, GPIO_Pin_8, GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
	#else
		/* TIM3 enable counter */
		TIM_Cmd(TIM3, DISABLE);
		TIM_DeInit(TIM3);

		RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, DISABLE);	// TIM关时钟
		GPIO_SetIn(GPIOB, GPIO_Pin_0, GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_NOPULL);
	#endif
	}
#endif
}

void FPGA_Init(void) 
{
    // 配置FPGA采集相关设置
	if (!m_bFpgaInitRdy)
	{
		FPGA_ClkConfig(true);				// 初始化FPGA时钟
		
	#if USE_FMC_CMD_PORT
		FPGA_FMC_Init(true); 						// 初始化FPGA与ARM的FMC接口
	#elif USE_SPI_CMD_PORT && USE_SPI1_AS_CMD_PORT
		SPI1_Init(true); delay(1);					// 初始化FPGA与ARM的SPI1接口
	#elif USE_SPI_CMD_PORT && USE_SPI4_AS_CMD_PORT
		SPI4_Init(true); delay(1);					// 初始化FPGA与ARM的SPI4接口
	#elif USE_SPI_CMD_PORT && USE_SPI5_AS_CMD_PORT
		SPI5_Init(true); delay(1);					// 初始化FPGA与ARM的SPI5接口
	#endif
		
	#if USE_SPI_DATA_PORT && USE_SPI1_AS_DATA_PORT
		SPI1_Init(true); delay(1);					// 初始化FPGA与ARM的SPI1接口
	#elif USE_SPI_DATA_PORT && USE_SPI4_AS_DATA_PORT
		SPI4_Init(true); delay(1);					// 初始化FPGA与ARM的SPI4接口
	#endif
	
	#if USE_UART_DATA_PORT && USE_COMM1_AS_DATA_PORT
		Comm1_Config(USE_SPORT_BAUD, true);			// 初始化串口波特率
	#endif
		
		FPGA_SetAcqEn(false);				// 初始化禁止采集
		FPGA_IntInit(true);					// 初始化FPGA采集中断
		m_bFpgaInitRdy = true;	
	}
	else
	{
		FPGA_ClkConfig(true);				// 初始化FPGA时钟
	}
}

void FPGA_DeInit() 
{
    // 配置FPGA采集相关设置
	if (m_bFpgaInitRdy)
	{
		FPGA_ClkConfig(false);				// 初始化FPGA时钟
	#if USE_FMC_CMD_PORT
		FPGA_FMC_Init(false); 						// 初始化FPGA与ARM的FMC接口
	#elif USE_SPI_CMD_PORT && USE_SPI1_AS_CMD_PORT
		SPI1_Init(false); delay(1);					// 初始化FPGA与ARM的SPI1接口
	#elif USE_SPI_CMD_PORT && USE_SPI4_AS_CMD_PORT
		SPI4_Init(false); delay(1);					// 初始化FPGA与ARM的SPI4接口
	#elif USE_SPI_CMD_PORT && USE_SPI5_AS_CMD_PORT
		SPI5_Init(false); delay(1);					// 初始化FPGA与ARM的SPI5接口
	#endif
	
	#if USE_UART_DATA_PORT && USE_COMM1_AS_DATA_PORT
		Comm1_Config(USE_SPORT_BAUD, false);		// 初始化串口波特率
	#endif
	
		FPGA_IntInit(false);				// 禁止FPGA采集中断
		FPGA_SetAcqEn(false);				// 禁止采集
		m_bFpgaInitRdy = false;	
	}
}

void FPGA_WriteBuffer(uint16_t* pBuffer, uint32_t WriteAddr, uint32_t NumWordToWrite)
{
	for (; NumWordToWrite != 0; NumWordToWrite--) 
	{
		/* Transfer data to the memory */
		*(uint16_t *) (Bank1_SRAM1_ADDR + WriteAddr) = (uint16_t)(*pBuffer++);
		WriteAddr += 2;	/* Increment the address*/
	}
}

void FPGA_ReadBuffer(uint16_t* pBuffer, uint32_t ReadAddr, uint32_t NumWordToWrite)
{
	for (; NumWordToWrite != 0; NumWordToWrite--) 
	{
		/* Read a word from the memory */
		*pBuffer++ = (uint16_t)(*(__IO uint16_t*) (Bank1_SRAM1_ADDR + ReadAddr));
		ReadAddr += 2;	/* Increment the address*/
	}
}

void FPGA_CMD_Write(u16 uAddr, u16 uData)
{
#if USE_SPI_CMD_PORT && USE_SPI1_AS_CMD_PORT		// FPGA_CMD_Write
	u16 uRecv, uAddrb = ((uAddr/2)&0x00FF)|0x0080;	// 为了兼容FSMC接口地址增加的2倍，这里要除以2，bit7 = 1 表示写入
	do {
		uRecv = SPI1_ReadWrite3Byte(uAddrb, uData);
	}while( uRecv != uData );						// 确保写入数据正确
#elif USE_SPI_CMD_PORT && USE_SPI4_AS_CMD_PORT		// FPGA_CMD_Write
	u16 uRecv, uAddrb = ((uAddr/2)&0x00FF)|0x0080;	// 为了兼容FSMC接口地址增加的2倍，这里要除以2，bit7 = 1 表示写入
	do {
		uRecv = SPI4_ReadWrite3Byte(uAddrb, uData);
	}while( uRecv != uData );						// 确保写入数据正确
#elif USE_SPI_CMD_PORT && USE_SPI5_AS_CMD_PORT		// FPGA_CMD_Write
	u16 uRecv, uAddrb = ((uAddr/2)&0x00FF)|0x0080;	// 为了兼容FSMC接口地址增加的2倍，这里要除以2，bit7 = 1 表示写入
	do {
		uRecv = SPI5_ReadWrite3Byte(uAddrb, uData);
	}while( uRecv != uData );						// 确保写入数据正确
#elif USE_FMC_CMD_PORT
	*(uint16_t *) (Bank1_SRAM1_ADDR + uAddr) = (uint16_t)uData;	delayus(1);
	*(uint16_t *) (Bank1_SRAM1_ADDR + uAddr) = (uint16_t)uData;
#endif
	delayus(1);
}

u16  FPGA_CMD_Read(u16 uAddr)
{
	u16 uResult = 0;
#if USE_SPI_CMD_PORT && USE_SPI1_AS_CMD_PORT	// FPGA_CMD_Read
	u16 uSend = (uAddr/2)&0x007F;				// 为了兼容FSMC接口地址增加的2倍，这里要除以2，bit7 = 0 表示读取
	uResult = SPI1_ReadWrite3Byte(uSend, 0x0000);
#elif USE_SPI_CMD_PORT && USE_SPI4_AS_CMD_PORT	// FPGA_CMD_Read
	u16 uSend = (uAddr/2)&0x007F;				// 为了兼容FSMC接口地址增加的2倍，这里要除以2，bit7 = 0 表示读取
	uResult = SPI4_ReadWrite3Byte(uSend, 0x0000);
#elif USE_SPI_CMD_PORT && USE_SPI5_AS_CMD_PORT	// FPGA_CMD_Read
	u16 uSend = (uAddr/2)&0x007F;				// 为了兼容FSMC接口地址增加的2倍，这里要除以2，bit7 = 0 表示读取
	uResult = SPI5_ReadWrite3Byte(uSend, 0x0000);
#elif USE_FMC_CMD_PORT
	uResult = (u16)*(uint16_t *) (Bank1_SRAM1_ADDR + uAddr);
#endif
	return uResult;
}

u16 FPGA_GetIntReg(void)
{
	// FPGA读取中断寄存器参数
	u16 uIntReg = FPGA_CMD_Read(FPGA_ADDR_INT_REG);

#if DEVICE_SUBTYPE/100 == iBeaconR2
	if ( TIM1->CR2 && 0x0001) TIM1->CR2 = 0;	// GD32F4 上电复位此寄存器有异常，需要强制清除
#endif
	return uIntReg;
}

void FPGA_SetAcqEn(bool bEn)
{
	// 设置FPGA采集使能信号
#ifdef ACQ_EN_GPIO
	if ( bEn )
		GPIO_ResetBits(ACQ_EN_GPIO_PORT, ACQ_EN_GPIO);
	else
		GPIO_SetBits(ACQ_EN_GPIO_PORT, ACQ_EN_GPIO);
#else
	if ( bEn )
		FPGA_CMD_Write(FPGA_ADDR_ACQ_ENABLE, 0x0001);
	else
		FPGA_CMD_Write(FPGA_ADDR_ACQ_ENABLE, 0x0000);
#endif
	m_bStartStatus = bEn;
}

void FPGA_SetCalEn(bool bEn)
{
	// 设置FPGA基阵校准使能
#if DEVICE_SUBTYPE/100 == iPositionT1
	iPosition_SetCalEn( bEn );
#endif
}

void FPGA_SetSampleRate(u8 nRate)
{
	// 设置AD的采样速率
#if DEVICE_SUBTYPE/100 == iBeaconR	|| DEVICE_SUBTYPE/100 == iBeaconR2
	FPGA_CMD_Write(FPGA_ADDR_SAMPLE_RATE, nRate);
#endif
}

void FPGA_SetSampleGain(u8 nGain)
{
	// 设置AD的增益
#if DEVICE_SUBTYPE/100 == iBeaconR	|| DEVICE_SUBTYPE/100 == iBeaconR2
	FPGA_CMD_Write(FPGA_ADDR_SAMPLE_GAIN, nGain);
#endif
}

void FPGA_SetEmitEn(bool bEn)
{
	// 设置校阵脉冲发射，或者根据压力传感器数值使能发射
#if DEVICE_SUBTYPE/100 == iPositionT1
	iPosition_SetEmitEn( bEn );
#endif
}

void FPGA_SetDeadGate(u16 nGate)
{
	FPGA_CMD_Write(FPGA_ADDR_DEAD_GATE, nGate);
}

void FPGA_SetCatchGate(u16 nGate)
{
	// 脉宽鉴宽时间
	FPGA_CMD_Write(FPGA_ADDR_CATCH_GATE, nGate);
}

float FPGA_GetNoiseAvgData(void)
{
	// 从FPGA读取噪声平滑滤波器的参数并转换为输出电压
	// AD数据参考电压按照宏定义设置
	float fData;
	fData = (float)FPGA_CMD_Read(FPGA_ADDR_SIG_AVERAGE);
	fData = fData/32768.0f*ACQ_REF_VOLTAGE;
	return fData;
}

int FPGA_AcqProc(void)
{
	// FPGA采集处理程序
	int nRes = ACQ_PROC_NULL;//函数开头先给返回值一个默认状态
	u16 uFpgaReg = FPGA_GetIntReg();						// 读取FGPA中断寄存器参数
#if DEVICE_SUBTYPE/100 == iBeaconB2
	iBeacon_OnEchoAcq();
#endif
#if DEVICE_SUBTYPE/100 == iBeaconR
	if (m_bStartStatus)
	{
		if (uFpgaReg & 0x4000)								// 先判断是否是一次新的处理过程开始如果是则初始化
		{
			// 从FPGA中读取原始数据，然后再进行处理
			m_nADBufStoreSel = 0;
			FPGA_ReadBuffer(m_dataBuf1, FPGA_ADDR_BUF_BASE, FPGA_BUF_SIZE);
			Device_OnEchoAcq();								// 调用处理函数，对接收的数据进行处理
			nRes = ACQ_PROC_OK;
		}
	}
	FPGA_CMD_Write(FPGA_ADDR_INT_CLR, 0xFFFF);				// 必须清除所有中断标志位以开始一次数据处理过程
#elif DEVICE_SUBTYPE/100 == iBeaconR2
	if (m_bStartStatus)
	{
		if (!m_bSelfTest && (uFpgaReg&0x1000)) 	
		{
			// 自检通过后必须清除自检使能信号
			FPGA_CMD_Write(FPGA_ADDR_ACQ_ENABLE, 0x0001);			
		}
		else if (!m_bSelfTest && !(uFpgaReg&0x2000) )
		{
			// 非自检状态，如果未启动FPGA的处理过程，则强制启动
			FPGA_CMD_Write(FPGA_ADDR_ACQ_ENABLE, 0x0001);	
		}
		else if (m_bSelfTest && !(uFpgaReg&0x1000) )
		{
			// 自检状态，如果未设置自检使能，则强制自检
			FPGA_CMD_Write(FPGA_ADDR_ACQ_ENABLE, 0x0003);	
		}
		if (uFpgaReg & 0x4000)								// 先判断是否是一次新的处理过程开始如果是则初始化
		{
			// 初始化并使能UART1接收DMA
			int nSize = FPGA_BUF_SIZE/1000*2000;
			Comm_Enable(COMM1, false);							
			Comm1_DMA_RxIntInit(true);						// 使能串口1DMA接收
			Comm1_DMA_RxConfig(true, (u8*)m_dataBuf1, nSize);	// 配置串口1DMA接收
			Comm_Enable(COMM1, true);						// 使能串口接收，防止启动DMA前接收到未知数据
			FPGA_CMD_Write(FPGA_ADDR_SIG_READ, 0xFFFF);		// 触发使能读取FPGA内部缓冲区数据
			nRes = ACQ_PROC_OK;
		}
	}
#endif
	return nRes;
}
