
#include "Gpio.h"
#include "SPI_operate.h"
#include "PCLINK_operate.h"

bool m_bSPI1Init = false;
bool m_bSPI4Init = false; 
bool m_bSPI5Init = false; 

#if USE_STM32H7XX
#include "STM32H7_operate.h"
SPI_HandleTypeDef m_hSpi1;
SPI_HandleTypeDef m_hSpi4;
DMA_HandleTypeDef m_hdma_spi1_rx;
DMA_HandleTypeDef m_hdma_spi4_rx;
#endif

void SPI1_Init(bool bInit)
{	
	// SPI1口初始化
	// 以下是SPI模块的初始化代码，配置成主机模式 	
#if USE_STM32F4XX
	GPIO_InitTypeDef  GPIO_InitStructure;
	SPI_InitTypeDef   SPI_InitStructure;
	if (bInit)
	{
	#if !USE_SPI1_PORTB
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);				// 使能GPIOA时钟
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);				// 使能SPI1时钟
	 
		//GPIOA 3,4,5初始化设置
		GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5;	// PA3~5复用功能输出	
		GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;						// 复用功能
		GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;						// 推挽输出
		GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;					// 100MHz
		GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;						// 上拉
		GPIO_Init(GPIOA, &GPIO_InitStructure);								// 初始化
		
		GPIO_PinAFConfig(GPIOA,GPIO_PinSource3,GPIO_AF_SPI1); 				// PA3复用为 SPI1
		GPIO_PinAFConfig(GPIOA,GPIO_PinSource4,GPIO_AF_SPI1); 				// PA4复用为 SPI1
		GPIO_PinAFConfig(GPIOA,GPIO_PinSource5,GPIO_AF_SPI1); 				// PA5复用为 SPI1
		
		GPIO_SetOut(GPIOA, GPIO_Pin_4, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_UP);
		GPIO_SetBits(GPIOA, GPIO_Pin_4);
	#else
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);				// 使能GPIOB时钟
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);				// 使能SPI1时钟
	 
		//GPIOB 3,4,5初始化设置
		GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5;	// PB3~5复用功能输出	
		GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;						// 复用功能
		GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;						// 推挽输出
		GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;					// 100MHz
		GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;						// 上拉
		GPIO_Init(GPIOB, &GPIO_InitStructure);								// 初始化
		
		GPIO_PinAFConfig(GPIOB,GPIO_PinSource3,GPIO_AF_SPI1); 				// PB3复用为 SPI1
		GPIO_PinAFConfig(GPIOB,GPIO_PinSource4,GPIO_AF_SPI1); 				// PB4复用为 SPI1
		GPIO_PinAFConfig(GPIOB,GPIO_PinSource5,GPIO_AF_SPI1); 				// PB5复用为 SPI1
		
		// 定义SPI_NSS管脚
		GPIO_SetOut(GPIOA, GPIO_Pin_15, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_UP);
		GPIO_SetBits(GPIOA, GPIO_Pin_15);
	#endif

		SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;  	// 设置SPI单向或者双向的数据模式:SPI设置为双线双向全双工
		SPI_InitStructure.SPI_Mode 		= SPI_Mode_Master;						// 设置SPI工作模式:设置为主SPI
		SPI_InitStructure.SPI_DataSize 	= SPI_DataSize_8b;						// 设置SPI的数据大小:SPI发送接收8位帧结构
		SPI_InitStructure.SPI_CPOL 		= SPI_CPOL_High;						// 串行同步时钟的空闲状态为高电平
		SPI_InitStructure.SPI_CPHA 		= SPI_CPHA_2Edge;						// 串行同步时钟的第二个跳变沿（上升或下降）数据被采样
		SPI_InitStructure.SPI_NSS 		= SPI_NSS_Soft;							// NSS信号由硬件（NSS管脚）还是软件（使用SSI位）管理:内部NSS信号有SSI位控制
		SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_128;	// 定义波特率预分频的值:波特率预分频值为128
		SPI_InitStructure.SPI_FirstBit 			= SPI_FirstBit_MSB;				// 指定数据传输从MSB位还是LSB位开始:数据传输从MSB位开始
		SPI_InitStructure.SPI_CRCPolynomial 	= 7;							// CRC值计算的多项式
		SPI_Init(SPI1, &SPI_InitStructure);  									// 根据SPI_InitStruct中指定的参数初始化外设SPIx寄存器
	 
		SPI_Cmd(SPI1, ENABLE); 		// 使能SPI外设
		m_bSPI1Init = true;
	}
	else
	{
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, DISABLE);	// 禁止SPI1时钟
		
		SPI_Cmd(SPI1, DISABLE); 	// 禁止SPI外设
		m_bSPI1Init = false;
	}
#elif USE_STM32H7XX
	/* SPI1 parameter configuration*/
	if (bInit)
	{
		m_hSpi1.Instance 				= SPI1;
		m_hSpi1.Init.Mode 				= SPI_MODE_MASTER;
		m_hSpi1.Init.Direction 			= SPI_DIRECTION_2LINES;
		m_hSpi1.Init.DataSize 			= SPI_DATASIZE_8BIT;
		m_hSpi1.Init.CLKPolarity 		= SPI_POLARITY_HIGH;
		m_hSpi1.Init.CLKPhase 			= SPI_PHASE_2EDGE;
		m_hSpi1.Init.NSS 				= SPI_NSS_SOFT;
		m_hSpi1.Init.BaudRatePrescaler 	= SPI_BAUDRATEPRESCALER_8;
		m_hSpi1.Init.FirstBit 			= SPI_FIRSTBIT_MSB;
		m_hSpi1.Init.TIMode 			= SPI_TIMODE_DISABLE;
		m_hSpi1.Init.CRCCalculation 	= SPI_CRCCALCULATION_DISABLE;
		m_hSpi1.Init.CRCPolynomial 		= 0x0;
		m_hSpi1.Init.NSSPMode 			= SPI_NSS_PULSE_DISABLE;
		m_hSpi1.Init.NSSPolarity 		= SPI_NSS_POLARITY_LOW;
		m_hSpi1.Init.FifoThreshold 		= SPI_FIFO_THRESHOLD_01DATA;
		m_hSpi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
		m_hSpi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
		m_hSpi1.Init.MasterSSIdleness 			= SPI_MASTER_SS_IDLENESS_00CYCLE;
		m_hSpi1.Init.MasterInterDataIdleness 	= SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
		m_hSpi1.Init.MasterReceiverAutoSusp 	= SPI_MASTER_RX_AUTOSUSP_DISABLE;
		m_hSpi1.Init.MasterKeepIOState 			= SPI_MASTER_KEEP_IO_STATE_DISABLE;
		m_hSpi1.Init.IOSwap 					= SPI_IO_SWAP_DISABLE;

		if (HAL_SPI_Init(&m_hSpi1) != HAL_OK)
		{
			Error_Handler();
		}
		
		__HAL_SPI_ENABLE(&m_hSpi1);
		m_bSPI1Init = true;
	}
	else
	{
		HAL_SPI_DeInit(&m_hSpi1); 			// 禁止SPI外设
		m_bSPI1Init = false;
	}
#endif
}   

u8 SPI1_ReadWriteByte(u8 TxData)
{		 			 
	// SPI1 读写一个字节
	// TxData:要写入的字节
	// 返回值:读取到的字节
	u8 uRecv = 0;
	if ( m_bSPI1Init )
	{
		GPIO_ResetBits(GPIOA, GPIO_Pin_4); delayus(1);
	#if USE_STM32F4XX
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET){}	// 等待发送区空  
		SPI_I2S_SendData(SPI1, TxData); 									// 通过外设SPIx发送一个 byte 数据
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个 byte 
		uRecv = SPI_I2S_ReceiveData(SPI1);
	#elif USE_STM32H7XX
		HAL_StatusTypeDef errorcode = HAL_OK;
		u8 buf[4] = {0};	buf[0] = TxData&0xFF; 							// 初始化数组
		errorcode = HAL_SPI_TransmitReceive(&m_hSpi1, buf, (uint8_t *)&uRecv, 1, 10);	// 发送数据
		if ( errorcode != HAL_OK )
		{
			m_hSpi1.ErrorCode = HAL_OK;
			__HAL_SPI_ENABLE(&m_hSpi1);
		}
	#endif
		delayus(1);	GPIO_SetBits(GPIOA, GPIO_Pin_4); delayus(1);	
	}
	return uRecv;	// 返回通过SPIx最近接收的数据
}

u16 SPI1_ReadWriteWord(u16 TxData)
{		 			 
	// SPI1 读写一个字节，此函数必须在配置为16bit模式下调用
	// TxData:要写入的字节
	// 返回值:读取到的字节
	u16 uRecv = 0;
	if ( m_bSPI1Init )
	{
		GPIO_ResetBits(GPIOA, GPIO_Pin_4); delayus(1);
	#if USE_STM32F4XX
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET){}	// 等待发送区空  
		SPI_I2S_SendData(SPI1, TxData); 									// 通过外设SPIx发送一个 word 数据
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个 word 
		uRecv = SPI_I2S_ReceiveData(SPI1);
	#elif USE_STM32H7XX
		u16 uReg = 0; u8 buf[4] = {0};
		HAL_StatusTypeDef errorcode = HAL_OK;
		buf[0] = (TxData>>8)&0xFF; buf[1] = TxData&0xFF; 					// 初始化数组
		errorcode = HAL_SPI_TransmitReceive(&m_hSpi1, buf, (uint8_t *)&uReg, 2, 10);	// 发送数据
		if ( errorcode != HAL_OK )
		{
			m_hSpi1.ErrorCode = HAL_OK;
			__HAL_SPI_ENABLE(&m_hSpi1);
		}
		uRecv = ((uReg<<8)&0xFF00)|((uReg>>8)&0x00FF);
	#endif	
		delayus(1);	GPIO_SetBits(GPIOA, GPIO_Pin_4); delayus(1);	
	}
	return uRecv;	// 返回通过SPIx最近接收的数据
}
	
u16 SPI1_ReadWrite3Byte(u8 uAddr, u16 TxData)
{		 			 
	// SPI1总线读写3字，第1字节是读写地址，数据为16位
	// uData: 要写入的字
	// 返回值:读取到的字
	u16 uReg, uRecv = 0;

	if (m_bSPI1Init)
	{
	#if !USE_SPI1_PORTB
		GPIO_ResetBits(GPIOA, GPIO_Pin_4);  delayus(1);
	#else
		GPIO_ResetBits(GPIOA, GPIO_Pin_15); delayus(1);
	#endif
	#if USE_STM32F4XX
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET){}	// 等待发送区空  
		
		SPI_I2S_SendData(SPI1, uAddr); 										// 通过外设SPIx发送一个 byte  数据
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个Byte 
		uReg = SPI_I2S_ReceiveData(SPI1); delayus(1);
	
		SPI_I2S_SendData(SPI1, (TxData>>8)&0xFF); 							// 通过外设SPIx发送一个 byte  数据
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个Byte 
		uReg = SPI_I2S_ReceiveData(SPI1); delayus(1);		
		uRecv = (uReg<<8)&0xFF00; 
			
		SPI_I2S_SendData(SPI1, TxData&0xFF); 								// 通过外设SPIx发送一个 byte  数据
		while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个Byte 
		uReg = SPI_I2S_ReceiveData(SPI1); delayus(1);	
		uRecv = uRecv + uReg;
	#endif // USE_STM32F4XX	
	#if USE_STM32H7XX
		u8 buf[4] = {0};
		HAL_StatusTypeDef errorcode = HAL_OK;
		buf[0] = uAddr;	buf[1] = (TxData>>8)&0xFF; buf[2] = TxData&0xFF; 	// 初始化数组
		
		errorcode = HAL_SPI_Transmit(&m_hSpi1, buf, 1, 10);	delayus(1);		// 发送地址
		if ( errorcode != HAL_OK )
		{
			m_hSpi1.ErrorCode = HAL_OK;
			__HAL_SPI_ENABLE(&m_hSpi1);
		}
		errorcode = HAL_SPI_TransmitReceive(&m_hSpi1, &buf[1], (uint8_t *)&uReg, 2, 10);	// 发送数据
		if ( errorcode != HAL_OK )
		{
			m_hSpi1.ErrorCode = HAL_OK;
			__HAL_SPI_ENABLE(&m_hSpi1);
		}
		uRecv = ((uReg<<8)&0xFF00)|((uReg>>8)&0x00FF);
	#endif // USE_STM32H7XX
		
	#if !USE_SPI1_PORTB
		delayus(1); GPIO_SetBits(GPIOA, GPIO_Pin_4); delayus(1);	
	#else
		delayus(1); GPIO_SetBits(GPIOA, GPIO_Pin_15); delayus(1);	
	#endif
	}	
	return uRecv; 	// 返回通过SPIx最近接收的数据	
}
			  
void SPI4_Init(bool bInit)
{	
	// SPI4接口Master模式初始化
	// 以下是SPI模块的初始化代码，配置成主机模式 	
#if USE_STM32F4XX
	GPIO_InitTypeDef  GPIO_InitStructure;
	SPI_InitTypeDef   SPI_InitStructure;
	if (bInit)
	{
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);	// 使能GPIOE时钟
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI4, ENABLE);	// 使能SPI4时钟
	 
		//GPIOE 2,5,6初始化设置
		GPIO_InitStructure.GPIO_Pin 	= GPIO_Pin_2|GPIO_Pin_5|GPIO_Pin_6;//PE2/5/6复用功能输出	
		GPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_AF;			// 复用功能
		GPIO_InitStructure.GPIO_OType 	= GPIO_OType_PP;		// 推挽输出
		GPIO_InitStructure.GPIO_Speed 	= GPIO_Speed_100MHz;	// 100MHz
		GPIO_InitStructure.GPIO_PuPd 	= GPIO_PuPd_UP;			// 上拉
		GPIO_Init(GPIOE, &GPIO_InitStructure);					// 初始化
		
		GPIO_PinAFConfig(GPIOE,GPIO_PinSource2,GPIO_AF_SPI4); 	// PE2复用为 SPI4
		GPIO_PinAFConfig(GPIOE,GPIO_PinSource5,GPIO_AF_SPI4); 	// PE5复用为 SPI4
		GPIO_PinAFConfig(GPIOE,GPIO_PinSource6,GPIO_AF_SPI4); 	// PE6复用为 SPI4
		
		GPIO_SetOut(GPIOE, GPIO_Pin_4, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_UP);	// NSS 初始化为通用IO
		GPIO_SetBits(GPIOE, GPIO_Pin_4);

		SPI_InitStructure.SPI_Direction 	= SPI_Direction_2Lines_FullDuplex;  // 设置SPI单向或者双向的数据模式:SPI设置为双线双向全双工
		SPI_InitStructure.SPI_Mode 			= SPI_Mode_Master;					// 设置SPI工作模式:设置为主SPI
		SPI_InitStructure.SPI_DataSize		= SPI_DataSize_8b;					// 设置SPI的数据大小:SPI发送接收8位帧结构
		SPI_InitStructure.SPI_CPOL 			= SPI_CPOL_High;					// 串行同步时钟的空闲状态为高电平
		SPI_InitStructure.SPI_CPHA 			= SPI_CPHA_2Edge;					// 串行同步时钟的第二个跳变沿（上升或下降）数据被采样
		SPI_InitStructure.SPI_NSS 			= SPI_NSS_Soft;						// NSS信号由硬件（NSS管脚）还是软件（使用SSI位）管理:内部NSS信号有SSI位控制
		SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_64;		// 定义波特率预分频的值:波特率预分频值
		SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;						// 指定数据传输从MSB位还是LSB位开始:数据传输从MSB位开始
		SPI_InitStructure.SPI_CRCPolynomial = 7;								// CRC值计算的多项式
		SPI_Init(SPI4, &SPI_InitStructure);  									// 根据SPI_InitStruct中指定的参数初始化外设SPIx寄存器
	 
		SPI_Cmd(SPI4, ENABLE); 		// 使能SPI外设
		m_bSPI4Init = true;
	}
	else
	{
		RCC_APB2PeriphResetCmd(RCC_APB2Periph_SPI4,DISABLE);	// 停止SPI4
		
		SPI_Cmd(SPI4, DISABLE); 	// 禁止SPI外设
		m_bSPI4Init = false;
	}	
#endif // USE_STM32F4XX	
#if USE_STM32H7XX
	/* SPI4 parameter configuration*/
	if (bInit)
	{
		m_hSpi4.Instance 				= SPI4;
		m_hSpi4.Init.Mode 				= SPI_MODE_MASTER;
		m_hSpi4.Init.Direction 			= SPI_DIRECTION_2LINES;
		m_hSpi4.Init.DataSize 			= SPI_DATASIZE_8BIT;
		m_hSpi4.Init.CLKPolarity 		= SPI_POLARITY_HIGH;
		m_hSpi4.Init.CLKPhase 			= SPI_PHASE_2EDGE;
		m_hSpi4.Init.NSS 				= SPI_NSS_SOFT;
		m_hSpi4.Init.BaudRatePrescaler 	= SPI_BAUDRATEPRESCALER_8;
		m_hSpi4.Init.FirstBit 			= SPI_FIRSTBIT_MSB;
		m_hSpi4.Init.TIMode 			= SPI_TIMODE_DISABLE;
		m_hSpi4.Init.CRCCalculation 	= SPI_CRCCALCULATION_DISABLE;
		m_hSpi4.Init.CRCPolynomial 		= 0x0;
		m_hSpi4.Init.NSSPMode 			= SPI_NSS_PULSE_DISABLE;
		m_hSpi4.Init.NSSPolarity 		= SPI_NSS_POLARITY_LOW;
		m_hSpi4.Init.FifoThreshold 		= SPI_FIFO_THRESHOLD_01DATA;
		m_hSpi4.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
		m_hSpi4.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
		m_hSpi4.Init.MasterSSIdleness 			= SPI_MASTER_SS_IDLENESS_00CYCLE;
		m_hSpi4.Init.MasterInterDataIdleness 	= SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
		m_hSpi4.Init.MasterReceiverAutoSusp 	= SPI_MASTER_RX_AUTOSUSP_DISABLE;
		m_hSpi4.Init.MasterKeepIOState 			= SPI_MASTER_KEEP_IO_STATE_DISABLE;
		m_hSpi4.Init.IOSwap 					= SPI_IO_SWAP_DISABLE;
		if (HAL_SPI_Init(&m_hSpi4) != HAL_OK)
		{
			Error_Handler();
		}
		__HAL_SPI_ENABLE(&m_hSpi4);
		m_bSPI4Init = true;
	}
	else
	{
		if (HAL_SPI_DeInit(&m_hSpi4) != HAL_OK)
		{
			Error_Handler();
		}
		m_bSPI4Init = false;
	}
#endif // USE_STM32H7XX
}   

u8 SPI4_ReadWriteByte(u8 TxData)
{		 			 
	// SPI1 读写一个字节
	// TxData:要写入的字节
	// 返回值:读取到的字节
	u8 uRecv = 0;
	if ( m_bSPI4Init )
	{
		GPIO_ResetBits(GPIOE, GPIO_Pin_4); delayus(1);
	#if USE_STM32F4XX
		while (SPI_I2S_GetFlagStatus(SPI4, SPI_I2S_FLAG_TXE) == RESET){}	// 等待发送区空  
		SPI_I2S_SendData(SPI4, TxData); 									// 通过外设SPIx发送一个 byte 数据
		while (SPI_I2S_GetFlagStatus(SPI4, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个 byte 
		uRecv = SPI_I2S_ReceiveData(SPI4);
	#elif USE_STM32H7XX
		HAL_StatusTypeDef errorcode = HAL_OK;
		u8 buf[4] = {0};	buf[0] = TxData&0xFF; 							// 初始化数组
		errorcode = HAL_SPI_TransmitReceive(&m_hSpi4, buf, (uint8_t *)&uRecv, 1, 10);	// 发送数据
		if ( errorcode != HAL_OK )
		{
			m_hSpi4.ErrorCode = HAL_OK;
			__HAL_SPI_ENABLE(&m_hSpi4);
		}
	#endif
		delayus(1);	GPIO_SetBits(GPIOE, GPIO_Pin_4); delayus(1);
	}
	return uRecv;	// 返回通过SPIx最近接收的数据
}

u16 SPI4_ReadWriteWord(u16 TxData)
{		 			 
	// SPI4 读写一个字，此函数必须在配置为16bit模式下调用
	// TxData:要写入的字
	// 返回值:读取到的字
	u16 uRecv = 0;
	if ( m_bSPI4Init )
	{
		GPIO_ResetBits(GPIOE, GPIO_Pin_4); delayus(1);
	#if USE_STM32F4XX
		while (SPI_I2S_GetFlagStatus(SPI4, SPI_I2S_FLAG_TXE) == RESET){}	// 等待发送区空  
		SPI_I2S_SendData(SPI4, TxData); 									// 通过外设SPIx发送一个 word 数据
		while (SPI_I2S_GetFlagStatus(SPI4, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个 word 
		uRecv = SPI_I2S_ReceiveData(SPI4);
	#elif USE_STM32H7XX
		u16 uReg = 0; u8 buf[4] = {0};
		HAL_StatusTypeDef errorcode = HAL_OK;
		buf[0] = (TxData>>8)&0xFF; buf[1] = TxData&0xFF; 								// 初始化数组
		errorcode = HAL_SPI_TransmitReceive(&m_hSpi4, buf, (uint8_t *)&uReg, 2, 10);	// 发送数据
		if ( errorcode != HAL_OK )
		{
			m_hSpi4.ErrorCode = HAL_OK;
			__HAL_SPI_ENABLE(&m_hSpi4);
		}
		uRecv = ((uReg<<8)&0xFF00)|((uReg>>8)&0x00FF);
	#endif
		delayus(1);	GPIO_SetBits(GPIOE, GPIO_Pin_4); delayus(1);
	}
	return uRecv;	// 返回通过SPIx最近接收的数据
}

u16 SPI4_ReadWrite3Byte(u8 uAddr, u16 TxData)
{		 			 
	// SPI4总线读写3字，第1字节是读写地址，数据为16位
	// uData: 要写入的字
	// 返回值:读取到的字
	u16 uReg, uRecv = 0;

	if (m_bSPI4Init)
	{
		GPIO_ResetBits(GPIOE, GPIO_Pin_4); delayus(1);
	#if USE_STM32F4XX
		while (SPI_I2S_GetFlagStatus(SPI4, SPI_I2S_FLAG_TXE) == RESET){}	// 等待发送区空  
		
		SPI_I2S_SendData(SPI4, uAddr); 										// 通过外设SPIx发送一个 byte  数据
		while (SPI_I2S_GetFlagStatus(SPI4, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个Byte 
		uReg = SPI_I2S_ReceiveData(SPI4); delayus(1);
	
		SPI_I2S_SendData(SPI4, (TxData>>8)&0xFF); 							// 通过外设SPIx发送一个 byte  数据
		while (SPI_I2S_GetFlagStatus(SPI4, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个Byte 
		uReg = SPI_I2S_ReceiveData(SPI4); delayus(1);		
		uRecv = (uReg<<8)&0xFF00; 
			
		SPI_I2S_SendData(SPI4, TxData&0xFF); 								// 通过外设SPIx发送一个 byte  数据
		while (SPI_I2S_GetFlagStatus(SPI4, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个Byte 
		uReg = SPI_I2S_ReceiveData(SPI4); delayus(1);	
		uRecv = uRecv + uReg;
	#endif // USE_STM32F4XX	
	#if USE_STM32H7XX
		u8 buf[4] = {0};
		HAL_StatusTypeDef errorcode = HAL_OK;
		buf[0] = uAddr;	buf[1] = (TxData>>8)&0xFF; buf[2] = TxData&0xFF; 	// 初始化数组
		
		errorcode = HAL_SPI_Transmit(&m_hSpi4, buf, 1, 10);	delayus(1);		// 发送地址
		if ( errorcode != HAL_OK )
		{
			m_hSpi4.ErrorCode = HAL_OK;
			__HAL_SPI_ENABLE(&m_hSpi4);
		}
		errorcode = HAL_SPI_TransmitReceive(&m_hSpi4, &buf[1], (uint8_t *)&uReg, 2, 10);	// 发送数据
		if ( errorcode != HAL_OK )
		{
			m_hSpi4.ErrorCode = HAL_OK;
			__HAL_SPI_ENABLE(&m_hSpi4);
		}
		uRecv = ((uReg<<8)&0xFF00)|((uReg>>8)&0x00FF);
	#endif // USE_STM32H7XX
		
		delayus(1); GPIO_SetBits(GPIOE, GPIO_Pin_4); delayus(1);	
	}	
	return uRecv; 	// 返回通过SPIx最近接收的数据	
}
		  
void SPI5_Init(bool bInit)
{	
	// SPI5接口Master模式初始化
	// 以下是SPI模块的初始化代码，配置成主机模式 	
#if USE_STM32F4XX
	GPIO_InitTypeDef  GPIO_InitStructure;
	SPI_InitTypeDef  SPI_InitStructure;
	if (bInit)
	{
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);	// 使能GPIOF时钟
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI5, ENABLE);	// 使能SPI5时钟
	 
		//GPIOF 7,8,9初始化设置
		GPIO_InitStructure.GPIO_Pin 	= GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_9;		// PF7/8/9复用功能输出	
		GPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_AF;			// 复用功能
		GPIO_InitStructure.GPIO_OType 	= GPIO_OType_PP;		// 推挽输出
		GPIO_InitStructure.GPIO_Speed 	= GPIO_Speed_100MHz;	// 100MHz
		GPIO_InitStructure.GPIO_PuPd 	= GPIO_PuPd_UP;			// 上拉
		GPIO_Init(GPIOF, &GPIO_InitStructure);					// 初始化
		
		GPIO_PinAFConfig(GPIOF,GPIO_PinSource7,GPIO_AF_SPI5); 	// PE7复用为 SPI5
		GPIO_PinAFConfig(GPIOF,GPIO_PinSource8,GPIO_AF_SPI5); 	// PE8复用为 SPI5
		GPIO_PinAFConfig(GPIOF,GPIO_PinSource9,GPIO_AF_SPI5); 	// PE9复用为 SPI5
		
		GPIO_SetOut(GPIOF, GPIO_Pin_6, GPIO_OType_PP, GPIO_Speed_100MHz, GPIO_PuPd_UP);	// NSS 初始化为通用IO
		GPIO_SetBits(GPIOF, GPIO_Pin_6);

		SPI_InitStructure.SPI_Direction 	= SPI_Direction_2Lines_FullDuplex;  // 设置SPI单向或者双向的数据模式:SPI设置为双线双向全双工
		SPI_InitStructure.SPI_Mode 			= SPI_Mode_Master;					// 设置SPI工作模式:设置为主SPI
		SPI_InitStructure.SPI_DataSize		= SPI_DataSize_8b;					// 设置SPI的数据大小:SPI发送接收8位帧结构
		SPI_InitStructure.SPI_CPOL 			= SPI_CPOL_High;					// 串行同步时钟的空闲状态为高电平
		SPI_InitStructure.SPI_CPHA 			= SPI_CPHA_2Edge;					// 串行同步时钟的第二个跳变沿（上升或下降）数据被采样
		SPI_InitStructure.SPI_NSS 			= SPI_NSS_Soft;						// NSS信号由硬件（NSS管脚）还是软件（使用SSI位）管理:内部NSS信号有SSI位控制
		SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_128;	// 定义波特率预分频的值:波特率预分频值
		SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;						// 指定数据传输从MSB位还是LSB位开始:数据传输从MSB位开始
		SPI_InitStructure.SPI_CRCPolynomial = 7;								// CRC值计算的多项式
		SPI_Init(SPI5, &SPI_InitStructure);  									// 根据SPI_InitStruct中指定的参数初始化外设SPIx寄存器
	 
		SPI_Cmd(SPI5, ENABLE); 		// 使能SPI外设
		m_bSPI5Init = true;
	}
	else
	{
		RCC_APB2PeriphResetCmd(RCC_APB2Periph_SPI5,DISABLE);	// 停止SPI5
		
		SPI_Cmd(SPI5, DISABLE); 	// 禁止SPI外设
		m_bSPI5Init = false;
	}	
#endif // #if USE_STM32F4XX	
}   

u8 SPI5_ReadWriteByte(u8 TxData)
{		 			 
	// SPI1 读写一个字节
	// TxData:要写入的字节
	// 返回值:读取到的字节
	u8 uRecv = 0;
	if ( m_bSPI5Init )
	{
		GPIO_ResetBits(GPIOF, GPIO_Pin_6); delayus(1);
	#if USE_STM32F4XX
		while (SPI_I2S_GetFlagStatus(SPI5, SPI_I2S_FLAG_TXE) == RESET){}	// 等待发送区空  
		SPI_I2S_SendData(SPI5, TxData); 									// 通过外设SPIx发送一个 byte 数据
		while (SPI_I2S_GetFlagStatus(SPI5, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个 byte 
		uRecv = SPI_I2S_ReceiveData(SPI5);
	#elif USE_STM32H7XX
		
	#endif
		delayus(1);	GPIO_SetBits(GPIOF, GPIO_Pin_6); delayus(1);
	}
	return uRecv;	// 返回通过SPIx最近接收的数据
}

u16 SPI5_ReadWriteWord(u16 TxData)
{		 			 
	// SPI5 读写一个字
	// TxData:要写入的字
	// 返回值:读取到的字
	u16 uRecv = 0;
	if ( m_bSPI5Init )
	{
		GPIO_ResetBits(GPIOF, GPIO_Pin_6); delayus(1);
	#if USE_STM32F4XX
		while (SPI_I2S_GetFlagStatus(SPI5, SPI_I2S_FLAG_TXE) == RESET){}	// 等待发送区空  
		SPI_I2S_SendData(SPI5, TxData); 									// 通过外设SPIx发送一个 byte 数据
		while (SPI_I2S_GetFlagStatus(SPI5, SPI_I2S_FLAG_RXNE) == RESET){} 	// 等待接收完一个 byte 
		uRecv = SPI_I2S_ReceiveData(SPI5);
	#elif USE_STM32H7XX
		
	#endif
		delayus(1);	GPIO_SetBits(GPIOF, GPIO_Pin_6); delayus(1);
	}
	return uRecv;	// 返回通过SPIx最近接收的数据
}


u16 SPI5_ReadWrite3Byte(u8 uAddr, u16 uData)
{		 			 
	// SPI5 读写一个字
	// TxData:要写入的字
	// 返回值:读取到的字
#if USE_STM32F4XX
	if (m_bSPI5Init)
	{
		u16 uReg, uRecv;
		u32 uOvTime = 0x01FFFFFF;
		while (SPI_I2S_GetFlagStatus(SPI5, SPI_I2S_FLAG_TXE) == RESET && uOvTime-- > 0){}//等待发送区空  
		
		GPIO_ResetBits(GPIOF, GPIO_Pin_6); delayus(1);
			
		SPI_I2S_SendData(SPI5, uAddr); 					// 通过外设SPIx发送一个byte  数据
		uOvTime = 0x01FFFFFF;
		while (SPI_I2S_GetFlagStatus(SPI5, SPI_I2S_FLAG_RXNE) == RESET && uOvTime-- > 0){} //等待接收完一个Byte
		uReg = SPI_I2S_ReceiveData(SPI5); delayus(1);	// 数据必须读走，处必须插入等待周期
		SPI_I2S_SendData(SPI5, (uData>>8)&0xFF); 		// 通过外设SPIx发送一个byte  数据
		uOvTime = 0x01FFFFFF;
		while (SPI_I2S_GetFlagStatus(SPI5, SPI_I2S_FLAG_RXNE) == RESET && uOvTime-- > 0){} //等待接收完一个Byte 
		uReg = SPI_I2S_ReceiveData(SPI5); delayus(1);		
		uRecv = (uReg<<8)&0xFF00;
		
		SPI_I2S_SendData(SPI5, uData&0xFF); 			// 通过外设SPIx发送一个byte  数据
		uOvTime = 0x01FFFFFF;
		while (SPI_I2S_GetFlagStatus(SPI5, SPI_I2S_FLAG_RXNE) == RESET && uOvTime-- > 0){} //等待接收完一个Byte 
		uReg = SPI_I2S_ReceiveData(SPI5); delayus(1);	
		uRecv = uRecv + uReg;
			
		delayus(1); GPIO_SetBits(GPIOF, GPIO_Pin_6); delayus(1);
	 
		return uRecv; 	// 返回通过SPIx最近接收的数据	
	}
#endif // #if USE_STM32F4XX	
	return 0;
}
		  
void SPI1_Init_Slave(bool bInit)
{	
	// SPI1的从模式初始化
	// 以下是SPI模块的初始化代码，配置成主机模式 	
#if USE_STM32F4XX
	GPIO_InitTypeDef  GPIO_InitStructure;
	SPI_InitTypeDef   SPI_InitStructure;
	
	if (bInit)
	{
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);	// 使能GPIOA时钟
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);	// 使能SPI1时钟
	 
		//GPIOA 4,5,7初始化设置
		GPIO_InitStructure.GPIO_Pin 	= GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_7;//PA4/5/7复用功能输出	
		GPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_AF;			// 复用功能
		GPIO_InitStructure.GPIO_OType 	= GPIO_OType_PP;		// 推挽输出
		GPIO_InitStructure.GPIO_Speed 	= GPIO_Speed_100MHz;	// 100MHz
		GPIO_InitStructure.GPIO_PuPd 	= GPIO_PuPd_UP;			// 上拉
		GPIO_Init(GPIOA, &GPIO_InitStructure);					// 初始化
		
		GPIO_PinAFConfig(GPIOA,GPIO_PinSource4,GPIO_AF_SPI1); 	// PA4复用为 SPI1
		GPIO_PinAFConfig(GPIOA,GPIO_PinSource5,GPIO_AF_SPI1); 	// PA5复用为 SPI1
		GPIO_PinAFConfig(GPIOA,GPIO_PinSource7,GPIO_AF_SPI1); 	// PA7复用为 SPI1

		SPI_InitStructure.SPI_Direction 	= SPI_Direction_2Lines_RxOnly;  	// 设置SPI单向或者双向的数据模式: SPI设置为双线单向接收
		SPI_InitStructure.SPI_Mode 			= SPI_Mode_Slave;					// 设置SPI工作模式:设置为SPI从模式
		SPI_InitStructure.SPI_DataSize		= SPI_DataSize_16b;					// 设置SPI的数据大小:SPI发送接收16位帧结构
		SPI_InitStructure.SPI_CPOL 			= SPI_CPOL_High;					// 串行同步时钟的空闲状态为高电平
		SPI_InitStructure.SPI_CPHA 			= SPI_CPHA_2Edge;					// 串行同步时钟的第二个跳变沿（上升或下降）数据被采样
		SPI_InitStructure.SPI_NSS 			= SPI_NSS_Hard;						// NSS信号由硬件（NSS管脚）还是软件（使用SSI位）管理:内部NSS信号有SSI位控制
		SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;		// 定义波特率预分频的值:波特率预分频值
		SPI_InitStructure.SPI_FirstBit 		= SPI_FirstBit_LSB;					// 指定数据传输从MSB位还是LSB位开始
		SPI_InitStructure.SPI_CRCPolynomial = 7;								// CRC值计算的多项式
		SPI_Init(SPI1, &SPI_InitStructure);  									// 根据SPI_InitStruct中指定的参数初始化外设SPIx寄存器
	 
		SPI_Cmd(SPI1, ENABLE); 		// 使能SPI外设
		m_bSPI1Init = true;
	}
	else
	{
		RCC_APB2PeriphResetCmd(RCC_APB2Periph_SPI1,DISABLE);	// 停止SPI4时钟
		
		SPI_Cmd(SPI1, DISABLE); 	// 禁止SPI外设
		m_bSPI1Init = false;
	}	 
#endif // #if USE_STM32F4XX	
#if USE_STM32H7XX
	if ( bInit )
	{
		if ( m_bSPI1Init ) HAL_SPI_DeInit(&m_hSpi1);	// 如果已经初始化，则先复位
		
		m_hSpi1.Instance 						= SPI1;
		m_hSpi1.Init.Mode 						= SPI_MODE_SLAVE;
		m_hSpi1.Init.Direction 					= SPI_DIRECTION_2LINES;
		m_hSpi1.Init.DataSize 					= SPI_DATASIZE_16BIT;
		m_hSpi1.Init.CLKPolarity 				= SPI_POLARITY_LOW;					// TI模式参数无效
		m_hSpi1.Init.CLKPhase 					= SPI_PHASE_2EDGE;					// TI模式参数无效
		m_hSpi1.Init.NSS 						= SPI_NSS_HARD_INPUT;
		m_hSpi1.Init.FirstBit 					= SPI_FIRSTBIT_LSB;					// TI模式参数无效，最高位在前
		m_hSpi1.Init.BaudRatePrescaler 			= SPI_BAUDRATEPRESCALER_2;
		m_hSpi1.Init.TIMode 					= SPI_TIMODE_DISABLE;				// TI模式使能
		m_hSpi1.Init.CRCCalculation 			= SPI_CRCCALCULATION_DISABLE;
		m_hSpi1.Init.CRCPolynomial 				= 0x0;
		m_hSpi1.Init.NSSPMode 					= SPI_NSS_PULSE_DISABLE;			// 只在主模式下有效
		m_hSpi1.Init.NSSPolarity 				= SPI_NSS_POLARITY_LOW;				// TI模式参数无效
		m_hSpi1.Init.FifoThreshold 				= SPI_FIFO_THRESHOLD_01DATA;
		m_hSpi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
		m_hSpi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
		m_hSpi1.Init.MasterSSIdleness 			= SPI_MASTER_SS_IDLENESS_00CYCLE;
		m_hSpi1.Init.MasterInterDataIdleness 	= SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
		m_hSpi1.Init.MasterReceiverAutoSusp 	= SPI_MASTER_RX_AUTOSUSP_DISABLE;
		m_hSpi1.Init.MasterKeepIOState 			= SPI_MASTER_KEEP_IO_STATE_DISABLE;
		m_hSpi1.Init.IOSwap 					= SPI_IO_SWAP_DISABLE;
		if (HAL_SPI_Init(&m_hSpi1) != HAL_OK)
		{
			Error_Handler();
		}
		m_bSPI1Init = true;
	}
	else
	{
		if (HAL_SPI_DeInit(&m_hSpi1) != HAL_OK)
		{
			Error_Handler();
		}
		m_bSPI1Init = false;
	}
#endif	// USE_STM32H7XX
}   

void SPI1_DMA_DBuf_RxConfig(bool bEn, uint8_t *BufferDST1, uint8_t *BufferDST2, uint32_t BufferSize)
{
#if USE_STM32F4XX
	DMA_InitTypeDef 	DMA_InitStructure;
	NVIC_InitTypeDef	NVIC_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);  
	
	DMA_DeInit(DMA2_Stream0);  	// see datasheet table43
	while (DMA_GetCmdStatus(DMA2_Stream0) != DISABLE){}
	
	if (bEn)
	{
		DMA_InitStructure.DMA_Channel 				= DMA_Channel_3;  				// enable channel 3
		DMA_InitStructure.DMA_PeripheralBaseAddr 	= (uint32_t)&(SPI1->DR);
		DMA_InitStructure.DMA_Memory0BaseAddr 		= (uint32_t)BufferDST1;
		DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;
		DMA_InitStructure.DMA_BufferSize 			= BufferSize;
		DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;
		DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;
		DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_HalfWord;
		DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_HalfWord;
		DMA_InitStructure.DMA_Mode 					= DMA_Mode_Circular;
		DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;
		DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable;         
		DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;
		DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
		DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;
		DMA_DoubleBufferModeConfig(DMA2_Stream0, (uint32_t)BufferDST2, DMA_Memory_0);
		DMA_DoubleBufferModeCmd(DMA2_Stream0, ENABLE);
		DMA_Init(DMA2_Stream0, &DMA_InitStructure);

		NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream0_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			// 使能DMA中断
		NVIC_Init(&NVIC_InitStructure); 
		
		DMA_SetCurrDataCounter(DMA2_Stream0, BufferSize);		// set data trans counter
		DMA_Cmd(DMA2_Stream0, ENABLE); 

		DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);
		DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TEIF0);
		DMA_ITConfig(DMA2_Stream0, DMA_IT_TC, ENABLE);
	}
	else
	{
		DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);
		DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TEIF0);
		DMA_ITConfig(DMA2_Stream0, DMA_IT_TC, DISABLE);
		
		DMA_SetCurrDataCounter(DMA2_Stream0, BufferSize);		// set data trans counter
		DMA_Cmd(DMA2_Stream0, DISABLE);  
	}
#elif USE_STM32H7XX
	__HAL_RCC_DMA2_CLK_ENABLE();
	if ( bEn )
	{
		m_hdma_spi1_rx.Instance 				= DMA2_Stream1;
		m_hdma_spi1_rx.Init.Request 			= DMA_REQUEST_SPI1_RX;
		m_hdma_spi1_rx.Init.Direction 			= DMA_PERIPH_TO_MEMORY;
		m_hdma_spi1_rx.Init.PeriphInc 			= DMA_PINC_DISABLE;
		m_hdma_spi1_rx.Init.MemInc 				= DMA_MINC_ENABLE;
		m_hdma_spi1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
		m_hdma_spi1_rx.Init.MemDataAlignment 	= DMA_MDATAALIGN_HALFWORD;
		m_hdma_spi1_rx.Init.Mode 				= DMA_CIRCULAR;
		m_hdma_spi1_rx.Init.Priority 			= DMA_PRIORITY_VERY_HIGH;
		m_hdma_spi1_rx.Init.FIFOMode 			= DMA_FIFOMODE_DISABLE;
		if (HAL_DMA_Init(&m_hdma_spi1_rx) != HAL_OK)
		{
			Error_Handler();
		}

		__HAL_LINKDMA(&m_hSpi1, hdmarx, m_hdma_spi1_rx);
	
		HAL_SPI_Receive_DMA_double(&m_hSpi1, BufferDST1, BufferDST2, BufferSize);
		HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0, 3);
		HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
	}
	else
	{
		HAL_SPI_DMAStop( &m_hSpi1 );
		HAL_NVIC_DisableIRQ(DMA2_Stream1_IRQn);
	}
#endif
}

void SPI4_Init_Slave(bool bInit)
{	
	// SPI4的从模式初始化
	// 以下是SPI模块的初始化代码，配置成主机模式 	
#if USE_STM32F4XX
	GPIO_InitTypeDef	GPIO_InitStructure;
	SPI_InitTypeDef		SPI_InitStructure;
	NVIC_InitTypeDef	NVIC_InitStructure;
	
	if (bInit)
	{
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);	// 使能GPIOE时钟
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI4, ENABLE);	// 使能SPI4时钟
	 
		// GPIOE 2,4,6初始化设置
		// 由于数据只接收，因此 MISO 管脚作为GPIO使用
		GPIO_InitStructure.GPIO_Pin 	= GPIO_Pin_2|GPIO_Pin_4|GPIO_Pin_6;//PE2/4/6复用功能输出	
		GPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_AF;			// 复用功能
		GPIO_InitStructure.GPIO_OType 	= GPIO_OType_PP;		// 推挽输出
		GPIO_InitStructure.GPIO_Speed 	= GPIO_Speed_100MHz;	// 100MHz
		GPIO_InitStructure.GPIO_PuPd 	= GPIO_PuPd_UP;			// 上拉
		GPIO_Init(GPIOE, &GPIO_InitStructure);					// 初始化
		
		GPIO_PinAFConfig(GPIOE,GPIO_PinSource2,GPIO_AF_SPI4); 	// PE2复用为 SPI4
		GPIO_PinAFConfig(GPIOE,GPIO_PinSource4,GPIO_AF_SPI4); 	// PE4复用为 SPI4
		GPIO_PinAFConfig(GPIOE,GPIO_PinSource6,GPIO_AF_SPI4); 	// PE6复用为 SPI4

		// DEVICE_SUBTYPE/100 == iListenS_USB_MINI DEVICE_SUBTYPE/100 == iListenS_USB 的SPI数据接口要求必须配置为 8Bit 模式
		// DEVICE_SUBTYPE/100 == iListenS_NET_MINI DEVICE_SUBTYPE/100 == iListenS_NET 的SPI数据接口要求必须配置为 8Bit 模式
		// DEVICE_SUBTYPE/100 == iListenS_USB_PRO  的SPI数据接口要求必须配置为 16Bit 模式
		SPI_InitStructure.SPI_Direction 	= SPI_Direction_2Lines_RxOnly;  	// 设置SPI单向或者双向的数据模式: SPI设置为双线单向接收
		SPI_InitStructure.SPI_Mode 			= SPI_Mode_Slave;					// 设置SPI工作模式:设置为SPI从模式
	#if USE_SPI4_WORD_MODE
		SPI_InitStructure.SPI_DataSize		= SPI_DataSize_16b;					// 设置SPI的数据大小:SPI发送接收16位帧结构
	#else
		SPI_InitStructure.SPI_DataSize		= SPI_DataSize_8b;					// 设置SPI的数据大小:SPI发送接收8位帧结构
	#endif
		SPI_InitStructure.SPI_CPOL 			= SPI_CPOL_High;					// 串行同步时钟的空闲状态为高电平
		SPI_InitStructure.SPI_CPHA 			= SPI_CPHA_2Edge;					// 串行同步时钟的第二个跳变沿（上升或下降）数据被采样
		SPI_InitStructure.SPI_NSS 			= SPI_NSS_Hard;						// NSS信号由硬件（NSS管脚）还是软件（使用SSI位）管理:内部NSS信号有SSI位控制
		SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;		// 定义波特率预分频的值:波特率预分频值
	#if USE_SPI4_FirstBit_MSB
		SPI_InitStructure.SPI_FirstBit 		= SPI_FirstBit_MSB;					// 指定数据传输从MSB位开始
	#else
		SPI_InitStructure.SPI_FirstBit 		= SPI_FirstBit_LSB;					// 指定数据传输从LSB位开始
	#endif
		SPI_InitStructure.SPI_CRCPolynomial = 7;								// CRC值计算的多项式
		SPI_Init(SPI4, &SPI_InitStructure);  									// 根据SPI_InitStruct中指定的参数初始化外设SPIx寄存器
	 
		SPI_Cmd(SPI4, ENABLE); 		// 使能SPI外设
		m_bSPI4Init = true;
		
		NVIC_InitStructure.NVIC_IRQChannel = SPI4_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
		NVIC_Init(&NVIC_InitStructure); 
	}
	else
	{
		NVIC_InitStructure.NVIC_IRQChannel = SPI4_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
		NVIC_Init(&NVIC_InitStructure);
		
		RCC_APB2PeriphResetCmd(RCC_APB2Periph_SPI4,DISABLE);	// 停止SPI4时钟
		SPI_Cmd(SPI4, DISABLE); 	// 禁止SPI外设
		m_bSPI4Init = false;
	}	 
#endif // #if USE_STM32F4XX	
#if USE_STM32H7XX
	if ( bInit )
	{
		if ( m_bSPI4Init ) HAL_SPI_DeInit(&m_hSpi4);	// 如果已经初始化，则先复位
		
		m_hSpi4.Instance 						= SPI4;
		m_hSpi4.Init.Mode 						= SPI_MODE_SLAVE;
		m_hSpi4.Init.Direction 					= SPI_DIRECTION_2LINES;
		m_hSpi4.Init.DataSize 					= SPI_DATASIZE_16BIT;
		m_hSpi4.Init.CLKPolarity 				= SPI_POLARITY_LOW;					// TI模式参数无效
		m_hSpi4.Init.CLKPhase 					= SPI_PHASE_2EDGE;					// TI模式参数无效
		m_hSpi4.Init.NSS 						= SPI_NSS_HARD_INPUT;
		m_hSpi4.Init.BaudRatePrescaler 			= SPI_BAUDRATEPRESCALER_2;
		m_hSpi4.Init.FirstBit 					= SPI_FIRSTBIT_LSB;					// TI模式参数无效
		m_hSpi4.Init.TIMode 					= SPI_TIMODE_DISABLE;				// TI模式使能
		m_hSpi4.Init.CRCCalculation 			= SPI_CRCCALCULATION_DISABLE;
		m_hSpi4.Init.CRCPolynomial 				= 0x0;
		m_hSpi4.Init.NSSPMode 					= SPI_NSS_PULSE_DISABLE;			// 只在主模式下有效
		m_hSpi4.Init.NSSPolarity 				= SPI_NSS_POLARITY_LOW;				// TI模式参数无效
		m_hSpi4.Init.FifoThreshold 				= SPI_FIFO_THRESHOLD_01DATA;
		m_hSpi4.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
		m_hSpi4.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
		m_hSpi4.Init.MasterSSIdleness 			= SPI_MASTER_SS_IDLENESS_00CYCLE;
		m_hSpi4.Init.MasterInterDataIdleness 	= SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
		m_hSpi4.Init.MasterReceiverAutoSusp 	= SPI_MASTER_RX_AUTOSUSP_DISABLE;
		m_hSpi4.Init.MasterKeepIOState 			= SPI_MASTER_KEEP_IO_STATE_DISABLE;
		m_hSpi4.Init.IOSwap 					= SPI_IO_SWAP_DISABLE;
		if (HAL_SPI_Init(&m_hSpi4) != HAL_OK)
		{
			Error_Handler();
		}
		m_bSPI4Init = true;
	}
	else
	{
		if (HAL_SPI_DeInit(&m_hSpi4) != HAL_OK)
		{
			Error_Handler();
		}
		m_bSPI4Init = false;
	}
#endif	// USE_STM32H7XX
}   

void SPI4_DMA_DBuf_RxConfig(bool bEn, uint8_t *BufferDST1, uint8_t *BufferDST2, uint32_t BufferSize)
{
#if USE_STM32F4XX
	DMA_InitTypeDef 	DMA_InitStructure;
	NVIC_InitTypeDef	NVIC_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);  
	
	DMA_DeInit(DMA2_Stream0);  	// see datasheet table43
	while (DMA_GetCmdStatus(DMA2_Stream0) != DISABLE){}
	
	if (bEn)
	{
		DMA_InitStructure.DMA_Channel 				= DMA_Channel_4;  				// enable channel 4
		DMA_InitStructure.DMA_PeripheralBaseAddr 	= (uint32_t)&(SPI4->DR);
		DMA_InitStructure.DMA_Memory0BaseAddr 		= (uint32_t)BufferDST1;
		DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;
		DMA_InitStructure.DMA_BufferSize 			= BufferSize;
		DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;
		DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;
	#if USE_SPI4_WORD_MODE
		DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_HalfWord;
		DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_HalfWord;
	#else
		DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_Byte;
		DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_Byte;
	#endif
		DMA_InitStructure.DMA_Mode 					= DMA_Mode_Circular;
		DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;
		DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable;         
		DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;
		DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
		DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;
		DMA_DoubleBufferModeConfig(DMA2_Stream0, (uint32_t)BufferDST2, DMA_Memory_0);
		DMA_DoubleBufferModeCmd(DMA2_Stream0, ENABLE);
		DMA_Init(DMA2_Stream0, &DMA_InitStructure);
		DMA_SetCurrDataCounter(DMA2_Stream0, BufferSize);		// set data trans counter
		DMA_Cmd(DMA2_Stream0, ENABLE); 							// 使能DMA通道
		
		NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream0_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			// 使能DMA中断
		NVIC_Init(&NVIC_InitStructure); 
		
		SPI_DMACmd(SPI4, SPI_DMAReq_Rx, ENABLE);				// 使能DMA接收
		
		DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);		// 清除DMA寄存器状态
		DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TEIF0);
		DMA_ITConfig(DMA2_Stream0, DMA_IT_TC, ENABLE);
	}
	else
	{
		SPI_DMACmd(SPI4, SPI_DMAReq_Rx, DISABLE);
		
		NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream0_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
		NVIC_Init(&NVIC_InitStructure); 
		
		DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);
		DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TEIF0);
		DMA_ITConfig(DMA2_Stream0, DMA_IT_TC, DISABLE);
		
		DMA_SetCurrDataCounter(DMA2_Stream0, BufferSize);		// set data trans counter
		DMA_Cmd(DMA2_Stream0, DISABLE);  
	}
#elif USE_STM32H7XX
	__HAL_RCC_DMA2_CLK_ENABLE();
	if ( bEn )
	{
		m_hdma_spi4_rx.Instance 				= DMA2_Stream1;
		m_hdma_spi4_rx.Init.Request 			= DMA_REQUEST_SPI4_RX;
		m_hdma_spi4_rx.Init.Direction 			= DMA_PERIPH_TO_MEMORY;
		m_hdma_spi4_rx.Init.PeriphInc 			= DMA_PINC_DISABLE;
		m_hdma_spi4_rx.Init.MemInc 				= DMA_MINC_ENABLE;
		m_hdma_spi4_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
		m_hdma_spi4_rx.Init.MemDataAlignment 	= DMA_MDATAALIGN_HALFWORD;
		m_hdma_spi4_rx.Init.Mode 				= DMA_CIRCULAR;
		m_hdma_spi4_rx.Init.Priority 			= DMA_PRIORITY_VERY_HIGH;
		m_hdma_spi4_rx.Init.FIFOMode 			= DMA_FIFOMODE_DISABLE;
		if (HAL_DMA_Init(&m_hdma_spi4_rx) != HAL_OK)
		{
			Error_Handler();
		}

		__HAL_LINKDMA(&m_hSpi4, hdmarx, m_hdma_spi4_rx);
	
		HAL_SPI_Receive_DMA_double(&m_hSpi4, BufferDST1, BufferDST2, BufferSize);
		HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0, 3);
		HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
	}
	else
	{
		// 2026.03.12 修改：增加判断条件：仅当DMA处于运行状态时才执行关闭操作
        // 1. 检查SPI句柄是否处于BUSY状态（DMA正在传输）
        // 2. 检查DMA接收句柄是否未处于就绪状态
        if ((m_hSpi4.State == HAL_SPI_STATE_BUSY_RX) && 
            (m_hdma_spi4_rx.State != HAL_DMA_STATE_READY))
        {
            HAL_SPI_DMAStop( &m_hSpi4 );
        }
        // 无论DMA是否开启，都禁用中断（避免中断残留）
        HAL_NVIC_DisableIRQ(DMA2_Stream1_IRQn);
	}
#endif
}

