/************************************************************************
 * 实验六 · 人机交互控制台（旋转编码器 + ADC + OLED + 有源蜂鸣器）
 * 平台：STM32F103C8T6（蓝丸） + 标准外设库(SPL)
 *
 * 本实验的目标：把“传感器读数”变成一个可以操作的嵌入式小设备。
 *
 *   旋转编码器 A/B  → 判断方向、修改参数
 *   旋转编码器 SW   → 切换当前编辑项
 *   第二个电位器   → 模拟一个外部传感器输入（ADC）
 *   OLED            → 显示状态和参数
 *   有源蜂鸣器      → 超过阈值时非阻塞报警
 *   串口            → 输出调试日志
 *
 * 练习的工程思想：
 *   - 编码器 A/B 两相正交解码
 *   - 中断只记录事件，主循环处理参数和显示
 *   - ADC 采样值转换成毫伏
 *   - 用时间戳实现 LED/蜂鸣器非阻塞任务
 *   - 一个参数由编码器修改，另一个输入由电位器模拟
 *
 * 接线见同目录《接线与说明.md》。
 * OLED 驱动沿用实验一：把实验一目录中的 oled.c、oled.h、oledfont.h
 * 加入当前 Keil 工程即可。
 ************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>
#include "oled.h"

/*======================= 引脚定义 =======================*/
#define LED_PORT        GPIOC
#define LED_PIN         GPIO_Pin_13          // 板载LED，低电平点亮

#define POT_PORT        GPIOA
#define POT_PIN         GPIO_Pin_4           // 第二个电位器，ADC1_IN4

#define ENC_PORT        GPIOB
#define ENC_A_PIN       GPIO_Pin_10          // 编码器 CLK/A
#define ENC_B_PIN       GPIO_Pin_11          // 编码器 DT/B
#define ENC_SW_PIN      GPIO_Pin_12          // 编码器按键，按下接GND

#define BUZZ_PORT       GPIOB
#define BUZZ_PIN        GPIO_Pin_1           // 有源蜂鸣器S脚，低电平触发

/*======================= 系统状态 =======================*/
volatile uint32_t g_msTick  = 0;
volatile uint8_t  g_1HzFlag = 0;

/* 编码器中断产生的事件，主循环再消费 */
volatile int8_t   g_encStepEvent = 0;       // 一个机械定位点对应+1或-1
volatile uint8_t  g_buttonFlag   = 0;

/* 0：编辑LED周期；1：编辑报警阈值 */
uint8_t  g_editItem = 0;
uint16_t g_blinkMs  = 500;
uint16_t g_alarmMv  = 1800;
uint16_t g_potMv    = 0;
uint8_t  g_alarmOn  = 0;

/*======================= SysTick：1ms系统节拍 =======================*/
void SysTick_Handler(void)
{
    g_msTick++;
    if (g_msTick % 1000 == 0) g_1HzFlag = 1;
}

/*======================= printf重定向到USART1 =======================*/
int fputc(int ch, FILE *f)
{
    (void)f;
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}

/*======================= 基础初始化 =======================*/
static void BSP_LED_USART_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_USART1,
                           ENABLE);

    /* PC13：板载LED，低电平点亮 */
    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_SetBits(LED_PORT, LED_PIN);

    /* PB1：有源蜂鸣器模块的S脚，低电平触发 */
    gpio.GPIO_Pin   = BUZZ_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(BUZZ_PORT, &gpio);
    GPIO_SetBits(BUZZ_PORT, BUZZ_PIN);       // 默认关闭

    /* USART1：PA9 TX、PA10 RX */
    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_10;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 115200;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);
}

/*======================= ADC：读取第二个电位器 =======================*/
static void ADC_Pot_Init(void)
{
    GPIO_InitTypeDef gpio;
    ADC_InitTypeDef adc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1,
                           ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);        // 72MHz / 6 = 12MHz

    gpio.GPIO_Pin   = POT_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(POT_PORT, &gpio);

    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode      = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv  = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign         = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel      = 1;
    ADC_Init(ADC1, &adc);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_4, 1,
                             ADC_SampleTime_55Cycles5);
    ADC_Cmd(ADC1, ENABLE);

    /* F1系列ADC上电后需要校准 */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

static uint16_t ADC_ReadPotMv(void)
{
    uint16_t raw;

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
    raw = ADC_GetConversionValue(ADC1);
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
    return (uint16_t)((uint32_t)raw * 3300 / 4096);
}

/*======================= 编码器：正交解码 + 按键中断 =======================*/
static uint8_t g_encPrevState = 0;
static int8_t  g_encQuarter   = 0;
static uint32_t g_lastButtonMs = 0;

/*
 * 四相正交解码表。
 * 如果实际旋转方向和屏幕显示相反，把编码器A/B两根线对调，
 * 或者把表中+1与-1的方向整体互换即可。
 */
static const int8_t g_encTable[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static uint8_t Encoder_ReadState(void)
{
    uint8_t a = (GPIO_ReadInputDataBit(ENC_PORT, ENC_A_PIN) != Bit_RESET);
    uint8_t b = (GPIO_ReadInputDataBit(ENC_PORT, ENC_B_PIN) != Bit_RESET);
    return (uint8_t)((a << 1) | b);
}

static void Encoder_EXTI_Init(void)
{
    GPIO_InitTypeDef gpio;
    EXTI_InitTypeDef exti;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO,
                           ENABLE);

    gpio.GPIO_Pin   = ENC_A_PIN | ENC_B_PIN | ENC_SW_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;          // 编码器触点闭合时接地
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ENC_PORT, &gpio);

    g_encPrevState = Encoder_ReadState();

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource10);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource11);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource12);

    exti.EXTI_Line    = EXTI_Line10 | EXTI_Line11 | EXTI_Line12;
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    nvic.NVIC_IRQChannel                   = EXTI15_10_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}

void EXTI15_10_IRQHandler(void)
{
    uint32_t encLines = EXTI_Line10 | EXTI_Line11;

    /* A/B 任意一相变化，就读取两相当前状态 */
    if (EXTI_GetITStatus(EXTI_Line10) != RESET ||
        EXTI_GetITStatus(EXTI_Line11) != RESET)
    {
        uint8_t cur = Encoder_ReadState();
        uint8_t transition = (uint8_t)((g_encPrevState << 2) | cur);
        int8_t delta = g_encTable[transition & 0x0F];

        g_encPrevState = cur;
        g_encQuarter += delta;

        /* 一个定位点通常包含四次有效边沿 */
        if (g_encQuarter >= 4)
        {
            g_encQuarter -= 4;
            g_encStepEvent++;
        }
        else if (g_encQuarter <= -4)
        {
            g_encQuarter += 4;
            g_encStepEvent--;
        }
        EXTI_ClearITPendingBit(encLines);
    }

    /* 编码器按键：中断只消抖并置标志，主循环切换编辑项 */
    if (EXTI_GetITStatus(EXTI_Line12) != RESET)
    {
        if (g_msTick - g_lastButtonMs > 200)
        {
            g_lastButtonMs = g_msTick;
            g_buttonFlag = 1;
        }
        EXTI_ClearITPendingBit(EXTI_Line12);
    }
}

/*======================= 后台任务 =======================*/
static void Task_LED(void)
{
    static uint32_t last = 0;
    uint16_t period = g_alarmOn ? 100 : g_blinkMs;

    if (g_msTick - last >= period)
    {
        last = g_msTick;
        GPIO_WriteBit(LED_PORT, LED_PIN,
                      (BitAction)(1 - GPIO_ReadOutputDataBit(LED_PORT, LED_PIN)));
    }
}

static void Task_Alarm(void)
{
    static uint32_t last = 0;
    static uint8_t phase = 0;
    static uint8_t wasAlarmOn = 0;

    if (!g_alarmOn)
    {
        wasAlarmOn = 0;
        phase = 0;
        GPIO_SetBits(BUZZ_PORT, BUZZ_PIN);     // 低电平触发，置高关闭
        return;
    }

    /* 刚进入报警时先保持静音，建立一个完整的节拍 */
    if (!wasAlarmOn)
    {
        last = g_msTick;
        phase = 0;                             // 0=静音，1=鸣叫
        wasAlarmOn = 1;
        GPIO_SetBits(BUZZ_PORT, BUZZ_PIN);
    }
    /* 900ms停、100ms响，避免蜂鸣器连续长响 */
    else if (phase == 0 && g_msTick - last >= 900)
    {
        last = g_msTick;
        phase = 1;
        GPIO_ResetBits(BUZZ_PORT, BUZZ_PIN);
    }
    else if (phase == 1 && g_msTick - last >= 100)
    {
        last = g_msTick;
        phase = 0;
        GPIO_SetBits(BUZZ_PORT, BUZZ_PIN);
    }
}

static void Task_Encoder(void)
{
    int8_t delta;

    if (g_buttonFlag)
    {
        g_buttonFlag = 0;
        g_editItem = (uint8_t)!g_editItem;
        printf("[UI] edit=%s\r\n", g_editItem ? "ALARM" : "LED");
    }

    /* 取走中断产生的步进事件 */
    __disable_irq();
    delta = g_encStepEvent;
    g_encStepEvent = 0;
    __enable_irq();

    if (delta == 0) return;

    if (g_editItem == 0)
    {
        int32_t value = (int32_t)g_blinkMs + (int32_t)delta * 50;
        if (value < 100)  value = 100;
        if (value > 1000) value = 1000;
        g_blinkMs = (uint16_t)value;
    }
    else
    {
        int32_t value = (int32_t)g_alarmMv + (int32_t)delta * 100;
        if (value < 300)  value = 300;
        if (value > 3200) value = 3200;
        g_alarmMv = (uint16_t)value;
    }
}

static void Task_ADC(void)
{
    static uint32_t last = 0;

    if (g_msTick - last >= 50)
    {
        last = g_msTick;
        g_potMv = ADC_ReadPotMv();
        g_alarmOn = (g_potMv >= g_alarmMv) ? 1 : 0;
    }
}

static void Task_OLED(void)
{
    static uint32_t last = 0;

    if (g_msTick - last < 200) return;
    last = g_msTick;

    OLED_ShowString(0, 0, "F103 LAB6       ");

    if (g_editItem == 0)
    {
        OLED_ShowString(0, 2, "SET:LED         ");
        OLED_ShowNum(64, 2, g_blinkMs, 4);
        OLED_ShowString(104, 2, "ms");
    }
    else
    {
        OLED_ShowString(0, 2, "SET:ALARM       ");
        OLED_ShowNum(64, 2, g_alarmMv, 4);
        OLED_ShowString(104, 2, "mV");
    }

    OLED_ShowString(0, 4, "POT:");
    OLED_ShowNum(32, 4, g_potMv, 4);
    OLED_ShowString(72, 4, "mV       ");

    if (g_alarmOn) OLED_ShowString(0, 6, "ALARM:ON        ");
    else           OLED_ShowString(0, 6, "ALARM:OFF       ");

}

static void Task_HeartBeat(void)
{
    if (g_1HzFlag)
    {
        g_1HzFlag = 0;
        printf("[HB] t=%lus edit=%s blink=%ums pot=%umV alarmTh=%umV alarm=%s\r\n",
               (unsigned long)(g_msTick / 1000),
               g_editItem ? "ALARM" : "LED",
               g_blinkMs, g_potMv, g_alarmMv,
               g_alarmOn ? "ON" : "OFF");
    }
}

/*======================= 主函数 =======================*/
int main(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    BSP_LED_USART_Init();
    ADC_Pot_Init();
    Encoder_EXTI_Init();
    OLED_Init();

    OLED_ShowString(0, 0, "F103 LAB6       ");
    OLED_ShowString(0, 2, "ROTARY CONSOLE  ");
    OLED_ShowString(0, 4, "PRESS TO EDIT   ");
    OLED_ShowString(0, 6, "BOOT OK         ");

    printf("\r\n==== F103 Lab6: Human-Machine Console ====\r\n");
    printf("Encoder PB10/PB11/PB12, pot PA4, buzzer PB1\r\n");

    while (1)
    {
        Task_Encoder();
        Task_ADC();
        Task_LED();
        Task_Alarm();
        Task_OLED();
        Task_HeartBeat();
    }
}
