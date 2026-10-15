#include "stm32f4xx.h"

/*
 * 本练习固定使用 HSI 16 MHz，不依赖板上外部晶振的频率。
 * SystemInit 在 C 运行库初始化之前调用，因此不依赖已初始化的全局变量。
 */
uint32_t SystemCoreClock = HSI_VALUE;

void SystemInit(void)
{
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0U) { }

    RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0U) { }
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_CSSON | RCC_CR_HSEON);

    SCB->CPACR |= (0xFUL << 20U);
    SCB->VTOR = FLASH_BASE;
    __DSB();
    __ISB();
}

void SystemCoreClockUpdate(void)
{
    /* 本练习没有运行时切换时钟的功能。 */
    SystemCoreClock = HSI_VALUE;
}
