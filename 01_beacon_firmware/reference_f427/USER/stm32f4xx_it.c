/**
  ******************************************************************************
  * @file    Project/STM32F4xx_StdPeriph_Templates/stm32f4xx_it.c 
  * @author  MCD Application Team
  * @version V1.4.0
  * @date    04-August-2014
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2014 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"

#include "PCLINK_operate.h"

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)//不可屏蔽中断，硬件级别严重错误
{
	while (1)
	{
	}
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)//程序跑飞、访问非法内存等
{
  /* Go to infinite loop when Hard Fault exception occurs */
	while (1)
	{
		NVIC_SystemReset();
	}
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)//内存访问违规
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)//总线错误
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)//指令错误、除零等
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
	// 系统滴答定时器中断响应函数
	// 由于网络配置函数启动了系统滴答定时器，此处必须添加中断函数
#if USE_NET
	Net_OnEchoSystick();
#endif
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/

/**
  * @}
  */ 

//DMA 中断
/*一个中断向量（比如DMA2_Stream0_IRQHandler）可能对应多个中断事件
对于DMA Stream，可能触发中断的事件有：

传输完成 (TCIF: Transfer Complete Flag)

半传输 (HTIF: Half Transfer Flag)

传输错误 (TEIF: Transfer Error Flag)

直接模式错误 (DMEIF: Direct Mode Error Flag)

FIFO溢出 (FEIF: FIFO Error Flag)*/
//DMA2_Stream0 精确指定了“DMA2控制器上的第0号数据流”  DMA_IT_TCIF0 —— 具体的中断标志位宏，所以，这个if判断是为了精确识别中断原因，避免错误处理。
void DMA2_Stream0_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA2_Stream0, DMA_IT_TCIF0))//TCIF 是"传输完成中断标志"
	{
		DMA_ClearFlag(DMA2_Stream0, DMA_IT_TCIF0);	// 清除中断标志
		
	}
}

void DMA2_Stream1_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA2_Stream1, DMA_IT_TCIF1))
	{
		// 清除中断标志
		DMA_ClearFlag(DMA2_Stream1, DMA_IT_TCIF1);
	}
}

void DMA2_Stream2_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA2_Stream2, DMA_IT_TCIF2))
	{	
		DMA_ClearFlag(DMA2_Stream2, DMA_IT_TCIF2);	// 清除中断标志
		
#if DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceIListenN || \
	DEVICE_TYPE == tDeviceIBeacon//条件编译，只有这四种设备才执行DMA
		Device_OnEchoComm1DMA();//调用DMA处理函数
#endif
	}
}

void DMA2_Stream4_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA2_Stream4, DMA_IT_TCIF4))
	{
		DMA_ClearFlag(DMA2_Stream4, DMA_IT_TCIF4);	// 清除中断标志
		
#if DEVICE_TYPE == tDeviceIBeacon
		Device_OnEchoAdDMA();//处理ADC采集数据
#endif
	}
	else
	{
		DMA_Cmd(DMA2_Stream4, DISABLE);				// 禁止DMA
	}
}
//GPIO外部中断
void EXTI0_IRQHandler(void)
{
	// FPGA 数据传输中断响应函数
	if(EXTI_GetITStatus(EXTI_Line0) != RESET)//FPGA拉高了这根线，EXTI_Line0 检测到的有效边沿，就是 FPGA 通过 GPIO 引脚发过来的脉冲，用于告诉 MCU "我还活着且有事件要处理
	{
		m_hRtcCnt.uAcqIdle = 0;						// 采集过程中每次FPGA中断清零一次
		EXTI_ClearITPendingBit(EXTI_Line0);			// 清除中断
		
#if DEVICE_TYPE == tDeviceIBeacon
		Device_OnEchoInt();//处理FPGA中断
#endif			
		
	}
}

void EXTI1_IRQHandler(void)
{
	if(EXTI_GetITStatus(EXTI_Line1) != RESET)
	{
        EXTI_ClearITPendingBit(EXTI_Line1);
	}
}

void EXTI2_IRQHandler(void)	 
{
	if(EXTI_GetITStatus(EXTI_Line2) != RESET)
	{
        EXTI_ClearITPendingBit(EXTI_Line2);
	}
}

void EXTI3_IRQHandler( void )
{
	if (EXTI_GetITStatus(EXTI_Line3) == SET)
	{
		EXTI_ClearITPendingBit(EXTI_Line3);
	}
}

void EXTI4_IRQHandler(void)	
{
	// 数据采集存储处理软中断
	if (EXTI_GetITStatus(EXTI_Line4) == SET)
	{
		EXTI_ClearITPendingBit(EXTI_Line4);
		

	}
}

void EXTI9_5_IRQHandler(void)	
{
	if (EXTI_GetITStatus(EXTI_Line5) == SET)
	{
		EXTI_ClearITPendingBit(EXTI_Line5);
		
	}
}

#if USE_FATFS
void SDIO_IRQHandler(void)
{
	/* Process All SDIO Interrupt Sources */
	SD_ProcessIRQSrc();
}
#endif
//串口中断
void USART1_IRQHandler(void)            
{                                      
	// 在中断服务程序中，由于主机响应中断时并不知道是哪个中断源发出中断请求，
	// 因此必须在中断服务程序中对中断源进行判别，然后分别进行处理。
	// 当然，如果只涉及到一个中断请求，是不用做上述判别的。
	// 但是无论什么情况，做上述判别是个好习惯
	// 在USART_DR寄存器中写入了最后一个数据字后，
    // 在关闭USART模块之前或设置微控制器进入低功耗模式之前，必须先等待TC=1。
	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)    
	{ 
		m_pComm1->chRx[0] = USART_ReceiveData(USART1);		
		
#if DEVICE_TYPE == tDeviceIBeacon
		Device_OnEchoComm1(m_pComm1);  // 信标设备：走设备层专用处理
#else
		if (Comm_GetFrameMsg(m_pComm1->chRx[0], m_pComm1))
			OnEchoComm1(); 	// 数据完整接收 // 其他设备：帧完整了再回调
#endif
	}
	else if (USART_GetITStatus(USART1, USART_IT_TC) != RESET)
	{
	//	USART_ClearITPendingBit(USART1, USART_IT_TC);
	}
	else if (USART_GetITStatus(USART1, USART_IT_TXE) != RESET)
	{
	//	USART_ClearITPendingBit(USART1, USART_IT_TXE);
	}
	
	USART_ReceiveData(USART1);
	USART_ClearFlag(USART1, USART_FLAG_LBD);									// 清除LBD标志（LIN 断路检测）
	USART_ClearFlag(USART1, USART_FLAG_ORE | USART_FLAG_FE | USART_FLAG_NE);	// 清除接收错误标志
} 

void USART2_IRQHandler(void)            
{                                      
	// 在中断服务程序中，由于主机响应中断时并不知道是哪个中断源发出中断请求，
	// 因此必须在中断服务程序中对中断源进行判别，然后分别进行处理。
	// 当然，如果只涉及到一个中断请求，是不用做上述判别的。
	// 但是无论什么情况，做上述判别是个好习惯
	// 在USART_DR寄存器中写入了最后一个数据字后，
    // 在关闭USART模块之前或设置微控制器进入低功耗模式之前，必须先等待TC=1。
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)   //RXNE收到新数据了 
	{   
		m_pComm2->chRx[0] = USART_ReceiveData(USART2);//读一个字节
        
		if ( Comm_GetFrameMsg(m_pComm2->chRx[0], m_pComm2) )
			OnEchoComm2();
	}
	else if (USART_GetITStatus(USART2, USART_IT_TC) != RESET)//TC是数据发送完成了
	{
	//	USART_ClearITPendingBit(USART2, USART_IT_TC);
	}
	else if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET)//TXE是发送缓冲区空了，可以发送下一个数据了
	{
	//	USART_ClearITPendingBit(USART2, USART_IT_TXE);
	}
	
	USART_ReceiveData(USART2);
	USART_ClearFlag(USART2, USART_FLAG_LBD);									// 清除LBD标志
	USART_ClearFlag(USART2, USART_FLAG_ORE | USART_FLAG_FE | USART_FLAG_NE);	// 清除接收错误标志	
} 

void USART3_IRQHandler(void)            
{                                      
	// 在中断服务程序中，由于主机响应中断时并不知道是哪个中断源发出中断请求，
	// 因此必须在中断服务程序中对中断源进行判别，然后分别进行处理。
	// 当然，如果只涉及到一个中断请求，是不用做上述判别的。
	// 但是无论什么情况，做上述判别是个好习惯
	// 在USART_DR寄存器中写入了最后一个数据字后，
    // 在关闭USART模块之前或设置微控制器进入低功耗模式之前，必须先等待TC=1。
	if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)    
	{   
		m_pComm3->chRx[0] = USART_ReceiveData(USART3);
				
#if DEVICE_TYPE == tDeviceIListenS   || DEVICE_TYPE == tDeviceIListenM  || \
	DEVICE_TYPE == tDeviceILogger	 || DEVICE_TYPE == tDeviceIPowerAmp || DEVICE_TYPE == tDeviceIBeacon 	
		Device_OnEchoComm3(m_pComm3);
#else
		if (Comm_GetFrameMsg(m_pComm3->chRx[0], m_pComm3))
			OnEchoComm3();
#endif
	}
	else if (USART_GetITStatus(USART3, USART_IT_TC) != RESET)
	{
	//	USART_ClearITPendingBit(USART3, USART_IT_TC);
	}
	else if (USART_GetITStatus(USART3, USART_IT_TXE) != RESET)
	{
	//	USART_ClearITPendingBit(USART3, USART_IT_TXE);
	}
	
	USART_ReceiveData(USART3);
	USART_ClearFlag(USART3, USART_FLAG_LBD);										// 清除LBD标志
	USART_ClearFlag(USART3, USART_FLAG_ORE | USART_FLAG_FE | USART_FLAG_NE);		// 清除接收错误标志
} 

void USART6_IRQHandler(void)            
{                                      
	// 在中断服务程序中，由于主机响应中断时并不知道是哪个中断源发出中断请求，
	// 因此必须在中断服务程序中对中断源进行判别，然后分别进行处理。
	// 当然，如果只涉及到一个中断请求，是不用做上述判别的。
	// 但是无论什么情况，做上述判别是个好习惯
	// 在USART_DR寄存器中写入了最后一个数据字后，
	// 在关闭USART模块之前或设置微控制器进入低功耗模式之前，必须先等待TC=1。
	if(USART_GetITStatus(USART6, USART_IT_RXNE) != RESET)    
	{   
		m_pComm6->chRx[0] = USART_ReceiveData(USART6);

#if DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM	|| \
	DEVICE_TYPE == tDeviceIPowerAmp
		Device_OnEchoComm6(m_pComm6);
#else
        if (Comm_GetFrameMsg(m_pComm6->chRx[0], m_pComm6))
			OnEchoComm6(); 						// 数据完整接收 
#endif		
	}
	else if (USART_GetITStatus(USART6, USART_IT_TC) != RESET)
	{
	//	USART_ClearITPendingBit(USART6, USART_IT_TC);
	}
	else if (USART_GetITStatus(USART6, USART_IT_TXE) != RESET)
	{
	//	USART_ClearITPendingBit(USART6, USART_IT_TXE);
	}
	
	USART_ReceiveData(USART6);
	USART_ClearFlag(USART6, USART_FLAG_LBD);										// 清除LBD标志
	USART_ClearFlag(USART6, USART_FLAG_ORE | USART_FLAG_FE | USART_FLAG_NE);		// 清除接收错误标志
} 

void SPI4_IRQHandler(void)
{
	if (SPI_GetFlagStatus(SPI4, SPI_FLAG_OVR) != RESET)
	{
		SPI_ClearFlag(SPI4, SPI_FLAG_OVR);
	}
}

void ETH_IRQHandler(void)
{
	// 网口接收中断响应函数
#if USE_NET
	while(ETH_CheckFrameReceived())
	{ 
		lwip_pkt_handle();		
	}
	ETH_DMAClearITPendingBit(ETH_DMA_IT_R); 	//清除DMA中断标志位
	ETH_DMAClearITPendingBit(ETH_DMA_IT_NIS);	//清除DMA接收中断标志位
#endif
}

void ETH_WKUP_IRQHandler(void)
{

}

void TIM2_IRQHandler(void)
{
	// 定时器2中断响应函数
	OnTimer2Int();
	TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
}

void TIM3_IRQHandler( void )
{
	// 定时器3中断响应函数
	// OnTimer3Int();
	TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
}

void TIM4_IRQHandler( void )
{
	// 定时器4中断响应函数
	OnTimer4Int();
	TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
}

void ADC_IRQHandler( void )
{
	
}

// RTC闹钟中断服务函数
void RTC_Alarm_IRQHandler(void)
{
	// ALARM A中断
	if (RTC_GetFlagStatus(RTC_FLAG_ALRAF) == SET)	
	{
		RTC_ClearFlag(RTC_FLAG_ALRAF);		//清除中断标志
	}
	EXTI_ClearITPendingBit(EXTI_Line17);	//清除中断线17的中断标志 											 
}

// RTC WAKE UP中断服务函数
void RTC_WKUP_IRQHandler(void)
{
	// WK_UP中断 秒中断
	if (RTC_GetFlagStatus(RTC_FLAG_WUTF) == SET)	
	{
		OnEchoRTC();
		RTC_ClearFlag(RTC_FLAG_WUTF);		//清除中断标志
	}
	EXTI_ClearITPendingBit(EXTI_Line22);	//清除中断线22的中断标志 								
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
