/*
 * STM32F407VGT6 single-hydrophone-channel bring-up for the LFM copy-
 * correlation detector (firmware/algo/lfm_algo.c -- the SAME source file
 * already validated line-for-line against result.txt on the PC, see
 * firmware/host_test).
 *
 * Written against the StdPeriph driver API (StdPeriph_Lib for STM32F4),
 * to match the Keil project you create via Project -> New uVision Project
 * (device STM32F407VG) + Manage Run-Time Environment. See firmware/README.md
 * for exact setup steps -- this file assumes GPIO/RCC/ADC/DMA/TIM/USART
 * StdPeriph modules are ticked in that dialog.
 *
 * Signal path: PA1 (ADC1_IN1) <- hydrophone analog front-end
 *              TIM3 update event (156.25kHz-ish, see note below) triggers
 *              ADC1 regular conversion -> DMA2 Stream0 fills a 2x2048
 *              ping-pong buffer -> each half-buffer (2048 samples = one
 *              lfm2_2.m "cnt2" sub-block) is copied into a 16384-sample
 *              packet staging buffer -> once 8 sub-blocks have landed,
 *              lfm_process_packet() runs exactly like one block_num
 *              iteration of lfm2_2.m -> detected peaks are reported over
 *              USART1 (PA9/PA10, 115200-8-N-1) in the same tab-separated
 *              layout as result.txt.
 *
 * Clock note: 156250 Hz does not divide evenly from any standard STM32F4
 * PLL-derived clock (156250 = 2 x 5^7). With SYSCLK=168MHz, APB1 timer
 * clock = 84MHz, the closest achievable TIM3 rate with PSC=0, ARR=537 is
 * 84000000/538 = 156133.8 Hz, ~0.07% low. That is negligible here (the
 * whole reason the packets carry an explicit sync pulse is that transmit
 * and receive clocks are never bit-locked anyway) -- adjust ADC_TIM_ARR
 * below if you retune SYSCLK.
 *
 * TODO / not yet done (see firmware/README.md for the full list):
 *   - only 1 of the 3 hydrophone channels is wired up (ADC1 only); scaling
 *     to 3 channels needs ADC1/2/3 triple-simultaneous mode + a RAM budget
 *     pass (see README).
 *   - there is no real "sync" detection here (has_sync is always 0); a
 *     real deployment needs either a detected sync condition or a
 *     scheduled-timer call to lfm_channel_force_select().
 *   - no attitude/position geometry solve -- this firmware only reports
 *     raw per-channel delay/amplitude peaks, same as result.txt.
 */

#include "main.h"
#include "stm32f4xx.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_flash.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_adc.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_tim.h"
#include "stm32f4xx_usart.h"
#include "misc.h"
#include <stdio.h>
#include <string.h>

#include "lfm_algo.h"

#define SUBBLOCK_LEN   2048
#define ADC_TIM_PSC    0
#define ADC_TIM_ARR    537     /* see clock note above: ~156133.8 Hz actual */

/* DMA ping-pong capture buffer: two halves of one sub-block each. */
static volatile uint16_t s_adc_dma_buf[2 * SUBBLOCK_LEN];

/* 16384-sample packet staging buffer + bookkeeping for "8 sub-blocks make
 * a packet" (mirrors DATA_LEN/cnt2 in lfm2_2.m). */
static uint16_t     s_packet_buf[LFM_DATA_LEN];
static volatile int  s_subblock_count = 0;
static volatile int  s_packet_ready   = 0;
static volatile int  s_overrun_count  = 0;

static lfm_channel_state_t s_ch1_state;
static long                 s_block_num = 0;

static void SystemClock_Config(void);
static void GPIO_Config(void);
static void USART1_Config(void);
static void TIM3_Config(void);
static void ADC1_DMA_Config(void);

static void usart_send_string(const char *s)
{
    while (*s) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {}
        USART_SendData(USART1, (uint8_t)*s++);
    }
}

/* Reports one detected peak over USART1, same fields/order as result.txt. */
static void report_peak(void *ctx, const lfm_report_t *r)
{
    (void)ctx;
    char line[128];
    snprintf(line, sizeof(line), "%ld\t%d\t%d\t%.4f\t%.4f\t%.1f\t%.2f\r\n",
             r->block_num, r->channel, r->peak_id,
             r->delay_s, r->amp_v, r->wide_ms, r->in_out_ratio);
    usart_send_string(line);
}

int main(void)
{
    SystemClock_Config();
    GPIO_Config();
    USART1_Config();

    usart_send_string("LFM copy-correlation, STM32F407VGT6, channel 1 bring-up\r\n");
    usart_send_string("Block\tChannel\tPeakID\tDelay\tAmp\tWid\tInOutRatio\r\n");

    lfm_coeffs_init();
    lfm_channel_reset(&s_ch1_state);

    TIM3_Config();
    ADC1_DMA_Config();

    while (1) {
        if (s_overrun_count > 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "# overrun x%d (processing fell behind DMA)\r\n", s_overrun_count);
            usart_send_string(msg);
            s_overrun_count = 0;
        }

        if (s_packet_ready) {
            s_packet_ready = 0;
            s_block_num++;
            /* has_sync=0: no a-priori sync in this bring-up, see header
             * comment. Swap in real sync detection or call
             * lfm_channel_force_select() on your own schedule instead. */
            lfm_process_packet(&s_ch1_state, s_packet_buf,
                                /*has_sync=*/0, /*sync_offset=*/0,
                                s_block_num, /*channel=*/1,
                                report_peak, NULL);
        }
    }
}

/* --- DMA half/full transfer: each callback delivers one 2048-sample
 * sub-block. Copied into the packet staging buffer at the right offset,
 * mirroring the cnt2=1..8 sub-block loop in lfm2_2.m. */
static void subblock_ready(const volatile uint16_t *half, int half_index)
{
    if (s_subblock_count >= 8) {
        /* previous packet not yet consumed by the main loop -- drop this
         * sub-block and flag it rather than corrupting the packet buffer */
        s_overrun_count++;
        return;
    }
    memcpy(&s_packet_buf[s_subblock_count * SUBBLOCK_LEN], (const void *)half,
           SUBBLOCK_LEN * sizeof(uint16_t));
    s_subblock_count++;
    if (s_subblock_count == 8) {
        s_subblock_count = 0;
        s_packet_ready = 1;
    }
    (void)half_index;
}

void DMA2_Stream0_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA2_Stream0, DMA_IT_HTIF0) != RESET) {
        DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_HTIF0);
        subblock_ready(&s_adc_dma_buf[0], 0);
    }
    if (DMA_GetITStatus(DMA2_Stream0, DMA_IT_TCIF0) != RESET) {
        DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);
        subblock_ready(&s_adc_dma_buf[SUBBLOCK_LEN], 1);
    }
}

/* --- clock / peripheral setup --- */

static void SystemClock_Config(void)
{
    /* Assumes an 8 MHz HSE crystal -> SYSCLK = 168 MHz. If your VGT6 board
     * uses a different crystal, change PLL_M (must equal HSE_MHz) below --
     * PLL_N/P/Q are already chosen for the standard 168MHz/48MHz split. */
    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    if (RCC_WaitForHSEStartUp() != SUCCESS) {
        while (1) {} /* HSE didn't start -- check the crystal/board */
    }

    RCC_PLLConfig(RCC_PLLSource_HSE, /*PLL_M=*/8, /*PLL_N=*/336,
                  /*PLL_P=*/RCC_PLLP_Div2, /*PLL_Q=*/7);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET) {}

    FLASH_SetLatency(FLASH_Latency_5);
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div4);   /* APB1 = 42MHz (timer clock 84MHz) */
    RCC_PCLK2Config(RCC_HCLK_Div2);   /* APB2 = 84MHz */

    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08) {}

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_DMA2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_USART1, ENABLE);
}

static void GPIO_Config(void)
{
    GPIO_InitTypeDef g;

    /* PA1 = ADC1_IN1, analog input */
    g.GPIO_Pin  = GPIO_Pin_1;
    g.GPIO_Mode = GPIO_Mode_AN;
    g.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &g);

    /* PA9 = USART1_TX, PA10 = USART1_RX */
    g.GPIO_Pin   = GPIO_Pin_9 | GPIO_Pin_10;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &g);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9,  GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);
}

static void USART1_Config(void)
{
    USART_InitTypeDef u;
    u.USART_BaudRate            = 115200;
    u.USART_WordLength           = USART_WordLength_8b;
    u.USART_StopBits             = USART_StopBits_1;
    u.USART_Parity                = USART_Parity_No;
    u.USART_Mode                  = USART_Mode_Tx | USART_Mode_Rx;
    u.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    USART_Init(USART1, &u);
    USART_Cmd(USART1, ENABLE);
}

static void TIM3_Config(void)
{
    TIM_TimeBaseInitTypeDef t;
    t.TIM_Prescaler     = ADC_TIM_PSC;
    t.TIM_CounterMode   = TIM_CounterMode_Up;
    t.TIM_Period        = ADC_TIM_ARR;
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM3, &t);
    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
    TIM_Cmd(TIM3, ENABLE);
}

static void ADC1_DMA_Config(void)
{
    ADC_CommonInitTypeDef c;
    ADC_InitTypeDef a;
    DMA_InitTypeDef d;
    NVIC_InitTypeDef n;

    c.ADC_Mode             = ADC_Mode_Independent;
    c.ADC_Prescaler        = ADC_Prescaler_Div2;
    c.ADC_DMAAccessMode    = ADC_DMAAccessMode_Disabled;
    c.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
    ADC_CommonInit(&c);

    a.ADC_Resolution           = ADC_Resolution_12b;
    a.ADC_ScanConvMode         = DISABLE;
    a.ADC_ContinuousConvMode   = DISABLE;
    a.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;
    a.ADC_ExternalTrigConv     = ADC_ExternalTrigConv_T3_TRGO;
    a.ADC_DataAlign            = ADC_DataAlign_Right;
    a.ADC_NbrOfConversion      = 1;
    ADC_Init(ADC1, &a);
    ADC_RegularChannelConfig(ADC1, ADC_CHANNEL_HYDRO1, 1, ADC_SampleTime_15Cycles);

    d.DMA_Channel            = DMA_Channel_0;
    d.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
    d.DMA_Memory0BaseAddr    = (uint32_t)s_adc_dma_buf;
    d.DMA_DIR                = DMA_DIR_PeripheralToMemory;
    d.DMA_BufferSize         = 2 * SUBBLOCK_LEN;
    d.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    d.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    d.DMA_Mode                = DMA_Mode_Circular;
    d.DMA_Priority             = DMA_Priority_High;
    d.DMA_FIFOMode            = DMA_FIFOMode_Disable;
    d.DMA_FIFOThreshold       = DMA_FIFOThreshold_HalfFull;
    d.DMA_MemoryBurst         = DMA_MemoryBurst_Single;
    d.DMA_PeripheralBurst     = DMA_PeripheralBurst_Single;
    DMA_Init(DMA2_Stream0, &d);
    DMA_ITConfig(DMA2_Stream0, DMA_IT_TC | DMA_IT_HT, ENABLE);
    DMA_Cmd(DMA2_Stream0, ENABLE);

    n.NVIC_IRQChannel                   = DMA2_Stream0_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 1;
    n.NVIC_IRQChannelSubPriority        = 0;
    n.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&n);

    ADC_DMARequestAfterLastTransferCmd(ADC1, ENABLE);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
}
