#ifndef __MS5837_H
#define __MS5837_H

#include "main.h"

extern   uint16_t  c1_senst1;
extern   uint16_t  c2_offt1;		
extern   uint16_t  c3_tcs;		
extern   uint16_t  c4_tcc;		
extern	 uint16_t  c5_tref;		
extern	 uint16_t  c6_tempsens;

void  MS5837_Init(void);
void  MS5837_Getdata(void);
float MS5837_GetPressure(void);		// 调用 MS5837_Getdata 函数并直接返回压力值，单位mbar 
float MS5837_GetTemperature(void);	// 不调用函数，直接返回温度值
float MS5837_GetDepth(void);		// 不调用函数，直接返回压力值转换的深度值


// IIC IO define
#define P_I2C_SCL 		GPIO_Pin_12
#define P_I2C_SDA 		GPIO_Pin_11
#define P_SCL_PORT 		GPIOA
#define P_SDA_PORT 		GPIOA

#if USE_STM32F1XX
#define P_SCL_APB2Periph			RCC_APB2Periph_GPIOA
#define P_SDA_APB2Periph			RCC_APB2Periph_GPIOA
#elif USE_STM32F4XX
#define P_SCL_AHB1Periph			RCC_AHB1Periph_GPIOA
#define P_SDA_AHB1Periph			RCC_AHB1Periph_GPIOA
#endif

#define P_I2C_SCL_H GPIO_SetBits(P_SCL_PORT,	P_I2C_SCL)
#define P_I2C_SCL_L GPIO_ResetBits(P_SCL_PORT,	P_I2C_SCL)

#define P_I2C_SDA_H GPIO_SetBits(P_SDA_PORT,	P_I2C_SDA)
#define P_I2C_SDA_L GPIO_ResetBits(P_SDA_PORT,	P_I2C_SDA)


#endif /* MS5837_H_ */


