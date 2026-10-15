
#include "IO_operate.h"
#include "PCLINK_operate.h"

#define LED_DELAY	50			// LED灯闪亮的时间

bool m_bGpioInit = false;		// Gpio初始化完成标志
LED_SHINE_TRIGER m_stLedTrig;	// LED灯闪烁触发信息，降低时延对函数响应的开销

void ResetShineFlag(void)
{
	// 复位闪烁触发信号
	m_stLedTrig.bShowBat = false;
	m_stLedTrig.bShineR  = false;
	m_stLedTrig.bShineG  = false;
	
	m_stLedTrig.bShineRR = false;
	m_stLedTrig.bShineGG = false;
	m_stLedTrig.bShineRG = false;
	m_stLedTrig.bShineGB = false;
}

void GPIO_INIT(void)
{
	/* Enable GPIOs clock */
#if USE_STM32F1XX
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
#elif USE_STM32F4XX
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA , ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB , ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC , ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD , ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE , ENABLE);
#elif USE_STM32H7XX
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();
#endif
	
	// LED
#if USE_STM32F1XX
	#ifdef ARM_LED_RED_GPIO
		GPIO_SetPin(ARM_LED_RED_GPIO_PORT, 		ARM_LED_RED_GPIO, 	GPIO_Mode_Out_PP, 	GPIO_Speed_2MHz);
	#endif
	#ifdef ARM_LED_GREEN_GPIO
		GPIO_SetPin(ARM_LED_GREEN_GPIO_PORT, 	ARM_LED_GREEN_GPIO, GPIO_Mode_Out_PP, 	GPIO_Speed_2MHz);
	#endif
	#ifdef ARM_LED_BLUE_GPIO
		GPIO_SetPin(ARM_LED_BLUE_GPIO_PORT, 	ARM_LED_BLUE_GPIO, 	GPIO_Mode_Out_PP, 	GPIO_Speed_2MHz);
	#endif
	
	// 由于FPGA接口初始化延时的问题，需要提前设定LED状态
	SetArmLedR(true);
	SetArmLedG(false);
	SetArmLedB(false);
	
#elif USE_STM32F4XX
	#ifdef ARM_LED_RED_GPIO
		GPIO_SetOut(ARM_LED_RED_GPIO_PORT, 		ARM_LED_RED_GPIO, GPIO_OType_PP, 	GPIO_Speed_2MHz, GPIO_PuPd_NOPULL);
	#endif
	#ifdef ARM_LED_GREEN_GPIO
		GPIO_SetOut(ARM_LED_GREEN_GPIO_PORT, 	ARM_LED_GREEN_GPIO, GPIO_OType_PP, 	GPIO_Speed_2MHz, GPIO_PuPd_NOPULL);
	#endif
	#ifdef ARM_LED_BLUE_GPIO
		GPIO_SetOut(ARM_LED_BLUE_GPIO_PORT, 	ARM_LED_BLUE_GPIO, GPIO_OType_PP, 	GPIO_Speed_2MHz, GPIO_PuPd_NOPULL);
	#endif
	
	// 由于FPGA接口初始化延时的问题，需要提前设定LED状态
	SetArmLedR(true);
	SetArmLedG(false);
	SetArmLedB(false);
	
#endif

#if DEVICE_SUBTYPE/100 == iBeaconB

#elif DEVICE_SUBTYPE/100 == iBeaconB2 || DEVICE_SUBTYPE/100 == iBeaconR
	GPIO_SetOut(DIGITAL_PW_SWITCH_GPIO_PORT, DIGITAL_PW_SWITCH_GPIO, GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_UP);
	GPIO_SetOut(RS485_PWEN_GPIO_PORT, 		 RS485_PWEN_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_UP);
	GPIO_SetOut(SELFTEST_GPIO_PORT, 		 SELFTEST_EN_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
	GPIO_SetOut(AMP_GPIO_PORT, 		 AMP_A1_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
	GPIO_SetOut(AMP_GPIO_PORT, 		 AMP_A0_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
	
#elif DEVICE_SUBTYPE/100 == iBeaconR2
	GPIO_SetOut(DIGITAL_PW_SWITCH_GPIO_PORT, DIGITAL_PW_SWITCH_GPIO, GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_UP);
	GPIO_SetOut(RS485_PWEN_GPIO_PORT, 		 RS485_PWEN_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_UP);
	GPIO_SetOut(SELFTEST_GPIO_PORT, 		 SELFTEST_EN_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
	GPIO_SetOut(AMP_GPIO_PORT, 		 AMP_A3_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
	GPIO_SetOut(AMP_GPIO_PORT, 		 AMP_A2_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
	GPIO_SetOut(AMP_GPIO_PORT, 		 AMP_A1_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
	GPIO_SetOut(AMP_GPIO_PORT, 		 AMP_A0_GPIO, 		 GPIO_OType_PP, GPIO_Speed_2MHz, GPIO_PuPd_DOWN);
#endif

	// 由于FPGA接口初始化延时的问题，需要提前设定LED状态
	SetArmLedR(true);
	SetArmLedG(false);
	SetArmLedB(false);
	
	ResetShineFlag();
	m_bGpioInit = 1;		// 置位初始化完成标志
}

/***************************************************************/
// 以下为LED闪烁控制函数
/***************************************************************/
void SetArmLedR(bool bOn)
{
#ifdef ARM_LED_RED_GPIO
	#if COMMON_GND
	if(bOn == true)
		GPIO_SetBits(ARM_LED_RED_GPIO_PORT, ARM_LED_RED_GPIO);
	else
		GPIO_ResetBits(ARM_LED_RED_GPIO_PORT, ARM_LED_RED_GPIO);
	#else
	if(bOn == false)
		GPIO_SetBits(ARM_LED_RED_GPIO_PORT, ARM_LED_RED_GPIO);
	else
		GPIO_ResetBits(ARM_LED_RED_GPIO_PORT, ARM_LED_RED_GPIO);
	#endif
#endif
}

void SetArmLedG(bool bOn)
{
#ifdef ARM_LED_GREEN_GPIO
	#if COMMON_GND
	if(bOn == true)
		GPIO_SetBits(ARM_LED_GREEN_GPIO_PORT, ARM_LED_GREEN_GPIO); 
	else
		GPIO_ResetBits(ARM_LED_GREEN_GPIO_PORT, ARM_LED_GREEN_GPIO);
	#else
	if(bOn == false)
		GPIO_SetBits(ARM_LED_GREEN_GPIO_PORT, ARM_LED_GREEN_GPIO); 
	else
		GPIO_ResetBits(ARM_LED_GREEN_GPIO_PORT, ARM_LED_GREEN_GPIO);
	#endif
#endif
}

void SetArmLedB(bool bOn)
{
	// 如果没有定义蓝色灯，默认绿色灯代替
#ifdef ARM_LED_BLUE_GPIO
	#if COMMON_GND
	if (bOn == true)
		GPIO_SetBits(ARM_LED_BLUE_GPIO_PORT, ARM_LED_BLUE_GPIO);
	else
		GPIO_ResetBits(ARM_LED_BLUE_GPIO_PORT, ARM_LED_BLUE_GPIO);
	#else
	if (bOn == false)
		GPIO_SetBits(ARM_LED_BLUE_GPIO_PORT, ARM_LED_BLUE_GPIO);
	else
		GPIO_ResetBits(ARM_LED_BLUE_GPIO_PORT, ARM_LED_BLUE_GPIO);
	#endif
#else
	SetArmLedG(bOn);
#endif
}

void ShineRed(void)
{
	ResetShineFlag();
	SetArmLedG(false);
	SetArmLedR(true);	delay(LED_DELAY);
	SetArmLedR(false);
}

void ShineGreen(void)
{
	ResetShineFlag();
	SetArmLedR(false);
	SetArmLedG(true);	delay(LED_DELAY);
	SetArmLedG(false);
}

void ShineBlue(void)
{
	// 由于蓝灯具有和红绿两灯不一样的逻辑
	// 常用于表示发射等功能，因此需要特殊处理其标志位
	// ResetShineFlag();
	m_stLedTrig.bShineB = false;
	SetArmLedB(true);	delay(LED_DELAY);
	SetArmLedB(false);
}

void ShineRG(void)
{
	// 红绿闪烁1次
	ResetShineFlag();
	ShineRed();		delay(LED_DELAY);
	ShineGreen();
}

void ShineRR(void)
{
	// 红红闪烁1次
	ResetShineFlag();
	ShineRed();		delay(LED_DELAY);
	ShineRed();
}

void ShineGG(void)
{
	// 绿绿闪烁1次
	ResetShineFlag();
	ShineGreen();	delay(LED_DELAY);
	ShineGreen();
}

void ShineGB(void)
{
	// 绿蓝闪烁1次
	ResetShineFlag();
	ShineGreen();	delay(LED_DELAY);
	ShineBlue();

	delay(50);
	ShineBlue();
}

