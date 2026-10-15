/************************************************************************
 * 实验二 · 上位机协议（串口组帧状态机 + 命令分发）
 * 平台：STM32F103C8T6（蓝丸） + 标准外设库(SPL)
 *
 * 复刻信标固件那条"命令链"：
 *   USART中断逐字节接收 → 组帧状态机 Comm_GetFrameMsg → 命令分发 OnEchoPCLINK
 *   ┌─────────────┬──────────────────────────┬──────────────────────┐
 *   │ 本实验       │ 对应真固件                 │ 文件                  │
 *   ├─────────────┼──────────────────────────┼──────────────────────┤
 *   │ COMM_OBJECT │ COMM_OBJECT 结构体         │ Comm_operate.h       │
 *   │ 组帧状态机   │ Comm_GetFrameMsg          │ Comm_operate.c       │
 *   │ USART1中断   │ USART2_IRQHandler         │ stm32f4xx_it.c       │
 *   │ 命令分发switch│ OnEchoPCLINK 的大switch   │ main.c               │
 *   │ 广播地址0    │ nAddr==0||==g_stSys.nAddr │ main.c               │
 *   └─────────────┴──────────────────────────┴──────────────────────┘
 *
 * 协议（和真固件一样）：$地址,命令号,参数1,参数2*
 *   地址=0 为广播(所有设备都响应)；否则要等于本机地址才响应
 *
 * 在实验一骨架(心跳/按键/LED/CPU占用)之上，叠加串口命令收发——滚雪球。
 *
 * 用串口助手(115200,8N1)发(整条带上 $ 和 *)：
 *   $1,1,100,0*  → 设LED闪烁周期=100ms
 *   $1,2,0,0*    → 查询状态(回 运行秒数/CPU/LED模式)
 *   $1,3,1,0*    → LED常亮(p1: 0灭 1常亮 2闪)
 *   $0,3,2,0*    → 广播(地址0)：所有设备LED都改成闪
 ************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>

/*======================= 引脚定义 =======================*/
#define LED_PORT   GPIOC
#define LED_PIN    GPIO_Pin_13          // 板载 LED，低电平点亮
#define KEY_PORT   GPIOA
#define KEY_PIN    GPIO_Pin_0           // 按键，按下接 GND（下降沿）

/*======================= 通信对象：复刻固件 COMM_OBJECT ===============*/
// 用一个结构体把"一个串口的全部状态"打包起来 —— 这就是 C 语言里的"面向对象"
#define COMM_BUF_SIZE  64
typedef struct
{
    uint8_t  rxFlag;                    // 0=空闲 / 1=正在接收一帧（$之后、*之前）
    uint8_t  ready;                     // 1=收齐一帧完整命令，等主循环处理
    uint16_t idx;                       // 当前写入位置
    char     buf[COMM_BUF_SIZE];        // 帧内容缓冲（不含 $ 和 *）
} COMM_OBJECT;

COMM_OBJECT g_comm;                     // 一个串口 = 一个对象实例（真固件有 m_pComm1~6 共6个）

/*======================= 命令号（对应固件 Device_Cmd_xxx）============*/
#define CMD_SET_BLINK   1               // 设置 LED 闪烁周期
#define CMD_GET_STATUS  2               // 查询运行状态
#define CMD_SET_LED     3               // LED 模式：0灭 1常亮 2闪烁

/*======================= 系统状态（实验一骨架 + 实验二新增）==========*/
volatile uint32_t g_msTick    = 0;      // 毫秒节拍
volatile uint8_t  g_1HzFlag   = 0;      // 1Hz 心跳标志
volatile uint8_t  g_bKeyFlag  = 0;      // 按键事件标志
volatile uint32_t g_lastKeyMs = 0;      // 上次按键时刻（消抖）

uint32_t g_loopCnt = 0, g_loopMax = 1;  // CPU 占用率测量
uint8_t  g_cpuUse  = 0;                 // 最近一次算出的 CPU 占用率(%)
uint32_t g_blinkMs = 500;               // LED 闪烁周期
uint8_t  g_ledMode = 2;                 // LED 模式：0灭 1常亮 2闪烁
uint8_t  g_myAddr  = 1;                 // 本机地址（广播地址0除外）
uint32_t g_linkIdle = 0;                // 多久没收到命令了（演示"通信即活性证明"）

/*======================= printf 重定向到 USART1 =====================*/
int fputc(int ch, FILE *f)// 重定向 printf 到 USART1，就不是打印到屏幕上了，就是把输出路径改到了串口
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}

/*======================= 组帧状态机（照搬固件 Comm_GetFrameMsg）======*/
// 每收到 1 个字节就喂进来；凑齐一帧（$...*）返回 1
// 关键：不是一次性收完整串，而是"来一个字节处理一个字节"——串口接收的标准做法
uint8_t Comm_GetFrameMsg(char rx, COMM_OBJECT* c)
{
    if (rx == '$')                                      // 帧头：复位缓冲，开始接收
    {
        c->rxFlag = 1; c->idx = 0; c->ready = 0;
    }
    else if (c->rxFlag == 1 && rx != '*' && c->idx < COMM_BUF_SIZE - 1)
    {
        c->buf[c->idx++] = rx;                          // 帧内容：逐字节存进缓冲
    }
    else if (c->rxFlag == 1 && rx == '*')               // 帧尾：收尾，整帧就绪
    {
        c->buf[c->idx] = '\0';                          // 补字符串结束符，方便 sscanf
        c->rxFlag = 0; c->ready = 1;
    }

    if (c->idx >= COMM_BUF_SIZE - 1)                    // 防溢出：太长就丢弃重来
    {
        c->rxFlag = 0; c->idx = 0; c->ready = 0;
    }
    return c->ready;//收进 c->buf = "1,2,500,0"
}

/*======================= 命令分发（对应固件 OnEchoPCLINK 大switch）===*/
void Comm_OnFrame(COMM_OBJECT* c)
{
    int addr, cmd, p1 = 0, p2 = 0, n;

    c->ready = 0;                                       // 取出后立刻清标志
    // 解析 "地址,命令,参数1,参数2"（对应固件 PCLINK_DecodeCmd 用 sscanf）
    n = sscanf(c->buf, "%d,%d,%d,%d", &addr, &cmd, &p1, &p2);//sscanf 从 c->buf（比如 "1,2,500,0"）里按逗号分隔提取 4 个整数
    if (n < 2) { printf("$ERR,bad frame:[%s]*\r\n", c->buf); return; }
    //n 是成功提取的个数。如果少于 2 个（至少要有地址和命令），说明格式错误。

    // 地址过滤：广播(0) 或 本机地址 才响应（对应固件 nAddr==0 || nAddr==g_stSys.nAddr）
    if (!(addr == 0 || addr == g_myAddr)) return;

    g_linkIdle = 0;                                     // 收到合法命令 = 上位机还连着（对应固件清看门狗）
//相当于在喂狗，因为下面的主循环在不断累加，收到命令就清零
    switch (cmd)                                        // ← 这就是命令分发表
    {
    case CMD_SET_BLINK:
        if (p1 >= 10) { g_blinkMs = p1; g_ledMode = 2; }
        printf("$%d,%d,ok,blink=%ums*\r\n", g_myAddr, cmd, g_blinkMs);
        break;

    case CMD_GET_STATUS:
        printf("$%d,%d,t=%us,cpu=%u%%,led=%u,blink=%ums*\r\n",
               g_myAddr, cmd, g_msTick / 1000, g_cpuUse, g_ledMode, g_blinkMs);
        break;

    case CMD_SET_LED:
        if (p1 <= 2) g_ledMode = p1;                   // 0灭 1常亮 2闪
        printf("$%d,%d,ok,led=%u*\r\n", g_myAddr, cmd, g_ledMode);
        break;

    default:
        printf("$%d,%d,unknown cmd*\r\n", g_myAddr, cmd);
        break;
    }
}

/*======================= 初始化 =====================================*/
static void BSP_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
//AFIO：复用功能时钟
    gpio.GPIO_Pin = LED_PIN; gpio.GPIO_Mode = GPIO_Mode_Out_PP; gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_SetBits(LED_PORT, LED_PIN);//输出设备需要规定一个初始状态
//一般来说默认都是灭灯，再加上这里给的是高电平，所以应该是低电平亮灯
    gpio.GPIO_Pin = KEY_PIN; gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(KEY_PORT, &gpio);
}

// USART1：这次要把 RX（接收）真正用起来 —— 开接收中断
static void BSP_USART1_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_9;  gpio.GPIO_Mode = GPIO_Mode_AF_PP;       gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);                                              // PA9  = TX
    gpio.GPIO_Pin = GPIO_Pin_10; gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);                                              // PA10 = RX

    usart.USART_BaudRate = 115200; usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1; usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &usart);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);       // ★ 使能"收到一个字节"中断
    //相当于你把“RXNE 标志位”这根线，接到了“CPU 中断线”上。
    USART_Cmd(USART1, ENABLE);

    nvic.NVIC_IRQChannel = USART1_IRQn;                  // ★ NVIC 里打开 USART1 中断
    nvic.NVIC_IRQChannelPreemptionPriority = 2;          // 抢占2/子2（和真固件 USART2 一致）
    nvic.NVIC_IRQChannelSubPriority = 2;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

static void BSP_KeyEXTI_Init(void)
//EXTI是一根“信号延长线”，负责把 GPIO 引脚上的电平变化（上升沿/下降沿）转成中断信号，送给 CPU。USART为什么不需要因为USART自己就可以产生中断
//STM32 支持 16 个 EXTI 线（0~15），每个线可以连接一个 GPIO 引脚
{
    EXTI_InitTypeDef exti;
    NVIC_InitTypeDef nvic;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);      // 优先级分组2（整个系统调一次）

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    exti.EXTI_Line = EXTI_Line0; exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Falling; exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    nvic.NVIC_IRQChannel = EXTI0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

/*======================= 中断服务程序（前台）========================*/
void SysTick_Handler(void)//因为 g_msTick 每 1 毫秒加 1 次，而我们需要的是“每 1 秒（1000 毫秒）产生一个标志”。
{
    g_msTick++;
    if (g_msTick % 1000 == 0) g_1HzFlag = 1;
}

// USART1 接收中断：每来 1 字节，喂给组帧机。ISR 只做"喂字节"，处理留给主循环
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        char c = (char)USART_ReceiveData(USART1);
        Comm_GetFrameMsg(c, &g_comm);                    // 组好一帧时 g_comm.ready 会被置1
    }
    // 清错误标志，防止 ORE 溢出把串口卡死（真固件每个串口中断末尾都做这件事）
    USART_ClearFlag(USART1, USART_FLAG_ORE | USART_FLAG_FE | USART_FLAG_NE);
}

void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        if (g_msTick - g_lastKeyMs > 200) { g_lastKeyMs = g_msTick; g_bKeyFlag = 1; }
        EXTI_ClearITPendingBit(EXTI_Line0);
    }
}

/*======================= 后台任务（主循环里调用）====================*/
// LED 按模式工作：0灭 1常亮 2闪
static void Task_Led(void)
{
    static uint32_t last = 0;
    if (g_ledMode == 0)       GPIO_SetBits(LED_PORT, LED_PIN);      // 灭（高电平）
    else if (g_ledMode == 1)  GPIO_ResetBits(LED_PORT, LED_PIN);    // 常亮（低电平）
    else if (g_msTick - last >= g_blinkMs)                          // 闪
    {
        last = g_msTick;
        GPIO_WriteBit(LED_PORT, LED_PIN, (BitAction)(1 - GPIO_ReadOutputDataBit(LED_PORT, LED_PIN)));
    }
}

// 按键：循环切换 LED 模式 灭→常亮→闪
static void Task_OnKey(void)
{
    if (g_bKeyFlag)
    {
        g_bKeyFlag = 0;
        g_ledMode = (g_ledMode + 1) % 3;
        printf(">> KEY: led mode -> %u\r\n", g_ledMode);
    }
}

// 处理收到的命令帧（标志位驱动：中断收齐帧置ready，后台来处理）
static void Task_OnComm(void)
{
    if (g_comm.ready) Comm_OnFrame(&g_comm);
}

// 心跳 + CPU占用率（实验一逻辑），并演示"通信掉线检测"
static void Task_HeartBeat(void)
{
    if (g_1HzFlag)
    {
        uint32_t cnt = g_loopCnt;//记录过去 1 秒内主循环执行次数
        g_loopCnt = 0; g_1HzFlag = 0;//清零为了下一秒做准备
        if (cnt > g_loopMax) g_loopMax = cnt;//更新历史最高
        g_cpuUse = 100 - (uint8_t)((uint64_t)cnt * 100 / g_loopMax);//cnt主循环一直在跑，所以可不是0-1之间的数字哦

        if (g_linkIdle < 0xFFFF) g_linkIdle++;           // 每秒+1；收到命令时清0
        // 心跳打印（带"上位机是否在线"：超过5秒没命令就标 offline）
        printf("[HB] t=%us cpu=%u%% led=%u link=%s\r\n",
               g_msTick / 1000, g_cpuUse, g_ledMode,
               g_linkIdle > 5 ? "offline" : "online");
    }
}

/*======================= 主函数 ====================================*/
int main(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    BSP_GPIO_Init();
    BSP_USART1_Init();
    BSP_KeyEXTI_Init();

    printf("\r\n==== F103 Lab2: PC-Link Protocol  addr=%d ====\r\n", g_myAddr);
    printf("try: $1,2,0,0*  (query status)\r\n");

    while (1)
    {
        Task_Led();           // LED 按模式
        Task_OnKey();         // 按键切模式
        Task_OnComm();        // ★ 处理串口命令（实验二核心）
        Task_HeartBeat();     // 心跳 + CPU + 在线检测

        g_loopCnt++;
    }
}
