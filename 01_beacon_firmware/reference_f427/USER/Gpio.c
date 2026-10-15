
#include "Gpio.h"

#if USE_STM32F1XX
/************************************************************************/
// set GPIO output mode
/************************************************************************/
void GPIO_SetPin(GPIO_TypeDef * G_Base, uint16_t G_Pin, GPIOMode_TypeDef G_Mode, GPIOSpeed_TypeDef G_Speed)
{
	GPIO_InitTypeDef 	GPIO_InitStructure;

	GPIO_InitStructure.GPIO_Pin = G_Pin;	    	// GPIO_Pin_XX

	/*
	typedef enum
	{ GPIO_Mode_AIN = 0x0,
	  GPIO_Mode_IN_FLOATING = 0x04,
	  GPIO_Mode_IPD = 0x28,
	  GPIO_Mode_IPU = 0x48,
	  GPIO_Mode_Out_OD = 0x14,
	  GPIO_Mode_Out_PP = 0x10,
	  GPIO_Mode_AF_OD = 0x1C,
	  GPIO_Mode_AF_PP = 0x18
	}GPIOMode_TypeDef;
	*/
	GPIO_InitStructure.GPIO_Mode = G_Mode;			// output mode

	/*
	typedef enum
	{
	GPIO_Speed_2MHz		= 0x00, < Low speed
	GPIO_Speed_25MHz	= 0x01, < Medium speed
	GPIO_Speed_50MHz	= 0x02, < Fast speed
	GPIO_Speed_100MHz	= 0x03	< High speed on 30 pF (80 MHz Output max speed on 15 pF)
	}GPIOSpeed_TypeDef;
	*/
	GPIO_InitStructure.GPIO_Speed = G_Speed;	    // scan frequence

	GPIO_Init(G_Base, &GPIO_InitStructure);
}
#endif

#if USE_STM32F4XX
/************************************************************************/
// set GPIO output mode
/************************************************************************/
void GPIO_SetOut(GPIO_TypeDef * G_Base, uint16_t G_Pin, GPIOOType_TypeDef G_OType, GPIOSpeed_TypeDef G_Speed, GPIOPuPd_TypeDef G_PPType)
{
	GPIO_InitTypeDef 	GPIO_InitStructure; 

	GPIO_InitStructure.GPIO_Pin = G_Pin ;	    	// GPIO_Pin_XX

	/*
	typedef enum
	{ 
	  GPIO_Mode_IN	 = 0x00, < GPIO Input Mode 
	  GPIO_Mode_OUT  = 0x01, < GPIO Output Mode 
	  GPIO_Mode_AF	 = 0x02, < GPIO Alternate function Mode 
	  GPIO_Mode_AN	 = 0x03  < GPIO Analog Mode 
	}GPIOMode_TypeDef;
	*/
  	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;	// output mode

	/*
	typedef enum
	{ 
	  GPIO_OType_PP = 0x00,
	  GPIO_OType_OD = 0x01
	}GPIOOType_TypeDef;
	*/
  	GPIO_InitStructure.GPIO_OType = G_OType;	    // 

	/*
	typedef enum
	{ 
	  GPIO_Speed_2MHz	= 0x00, < Low speed 
	  GPIO_Speed_25MHz	= 0x01, < Medium speed 
	  GPIO_Speed_50MHz	= 0x02, < Fast speed 
	  GPIO_Speed_100MHz = 0x03	< High speed on 30 pF (80 MHz Output max speed on 15 pF) 
	}GPIOSpeed_TypeDef;
	*/
  	GPIO_InitStructure.GPIO_Speed = G_Speed;	    // scan frequence

	/*
	typedef enum
	{ 
	  GPIO_PuPd_NOPULL = 0x00,
	  GPIO_PuPd_UP	   = 0x01,
	  GPIO_PuPd_DOWN   = 0x02
	}GPIOPuPd_TypeDef;
	*/
  	GPIO_InitStructure.GPIO_PuPd = G_PPType;
		
  	GPIO_Init(G_Base, &GPIO_InitStructure);
}

/************************************************************************/
// set GPIO input mode
/************************************************************************/
void GPIO_SetIn(GPIO_TypeDef * G_Base, uint16_t G_Pin, GPIOOType_TypeDef G_OType, GPIOSpeed_TypeDef G_Speed, GPIOPuPd_TypeDef G_PPType)
{
	GPIO_InitTypeDef 	GPIO_InitStructure; 

	GPIO_InitStructure.GPIO_Pin = G_Pin ;		    // 

	/*	typedef enum
	{ 
	  GPIO_Mode_IN	 = 0x00, < GPIO Input Mode 
	  GPIO_Mode_OUT  = 0x01, < GPIO Output Mode 
	  GPIO_Mode_AF	 = 0x02, < GPIO Alternate function Mode 
	  GPIO_Mode_AN	 = 0x03  < GPIO Analog Mode 
	}GPIOMode_TypeDef;	*/
  	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;	// input mode

	/*	typedef enum
	{ 
	  GPIO_OType_PP = 0x00,
	  GPIO_OType_OD = 0x01
	}GPIOOType_TypeDef;	*/
  	GPIO_InitStructure.GPIO_OType = G_OType;    	// 

	/*	typedef enum
	{ 
	  GPIO_Speed_2MHz	= 0x00, < Low speed 
	  GPIO_Speed_25MHz	= 0x01, < Medium speed 
	  GPIO_Speed_50MHz	= 0x02, < Fast speed 
	  GPIO_Speed_100MHz = 0x03	< High speed on 30 pF (80 MHz Output max speed on 15 pF) 
	}GPIOSpeed_TypeDef;	*/
  	GPIO_InitStructure.GPIO_Speed = G_Speed;    	// scan frequence

	/*	typedef enum
	{ 
	  GPIO_PuPd_NOPULL = 0x00,
	  GPIO_PuPd_UP	   = 0x01,
	  GPIO_PuPd_DOWN   = 0x02
	}GPIOPuPd_TypeDef;	*/
  	GPIO_InitStructure.GPIO_PuPd = G_PPType;
		
  	GPIO_Init(G_Base, &GPIO_InitStructure);
}

/************************************************************************/
// set GPIO function
/************************************************************************/
void GPIO_SetFunc(GPIO_TypeDef * G_Base, uint16_t G_Pin, GPIOOType_TypeDef G_OType, GPIOSpeed_TypeDef G_Speed, GPIOPuPd_TypeDef G_PPType)
{
	GPIO_InitTypeDef 	GPIO_InitStructure; 

	GPIO_InitStructure.GPIO_Pin = G_Pin ;		    // 

	/*	typedef enum
	{ 
	  GPIO_Mode_IN	 = 0x00, < GPIO Input Mode 
	  GPIO_Mode_OUT  = 0x01, < GPIO Output Mode 
	  GPIO_Mode_AF	 = 0x02, < GPIO Alternate function Mode 
	  GPIO_Mode_AN	 = 0x03  < GPIO Analog Mode 
	}GPIOMode_TypeDef;	*/
  	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;	// funciton mode

	/*	typedef enum
	{ 
	  GPIO_OType_PP = 0x00,
	  GPIO_OType_OD = 0x01
	}GPIOOType_TypeDef;	*/
  	GPIO_InitStructure.GPIO_OType = G_OType;    	// 

	/*	typedef enum
	{ 
	  GPIO_Speed_2MHz	= 0x00, < Low speed 
	  GPIO_Speed_25MHz	= 0x01, < Medium speed 
	  GPIO_Speed_50MHz	= 0x02, < Fast speed 
	  GPIO_Speed_100MHz = 0x03	< High speed on 30 pF (80 MHz Output max speed on 15 pF) 
	}GPIOSpeed_TypeDef;	*/
  	GPIO_InitStructure.GPIO_Speed = G_Speed;    	// scan frequence

	/*	typedef enum
	{ 
	  GPIO_PuPd_NOPULL = 0x00,
	  GPIO_PuPd_UP	   = 0x01,
	  GPIO_PuPd_DOWN   = 0x02
	}GPIOPuPd_TypeDef;	*/
  	GPIO_InitStructure.GPIO_PuPd = G_PPType;
		
  	GPIO_Init(G_Base, &GPIO_InitStructure);
}
#endif

#if USE_STM32H7XX
/************************************************************************/
// set GPIO output mode
/************************************************************************/
void GPIO_SetOut(GPIO_TypeDef* G_Base, uint16_t G_Pin, uint32_t G_Mode, uint32_t G_Speed, uint32_t G_PPType)
{
	GPIO_InitTypeDef 	GPIO_InitStructure; 

	GPIO_InitStructure.Pin = G_Pin ;		// GPIO_PIN_XX

//	#define GPIO_MODE_INPUT                 MODE_INPUT                                                  /*!< Input Floating Mode                                                */
//	#define GPIO_MODE_OUTPUT_PP             (MODE_OUTPUT | OUTPUT_PP)                                   /*!< Output Push Pull Mode                                              */
//	#define GPIO_MODE_OUTPUT_OD             (MODE_OUTPUT | OUTPUT_OD)                                   /*!< Output Open Drain Mode                                             */
//	#define GPIO_MODE_AF_PP                 (MODE_AF | OUTPUT_PP)                                       /*!< Alternate Function Push Pull Mode                                  */
//	#define GPIO_MODE_AF_OD                 (MODE_AF | OUTPUT_OD)                                       /*!< Alternate Function Open Drain Mode                                 */
//	#define GPIO_MODE_ANALOG                MODE_ANALOG                                                 /*!< Analog Mode                                                        */
//	#define GPIO_MODE_IT_RISING             (MODE_INPUT | EXTI_IT | TRIGGER_RISING)                     /*!< External Interrupt Mode with Rising edge trigger detection         */
//	#define GPIO_MODE_IT_FALLING            (MODE_INPUT | EXTI_IT | TRIGGER_FALLING)                    /*!< External Interrupt Mode with Falling edge trigger detection        */
//	#define GPIO_MODE_IT_RISING_FALLING     (MODE_INPUT | EXTI_IT | TRIGGER_RISING | TRIGGER_FALLING)   /*!< External Interrupt Mode with Rising/Falling edge trigger detection */

//	#define GPIO_MODE_EVT_RISING            (MODE_INPUT | EXTI_EVT | TRIGGER_RISING)                    /*!< External Event Mode with Rising edge trigger detection             */
//	#define GPIO_MODE_EVT_FALLING           (MODE_INPUT | EXTI_EVT | TRIGGER_FALLING)                   /*!< External Event Mode with Falling edge trigger detection            */
//	#define GPIO_MODE_EVT_RISING_FALLING    (MODE_INPUT | EXTI_EVT | TRIGGER_RISING | TRIGGER_FALLING)  /*!< External Event Mode with Rising/Falling edge trigger detection     */

  	GPIO_InitStructure.Mode = G_Mode;		// output mode

//	#define  GPIO_SPEED_FREQ_LOW         (0x00000000U)  /*!< Low speed     */
//	#define  GPIO_SPEED_FREQ_MEDIUM      (0x00000001U)  /*!< Medium speed  */
//	#define  GPIO_SPEED_FREQ_HIGH        (0x00000002U)  /*!< Fast speed    */
//	#define  GPIO_SPEED_FREQ_VERY_HIGH   (0x00000003U)  /*!< High speed    */
  	GPIO_InitStructure.Speed = G_Speed;	    // scan frequence

//	#define  GPIO_NOPULL        (0x00000000U)   /*!< No Pull-up or Pull-down activation  */
//	#define  GPIO_PULLUP        (0x00000001U)   /*!< Pull-up activation                  */
//	#define  GPIO_PULLDOWN      (0x00000002U)   /*!< Pull-down activation                */
  	GPIO_InitStructure.Pull = G_PPType;
		
  	HAL_GPIO_Init(G_Base, &GPIO_InitStructure);
}

/************************************************************************/
// set GPIO input mode
/************************************************************************/
void GPIO_SetIn(GPIO_TypeDef* G_Base, uint16_t G_Pin, uint32_t G_Mode, uint32_t G_Speed, uint32_t G_PPType)
{
	GPIO_InitTypeDef 	GPIO_InitStructure; 

	GPIO_InitStructure.Pin = G_Pin ;		// GPIO_PIN_XX

//	#define GPIO_MODE_INPUT                 MODE_INPUT                                                  /*!< Input Floating Mode                                                */
//	#define GPIO_MODE_OUTPUT_PP             (MODE_OUTPUT | OUTPUT_PP)                                   /*!< Output Push Pull Mode                                              */
//	#define GPIO_MODE_OUTPUT_OD             (MODE_OUTPUT | OUTPUT_OD)                                   /*!< Output Open Drain Mode                                             */
//	#define GPIO_MODE_AF_PP                 (MODE_AF | OUTPUT_PP)                                       /*!< Alternate Function Push Pull Mode                                  */
//	#define GPIO_MODE_AF_OD                 (MODE_AF | OUTPUT_OD)                                       /*!< Alternate Function Open Drain Mode                                 */
//	#define GPIO_MODE_ANALOG                MODE_ANALOG                                                 /*!< Analog Mode                                                        */
//	#define GPIO_MODE_IT_RISING             (MODE_INPUT | EXTI_IT | TRIGGER_RISING)                     /*!< External Interrupt Mode with Rising edge trigger detection         */
//	#define GPIO_MODE_IT_FALLING            (MODE_INPUT | EXTI_IT | TRIGGER_FALLING)                    /*!< External Interrupt Mode with Falling edge trigger detection        */
//	#define GPIO_MODE_IT_RISING_FALLING     (MODE_INPUT | EXTI_IT | TRIGGER_RISING | TRIGGER_FALLING)   /*!< External Interrupt Mode with Rising/Falling edge trigger detection */

//	#define GPIO_MODE_EVT_RISING            (MODE_INPUT | EXTI_EVT | TRIGGER_RISING)                    /*!< External Event Mode with Rising edge trigger detection             */
//	#define GPIO_MODE_EVT_FALLING           (MODE_INPUT | EXTI_EVT | TRIGGER_FALLING)                   /*!< External Event Mode with Falling edge trigger detection            */
//	#define GPIO_MODE_EVT_RISING_FALLING    (MODE_INPUT | EXTI_EVT | TRIGGER_RISING | TRIGGER_FALLING)  /*!< External Event Mode with Rising/Falling edge trigger detection     */

  	GPIO_InitStructure.Mode = G_Mode;	// input mode
	
//	#define  GPIO_SPEED_FREQ_LOW         (0x00000000U)  /*!< Low speed     */
//	#define  GPIO_SPEED_FREQ_MEDIUM      (0x00000001U)  /*!< Medium speed  */
//	#define  GPIO_SPEED_FREQ_HIGH        (0x00000002U)  /*!< Fast speed    */
//	#define  GPIO_SPEED_FREQ_VERY_HIGH   (0x00000003U)  /*!< High speed    */
  	GPIO_InitStructure.Speed = G_Speed;	    // scan frequence

//	#define  GPIO_NOPULL        (0x00000000U)   /*!< No Pull-up or Pull-down activation  */
//	#define  GPIO_PULLUP        (0x00000001U)   /*!< Pull-up activation                  */
//	#define  GPIO_PULLDOWN      (0x00000002U)   /*!< Pull-down activation                */
  	GPIO_InitStructure.Pull = G_PPType;
		
  	HAL_GPIO_Init(G_Base, &GPIO_InitStructure);
}

/************************************************************************/
// set GPIO function
/************************************************************************/
void GPIO_SetFunc(GPIO_TypeDef* G_Base, uint16_t G_Pin, uint8_t G_AF, uint32_t G_Mode, uint32_t G_Speed, uint32_t G_PPType)
{
	GPIO_InitTypeDef 	GPIO_InitStructure; 

	GPIO_InitStructure.Pin = G_Pin ;		// GPIO_PIN_XX

//	#define GPIO_MODE_INPUT                 MODE_INPUT                                                  /*!< Input Floating Mode                                                */
//	#define GPIO_MODE_OUTPUT_PP             (MODE_OUTPUT | OUTPUT_PP)                                   /*!< Output Push Pull Mode                                              */
//	#define GPIO_MODE_OUTPUT_OD             (MODE_OUTPUT | OUTPUT_OD)                                   /*!< Output Open Drain Mode                                             */
//	#define GPIO_MODE_AF_PP                 (MODE_AF | OUTPUT_PP)                                       /*!< Alternate Function Push Pull Mode                                  */
//	#define GPIO_MODE_AF_OD                 (MODE_AF | OUTPUT_OD)                                       /*!< Alternate Function Open Drain Mode                                 */
//	#define GPIO_MODE_ANALOG                MODE_ANALOG                                                 /*!< Analog Mode                                                        */
//	#define GPIO_MODE_IT_RISING             (MODE_INPUT | EXTI_IT | TRIGGER_RISING)                     /*!< External Interrupt Mode with Rising edge trigger detection         */
//	#define GPIO_MODE_IT_FALLING            (MODE_INPUT | EXTI_IT | TRIGGER_FALLING)                    /*!< External Interrupt Mode with Falling edge trigger detection        */
//	#define GPIO_MODE_IT_RISING_FALLING     (MODE_INPUT | EXTI_IT | TRIGGER_RISING | TRIGGER_FALLING)   /*!< External Interrupt Mode with Rising/Falling edge trigger detection */

//	#define GPIO_MODE_EVT_RISING            (MODE_INPUT | EXTI_EVT | TRIGGER_RISING)                    /*!< External Event Mode with Rising edge trigger detection             */
//	#define GPIO_MODE_EVT_FALLING           (MODE_INPUT | EXTI_EVT | TRIGGER_FALLING)                   /*!< External Event Mode with Falling edge trigger detection            */
//	#define GPIO_MODE_EVT_RISING_FALLING    (MODE_INPUT | EXTI_EVT | TRIGGER_RISING | TRIGGER_FALLING)  /*!< External Event Mode with Rising/Falling edge trigger detection     */

  	GPIO_InitStructure.Mode = G_Mode;		// input mode
	GPIO_InitStructure.Alternate = G_AF;	// 功能模式

//	#define  GPIO_SPEED_FREQ_LOW         (0x00000000U)  /*!< Low speed     */
//	#define  GPIO_SPEED_FREQ_MEDIUM      (0x00000001U)  /*!< Medium speed  */
//	#define  GPIO_SPEED_FREQ_HIGH        (0x00000002U)  /*!< Fast speed    */
//	#define  GPIO_SPEED_FREQ_VERY_HIGH   (0x00000003U)  /*!< High speed    */
  	GPIO_InitStructure.Speed = G_Speed;	    // scan frequence

//	#define  GPIO_NOPULL        (0x00000000U)   /*!< No Pull-up or Pull-down activation  */
//	#define  GPIO_PULLUP        (0x00000001U)   /*!< Pull-up activation                  */
//	#define  GPIO_PULLDOWN      (0x00000002U)   /*!< Pull-down activation                */
  	GPIO_InitStructure.Pull = G_PPType;
		
  	HAL_GPIO_Init(G_Base, &GPIO_InitStructure);
}

void GPIO_SetBits(GPIO_TypeDef* G_Base, uint32_t G_Pin)
{
	HAL_GPIO_WritePin(G_Base, G_Pin, GPIO_PIN_SET);
}

void GPIO_ResetBits(GPIO_TypeDef* G_Base, uint32_t G_Pin)
{
	HAL_GPIO_WritePin(G_Base, G_Pin, GPIO_PIN_RESET);
}

uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef* G_Base, uint32_t G_Pin)
{
	return HAL_GPIO_ReadPin(G_Base, G_Pin);
}

#endif


