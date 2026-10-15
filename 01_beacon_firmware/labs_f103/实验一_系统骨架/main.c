/************************************************************************
 * 实验一 · 系统骨架（前后台架构 + 中断 + 心跳 + CPU占用率）
 * 平台：STM32F103C8T6（蓝丸） + 标准外设库(SPL)
 *
 * 复刻信标固件的“骨架”：
 *   - while(1) 主循环 + 标志位延迟执行 ...... 对应 USER/main.c 主循环
 *   - SysTick 1Hz 心跳 ...................... 对应 OnEchoRTC() 心跳链
 *   - 主循环空转计数算 CPU 占用率 ........... 对应 m_uCpuIdle / m_fCpuIdle
 *   - 按键中断只置标志、后台干活 ............ 无 RTOS 系统的任务调度精髓
 *   - LED 非阻塞闪烁 ........................ 对应 LedShine()
 *   - printf 重定向到串口 .................. 对应 DEBUG_Comm 调试串口
 *
 * 接线见同目录《接线与说明.md》。Keil 记得勾选 Use MicroLIB。
 ************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>

/*======================= 引脚定义（统一引脚表的子集）=======================*/
#define LED_PORT        GPIOC
#define LED_PIN         GPIO_Pin_13          // 板载 LED，低电平点亮
#define KEY_PORT        GPIOA
#define KEY_PIN         GPIO_Pin_0           // 按键，按下接 GND（下降沿）

/*======================= 全局状态（对应固件的全局标志/计数器）=============*/
// volatile：这些变量在中断里被改、在主循环里被读，必须加 volatile 防编译器优化
volatile uint32_t g_msTick      = 0;         // 毫秒计数，SysTick 每 1ms +1（相当于固件的系统节拍）
volatile uint8_t  g_1HzFlag     = 0;         // 1Hz 心跳标志，SysTick 每满 1000ms 置 1
volatile uint8_t  g_bKeyFlag    = 0;         // 按键事件标志：中断只置位，主循环来处理
volatile uint32_t g_lastKeyMs   = 0;         // 上次按键时刻，用于软件消抖

uint32_t g_loopCnt    = 0;                   // 主循环每转一圈 +1（一秒内能转多少圈 = 有多空闲）
uint32_t g_loopMax    = 1;                   // 记录“最空闲”时每秒的圈数，作为 100% 空闲基准
uint32_t g_blinkMs    = 500;                 // LED 闪烁周期，可被按键切换

/*======================= printf 重定向到 USART1 =======================*/
int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}

/*======================= 初始化：时钟/GPIO/串口/中断 =======================*/
static void BSP_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_AFIO,  ENABLE);//使能 AFIO 时钟（打开 AFIO 外设的时钟）。
                           /*AFIO 主要功能：

引脚重映射（Remap）
把某些外设的功能（USART、I2C、TIM、SPI 等）映射到不同的 GPIO 引脚上。
外部中断引脚配置（EXTI）
配置 GPIO 作为外部中断源时，需要通过 AFIO 来选择哪个引脚连接到哪个中断线。
JTAG/SWD 调试引脚复用
经常需要通过 AFIO 把 JTAG 引脚释放成普通 IO 使用。*/

    // LED：推挽输出
    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_SetBits(LED_PORT, LED_PIN);         // 先熄灭（高电平灭）

    // 按键：上拉输入（按下接地 → 读到低电平）
    gpio.GPIO_Pin  = KEY_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(KEY_PORT, &gpio);
}

static void BSP_USART1_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    // PA9 = TX，复用推挽
    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;//USART1 的 TX（发送） 功能是 外设复用功能，不是普通 GPIO 输出
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    // PA10 = RX，浮空输入（本实验只打印，RX 留给实验二）
    gpio.GPIO_Pin  = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入 = 没有内部上拉也没有内部下拉，引脚处于高阻状态，外部电平完全由外界电路决定  串口通信中，通常外部设备（电脑、USB转TTL）会提供信号电平，所以不需要 STM32 内部上拉或下拉。
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 115200;//波特率
    usart.USART_WordLength          = USART_WordLength_8b;//数据位8位
    usart.USART_StopBits            = USART_StopBits_1;//停止位1位
    usart.USART_Parity              = USART_Parity_No;//无奇偶校验
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//不使用硬件流控制
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;//同时使能发送和接收模式
    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);
}

static void BSP_KeyEXTI_Init(void)
{
    EXTI_InitTypeDef exti;
    NVIC_InitTypeDef nvic;

    // 优先级分组 2：2 位抢占 + 2 位子优先级（和真固件 Nvic.c 一致）
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    // PA0 → EXTI0
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    exti.EXTI_Line    = EXTI_Line0;
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Falling;   // 按下 = 下降沿
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    nvic.NVIC_IRQChannel                   = EXTI0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;  // 抢占 2（比心跳高、比关键任务低）
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}

/*======================= 中断服务程序（前台）=======================*/
// SysTick：系统心跳源，每 1ms 进一次
void SysTick_Handler(void)
{
    g_msTick++;
    if (g_msTick % 1000 == 0)                 // 每满 1 秒，置心跳标志（交给后台处理，ISR 本身只做最少的事）
        g_1HzFlag = 1;
}

// 按键外部中断：只做消抖 + 置标志，绝不在这里干重活（这是核心思想！）
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)//只要第一次中断来了，立刻记录时间戳 g_lastKeyMs，然后接下来200ms内所有的中断（包括后面的抖动和真正的第二次按键）全部忽略。
    {
        if (g_msTick - g_lastKeyMs > 200)     // 200ms 软件消抖 g_msTick	系统当前时间 - g_lastKeyMs	上次按键时间 > 200ms 就认为是有效按键事件
        {
            g_lastKeyMs = g_msTick;
            g_bKeyFlag  = 1;                  // 通知后台：有人按了键
        }
        EXTI_ClearITPendingBit(EXTI_Line0);
    }
}

/*======================= 后台任务（主循环里调用）=======================*/
// 非阻塞闪灯：不用 delay，靠比对时间戳翻转（对应固件 LedShine 不堵塞主循环）
static void Task_LedShine(void)
{
    static uint32_t last = 0;
    if (g_msTick - last >= g_blinkMs)
    {
        last = g_msTick;
        GPIO_WriteBit(LED_PORT, LED_PIN,
                      (BitAction)(1 - GPIO_ReadOutputDataBit(LED_PORT, LED_PIN)));
    }
}

// 处理按键事件：在后台切换闪烁速度（中断置标志、后台执行的完整闭环）
static void Task_OnKey(void)
{
    if (g_bKeyFlag)
    {
        g_bKeyFlag = 0;
        g_blinkMs  = (g_blinkMs == 500) ? 100 : 500;
        printf(">> KEY pressed, blink -> %ums\r\n", g_blinkMs);
    }
}

// 心跳任务：每秒算一次 CPU 占用率并打印（对应固件 OnEchoRTC 里的负载统计）
static void Task_HeartBeat(void)
{
    if (g_1HzFlag)//每秒被 Systick 中断置1一次
    {
        uint32_t cnt = g_loopCnt;            // 取出这一秒主循环转了多少圈
        uint32_t use;
        g_loopCnt = 0;//归零，开始下一秒
        g_1HzFlag = 0;

        if (cnt > g_loopMax) g_loopMax = cnt; // 圈数越多 = 越空闲，记下历史最大值当“满空闲”基准
        // CPU 占用率(%) = 100 - 空闲率；空闲率 = 本秒圈数 / 满空闲圈数
        use = 100 - (uint32_t)((uint64_t)cnt * 100 / g_loopMax);

        printf("[HeartBeat] t=%us  SysClk=%uMHz  CPU=%u%%  LED=%ums\r\n",
               g_msTick / 1000,
               (unsigned)(SystemCoreClock / 1000000),
               use, g_blinkMs);
    }
}

/*======================= 主函数（后台）=======================*/
int main(void)
{
    // SystemInit() 已在启动文件里调过，SystemCoreClock 反映当前真实主频
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);  // 1ms 心跳节拍

    BSP_GPIO_Init();
    BSP_USART1_Init();
    BSP_KeyEXTI_Init();

    printf("\r\n==== F103 Lab1: System Skeleton boot ok ====\r\n");

    // 前后台结构：中断在前台抢着处理事件，主循环在后台慢慢干活
    while (1)
    {
        Task_LedShine();      // 非阻塞灯效
        Task_OnKey();         // 处理按键事件（标志位驱动）
        Task_HeartBeat();     // 每秒心跳 + CPU 占用率

        g_loopCnt++;          // 主循环空转计数——这一句就是 CPU 占用率的“尺子”
    }
}
