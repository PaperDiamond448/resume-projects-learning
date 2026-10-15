#include "stm32f4xx.h"

/* 在调试器 Watch 窗口中可观察这两个变量。 */
volatile uint32_t g_milliseconds = 0U;
volatile uint32_t g_led_toggle_count = 0U;

void SysTick_Handler(void)
{
    /* SysTick 每 1 ms 进入一次这里。 */
    g_milliseconds++;
}

int main(void)
{
    GPIO_InitTypeDef led;
    uint32_t last_toggle = 0U;

    SystemCoreClockUpdate();

    /* 第一步：给 GPIOC 开启外设时钟。 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

    /* 第二步：把 PC0 配置成普通推挽输出。 */
    GPIO_StructInit(&led);
    led.GPIO_Pin = GPIO_Pin_0;
    led.GPIO_Mode = GPIO_Mode_OUT;
    led.GPIO_OType = GPIO_OType_PP;
    led.GPIO_Speed = GPIO_Speed_2MHz;
    led.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOC, &led);
    GPIO_ResetBits(GPIOC, GPIO_Pin_0);

    /* 第三步：用内部 16 MHz 时钟产生 1 ms 系统节拍。 */
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U)
    {
        while (1) { /* 配置失败时停在此处，便于调试。 */ }
    }

    while (1)
    {
        /* 每过 500 ms 翻转一次 PC0，得到约 1 秒的亮灭周期。 */
        if ((uint32_t)(g_milliseconds - last_toggle) >= 500U)
        {
            last_toggle = g_milliseconds;
            GPIO_ToggleBits(GPIOC, GPIO_Pin_0);
            g_led_toggle_count++;
        }
        __WFI();
    }
}
