
/*************************************************************************************************/
/*
// 2026.03.18 修改：在Comm_Enable函数中，补充 STM32H7XX 的配置bug
*/
/*************************************************************************************************/
#include "Comm_Operate.h"

#if USE_STM32H7XX
#include "STM32H7_operate.h"
#endif

COMM_OBJECT m_stComm1;
COMM_OBJECT m_stComm2;
COMM_OBJECT m_stComm3;
COMM_OBJECT* m_pComm1 = &m_stComm1;
COMM_OBJECT* m_pComm2 = &m_stComm2;
COMM_OBJECT* m_pComm3 = &m_stComm3;
COMM_OBJECT* m_pComm4 = 0;
COMM_OBJECT* m_pComm6 = 0;
COMM_OBJECT* m_pComm7 = 0;

#if USE_STM32F1XX
bool m_bComm1TxDmaEnd = false;
bool m_bComm1RxDmaEnd = false;
bool m_bComm2TxDmaEnd = false;
bool m_bComm2RxDmaEnd = false;
bool m_bComm3TxDmaEnd = false;
bool m_bComm3RxDmaEnd = false;

#elif USE_STM32H7XX
UART_HandleTypeDef m_huart1;
UART_HandleTypeDef m_huart2;
UART_HandleTypeDef m_huart3;
UART_HandleTypeDef m_huart4;
UART_HandleTypeDef m_huart6;
UART_HandleTypeDef m_huart7;
DMA_HandleTypeDef  m_hdma_usart1_rx;
#endif

void Comm_PutChar(int nComm, u8 ch);
void Comm_PutStr(int nComm, char * str);			// 由于 RS485 接口的原因，发送函数统一设置，但是每个串口的发送函数需要独立设置
void Comm_PutBuf(int nComm, char * buf, u32 nBuf);	// 由于 RS485 接口的原因，发送函数统一设置，但是每个串口的发送函数需要独立设置

u8 UART_SetCommBaud(int nComm, uint32_t nBaud)
{
 	// 设置串口波特率，此函数不改变 COMM_OBJECT 对象中的波特率参数值
	// 如果 nBaud < 1  则设置对象波特率参数值为结构体中已经保存的波特率参数值
	// 此函数不影响串口的使能状态，仅改变波特率，方便在共用串口时进行便捷操作
	COMM_OBJECT* pCommObj = Comm_GetObject( nComm );
	u32 uB = nBaud < 1 ? UART_GetRealBaud( Comm_GetObject(nComm)->nCommBaud ) : nBaud;

#if USE_STM32F1XX || USE_STM32F4XX
	if ( pCommObj ) 
	{
		USART_InitTypeDef USART_InitStructure; 

		if ( nBaud >= 1000000 )
		{
			USART_OverSampling8Cmd(pCommObj->USARTn, ENABLE);			// 超过2M采样率建议使用8倍过采样
			USART_InitStructure.USART_StopBits 	= USART_StopBits_2;		// 串口作为数传通道时，停止位设置为2bit
		}
		else
		{
			USART_InitStructure.USART_StopBits 	= USART_StopBits_1;		// 串口作为一般通道时，停止位设置为1bit
		}
		
		USART_InitStructure.USART_BaudRate 		= uB;
		USART_InitStructure.USART_WordLength 	= USART_WordLength_8b;
		USART_InitStructure.USART_Parity 		= USART_Parity_No;
		USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None ;
		USART_InitStructure.USART_Mode 			= USART_Mode_Rx | USART_Mode_Tx;
		USART_Init(pCommObj->USARTn, &USART_InitStructure);
	}
#endif

#if USE_STM32H7XX
	if ( pCommObj && pCommObj->pUart && !pCommObj->bEnable )
	{
		HAL_UART_DeInit(pCommObj->pUart);	// 先复位再初始化
				
		pCommObj->pUart->Init.BaudRate 			= uB;
		pCommObj->pUart->Init.WordLength 		= UART_WORDLENGTH_8B;
		pCommObj->pUart->Init.StopBits 			= UART_STOPBITS_1;
		pCommObj->pUart->Init.Parity 			= UART_PARITY_NONE;
		pCommObj->pUart->Init.Mode 				= UART_MODE_TX_RX;
		pCommObj->pUart->Init.HwFlowCtl 		= UART_HWCONTROL_NONE;
		pCommObj->pUart->Init.OverSampling 		= UART_OVERSAMPLING_16;
		pCommObj->pUart->Init.OneBitSampling 	= UART_ONE_BIT_SAMPLE_DISABLE;
		pCommObj->pUart->Init.ClockPrescaler 	= UART_PRESCALER_DIV1;
		
		if ( nBaud > 2000000 )
		{
			pCommObj->pUart->Init.StopBits 		= UART_STOPBITS_2;
			pCommObj->pUart->Init.Mode 			= UART_MODE_RX;
			pCommObj->pUart->Init.OverSampling 	= UART_OVERSAMPLING_8;
		}
		
		if ( pCommObj->nCommSn == PCLINK_Comm )
		{
			// 对上位机串口设置自动波特率功能
			pCommObj->pUart->AdvancedInit.AdvFeatureInit    = UART_ADVFEATURE_RXOVERRUNDISABLE_INIT | UART_ADVFEATURE_AUTOBAUDRATE_INIT;
			pCommObj->pUart->AdvancedInit.OverrunDisable 	= UART_ADVFEATURE_OVERRUN_DISABLE;
			pCommObj->pUart->AdvancedInit.AutoBaudRateMode	= UART_ADVFEATURE_AUTOBAUDRATE_ON0X55FRAME;
			pCommObj->pUart->AdvancedInit.AutoBaudRateEnable= UART_ADVFEATURE_AUTOBAUDRATE_ENABLE;
		}
		else
		{
			pCommObj->pUart->AdvancedInit.AdvFeatureInit 	= UART_ADVFEATURE_RXOVERRUNDISABLE_INIT;
			pCommObj->pUart->AdvancedInit.OverrunDisable 	= UART_ADVFEATURE_OVERRUN_DISABLE;
		}
		
		if (HAL_UART_Init(pCommObj->pUart) != HAL_OK)
		{
			Error_Handler();
		}
		if (HAL_UARTEx_SetTxFifoThreshold(pCommObj->pUart, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
		{
			Error_Handler();
		}
		if (HAL_UARTEx_SetRxFifoThreshold(pCommObj->pUart, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
		{
			Error_Handler();
		}
		if (HAL_UARTEx_DisableFifoMode(pCommObj->pUart) != HAL_OK)
		{
			Error_Handler();
		}
	}
	else if ( pCommObj && pCommObj->pUart && pCommObj->bEnable )
	{
		// 已经初始化的情况下，只更改波特率
		// 1. 禁止 UART (只关闭控制器，不关引脚)
		// HAL_UART_Init()内部虽然有串口禁止操作，但是对于中断标志的处理不全，此处不要更改
		// 此处的禁止操作不能用 Comm_Enable() 来替代，因为 Comm_Enable() 内部会清零串口参数
		__HAL_UART_DISABLE(pCommObj->pUart);
		CLEAR_BIT(pCommObj->pUart->Instance->CR3, USART_CR3_EIE);
		CLEAR_BIT(pCommObj->pUart->Instance->CR1, USART_CR1_RXNEIE_RXFNEIE);		
		CLEAR_BIT(pCommObj->pUart->Instance->ISR, (USART_ISR_TC | USART_ISR_RXNE_RXFNE));		
		
		// 2. 修改波特率寄存器
		pCommObj->pUart->Init.BaudRate = uB;
				
		if ( nBaud > 2000000 )
		{
			pCommObj->pUart->Init.StopBits 		= UART_STOPBITS_2;
			pCommObj->pUart->Init.Mode 			= UART_MODE_RX;
			pCommObj->pUart->Init.OverSampling 	= UART_OVERSAMPLING_8;
		}
		
		// HAL_UART_Init() 内部会重算BRR、配置
		if (HAL_UART_Init(pCommObj->pUart) != HAL_OK) 
		{
			printf(" Error: HAL_UART_Init() return HAL_ERROR.\r\n");
		}

		// 3. 重新打开 UART
		// HAL_UART_Init()内部虽然有串口使能操作，但是对于中断标志的处理不全，此处不要更改
		// 此处的使能操作不能用 Comm_Enable() 来替代，因为 Comm_Enable() 内部会清零串口参数
		__HAL_UART_ENABLE(pCommObj->pUart);		// 使能串口 CR1->UE 位
		SET_BIT(pCommObj->pUart->Instance->CR3, USART_CR3_EIE);				// 使能错误中断标志位
		SET_BIT(pCommObj->pUart->Instance->CR1, USART_CR1_RXNEIE_RXFNEIE); 	// 使能接收中断标志位
		
		// HAL_UART_Receive_IT 会开启接收中断：标志位UART_IT_RXNE，并且设置接收缓冲以及接收缓冲接收最大数据量
		if ( HAL_UART_Receive_IT(pCommObj->pUart, (u8*)pCommObj->chRx, RXBUFFER_SIZE) == HAL_OK )
		{
			// 由于HAL库内部在开启中断的过程中设置了 pUart->RxState 为 HAL_UART_STATE_BUSY_RX
			// 而在退出时没有清除该标志位，因此此处必须设置准备好标志
			pCommObj->pUart->RxState = HAL_UART_STATE_READY;			
		}	
	}
#endif
	return UART_GetEnumBaud(uB);
}

int fputc(int ch, FILE *f)
{
	/* Place your implementation of fputc here */
	/* e.g. write a character to the USART */
#if DEBUG_PRINTF
	Comm_PutChar( DEBUG_Comm, (u8) ch);
#endif
	return ch;
}

void Comm1_Config(int nBaud, bool bEn)
{
	if ( m_pComm1 && m_pComm1->bEnable )
	{
		m_pComm1->nCommBaud = UART_SetCommBaud(COMM1, nBaud); 
		Comm_Enable(m_pComm1->nCommSn, true); return;
	}
/*----------------------------------------------*/
#if USE_STM32F1XX
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	/* Enable GPIO clock */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);	/* Enable USART clock */

#if COMM1_USE_TX_PIN
	GPIO_SetPin(GPIOA, GPIO_Pin_9,  GPIO_Mode_AF_PP, 	   GPIO_Speed_50MHz);
#endif
	GPIO_SetPin(GPIOA, GPIO_Pin_10, GPIO_Mode_IN_FLOATING, GPIO_Speed_50MHz);
#if COMM1_RS485_CTRL
	GPIO_SetPin(COMM1_RS485_DE_GPIO_PORT, COMM1_RS485_DE_GPIO, GPIO_Mode_Out_PP, GPIO_Speed_2MHz);
#endif
#endif
/*----------------------------------------------*/
#if USE_STM32F4XX
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);	/* Enable GPIO clock */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);	/* Enable USART clock */
	
#if COMM1_USE_TX_PIN
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
	GPIO_SetFunc(GPIOA, GPIO_Pin_9,  GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
#endif
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);
	GPIO_SetFunc(GPIOA, GPIO_Pin_10, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
#if COMM1_RS485_CTRL
	GPIO_SetOut(COMM1_RS485_DE_GPIO_PORT, COMM1_RS485_DE_GPIO, GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
#endif
#endif
/*----------------------------------------------*/
#if USE_STM32F4XX || USE_STM32H7XX
	if (m_pComm1 == 0) m_pComm1 = mymalloc(SRAMIN, sizeof(COMM_OBJECT)); 
#endif
#if USE_STM32H7XX
	if ( m_pComm1 )
	{
		m_pComm1->pUart = &m_huart1;
		m_pComm1->pUart->Instance = USART1;
	}
#endif

	if ( m_pComm1 )
	{
		m_pComm1->nCommSn   = 1;
		m_pComm1->USARTn	= USART1;
		m_pComm1->nCommBaud = UART_SetCommBaud(COMM1, nBaud);	
		m_pComm1->bRS485 = COMM1_RS485 ? true : false; 
		Comm_Enable(m_pComm1->nCommSn, bEn);
	}
}

void Comm2_Config(int nBaud, bool bEn)
{
	if ( m_pComm2 && m_pComm2->bEnable )
	{
		m_pComm2->nCommBaud = UART_SetCommBaud(COMM2, nBaud); 
		Comm_Enable(m_pComm2->nCommSn, true); return;
	}
/*----------------------------------------------*/
#if USE_STM32F1XX
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	/* Enable GPIO clock */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);	/* Enable USART clock */

#if COMM2_USE_TX_PIN
	GPIO_SetPin(GPIOA, GPIO_Pin_2, GPIO_Mode_AF_PP, GPIO_Speed_50MHz);
#endif
#if COMM2_USE_RX_PIN
	GPIO_SetPin(GPIOA, GPIO_Pin_3, GPIO_Mode_IN_FLOATING, GPIO_Speed_50MHz);	
#endif
/*----------------------------------------------*/
#elif USE_STM32F4XX
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);	/* Enable GPIO clock */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);	/* Enable USART clock */

#if COMM2_USE_TX_PIN
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
	GPIO_SetFunc(GPIOA, GPIO_Pin_2, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
#endif
#if COMM2_USE_RX_PIN
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);
	GPIO_SetFunc(GPIOA, GPIO_Pin_3, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
#endif
#endif
/*----------------------------------------------*/
#if USE_STM32F1XX || USE_STM32F4XX || USE_STM32H7XX
	if (m_pComm2 == 0) m_pComm2 = mymalloc(SRAMIN, sizeof(COMM_OBJECT)); 
#endif
#if USE_STM32H7XX
	if ( m_pComm2 )
	{
		m_pComm2->pUart = &m_huart2;
		m_pComm2->pUart->Instance = USART2;
	}
#endif
	if ( m_pComm2 )
	{
		m_pComm2->nCommSn   = 2;
		m_pComm2->USARTn	= USART2;
		m_pComm2->nCommBaud = UART_SetCommBaud(COMM2, nBaud);	
		m_pComm2->bRS485 = COMM2_RS485 ? true : false; 
		Comm_Enable(m_pComm2->nCommSn, bEn);
	}
}

void Comm3_Config(int nBaud, bool bEn)
{
	if ( m_pComm3 && m_pComm3->bEnable )
	{
		m_pComm3->nCommBaud = UART_SetCommBaud(COMM3, nBaud); 
		Comm_Enable(m_pComm3->nCommSn, true); return;
	}
/*----------------------------------------------*/
#if USE_STM32F1XX
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	/* Enable GPIO clock */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);	/* config USART1 clock */

    GPIO_SetPin(GPIOB, GPIO_Pin_10, GPIO_Mode_AF_PP, 		GPIO_Speed_50MHz);
	GPIO_SetPin(GPIOB, GPIO_Pin_11, GPIO_Mode_IN_FLOATING, 	GPIO_Speed_50MHz);	
#endif
/*----------------------------------------------*/
#if USE_STM32F4XX
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);		// /* Enable USART clock */
	
#if COMM3_AF_PD8_9
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);		// /* Enable GPIO clock */
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource8, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource9, GPIO_AF_USART3);
	GPIO_SetFunc(GPIOD, GPIO_Pin_8, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
	GPIO_SetFunc(GPIOD, GPIO_Pin_9, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
#else
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);		// /* Enable GPIO clock */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);
	GPIO_SetFunc(GPIOB, GPIO_Pin_10, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
	GPIO_SetFunc(GPIOB, GPIO_Pin_11, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
#endif
#if COMM3_RS485_CTRL
	GPIO_SetOut(COMM3_RS485_DE_GPIO_PORT, COMM3_RS485_DE_GPIO, GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
#endif
#endif
/*----------------------------------------------*/
#if USE_STM32F1XX || USE_STM32F4XX || USE_STM32H7XX
	if (m_pComm3 == 0) m_pComm3 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
#endif
#if USE_STM32H7XX
	if ( m_pComm3 )
	{
		m_pComm3->pUart = &m_huart3;
		m_pComm3->pUart->Instance = USART3;
	}
#endif

	if ( m_pComm3 )
	{
		m_pComm3->nCommSn   = 3;
		m_pComm3->USARTn	= USART3;
		m_pComm3->nCommBaud = UART_SetCommBaud(COMM3, nBaud);	
		m_pComm3->bRS485 = COMM3_RS485 ? true : false; 
		Comm_Enable(m_pComm3->nCommSn, bEn);
	}
}

/***********************************************/
#if USE_STM32F4XX || USE_STM32H7XX
void Comm4_Config(int nBaud, bool bEn)
{   
	if ( m_pComm4 && m_pComm4->bEnable )
	{
		m_pComm4->nCommBaud = UART_SetCommBaud(COMM4, nBaud); 
		Comm_Enable(m_pComm4->nCommSn, true); return;
	}
/*----------------------------------------------*/
#if USE_STM32F4XX	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);	/* Enable GPIO clock */
	RCC_APB2PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);	/* Enable USART clock */
    
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource0, GPIO_AF_UART4);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource1, GPIO_AF_UART4);
	GPIO_SetFunc(GPIOA, GPIO_Pin_0, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
	GPIO_SetFunc(GPIOA, GPIO_Pin_1, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
	if (m_pComm4 == 0) 
#endif
/*----------------------------------------------*/
#if USE_STM32F4XX || USE_STM32H7XX
	if (m_pComm4 == 0) m_pComm4 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
#endif
#if USE_STM32H7XX
	if ( m_pComm4 )
	{
		m_pComm4->pUart = &m_huart4;
		m_pComm4->pUart->Instance = UART4;
	}
#endif

	if ( m_pComm4 )
	{
		m_pComm4->nCommSn   = 4;
		m_pComm4->USARTn	= UART4;
		m_pComm4->nCommBaud = UART_SetCommBaud(COMM4, nBaud);	
		m_pComm4->bRS485 = COMM4_RS485 ? true : false; 
		Comm_Enable(m_pComm4->nCommSn, bEn);
	}
}

void Comm6_Config(int nBaud, bool bEn)
{   
	if ( m_pComm6 && m_pComm6->bEnable )
	{
		m_pComm6->nCommBaud = UART_SetCommBaud(COMM6, nBaud); 
		Comm_Enable(m_pComm6->nCommSn, true); return;
	}
/*----------------------------------------------*/
#if USE_STM32F4XX
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);	/* Enable GPIO clock */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART6, ENABLE);	/* Enable USART clock */
    
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource6, GPIO_AF_USART6);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource7, GPIO_AF_USART6);
	GPIO_SetFunc(GPIOC, GPIO_Pin_6, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
	GPIO_SetFunc(GPIOC, GPIO_Pin_7, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
#endif
/*----------------------------------------------*/
#if USE_STM32F4XX || USE_STM32H7XX
	if (m_pComm6 == 0) m_pComm6 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
#endif
#if USE_STM32H7XX
	if ( m_pComm6 )
	{
		m_pComm6->pUart = &m_huart6;
		m_pComm6->pUart->Instance = USART6;
	}
#endif

	if ( m_pComm6 )
	{
		m_pComm6->nCommSn   = 6;
		m_pComm6->USARTn	= USART6;
		m_pComm6->nCommBaud = UART_SetCommBaud(COMM6, nBaud);	
		m_pComm6->bRS485 = COMM6_RS485 ? true : false; 
		Comm_Enable(m_pComm6->nCommSn, bEn);
	}
}

void Comm7_Config(int nBaud, bool bEn)
{   
	if ( m_pComm7 && m_pComm7->bEnable )
	{
		m_pComm7->nCommBaud = UART_SetCommBaud(COMM7, nBaud); 
		Comm_Enable(m_pComm7->nCommSn, true); return;
	}
/*----------------------------------------------*/
#if USE_STM32F4XX
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);	/* Enable GPIO clock */
	RCC_APB2PeriphClockCmd(RCC_APB1Periph_UART7, ENABLE);	/* Enable USART clock */
    
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource7, GPIO_AF_UART7);
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource8, GPIO_AF_UART7);
	GPIO_SetFunc(GPIOE, GPIO_Pin_7, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
	GPIO_SetFunc(GPIOE, GPIO_Pin_8, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
#endif
/*----------------------------------------------*/
#if USE_STM32F4XX || USE_STM32H7XX
	if (m_pComm7 == 0) m_pComm7 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
#endif
#if USE_STM32H7XX
	if ( m_pComm7 )
	{
		m_pComm7->pUart = &m_huart7;
		m_pComm7->pUart->Instance = UART7;
	}
#endif

	if ( m_pComm7 )
	{
		m_pComm7->nCommSn   = 7;
		m_pComm7->USARTn	= UART7;
		m_pComm7->nCommBaud = UART_SetCommBaud(COMM7, nBaud);	
		m_pComm7->bRS485 = COMM7_RS485 ? true : false;
		Comm_Enable(m_pComm7->nCommSn, bEn);
	}
}
#endif	// USE_STM32F4XX || USE_STM32H7XX
/***********************************************/

void Comm_PutChar(int nComm, u8 ch)
{
	COMM_OBJECT* pCommObj = Comm_GetObject(nComm);
	if ( pCommObj && pCommObj->bEnable )
	{
	#if USE_STM32H7XX
		uint32_t tickstart;
		/* Check that a Tx process is not already ongoing */
		if (pCommObj->pUart && pCommObj->pUart->gState == HAL_UART_STATE_READY)
		{
			pCommObj->pUart->ErrorCode = HAL_UART_ERROR_NONE;
			pCommObj->pUart->gState = HAL_UART_STATE_BUSY_TX;

			/* Init tickstart for timeout management */
			tickstart = HAL_GetTick();

			if (UART_WaitOnFlagUntilTimeout(pCommObj->pUart, UART_FLAG_TXE, RESET, tickstart, 0x0F) != HAL_OK)
			{
				pCommObj->pUart->gState = HAL_UART_STATE_READY;	return;
			}
		  
			pCommObj->pUart->Instance->TDR = ch;

			if (UART_WaitOnFlagUntilTimeout(pCommObj->pUart, UART_FLAG_TC, RESET, tickstart, 0x0F) != HAL_OK)
			{
				pCommObj->pUart->gState = HAL_UART_STATE_READY; return;
			}

			/* At end of Tx process, restore huart->gState to Ready */
			pCommObj->pUart->gState = HAL_UART_STATE_READY;
		}
	#else
		USART_SendData(pCommObj->USARTn, ch);
		while(USART_GetFlagStatus(pCommObj->USARTn, USART_FLAG_TC) == RESET);
	#endif
	}
}

void Comm_PutStr(int nComm, char * str)
{
	COMM_OBJECT* pCommObj = Comm_GetObject(nComm);
	if ( pCommObj && pCommObj->bEnable && str )
	{
#if USE_STM32H7XX
		int nLen = strlen(str);
		HAL_UART_Transmit(pCommObj->pUart, (u8*)str, nLen, 0xFFFF);
#else
		while(*str)	
		{ 
			USART_SendData(pCommObj->USARTn, *str++);
			while(USART_GetFlagStatus(pCommObj->USARTn, USART_FLAG_TC) == RESET);
		}  
#endif
	}
}

void Comm_PutBuf(int nComm, char * buf, u32 nBuf)
{
	COMM_OBJECT* pCommObj = Comm_GetObject(nComm);
	if ( pCommObj && pCommObj->bEnable && buf )
	{
#if USE_STM32H7XX
		HAL_UART_Transmit(pCommObj->pUart, (u8*)buf, nBuf, 0xFFFF);
#else
		int i = 0;
		for (i = 0; i < nBuf; i++)
		{
			USART_SendData(pCommObj->USARTn, *buf++);
			while(USART_GetFlagStatus(pCommObj->USARTn, USART_FLAG_TC) == RESET);
		}  
#endif
	}
}

void Comm1_PutStr(char * str)
{
#if COMM1_RS485 && COMM1_RS485_CTRL
	COMM1_RS485_DE;
#elif COMM1_RS485
	delayus(m_pComm1->nDelay);
#endif
	Comm_PutStr( COMM1, str );
#if COMM1_RS485_CTRL
	COMM1_RS485_RE;
#endif
}

void Comm2_PutStr(char * str)
{
#if COMM2_RS485 && COMM2_RS485_CTRL
	COMM2_RS485_DE;
#elif COMM2_RS485
	delayus(m_pComm2->nDelay);
#endif
	Comm_PutStr( COMM2, str );
#if COMM2_RS485_CTRL
	COMM2_RS485_RE;
#endif
}

void Comm3_PutStr(char * str)
{
#if COMM3_RS485 && COMM3_RS485_CTRL
	COMM3_RS485_DE;
#elif COMM3_RS485
	delayus(m_pComm3->nDelay);
#endif
	Comm_PutStr( COMM3, str );
#if COMM3_RS485_CTRL
	COMM3_RS485_RE;
#endif
}

void Comm1_PutBuf(char * buf, u32 nBuf)
{
#if COMM1_RS485 && COMM1_RS485_CTRL
	COMM1_RS485_DE;
#elif COMM1_RS485
	delayus(m_pComm1->nDelay);
#endif
	Comm_PutBuf( COMM1, buf, nBuf );
#if COMM1_RS485_CTRL
	COMM1_RS485_RE;
#endif
}

void Comm2_PutBuf(char * buf, u32 nBuf)
{
#if COMM2_RS485 && COMM2_RS485_CTRL
	COMM2_RS485_DE;
#elif COMM2_RS485
	delayus(m_pComm2->nDelay);
#endif
	Comm_PutBuf( COMM2, buf, nBuf );
#if COMM2_RS485_CTRL
	COMM2_RS485_RE;
#endif
}

void Comm3_PutBuf(char * buf, u32 nBuf)
{
#if COMM3_RS485 && COMM3_RS485_CTRL
	COMM3_RS485_DE;
#elif COMM3_RS485
	delayus(m_pComm3->nDelay);
#endif
	Comm_PutBuf( COMM3, buf, nBuf );
#if COMM3_RS485_CTRL
	COMM3_RS485_RE;
#endif
}

/************************************************/
#if USE_STM32F4XX || USE_STM32H7XX

void Comm4_PutStr(char * str)
{
#if COMM4_RS485 && COMM4_RS485_CTRL
	COMM4_RS485_DE;
#elif COMM4_RS485
	delayus(m_pComm4->nDelay);
#endif
	Comm_PutStr( COMM4, str );
#if COMM4_RS485_CTRL
	COMM4_RS485_RE;
#endif
}

void Comm4_PutBuf(char * buf, u32 nBuf)
{
#if COMM4_RS485 && COMM4_RS485_CTRL
	COMM4_RS485_DE;
#elif COMM4_RS485
	delayus(m_pComm4->nDelay);
#endif
	Comm_PutBuf( COMM4, buf, nBuf );
#if COMM4_RS485_CTRL
	COMM4_RS485_RE;
#endif
}

void Comm6_PutStr(char * str)
{
#if COMM6_RS485 && COMM6_RS485_CTRL
	COMM6_RS485_DE;
#elif COMM6_RS485
	delayus(m_pComm6->nDelay);
#endif
	Comm_PutStr( COMM6, str );
#if COMM6_RS485_CTRL
	COMM6_RS485_RE;
#endif
}

void Comm6_PutBuf(char * buf, u32 nBuf)
{
#if COMM6_RS485 && COMM6_RS485_CTRL
	COMM6_RS485_DE;
#elif COMM6_RS485
	delayus(m_pComm6->nDelay);
#endif
	Comm_PutBuf( COMM6, buf, nBuf );
#if COMM6_RS485_CTRL
	COMM6_RS485_RE;
#endif
}

void Comm7_PutStr(char * str)
{
#if COMM7_RS485 && COMM7_RS485_CTRL
	COMM7_RS485_DE;
#elif COMM7_RS485
	delayus(m_pComm7->nDelay);
#endif
	Comm_PutStr( COMM7, str );
#if COMM7_RS485_CTRL
	COMM7_RS485_RE;
#endif
}

void Comm7_PutBuf(char * buf, u32 nBuf)
{
#if COMM7_RS485 && COMM7_RS485_CTRL
	COMM7_RS485_DE;
#elif COMM7_RS485
	delayus(m_pComm7->nDelay);
#endif
	Comm_PutBuf( COMM7, buf, nBuf );
#if COMM7_RS485_CTRL
	COMM7_RS485_RE;
#endif
}

#endif
/************************************************/

void Comm_Config(int nComm, int nBaud, bool bEn)//nComm串口号 nbaud波特率 bEn使能
{
	// Init comm
	// nBaud 是波特率的真实值，例如：9600，,115200...，根据你指定的串口号和波特率，去初始化对应的串口通信。
	if (nComm == 1)
		Comm1_Config(nBaud, bEn);
	else if (nComm == 2)
		Comm2_Config(nBaud, bEn);
	else if (nComm == 3)
		Comm3_Config(nBaud, bEn);
#if USE_STM32F4XX || USE_STM32H7XX
	else if (nComm == 4)
		Comm4_Config(nBaud, bEn);
	else if (nComm == 6)
		Comm6_Config(nBaud, bEn);
	else if (nComm == 7)
		Comm7_Config(nBaud, bEn);
#endif
}

void Comm_Config2(int nComm, int nBaud, bool bEn, bool bVirtual)
{
	// Init comm, if bVirtual = true, only generate buffer
	COMM_OBJECT* pCommObj = Comm_GetObject(nComm);
	if (bVirtual)
	{
		if (nComm == 1 || nComm == 2 || nComm == 3)
		{
		#if USE_STM32F1XX || USE_STM32F4XX
			if (nComm == 1 && pCommObj == 0) m_pComm1 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
			if (nComm == 2 && pCommObj == 0) m_pComm2 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
			if (nComm == 3 && pCommObj == 0) m_pComm3 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
		#elif USE_STM32H7XX
			if (nComm == 1 && pCommObj == 0) m_pComm1 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
			if (nComm == 2 && pCommObj == 0) m_pComm2 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
			if (nComm == 3 && pCommObj == 0) m_pComm3 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
		#endif
		}
	#if USE_STM32F4XX
		else if (nComm == 4 || nComm == 6 || nComm == 7)
		{
			if (nComm == 4 && pCommObj == 0) m_pComm4 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
			if (nComm == 6 && pCommObj == 0) m_pComm6 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
			if (nComm == 7 && pCommObj == 0) m_pComm7 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
		}
	#elif USE_STM32H7XX
		else if (nComm == 4 || nComm == 6 || nComm == 7)
		{
			if (nComm == 4 && pCommObj == 0) m_pComm4 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
			if (nComm == 6 && pCommObj == 0) m_pComm6 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
			if (nComm == 7 && pCommObj == 0) m_pComm7 = mymalloc(SRAMIN, sizeof(COMM_OBJECT));
		}
	#endif
		pCommObj = Comm_GetObject(nComm);
		if ( pCommObj )
		{
			pCommObj->USARTn	= UART_CommSnToUSARTn(nComm);
			pCommObj->nCommBaud = UART_GetEnumBaud(nBaud);	
			Comm_ClearParam( nComm );
		}
	}
	else
	{
		Comm_Config(nComm, nBaud, bEn);
	}
}

void Comm_Enable(int nComm, bool bEn)
{
	// enable special comm
	int nBaud;
	COMM_OBJECT* pCommObj = Comm_GetObject(nComm);
#if USE_STM32F1XX || USE_STM32F4XX
	if ( pCommObj == 0 ) return;
#elif USE_STM32H7XX
	if ( pCommObj == 0 || pCommObj->pUart == 0 ) return;
#endif
	
	pCommObj->bEnable = bEn;
	// 根据波特率计算RS485串口发送延迟时间参数
	// 注意，延迟时间按照uS为单位，延迟10个字节以上的发送时间
	nBaud = UART_GetRealBaud(pCommObj->nCommBaud);
	pCommObj->nDelay = (u16)(1000000.0f/((float)nBaud)*100);	
	// 清除串口缓冲区相关参数
	Comm_ClearParam(nComm);	
	
#if USE_STM32H7XX
	if( bEn )
	{
		__HAL_UART_ENABLE(pCommObj->pUart);  							// 使能串口 CR1->UE 位
		SET_BIT(pCommObj->pUart->Instance->CR3, USART_CR3_EIE);				// 使能错误中断标志位
		SET_BIT(pCommObj->pUart->Instance->CR1, USART_CR1_RXNEIE_RXFNEIE); 	// 使能接收中断标志位
		
		// HAL_UART_Receive_IT 会开启接收中断：标志位UART_IT_RXNE，并且设置接收缓冲以及接收缓冲接收最大数据量
		if ( HAL_UART_Receive_IT(pCommObj->pUart, (u8*)pCommObj->chRx, RXBUFFER_SIZE) == HAL_OK )
		{
			// 由于HAL库内部在开启中断的过程中设置了 pUart->RxState 为 HAL_UART_STATE_BUSY_RX
			// 而在退出时没有清除该标志位，因此此处必须设置准备好标志
			pCommObj->pUart->RxState = HAL_UART_STATE_READY;			
		}			
	}
	else
	{
		__HAL_UART_DISABLE(pCommObj->pUart);
		CLEAR_BIT(pCommObj->pUart->Instance->CR3, USART_CR3_EIE);
		CLEAR_BIT(pCommObj->pUart->Instance->CR1, USART_CR1_RXNEIE_RXFNEIE);		
		CLEAR_BIT(pCommObj->pUart->Instance->ISR, (USART_ISR_TC | USART_ISR_RXNE_RXFNE));		
	}
#else
    if( bEn )
    {
		USART_ClearFlag(pCommObj->USARTn, USART_FLAG_TC | USART_FLAG_RXNE | USART_FLAG_LBD);			
		USART_ITConfig(pCommObj->USARTn, USART_IT_RXNE, ENABLE);	
		USART_Cmd(pCommObj->USARTn, ENABLE);					
    }
    else
    {
        USART_Cmd(pCommObj->USARTn, DISABLE);
		USART_ITConfig(pCommObj->USARTn, USART_IT_RXNE, DISABLE);	
		USART_ClearFlag(pCommObj->USARTn, USART_FLAG_TC | USART_FLAG_RXNE );		
    }
#endif
}

void Comm_ClearParam(int nComm)
{
	COMM_OBJECT* pCommObj = Comm_GetObject(nComm);
	if (pCommObj == 0) return;
	pCommObj->bRdy = false;
	pCommObj->nRFlag = 0;
	pCommObj->uCommRx = 0;
	pCommObj->uCommTx = 0;
	memset( pCommObj->chRx, 0, 8 );
	memset( pCommObj->chTxBuf, 0, MAX_COMM_BUF_SIZE );
	memset( pCommObj->chRxBuf, 0, MAX_COMM_BUF_SIZE );
}

void Comm_EnableTxOnly(int nComm)
{
	// 串口仅发送使能
#if USE_STM32H7XX
	COMM_OBJECT* pCommObj = Comm_GetObject(nComm);
	if ( pCommObj == 0 || pCommObj->pUart == 0 ) return;
	CLEAR_BIT(pCommObj->pUart->Instance->CR1, (USART_ISR_TC | USART_ISR_RXNE_RXFNE));		
	CLEAR_BIT(pCommObj->pUart->Instance->CR1,  USART_CR1_RE);
	__HAL_UART_ENABLE_IT(pCommObj->pUart,  UART_IT_TC);
	__HAL_UART_DISABLE_IT(pCommObj->pUart, UART_IT_RXNE);
	__HAL_UART_ENABLE(pCommObj->pUart);	
#else
	COMM_OBJECT* pCommObj = Comm_GetObject(nComm);
	if ( pCommObj == 0 ) return;
	USART_ClearFlag(pCommObj->USARTn, USART_FLAG_TC);			
	USART_ClearFlag(pCommObj->USARTn, USART_FLAG_RXNE);		
	USART_ITConfig(pCommObj->USARTn,  USART_IT_RXNE, DISABLE);	
	USART_ITConfig(pCommObj->USARTn,  USART_IT_TC, ENABLE);
	USART_Cmd(pCommObj->USARTn, ENABLE);	
#endif
}

bool Comm_GetFrameMsg(char rx, COMM_OBJECT* pComm)
{	
	if (!pComm) return false;
	pComm->chRx[0] = rx;
	
	if (rx == '$')
	{
		pComm->bRdy = false;//还没接受完
		pComm->nRFlag = 1;//标记“正在接收”
		pComm->uCommRx = 0;//把缓冲区清零，重新开始
	//	pComm->chRxBuf[pComm->uCommRx++] = rx;
	}
	else if (pComm->nRFlag == 1 && rx != '*' && pComm->uCommRx < MAX_COMM_BUF_SIZE)
	{
		pComm->chRxBuf[pComm->uCommRx++] = rx;
	}
	else if (pComm->nRFlag == 1 && rx == '*' && pComm->uCommRx+3 < MAX_COMM_BUF_SIZE)
	{
		pComm->chRxBuf[pComm->uCommRx++] = rx;
		pComm->chRxBuf[pComm->uCommRx++] = '\r';
		pComm->chRxBuf[pComm->uCommRx++] = '\n';
		pComm->chRxBuf[pComm->uCommRx++] = 0x00;
		pComm->nRFlag = 0;
		
		pComm->bRdy = true;
	}
	
	if (pComm->uCommRx >= MAX_COMM_BUF_SIZE)
	{
		pComm->bRdy = false;
		pComm->nRFlag = 0;
		pComm->uCommRx = 0;//缓冲区满了就丢掉重来
	}
	
	return pComm->bRdy;
}

bool Comm_GetFullFrameMsg(char rx, COMM_OBJECT* pComm)
{	
	if (!pComm) return false;
	
	if (rx == '$')
	{
		pComm->bRdy = false;
		pComm->nRFlag = 1;
		pComm->uCommRx = 0;
		pComm->chRxBuf[pComm->uCommRx++] = rx;
	}
	else if (pComm->nRFlag == 1 && rx != '*' && pComm->uCommRx < MAX_COMM_BUF_SIZE)
	{
		pComm->chRxBuf[pComm->uCommRx++] = rx;
	}
	else if (pComm->nRFlag == 1 && rx == '*' && pComm->uCommRx+3 < MAX_COMM_BUF_SIZE)
	{
		pComm->chRxBuf[pComm->uCommRx++] = rx;
		pComm->chRxBuf[pComm->uCommRx++] = '\r';
		pComm->chRxBuf[pComm->uCommRx++] = '\n';
		pComm->chRxBuf[pComm->uCommRx++] = 0x00;
		pComm->nRFlag = 0;
		
		pComm->bRdy = true;
	}
	
	if (pComm->uCommRx >= MAX_COMM_BUF_SIZE)
	{
		pComm->bRdy = false;
		pComm->nRFlag = 0;
		pComm->uCommRx = 0;
	}
	
	return pComm->bRdy;
}

bool Comm_GetData(char rx, COMM_OBJECT* pComm)
{
	if (!pComm) return false;
	
	pComm->chRx[0] = rx;
	if (pComm->uCommRx < MAX_COMM_BUF_SIZE)
	{
		pComm->chRxBuf[pComm->uCommRx++] = rx;
		return true;
	}
	if (pComm->uCommRx >= MAX_COMM_BUF_SIZE)
	{
		pComm->uCommRx = 0;
	}
	
	return false;
}

void Comm_SendTxBuf(COMM_OBJECT* pComm)
{
	// Send COMM_OBJECT TxBuf data
	// data length is decide by uCommTx
	if (pComm == 0 || pComm->uCommTx <= 0 || pComm->uCommTx > MAX_COMM_BUF_SIZE) return;
	if (pComm->nCommSn == COMM1)
		Comm1_PutBuf(pComm->chTxBuf, pComm->uCommTx);
	else if (pComm->nCommSn == COMM2)
		Comm2_PutBuf(pComm->chTxBuf, pComm->uCommTx);
	else if (pComm->nCommSn == COMM3)
		Comm3_PutBuf(pComm->chTxBuf, pComm->uCommTx);
#if USE_STM32F4XX || USE_STM32H7XX
	else if (pComm->nCommSn == COMM4)
		Comm4_PutBuf(pComm->chTxBuf, pComm->uCommTx);
	else if (pComm->nCommSn == COMM6)
		Comm6_PutBuf(pComm->chTxBuf, pComm->uCommTx);
	else if (pComm->nCommSn == COMM7)
		Comm7_PutBuf(pComm->chTxBuf, pComm->uCommTx);
#endif
}

void Comm_SendMsg(COMM_OBJECT* pComm)
{
	// Send string, data lenth is decide by string length
	int nlen = strlen(pComm->chTxBuf);
	if (nlen > 0 && nlen <= MAX_COMM_BUF_SIZE)
	{
		if (pComm->nCommSn == COMM1)	Comm1_PutStr(pComm->chTxBuf);
		else if (pComm->nCommSn == COMM2)	Comm2_PutStr(pComm->chTxBuf);
		else if (pComm->nCommSn == COMM3)	Comm3_PutStr(pComm->chTxBuf);
	#if USE_STM32F4XX || USE_STM32H7XX
		else if (pComm->nCommSn == COMM4)	Comm4_PutStr(pComm->chTxBuf);
		else if (pComm->nCommSn == COMM6)	Comm6_PutStr(pComm->chTxBuf);
		else if (pComm->nCommSn == COMM7)	Comm7_PutStr(pComm->chTxBuf);
	#endif
	}
	else if (nlen > MAX_COMM_BUF_SIZE)
		printf(" Error: can't send command message more than 256 byte.\r\n");
}

COMM_OBJECT* Comm_GetObject(int nComm)
{
	// Get COMM_OBJECT by comm sn
	if (nComm == COMM1)	return m_pComm1;
	else if (nComm == COMM2)	return m_pComm2;
	else if (nComm == COMM3)	return m_pComm3;
#if USE_STM32F4XX || USE_STM32H7XX
	else if (nComm == COMM4)	return m_pComm4;
	else if (nComm == COMM6)	return m_pComm6;
	else if (nComm == COMM7)	return m_pComm7;
#endif
	else return 0;
}

void Comm1_DMA_TxIntInit(bool bEn)
{
	/* Enable the DMA Stream IRQ Channel */
#if USE_STM32F1XX
	if (bEn)
	{
		USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
		USART_ITConfig(USART1, USART_IT_TC,   DISABLE);	
		USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);	
		USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
	}
	else
	{
		USART_DMACmd(USART1, USART_DMAReq_Tx, DISABLE);
		DMA_ClearITPendingBit(DMA1_IT_TC4);
	}
#elif USE_STM32F4XX
	NVIC_InitTypeDef NVIC_InitStructure;
	if (bEn)
	{
		USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);	
		USART_ITConfig(USART1, USART_IT_TC,   DISABLE);	
		USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);	
		USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
		
		NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream3_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
		NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
		NVIC_Init(&NVIC_InitStructure);  
	}
	else
	{
		USART_DMACmd(USART1, USART_DMAReq_Tx, DISABLE);
		
		NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream3_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
		NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
		NVIC_Init(&NVIC_InitStructure);  
	}
#elif USE_STM32H7XX
	
#endif
}

void Comm1_DMA_RxIntInit(bool bEn)
{
	/* Enable the DMA Stream IRQ Channel */
#if USE_STM32F1XX
	if (bEn)
	{
		USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
		USART_ITConfig(USART1, USART_IT_TC,   DISABLE);	
		USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);	
		USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);
	}
	else
	{
		USART_DMACmd(USART1, USART_DMAReq_Rx, DISABLE);
		DMA_ClearITPendingBit(DMA1_IT_TC5);
	}
#elif USE_STM32F4XX
	NVIC_InitTypeDef NVIC_InitStructure;
	if (bEn)
	{
		USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);	
		USART_ITConfig(USART1, USART_IT_TC,   DISABLE);	
		USART_ITConfig(USART1, USART_IT_IDLE, DISABLE);
		USART_ClearITPendingBit(USART1, USART_IT_LBD);
		USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);
		
		NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream2_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
		NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
		NVIC_Init(&NVIC_InitStructure);  
	}
	else
	{
		NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream2_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
		NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
		NVIC_Init(&NVIC_InitStructure);  
		
		USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);	
		USART_ITConfig(USART1, USART_IT_TC,   DISABLE);	
		USART_ITConfig(USART1, USART_IT_IDLE, DISABLE);	
		USART_DMACmd(USART1, USART_DMAReq_Rx, DISABLE);
		USART_ClearITPendingBit(USART1, USART_IT_LBD);
	}
#endif
}

void Comm1_DMA_TxConfig(uint32_t *BufferSRC, uint32_t BufferSize)
{
#if USE_STM32F1XX
	DMA_InitTypeDef DMA_InitStructure;
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);  

	DMA_DeInit(DMA1_Channel4);  
	DMA_InitStructure.DMA_PeripheralBaseAddr 	= (u32)(&USART1->DR);  
	DMA_InitStructure.DMA_MemoryBaseAddr 		= (uint32_t)BufferSRC;  
	DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralDST;  
	DMA_InitStructure.DMA_BufferSize 			= BufferSize;  
	DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;  
	DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;  
	DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_Byte;  
	DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_Byte;  
	DMA_InitStructure.DMA_Mode 					= DMA_Mode_Normal;  
	DMA_InitStructure.DMA_Priority 				= DMA_Priority_Medium;  
	DMA_InitStructure.DMA_M2M 					= DMA_M2M_Disable;  
	DMA_Init(DMA1_Channel4, &DMA_InitStructure);  
	DMA_ITConfig(DMA1_Channel4, DMA_IT_TC, ENABLE);  
	
	m_bComm1TxDmaEnd = false;	  
	DMA_Cmd(DMA1_Channel4, ENABLE);  
#elif USE_STM32F4XX

#elif USE_STM32H7XX
	
#endif
}

void Comm1_DMA_RxConfig(bool bEn, uint8_t *BufferDST, uint32_t BufferSize)
{
#if USE_STM32F4XX
	DMA_InitTypeDef DMA_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);  
	
	DMA_DeInit(DMA2_Stream2);  	// see datasheet table43
	while (DMA_GetCmdStatus(DMA2_Stream2) != DISABLE){}
		
	DMA_InitStructure.DMA_Channel 				= DMA_Channel_4;  
	DMA_InitStructure.DMA_PeripheralBaseAddr 	= (uint32_t)&(USART1->DR);
	DMA_InitStructure.DMA_Memory0BaseAddr 		= (uint32_t)BufferDST;
	DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;
	DMA_InitStructure.DMA_BufferSize 			= BufferSize;
	DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode 					= DMA_Mode_Normal;
	DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;
	DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable;         
	DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;
	DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
	DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;
	DMA_Init(DMA2_Stream2, &DMA_InitStructure);	
	
	DMA_ClearITPendingBit(DMA2_Stream2, DMA_IT_TCIF2);
	DMA_ClearITPendingBit(DMA2_Stream2, DMA_IT_TEIF2);
	
	if ( bEn )
	{
		DMA_ITConfig(DMA2_Stream2, DMA_IT_TC, ENABLE);
		DMA_SetCurrDataCounter(DMA2_Stream2, BufferSize);
		DMA_Cmd(DMA2_Stream2, ENABLE);
	}
	else
	{
		DMA_ITConfig(DMA2_Stream2, DMA_IT_TC, DISABLE);
		DMA_SetCurrDataCounter(DMA2_Stream2, 0);
		DMA_Cmd(DMA2_Stream2, DISABLE);
	}
	
#elif USE_STM32H7XX
	
#endif	
}

void Comm1_DMA_DBuf_RxConfig(bool bEn, uint8_t *BufferDST1, uint8_t *BufferDST2, uint32_t BufferSize)
{
#if USE_STM32F4XX
	DMA_InitTypeDef DMA_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);  
	
	DMA_DeInit(DMA2_Stream2);  	// see datasheet table43
	while (DMA_GetCmdStatus(DMA2_Stream2) != DISABLE){}
			
	if (bEn)
	{
		DMA_InitStructure.DMA_Channel 				= DMA_Channel_4;  								// enable channel 4
		DMA_InitStructure.DMA_PeripheralBaseAddr 	= (uint32_t)&(USART1->DR);
		DMA_InitStructure.DMA_Memory0BaseAddr 		= (uint32_t)BufferDST1;
		DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;
		DMA_InitStructure.DMA_BufferSize 			= BufferSize;
		DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;
		DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;
		DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_Byte;
		DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_Byte;
		DMA_InitStructure.DMA_Mode 					= DMA_Mode_Circular;
		DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;
		DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable;         
		DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;
		DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
		DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;
		DMA_DoubleBufferModeConfig(DMA2_Stream2, (uint32_t)BufferDST2, DMA_Memory_0);
		DMA_DoubleBufferModeCmd(DMA2_Stream2, ENABLE);
		DMA_Init(DMA2_Stream2, &DMA_InitStructure);

		DMA_ClearITPendingBit(DMA2_Stream2, DMA_IT_TCIF2);
		DMA_ClearITPendingBit(DMA2_Stream2, DMA_IT_TEIF2);
		DMA_ITConfig(DMA2_Stream2, DMA_IT_TC, ENABLE);
		
		// set data trans counter
		DMA_SetCurrDataCounter(DMA2_Stream2, BufferSize);
		DMA_Cmd(DMA2_Stream2, ENABLE); 
	}
	else
	{
		DMA_ClearITPendingBit(DMA2_Stream2, DMA_IT_TCIF2);
		DMA_ClearITPendingBit(DMA2_Stream2, DMA_IT_TEIF2);
		DMA_ITConfig(DMA2_Stream2, DMA_IT_TC, DISABLE);
		
		// set data trans counter
		DMA_SetCurrDataCounter(DMA2_Stream2, BufferSize);
		DMA_Cmd(DMA2_Stream2, DISABLE); 
	}
#elif USE_STM32H7XX
	__HAL_RCC_DMA2_CLK_ENABLE();
	if ( bEn )
	{
		m_hdma_usart1_rx.Instance 		= DMA2_Stream2;
		m_hdma_usart1_rx.Init.Request 	= DMA_REQUEST_USART1_RX;
		m_hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
		m_hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
		m_hdma_usart1_rx.Init.MemInc 	= DMA_MINC_ENABLE;
		m_hdma_usart1_rx.Init.PeriphDataAlignment 	= DMA_PDATAALIGN_BYTE;
		m_hdma_usart1_rx.Init.MemDataAlignment 		= DMA_MDATAALIGN_BYTE;
		m_hdma_usart1_rx.Init.Mode 		= DMA_CIRCULAR;
		m_hdma_usart1_rx.Init.Priority 	= DMA_PRIORITY_HIGH;
		m_hdma_usart1_rx.Init.FIFOMode 	= DMA_FIFOMODE_DISABLE;
		if (HAL_DMA_Init(&m_hdma_usart1_rx) != HAL_OK)
		{
			Error_Handler();
		}

		__HAL_LINKDMA(m_pComm1->pUart, hdmarx, m_hdma_usart1_rx);
		
		HAL_UART_Receive_DMA_double(&m_huart1, BufferDST1, BufferDST2, BufferSize);
		HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 3);
		HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
	}
	else
	{
		HAL_UART_DMAStop( &m_huart1 );
		HAL_NVIC_DisableIRQ(DMA2_Stream2_IRQn);
	}
#endif	
}

void Comm2_DMA_TxConfig(uint32_t *BufferSRC, uint32_t BufferSize)
{
#if USE_STM32F1XX
	DMA_InitTypeDef DMA_InitStructure;
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE); 
	 
	DMA_DeInit(DMA1_Channel7);  
	
	DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)(&USART2->DR);   
	DMA_InitStructure.DMA_MemoryBaseAddr 	 = (uint32_t)BufferSRC;  
	DMA_InitStructure.DMA_DIR 				 = DMA_DIR_PeripheralDST;  
	DMA_InitStructure.DMA_BufferSize 		 = BufferSize;  
	DMA_InitStructure.DMA_PeripheralInc 	 = DMA_PeripheralInc_Disable;  
	DMA_InitStructure.DMA_MemoryInc 		 = DMA_MemoryInc_Enable;  
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  
	DMA_InitStructure.DMA_MemoryDataSize 	 = DMA_PeripheralDataSize_Byte;  
	DMA_InitStructure.DMA_Mode 				 = DMA_Mode_Normal;  
	DMA_InitStructure.DMA_Priority 			 = DMA_Priority_Medium;  
	DMA_InitStructure.DMA_M2M 				 = DMA_M2M_Disable;  
	DMA_Init(DMA1_Channel7, &DMA_InitStructure);  
	DMA_ITConfig(DMA1_Channel7, DMA_IT_TC, ENABLE);  
	
	m_bComm2TxDmaEnd = false;
	DMA_Cmd(DMA1_Channel7, ENABLE);  
#elif USE_STM32F4XX
	
#endif
}

void Comm3_DMA_TxIntInit(bool bEn)
{
	/* Enable the DMA Stream IRQ Channel */
#if USE_STM32F1XX
	if (bEn)
	{
		USART_ITConfig(USART3, USART_IT_RXNE, DISABLE);
		USART_ITConfig(USART3, USART_IT_TC,   DISABLE);	
		USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);	
		USART_DMACmd(USART3, USART_DMAReq_Tx, ENABLE);
	}
	else
	{
		USART_DMACmd(USART3, USART_DMAReq_Tx, DISABLE);
		DMA_ClearITPendingBit(DMA1_IT_TC2);
	}
#elif USE_STM32F4XX

#endif
}

void Comm3_DMA_RxIntInit(bool bEn)
{
	/* Enable the DMA Stream IRQ Channel */
#if USE_STM32F1XX
	if (bEn)
	{
		USART_ITConfig(USART3, USART_IT_RXNE, DISABLE);
		USART_ITConfig(USART3, USART_IT_TC,   DISABLE);	
		USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);	
		USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);
	}
	else
	{
		USART_DMACmd(USART3, USART_DMAReq_Rx, DISABLE);
		DMA_ClearITPendingBit(DMA1_IT_TC3);
	}
#elif USE_STM32F4XX

#endif
}

void Comm3_DMA_TxConfig(uint32_t *BufferSRC, uint32_t BufferSize)
{
#if USE_STM32F1XX
	DMA_InitTypeDef DMA_InitStructure;
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);  
	
	DMA_DeInit(DMA1_Channel2);  

	DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)(&USART3->DR);  
	DMA_InitStructure.DMA_MemoryBaseAddr 	 = (uint32_t)BufferSRC;  
	DMA_InitStructure.DMA_DIR 				 = DMA_DIR_PeripheralDST;  
	DMA_InitStructure.DMA_BufferSize 		 = BufferSize;  
	DMA_InitStructure.DMA_PeripheralInc 	 = DMA_PeripheralInc_Disable;  
	DMA_InitStructure.DMA_MemoryInc 		 = DMA_MemoryInc_Enable;  
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  
	DMA_InitStructure.DMA_MemoryDataSize 	 = DMA_MemoryDataSize_Byte;  
	DMA_InitStructure.DMA_Mode 				 = DMA_Mode_Normal;  
	DMA_InitStructure.DMA_Priority 			 = DMA_Priority_Medium;  
	DMA_InitStructure.DMA_M2M 				 = DMA_M2M_Disable;  
	DMA_Init(DMA1_Channel2, &DMA_InitStructure);  
	DMA_ITConfig(DMA1_Channel2, DMA_IT_TC, ENABLE);  
	
	m_bComm3TxDmaEnd = false;
	DMA_Cmd(DMA1_Channel2, ENABLE); 
#elif USE_STM32F4XX
	
#endif	
}

void Comm3_DMA_RxConfig(uint8_t *BufferDST, uint32_t BufferSize)
{
#if USE_STM32F1XX
	DMA_InitTypeDef DMA_InitStructure;
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE); 
	 
	DMA_DeInit(DMA1_Channel3);  
	
	DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)(&USART3->DR);   
	DMA_InitStructure.DMA_MemoryBaseAddr 	 = (uint32_t)BufferDST;  
	DMA_InitStructure.DMA_DIR 				 = DMA_DIR_PeripheralSRC;  
	DMA_InitStructure.DMA_BufferSize 		 = BufferSize;  
	DMA_InitStructure.DMA_PeripheralInc 	 = DMA_PeripheralInc_Disable;  
	DMA_InitStructure.DMA_MemoryInc 		 = DMA_MemoryInc_Enable;  
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  
	DMA_InitStructure.DMA_MemoryDataSize 	 = DMA_MemoryDataSize_Byte;  
	DMA_InitStructure.DMA_Mode 				 = DMA_Mode_Normal;  
	DMA_InitStructure.DMA_Priority 			 = DMA_Priority_High;  
	DMA_InitStructure.DMA_M2M 				 = DMA_M2M_Disable;  
	DMA_Init(DMA1_Channel3, &DMA_InitStructure); 
	
	DMA_ClearITPendingBit(DMA1_IT_TC3);
	DMA_ClearITPendingBit(DMA1_IT_TE3);
	DMA_ITConfig(DMA1_Channel3, DMA_IT_TC, ENABLE);  	
	
	m_bComm3RxDmaEnd = false;
	DMA_Cmd(DMA1_Channel3, ENABLE); 
#elif USE_STM32F4XX
	
#endif	
}

void Comm3_DMA_DBuf_RxConfig(uint8_t *BufferDST1, uint8_t *BufferDST2, uint32_t BufferSize)
{
#if USE_STM32F4XX
	
#endif
}

#if USE_STM32F4XX || USE_STM32H7XX
void Comm6_DMA_RxIntInit(bool bEn)
{
	/* Enable the DMA Stream IRQ Channel */
#if USE_STM32F4XX
	NVIC_InitTypeDef NVIC_InitStructure;
	if (bEn)
	{
		USART_ITConfig(USART6, USART_IT_RXNE, DISABLE);	
		USART_ITConfig(USART6, USART_IT_TC,   DISABLE);	
		USART_ITConfig(USART6, USART_IT_IDLE, ENABLE);	
		USART_ClearITPendingBit(USART6, USART_IT_LBD);
		USART_DMACmd(USART6, USART_DMAReq_Rx, ENABLE);
		
		NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream1_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
		NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
		NVIC_Init(&NVIC_InitStructure);  
	}
	else
	{
		NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream1_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
		NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
		NVIC_Init(&NVIC_InitStructure);  
		
		USART_ITConfig(USART6, USART_IT_RXNE, DISABLE);	
		USART_ITConfig(USART6, USART_IT_TC,   DISABLE);	
		USART_ITConfig(USART6, USART_IT_IDLE, DISABLE);	
		USART_DMACmd(USART6, USART_DMAReq_Rx, DISABLE);
		USART_ClearITPendingBit(USART6, USART_IT_LBD);
	}
#elif USE_STM32H7XX
	
#endif
}

void Comm6_DMA_RxConfig(uint8_t *BufferDST, uint32_t BufferSize)
{
#if USE_STM32F4XX
	DMA_InitTypeDef DMA_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);  
	
	DMA_DeInit(DMA2_Stream1);  	// see datasheet table43
	while (DMA_GetCmdStatus(DMA2_Stream1) != DISABLE){}
	
	DMA_InitStructure.DMA_Channel 				= DMA_Channel_5;  
	DMA_InitStructure.DMA_PeripheralBaseAddr 	= (uint32_t)&(USART6->DR);
	DMA_InitStructure.DMA_Memory0BaseAddr 		= (uint32_t)BufferDST;
	DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;
	DMA_InitStructure.DMA_BufferSize 			= BufferSize;
	DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode 					= DMA_Mode_Normal;
	DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;
	DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable;         
	DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;
	DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
	DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;
	DMA_Init(DMA2_Stream1, &DMA_InitStructure);

	// enable channel 5
	DMA_ClearITPendingBit(DMA2_Stream1, DMA_IT_TCIF1);
	DMA_ClearITPendingBit(DMA2_Stream1, DMA_IT_TEIF1);
	DMA_ITConfig(DMA2_Stream1, DMA_IT_TC, ENABLE);
	
	// set data trans counter
//	DMA_SetCurrDataCounter(DMA2_Stream1, BufferSize);

	DMA_Cmd(DMA2_Stream1, ENABLE); 
#elif USE_STM32H7XX

#endif
}

void Comm6_DMA_DBuf_RxConfig(bool bEn, uint8_t *BufferDST1, uint8_t *BufferDST2, uint32_t BufferSize)
{
#if USE_STM32F4XX
	DMA_InitTypeDef DMA_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);  
	
	DMA_DeInit(DMA2_Stream1);  	// see datasheet table43
	while (DMA_GetCmdStatus(DMA2_Stream1) != DISABLE){}
	
	if (bEn)
	{
		DMA_InitStructure.DMA_Channel 				= DMA_Channel_5;  				// enable channel 5
		DMA_InitStructure.DMA_PeripheralBaseAddr 	= (uint32_t)&(USART6->DR);
		DMA_InitStructure.DMA_Memory0BaseAddr 		= (uint32_t)BufferDST1;
		DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;
		DMA_InitStructure.DMA_BufferSize 			= BufferSize;
		DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;
		DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;
		DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_Byte;
		DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_Byte;
		DMA_InitStructure.DMA_Mode 					= DMA_Mode_Circular;
		DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;
		DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable;         
		DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;
		DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
		DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;
		DMA_DoubleBufferModeConfig(DMA2_Stream1, (uint32_t)BufferDST2, DMA_Memory_0);
		DMA_DoubleBufferModeCmd(DMA2_Stream1, ENABLE);
		DMA_Init(DMA2_Stream1, &DMA_InitStructure);

		DMA_ClearITPendingBit(DMA2_Stream1, DMA_IT_TCIF1);
		DMA_ClearITPendingBit(DMA2_Stream1, DMA_IT_TEIF1);
		DMA_ITConfig(DMA2_Stream1, DMA_IT_TC, ENABLE);
		
		DMA_SetCurrDataCounter(DMA2_Stream1, BufferSize);		// set data trans counter
		DMA_Cmd(DMA2_Stream1, ENABLE);  
	}
	else
	{
		DMA_ClearITPendingBit(DMA2_Stream1, DMA_IT_TCIF1);
		DMA_ClearITPendingBit(DMA2_Stream1, DMA_IT_TEIF1);
		DMA_ITConfig(DMA2_Stream1, DMA_IT_TC, DISABLE);
		
		DMA_SetCurrDataCounter(DMA2_Stream1, BufferSize);		// set data trans counter
		DMA_Cmd(DMA2_Stream1, DISABLE);  
	}
#elif USE_STM32H7XX
	if ( bEn )
		HAL_UART_Receive_DMA_double(&m_huart6, BufferDST1, BufferDST2, BufferSize);
	else
		HAL_UART_DMAStop( &m_huart6 );
#endif
}

#endif	// USE_STM32F4XX || USE_STM32H7XX

/***********************************************/
USART_TypeDef* UART_CommSnToUSARTn(int nComm)
{
	// 通过串口的枚举值返回 串口结构体对象
	switch ( nComm )
	{
		case COMM1 : return USART1;
		case COMM2 : return USART2;
		case COMM3 : return USART3;
	#if USE_STM32F4XX || USE_STM32H7XX
		case COMM4 : return UART4;
		case COMM6 : return USART6;
		case COMM7 : return UART7;
	#endif
		default : return 0;
	}
}

int UART_USARTnToCommSn( USART_TypeDef* USARTn)
{
	// 通过串口的串口结构体对象返回 串口序号
	if (USARTn == USART1)	return COMM1;
	else if (USARTn == USART2)	return COMM2;
	else if (USARTn == USART3)	return COMM3;
#if USE_STM32F4XX || USE_STM32H7XX
	else if (USARTn == UART4)	return COMM4;
	else if (USARTn == USART6)	return COMM6;
	else if (USARTn == UART7)	return COMM7;
#endif
	else return 0;
}

int UART_GetRealBaud(u8 nEnumBaud)
{
	// 根据串口的枚举值返回波特率（实际数值）
	switch (nEnumBaud)
	{
		case tBaud150		: return 150;
		case tBaud300		: return 300;
		case tBaud600		: return 600;
		case tBaud1200		: return 1200;
		case tBaud1800		: return 1800;
		case tBaud2400		: return 2400;
		case tBaud4800		: return 4800;
		case tBaud7200 		: return 7200;
		case tBaud9600 		: return 9600;
		case tBaud19200 	: return 19200;
		case tBaud38400 	: return 38400;
		case tBaud57600		: return 57600;
		case tBaud115200	: return 115200;
		case tBaud230400 	: return 230400;
		case tBaud460800	: return 460800;
		case tBaud921600	: return 921600;
		default : return 9600;
	}
}

u8  UART_GetEnumBaud(u32 nRealBaud)
{
	// 根据串口的波特率（实际数值）返回枚举值
	switch (nRealBaud)
	{
		case 150 	: return tBaud150;
		case 300 	: return tBaud300;
		case 600 	: return tBaud600;
		case 1200 	: return tBaud1200;
		case 1800 	: return tBaud1800;
		case 2400 	: return tBaud2400;
		case 4800 	: return tBaud4800;
		case 7200 	: return tBaud7200;
		case 9600 	: return tBaud9600;
		case 19200 	: return tBaud19200;
		case 38400 	: return tBaud38400;
		case 57600	: return tBaud57600;
		case 115200	: return tBaud115200;
		case 230400	: return tBaud230400;
		case 460800	: return tBaud460800;
		case 921600	: return tBaud921600;
		default : return tBaud9600;
	}
}

/***********************************************/


