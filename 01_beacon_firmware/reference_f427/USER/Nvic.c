#include "main.h"
#include "nvic.h"
#include "IO_operate.h"
#include "PCLink_operate.h"

#if USE_STM32F1XX || USE_STM32F4XX
// warning: NVIC_SubPriority and NVIC_PreemptionPriority must be set in NVIC regin, if not will occur unexpected error
// group set:
// group 0 : 0 PreemptionPriority, 4 SubPriority
// group 1 : 1 PreemptionPriority, 3 SubPriority
// group 2 : 2 PreemptionPriority, 2 SubPriority
// group 3 : 3 PreemptionPriority, 1 SubPriority
// group 4 : 4 PreemptionPriority, 0 SubPriority
// NVIC_SubPriority and NVIC_PreemptionPriority : The principle is, the smaller the value, the higher the priority. 
#endif

void NVIC_Config(void)
{
/*---------------------------------------------*/
#if USE_STM32F4XX
	EXTI_InitTypeDef	EXTI_InitStructure;
	NVIC_InitTypeDef 	NVIC_InitStructure;
	/* Configure the NVIC Preemption Priority Bits */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);	//2 bits for pre-emption priority
                                                    //2 bits for subpriority 
	//抢占优先级：0、1、2、3（越小越优先）  子优先级：0、1、2、3（越小越优先）
	// FPGA interrupt is set in FPGA_IntInit()
	// the level can't be set equal to SD card interrupt, Otherwise, it will cause interruptions that cannot be nesting. 
	// FPGA interrupt is set 0-2 level;
	// SPI or UART interrupt is set 0-3 level
	// Storage interrupt is set 1-0 level
	
	// synchronize self-contained hydrohpone
	// atomic PPS interrupt is set 2-0 level
	
	// TIM2 interrupt is used for file reading or file deleting, can't be used for other process
	// TIM2 interrupt is set 1-2 level  文件读写用
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	
	// TIM4 interrupt is used for charging display, can't be used for other process
	// TIM4 interrupt is set 1-3 level   充电显示用
	NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	
	// USART1 interrupt set   串口1通信
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	// USART2 interrupt set   串口2通信
	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	//USART3中断设置
	//	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	//	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	//	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
	//	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	//	NVIC_Init(&NVIC_InitStructure);

	//USART6中断设置
	//	NVIC_InitStructure.NVIC_IRQChannel = USART6_IRQn;
	//	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	//	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	//	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	//	NVIC_Init(&NVIC_InitStructure);
	
	// Systick interrupt is used for net operation
	//  NVIC_SetPriority(SysTick_IRQn, n);  
	// n=0x00~0x03	set systick interrupt level 0
	// n=0x04~0x07	set systick interrupt level 1
	// n=0x08~0x0B	set systick interrupt level 2
	// n=0x0C~0x0F	set systick interrupt level 3
	/* Update the SysTick IRQ priority should be higher than the Ethernet IRQ */
	/* The Localtime should be updated during the Ethernet packets processing */
#endif
	
#if USE_STM32F4XX
	// RTC second counter interrupt  RTC唤醒中断
	EXTI_InitStructure.EXTI_Line = EXTI_Line22;					// LINE22
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;			//
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;		//
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;					//
	EXTI_Init(&EXTI_InitStructure);								//

	NVIC_InitStructure.NVIC_IRQChannel = RTC_WKUP_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);	
	
	RTC_ClearFlag(RTC_FLAG_WUTF);			// 清除RTC WAKE UP的中断标志
#endif
}//RTC 本身不是一个“中断源”，它产生的是一个事件信号，这个信号需要先经过 EXTI（外部中断/事件控制器） 的某条线（Line 22），才能送到 NVIC。
//不只是 RTC，所有"外部事件"类型的中断都需要 EXTI：包括外部 GPIO
/*EXTI 的存在是为了：

让 GPIO 引脚能产生中断（每条 GPIO Pin 对应一条 EXTI Line）

让 RTC、PVD、USB 等特殊模块也能触发中断

提供事件唤醒功能（低功耗模式下）*/