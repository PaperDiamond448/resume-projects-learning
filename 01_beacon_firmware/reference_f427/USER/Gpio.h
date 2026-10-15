
#ifndef __GPIO_H
#define __GPIO_H

#include "main.h"

#if USE_STM32F1XX
extern void GPIO_SetPin(GPIO_TypeDef * G_Base, uint16_t G_Pin, GPIOMode_TypeDef G_Mode, GPIOSpeed_TypeDef G_Speed);
#elif USE_STM32F4XX
extern void GPIO_SetOut(GPIO_TypeDef * G_Base, uint16_t G_Pin, GPIOOType_TypeDef G_OType, GPIOSpeed_TypeDef G_Speed, GPIOPuPd_TypeDef G_PPType);
extern void GPIO_SetIn( GPIO_TypeDef * G_Base, uint16_t G_Pin, GPIOOType_TypeDef G_OType, GPIOSpeed_TypeDef G_Speed, GPIOPuPd_TypeDef G_PPType);
extern void GPIO_SetFunc(GPIO_TypeDef * G_Base, uint16_t G_Pin, GPIOOType_TypeDef G_OType, GPIOSpeed_TypeDef G_Speed, GPIOPuPd_TypeDef G_PPType);
#elif USE_STM32H7XX
extern void GPIO_SetOut(GPIO_TypeDef* G_Base, uint16_t G_Pin, uint32_t G_Mode, uint32_t G_Speed,  uint32_t G_PPType);
extern void GPIO_SetIn( GPIO_TypeDef* G_Base, uint16_t G_Pin, uint32_t G_Mode, uint32_t G_Speed,  uint32_t G_PPType);
extern void GPIO_SetFunc(GPIO_TypeDef* G_Base, uint16_t G_Pin, uint8_t G_AF, uint32_t G_Mode, uint32_t G_Speed, uint32_t G_PPType);
extern void GPIO_SetBits(GPIO_TypeDef* G_Base, uint32_t G_Pin);
extern void GPIO_ResetBits(GPIO_TypeDef* G_Base, uint32_t G_Pin);
extern uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef* G_Base, uint32_t G_Pin);
#endif

#if USE_STM32F1XX
#define GPIOA_ODR_Addr    (GPIOA_BASE+12) //0x4001080C 
#define GPIOB_ODR_Addr    (GPIOB_BASE+12) //0x40010C0C 
#define GPIOC_ODR_Addr    (GPIOC_BASE+12) //0x4001100C 
#define GPIOD_ODR_Addr    (GPIOD_BASE+12) //0x4001140C 
#define GPIOE_ODR_Addr    (GPIOE_BASE+12) //0x4001180C 
#define GPIOF_ODR_Addr    (GPIOF_BASE+12) //0x40011A0C    
#define GPIOG_ODR_Addr    (GPIOG_BASE+12) //0x40011E0C    

#define GPIOA_IDR_Addr    (GPIOA_BASE+8) //0x40010808 
#define GPIOB_IDR_Addr    (GPIOB_BASE+8) //0x40010C08 
#define GPIOC_IDR_Addr    (GPIOC_BASE+8) //0x40011008 
#define GPIOD_IDR_Addr    (GPIOD_BASE+8) //0x40011408 
#define GPIOE_IDR_Addr    (GPIOE_BASE+8) //0x40011808 
#define GPIOF_IDR_Addr    (GPIOF_BASE+8) //0x40011A08 
#define GPIOG_IDR_Addr    (GPIOG_BASE+8) //0x40011E08 

#elif USE_STM32F4XX
#define GPIOA_ODR_Addr    (GPIOA_BASE+20) //0x40020014
#define GPIOB_ODR_Addr    (GPIOB_BASE+20) //0x40020414 
#define GPIOC_ODR_Addr    (GPIOC_BASE+20) //0x40020814 
#define GPIOD_ODR_Addr    (GPIOD_BASE+20) //0x40020C14 
#define GPIOE_ODR_Addr    (GPIOE_BASE+20) //0x40021014 
#define GPIOF_ODR_Addr    (GPIOF_BASE+20) //0x40021414    
#define GPIOG_ODR_Addr    (GPIOG_BASE+20) //0x40021814   
#define GPIOH_ODR_Addr    (GPIOH_BASE+20) //0x40021C14    
#define GPIOI_ODR_Addr    (GPIOI_BASE+20) //0x40022014     

#define GPIOA_IDR_Addr    (GPIOA_BASE+16) //0x40020010 
#define GPIOB_IDR_Addr    (GPIOB_BASE+16) //0x40020410 
#define GPIOC_IDR_Addr    (GPIOC_BASE+16) //0x40020810 
#define GPIOD_IDR_Addr    (GPIOD_BASE+16) //0x40020C10 
#define GPIOE_IDR_Addr    (GPIOE_BASE+16) //0x40021010 
#define GPIOF_IDR_Addr    (GPIOF_BASE+16) //0x40021410 
#define GPIOG_IDR_Addr    (GPIOG_BASE+16) //0x40021810 
#define GPIOH_IDR_Addr    (GPIOH_BASE+16) //0x40021C10 
#define GPIOI_IDR_Addr    (GPIOI_BASE+16) //0x40022010 

#elif  USE_STM32H7XX
#define GPIO_Pin_0		GPIO_PIN_0
#define GPIO_Pin_1		GPIO_PIN_1
#define GPIO_Pin_2		GPIO_PIN_2
#define GPIO_Pin_3		GPIO_PIN_3
#define GPIO_Pin_4		GPIO_PIN_4
#define GPIO_Pin_5		GPIO_PIN_5
#define GPIO_Pin_6		GPIO_PIN_6
#define GPIO_Pin_7		GPIO_PIN_7
#define GPIO_Pin_8		GPIO_PIN_8
#define GPIO_Pin_9		GPIO_PIN_9
#define GPIO_Pin_10		GPIO_PIN_10
#define GPIO_Pin_11		GPIO_PIN_11
#define GPIO_Pin_12		GPIO_PIN_12
#define GPIO_Pin_13		GPIO_PIN_13
#define GPIO_Pin_14		GPIO_PIN_14
#define GPIO_Pin_15		GPIO_PIN_15
#define GPIO_Pin_All    ((uint16_t)0xFFFF)  /* All pins selected */
#endif

#define BITBAND(addr, bitnum) ((addr & 0xF0000000)+0x2000000+((addr &0xFFFFF)<<5)+(bitnum<<2)) 
#define MEM_ADDR(addr)  *((volatile unsigned long  *)(addr)) 
#define BIT_ADDR(addr, bitnum)   MEM_ADDR(BITBAND(addr, bitnum)) 

#define PAout(n)   BIT_ADDR(GPIOA_ODR_Addr,n)  
#define PAin(n)    BIT_ADDR(GPIOA_IDR_Addr,n)  

#define PBout(n)   BIT_ADDR(GPIOB_ODR_Addr,n)  
#define PBin(n)    BIT_ADDR(GPIOB_IDR_Addr,n)  

#define PCout(n)   BIT_ADDR(GPIOC_ODR_Addr,n)  
#define PCin(n)    BIT_ADDR(GPIOC_IDR_Addr,n)  

#define PDout(n)   BIT_ADDR(GPIOD_ODR_Addr,n)  
#define PDin(n)    BIT_ADDR(GPIOD_IDR_Addr,n)  

#define PEout(n)   BIT_ADDR(GPIOE_ODR_Addr,n)  
#define PEin(n)    BIT_ADDR(GPIOE_IDR_Addr,n)  

#define PFout(n)   BIT_ADDR(GPIOF_ODR_Addr,n)   
#define PFin(n)    BIT_ADDR(GPIOF_IDR_Addr,n)  

#define PGout(n)   BIT_ADDR(GPIOG_ODR_Addr,n)   
#define PGin(n)    BIT_ADDR(GPIOG_IDR_Addr,n)  

#if USE_STM32F4XX
#define PHout(n)   BIT_ADDR(GPIOH_ODR_Addr,n)  
#define PHin(n)    BIT_ADDR(GPIOH_IDR_Addr,n)  

#define PIout(n)   BIT_ADDR(GPIOI_ODR_Addr,n)  
#define PIin(n)    BIT_ADDR(GPIOI_IDR_Addr,n)  
#endif

// 
#define PINA       (uint16_t)GPIOA->IDR
#define PINB       (uint16_t)GPIOB->IDR
#define PINC       (uint16_t)GPIOC->IDR
#define PIND       (uint16_t)GPIOD->IDR
#define PINE       (uint16_t)GPIOE->IDR
#define PINF       (uint16_t)GPIOF->IDR
#define PING       (uint16_t)GPIOG->IDR

/*
USE_STM32F4XX
1   void GPIO_DeInit(GPIO_TypeDef* GPIOx);
2	void GPIO_Init(GPIO_TypeDef* GPIOx, GPIO_InitTypeDef* GPIO_InitStruct);
3	void GPIO_StructInit(GPIO_InitTypeDef* GPIO_InitStruct);
4	void GPIO_PinLockConfig(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
5	uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
6	uint16_t GPIO_ReadInputData(GPIO_TypeDef* GPIOx);
7	uint8_t GPIO_ReadOutputDataBit(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
8	uint16_t GPIO_ReadOutputData(GPIO_TypeDef* GPIOx);
9	void GPIO_SetBits(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
10	void GPIO_ResetBits(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
11	void GPIO_WriteBit(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin, BitAction BitVal);
12	void GPIO_Write(GPIO_TypeDef* GPIOx, uint16_t PortVal);
13	void GPIO_ToggleBits(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
14	void GPIO_PinAFConfig(GPIO_TypeDef* GPIOx, uint16_t GPIO_PinSource, uint8_t GPIO_AF);
*/

/*
USE_STM32H7XX
void  HAL_GPIO_Init(GPIO_TypeDef  *GPIOx, GPIO_InitTypeDef *GPIO_Init);
void  HAL_GPIO_DeInit(GPIO_TypeDef  *GPIOx, uint32_t GPIO_Pin);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
HAL_StatusTypeDef HAL_GPIO_LockPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void HAL_GPIO_EXTI_IRQHandler(uint16_t GPIO_Pin);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);
*/

#endif


