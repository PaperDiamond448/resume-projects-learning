/************************************************************************
 * 实验五 · 阵列信号处理入门：2元阵 TDOA 声源方向估计
 * 平台：STM32F103C8T6（蓝丸） + 标准外设库(SPL)
 *
 * 为什么做这个实验：
 *   你的毕设（短基线信标）是"发射端"，而短基线定位系统真正的数学核心在"接收端"——
 *   多个阵元(水听器)测同一个波前的到达时间差(TDOA)，反解出方位/位置。
 *   这个实验就是把那套数学，用2个声音传感器缩小复刻一遍：
 *
 *   声源发一声脉冲 → 阵元A、阵元B 先后"听到" → 到达时刻差 Δt = tB - tA
 *   sin(θ) = c·Δt / d      (θ: 相对阵列法线方向的入射角; d: 阵元间距; c: 声速)
 *
 *   这就是阵列信号处理里最基础的"远场平面波 + 均匀线阵(ULA)"测向公式，
 *   短基线定位、麦克风阵列测向、雷达/声呐 DOA 估计，用的都是这同一套几何关系，
 *   区别只是"用时间差"还是"用相位差"去解这个 Δt。
 *
 * 硬件：
 *   声音传感器模块(比较器型，带 DO 数字输出) × 2，固定在直尺/木条上，间距 d 实测量出
 *     DO(A) → PA0 (EXTI0)      DO(B) → PA1 (EXTI1)
 *     VCC → 3.3V   GND → GND（两个模块都接）
 *   声源：现有的蜂鸣器，或直接拍手/敲桌子，正对阵列前方
 *   USB转TTL：看串口打印结果（TXD→PA10, RXD→PA9, GND，同前面几个实验）
 *
 * 测时原理：
 *   TIM2 配成 1MHz 自由计数的"秒表"（每 1 微秒走一格，16位，约 65.5ms 绕一圈）。
 *   两个 DO 脚各自接一路外部中断，一触发就在中断里记下当时的 TIM2->CNT，
 *   两路都到齐后在主循环里做减法、算 Δt、反解角度、打印。
 *
 * 这不是精密仪器，是"能体现原理"的教学实验，几个天然的误差来源恰好就是
 * 阵列信号处理里"阵元标定/失配"要讨论的东西，实验最后有专门的分析小节。
 ************************************************************************/
#include "stm32f10x.h"
#include <stdio.h>
#include <math.h>

/*======================= 引脚 =======================*/
#define LED_PORT     GPIOC
#define LED_PIN      GPIO_Pin_13

#define MIC_A_PIN    GPIO_Pin_0        // EXTI0，PA0
#define MIC_B_PIN    GPIO_Pin_1        // EXTI1，PA1

/*======================= 阵列参数（按实际摆放改这两个数） =======================*/
#define ARRAY_D_MM       150            // 两个传感器间距(毫米)，拿尺子实测后改这里
#define SOUND_SPEED_MPS  343.0f         // 声速(米/秒)，20℃左右空气中的近似值

/*======================= 系统状态 =======================*/
volatile uint32_t g_msTick  = 0;
volatile uint8_t  g_1HzFlag = 0;

void SysTick_Handler(void)
{
    g_msTick++;
    if (g_msTick % 1000 == 0) g_1HzFlag = 1;
}

/*======================= printf 重定向（USART1） =======================*/
int fputc(int ch, FILE *f)
{
    (void)f;
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}

/*===================================================================*/
/*  一、麦克风"听到了"的时间戳捕获：EXTI + TIM2 自由计数              */
/*===================================================================*/
typedef struct {
    volatile uint8_t  got;      // 这一路是否已经记录到一次触发，等待和另一路配对
    volatile uint16_t tCnt;     // 触发瞬间的 TIM2 计数值（微秒级"秒表"读数）
    volatile uint32_t tMs;      // 触发瞬间的毫秒时间戳，只用来判断"是不是同一次声音事件"
} MicCapture_t;

volatile MicCapture_t g_micA = {0, 0, 0};
volatile MicCapture_t g_micB = {0, 0, 0};

// 纯调试用：各自累计触发次数，跟测向解算无关，只用来现场确认"这一路到底有没有反应"
volatile uint32_t g_cntA = 0, g_cntB = 0;

#define PAIR_WINDOW_MS   50   // 两路触发若相隔超过这个时间，判定不是同一次声音事件，丢弃重新等待

// TIM2：不产生中断，纯粹当一个 1MHz 自由计数的"秒表"用，EXTI 里直接读它的瞬时值
static void TIM2_FreeRunInit(void)
{
    TIM_TimeBaseInitTypeDef tb;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);//F103 里 TIM2 挂在 APB1 总线上

    tb.TIM_Prescaler     = 72 - 1;      // 72MHz / 72 = 1MHz，每计数1格=1微秒
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    tb.TIM_Period        = 0xFFFF;      // 16位最大，自由跑，溢出自动回0（约65.5ms绕一圈）
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &tb);
    TIM_Cmd(TIM2, ENABLE);
}

// 两路麦克风 DO 脚配成外部中断
// 注意"极性"：多数比较器型声音传感器模块空闲=低电平、检测到声音瞬间跳高电平，
// 这里先按"低电平常态 + 上升沿触发"配置；如果实测发现触发计数一直不动，
// 大概率是你这批模块极性相反，把 GPIO_Mode_IPD 换成 GPIO_Mode_IPU、
// EXTI_Trigger_Rising 换成 EXTI_Trigger_Falling 就行，接线与说明.md 里有讲怎么测。
/*这段代码是给两路麦克风(声音传感器)配置外部中断,目的是在"检测到声音的那一瞬间"立刻被 CPU 感知到——这通常是做"声源定位"或"到达时间差(TDOA)测量"的基础模块:两个麦克风谁先响,响的时间差多少,就能算出声源方向。*/
static void Mic_EXTI_Init(void)
{
    GPIO_InitTypeDef gpio;
    EXTI_InitTypeDef exti;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    gpio.GPIO_Pin  = MIC_A_PIN | MIC_B_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPD;             // 下拉输入：空闲拉低，模块触发时给高电平
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);

    exti.EXTI_Line    = EXTI_Line0 | EXTI_Line1;
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Rising;    // 听到声音的那一瞬间触发
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    nvic.NVIC_IRQChannel                   = EXTI0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;  // 两路麦克风优先级要一致，谁先到就先记，不能互相打断导致偏差
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    nvic.NVIC_IRQChannel = EXTI1_IRQn;
    NVIC_Init(&nvic);
}

// 麦克风A：听到声音——中断里只做"记时刻"这一件事，尽量快、尽量准
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)//判断是不是 EXTI0 产生的中断
    {
        EXTI_ClearITPendingBit(EXTI_Line0);/*外部中断触发后，STM32 会把对应的挂起标志位置 1。

处理完后必须清除，否则退出中断后，处理器可能再次进入同一个中断函数。*/
        g_cntA++;//每当麦克风 A 产生一次外部中断，计数器 g_cntA 就加 1。
        if (!g_micA.got) { g_micA.tCnt = (uint16_t)TIM2->CNT; g_micA.tMs = g_msTick; g_micA.got = 1; }
    }//等价于if (g_micA.got == 0)，意思是如果记录过了就不记录了，只记录没记录过的。因为一次拍手可能产生好几次上升和下降边沿，我们只取最早的那一次的
}//g_micA.tCnt = (uint16_t)TIM2->CNT记录定时器的精细时间，
// 麦克风B：听到声音
void EXTI1_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line1) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line1);
        g_cntB++;
        if (!g_micB.got) { g_micB.tCnt = (uint16_t)TIM2->CNT; g_micB.tMs = g_msTick; g_micB.got = 1; }
    }
}

/*===================================================================*/
/*  二、骨架：LED / USART                                             */
/*===================================================================*/
static void BSP_LED_USART_Init(void)
{
    GPIO_InitTypeDef gpio; USART_InitTypeDef u;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin = LED_PIN; gpio.GPIO_Mode = GPIO_Mode_Out_PP; gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(LED_PORT, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_9; gpio.GPIO_Mode = GPIO_Mode_AF_PP; gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);                              // PA9 = TX

    u.USART_BaudRate = 115200; u.USART_WordLength = USART_WordLength_8b;
    u.USART_StopBits = USART_StopBits_1; u.USART_Parity = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode = USART_Mode_Tx;
    USART_Init(USART1, &u);
    USART_Cmd(USART1, ENABLE);
}

/*===================================================================*/
/*  三、打印用的定点数格式化——绕开 Keil MicroLIB 的 printf 浮点坑     */
/*      （原理和实验三的整数打印一样：内部用 float 算，只是打印前     */
/*        手动转成"整数部分.小数部分"，不依赖 printf 的 %f）          */
/*===================================================================*/
static void PrintFixed(int32_t scaled, int32_t scale)   // scaled已经乘过scale倍，scale=1000表示打印3位小数
{
    int32_t whole, frac;//整数部分和小数部分，scale表示之前放大了多少倍，同时决定保留几位小数
    if (scaled < 0) { printf("-"); scaled = -scaled; }
    whole = scaled / scale;
    frac  = scaled % scale;
    if      (scale == 1000) printf("%ld.%03ld", (long)whole, (long)frac);
    else if (scale == 10)   printf("%ld.%ld",   (long)whole, (long)frac);
    else                    printf("%ld", (long)whole);
}

/*===================================================================*/
/*  四、配对与解算：两路都到齐才算一次事件                            */
/*===================================================================*/
static void ProcessCaptureIfReady(void)
{
    int16_t  dCnt;
    float    dtSec, sinTheta, thetaDeg;

    // 只有一路到、等太久还没等到另一路——判定这次触发和"另一路"没关系，超时丢弃
    if (g_micA.got && !g_micB.got && (g_msTick - g_micA.tMs) > PAIR_WINDOW_MS) { g_micA.got = 0; return; }
    if (g_micB.got && !g_micA.got && (g_msTick - g_micB.tMs) > PAIR_WINDOW_MS) { g_micB.got = 0; return; }

    if (!(g_micA.got && g_micB.got)) return;   // 两路都到齐才继续

    if ((g_micA.tMs > g_micB.tMs ? g_micA.tMs - g_micB.tMs : g_micB.tMs - g_micA.tMs) > PAIR_WINDOW_MS)
    {
        printf("[discard] A/B 相隔>%dms，判定不是同一次事件\r\n", PAIR_WINDOW_MS);
    }
    else
    {
        // (uint16_t)相减自动处理"TIM2 计数跨越65536回卷"这一次的情况，
        // 只要真实的到达时间差远小于65536us（本实验d最多几十厘米，最大延时才几百微秒），就不会出错
        dCnt  = (int16_t)(g_micB.tCnt - g_micA.tCnt);
        dtSec = (float)dCnt * 1e-6f;                       // 微秒转秒

        sinTheta = (SOUND_SPEED_MPS * dtSec) / (ARRAY_D_MM * 1e-3f);

        printf("A_t=%uus B_t=%uus dt=%dus  ", g_micA.tCnt, g_micB.tCnt, dCnt);

        if (sinTheta > 1.0f || sinTheta < -1.0f)
        {
            printf("|sinTheta|>1（超出物理范围），多半是阈值抖动，丢弃本次\r\n");
        }
        else
        {
            thetaDeg = asinf(sinTheta) * (180.0f / 3.14159265f);
            printf("sinTheta="); PrintFixed((int32_t)(sinTheta * 1000.0f), 1000);
            printf("  angle="); PrintFixed((int32_t)(thetaDeg * 10.0f), 10);
            printf("deg (%s)\r\n", dCnt > 0 ? "偏向A侧" : (dCnt < 0 ? "偏向B侧" : "正前方"));
        }
    }

    g_micA.got = 0; g_micB.got = 0;   // 复位，等下一次声音事件
}

/*===================================================================*/
int main(void)
{
    uint32_t lastBlink = 0;

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    BSP_LED_USART_Init();
    TIM2_FreeRunInit();
    Mic_EXTI_Init();

    printf("\r\n==== F103 Lab5: 2-Element TDOA Direction Finding ====\r\n");
    printf("d=%dmm, c=343m/s, timer=1MHz(1us/tick)\r\n", ARRAY_D_MM);
    printf("拍手/敲桌子/触发蜂鸣器 试试，正对阵列中垂线附近角度应接近0\r\n");

    while (1)
    {
        // LED 心跳闪烁，串口不开也能看板子是不是活着
        if (g_msTick - lastBlink >= 500)
        {
            lastBlink = g_msTick;
            GPIO_WriteBit(LED_PORT, LED_PIN, (BitAction)(1 - GPIO_ReadOutputDataBit(LED_PORT, LED_PIN)));
        }

        if (g_1HzFlag)   // 每秒打印一次两路的"累计触发次数"——先用这个确认两个麦克风各自都有反应
        {
            g_1HzFlag = 0;
            printf("[HB] t=%lus  triggerCount A=%lu B=%lu\r\n",
                   (unsigned long)(g_msTick / 1000), (unsigned long)g_cntA, (unsigned long)g_cntB);
        }

        ProcessCaptureIfReady();   // 两路都到齐就解算一次方位角
    }
}
