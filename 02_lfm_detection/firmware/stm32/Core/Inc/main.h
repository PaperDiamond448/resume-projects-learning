#ifndef MAIN_H
#define MAIN_H

#include "stm32f4xx.h"
#include "stm32f4xx_adc.h"

/* Board wiring assumed by this skeleton -- change to match your actual
 * STM32F407VGT6 board:
 *   - HSE = 8 MHz crystal -> SYSCLK = 168 MHz (see SystemClock_Config in main.c)
 *   - Hydrophone channel 1 analog front-end output -> PA1 (ADC1_IN1)
 *   - Debug/report UART -> USART1 on PA9 (TX) / PA10 (RX), 115200-8-N-1
 * If your board differs (different HSE frequency, different ADC pin,
 * different UART), this is the place to change it.
 */
#define HSE_VALUE_HZ        8000000U
#define ADC_CHANNEL_HYDRO1  ADC_Channel_1   /* PA1 -- StdPeriph enum, mixed case */

#endif
