#ifndef _TIMER_H
#define _TIMER_H

#include "main.h"

extern uint32_t	m_uPCLK1;	// PCLK1时钟频率
extern uint32_t m_uPCLK2;	// PCLK2时钟频率

typedef enum DELAY_MODE
{
	DELAY_US,
	DELAY_MS,
}Delay_Type;

extern bool m_bTim3Int;

#if USE_STM32H7XX
extern TIM_HandleTypeDef m_htim2;
extern TIM_HandleTypeDef m_htim3;
extern TIM_HandleTypeDef m_htim4;
#endif

void TIMER2_Config(u16 HzPerSecond, bool bEnable);
void TIMER3_Config(u16 HzPerSecond, bool bEnable);
void TIMER3_Delay(Delay_Type nMode, const u16 nDelay);
void TIMER4_Config(u16 HzPerSecond, bool bEnable);

void SysTick_Init(uint32_t HzPerSecond);
void SysTick_DeInit(void);

// 由于延时函数和系统时钟配置有直接联系，因此在改变系统时钟配置时
// 通过参数传递改变延时函数的参数，保证延时时间的准确性
void delay_ms(u16 nms);				// 利用定时器3进行延时控制
void delay_us(u16 nus);				// 利用定时器3进行延时控制

void delay(u16 nms);                // 为了避免中断嵌套，使用CPU空转进行延时
void delayus(u16 nus);              // 为了避免中断嵌套，使用CPU空转进行延时

void OnTimer2Int(void);				// 响应TIMER2定时器，特定用于数据读取
void OnTimer4Int(void);				// 响应TIMER4定时器，特定用于电池状态显示

#endif



