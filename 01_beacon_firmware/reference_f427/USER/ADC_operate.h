#ifndef __ADC_H
#define	__ADC_H

#include "main.h"

#if USE_STM32H7XX
extern ADC_HandleTypeDef  m_hadc1;
#endif

#define ADC_REF_U			3.26f			// Reference voltage
#define VoltageBuf_MaxSize  32				// ADC measure Li or Power voltage slide window
#define ADC_COEF			0.000805664f	// ADC data convert to voltage coefficient

#define LiCells				4				// Li-on battery cell number
#define RESISTOR_DIV		4				// 分压电阻系数选择

#if LiCells == 20
#define VoltLi_UpLimit		84.0f
#define VoltLi_LowLimit		50.0f
#elif LiCells == 14
#define VoltLi_UpLimit		58.0f
#define VoltLi_LowLimit		42.0f
#elif LiCells == 7
#define VoltLi_UpLimit		29.4f
#define VoltLi_LowLimit		14.0f
#elif LiCells == 6
#define VoltLi_UpLimit		25.2f
#define VoltLi_LowLimit		18.0f
#elif LiCells == 4
#define VoltLi_UpLimit		16.8f
#define VoltLi_LowLimit		10.0f
#elif LiCells == 3
#define VoltLi_UpLimit		12.6f
#define VoltLi_LowLimit		7.5f
#elif LiCells == 2
#define VoltLi_UpLimit		8.4f
#define VoltLi_LowLimit		5.0f
#elif LiCells == 1
#define VoltLi_UpLimit		4.2f
#define VoltLi_LowLimit		2.5f
#else
#define VoltLi_UpLimit		50.0f		// 自定义电压上限
#define VoltLi_LowLimit		5.0f		// 自定义电压下限
#endif

#if LiCells == 20
#define INPUT_DIVIDE_U		0.0291f		// AD input voltage divide coefficient 120k/(4000k+120k)
#elif LiCells == 14
#define INPUT_DIVIDE_U		0.0322f		// AD input voltage divide coefficient 100k/(3010k+100k)
#elif RESISTOR_DIV == 7
#define INPUT_DIVIDE_U		0.085f		// AD input voltage divide coefficient 120k/(1300k+120k)
#elif RESISTOR_DIV == 6
#define INPUT_DIVIDE_U		0.099f		// AD input voltage divide coefficient 100k/(910k+100k)
#elif RESISTOR_DIV == 4
#define INPUT_DIVIDE_U		0.164f		// AD input voltage divide coefficient 100k/(510k+100k)
#elif RESISTOR_DIV == 3
#define INPUT_DIVIDE_U		0.204f		// AD input voltage divide coefficient 100k/(390k+100k)
#elif RESISTOR_DIV == 2
#define INPUT_DIVIDE_U		0.333f		// AD input voltage divide coefficient 100k/(200k+100k)
#elif RESISTOR_DIV == 1
#define INPUT_DIVIDE_U		0.500f		// AD input voltage divide coefficient 100k/(100k+100K)
#else
#define INPUT_DIVIDE_U		1.000f
#endif

extern float m_fLiPowerVoltage;		// Li-On battery voltage

bool ADC1_Initialize(uint32_t nCh, bool bInitBuf);						// AD initialize
bool ADC1_DeInitialize(void);											// AD DeInitialize
void ShowBatteryStatus(void);											// show Li-on battery power status
void ShowBatteryStatusLedG(void);										// show Li-on battery power status, Only Green LED
float GetVoltage(void);													// get voltage, not use limit set，仅ADC1单通道
float GetLiPowerVoltage(void);											// get the battery voltage
float GetLiPowerVoltCorrect(float fVolt, float fCoef1, float fCoef2);	// 利用1阶线性拟合修正的电压测量结果
float GetLiChargePercent(void);											// get the battery charge percent rate

void ADC_InitAutoAcq(bool bEn, uint8_t nCh, uint32_t* pBuf1, uint32_t* pBuf2, u32 uBufSize, u8 uSpeed);	// 初始化某一通道的AD为连续自动采集，采样率在函数内设定
void ADC_SwitchSingleDMABuf(uint32_t* pBuf1, u32 uBufSize);				// 在不改变配置的情况下，切换缓冲区
#endif 

