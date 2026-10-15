/************************************************************************
 * 实验四 · 桌面信标（毕设集成：Chirp扫频发射 + 参数掉电保存 + 看门狗）
 * 平台：STM32F103C8T6（蓝丸） + 标准外设库(SPL)
 *
 * 这是四个实验的收官整合——把信标固件最有代表性的三块新逻辑，
 * 叠加在实验二的协议骨架(COMM_OBJECT/组帧/命令分发/地址过滤)之上：
 *   ┌───────────────────────┬───────────────────────────────────┬──────────────────┐
 *   │ 本实验                 │ 对应真固件                          │ 文件              │
 *   ├───────────────────────┼───────────────────────────────────┼──────────────────┤
 *   │ TIM3中断手动翻转GPIO    │ FPGA"类DDS"内部产生CW/LFM波形        │ FPGA_operate.c   │
 *   │ 频率→ARR的换算公式      │ FreqDiv = FPGA_CLOCK/(2·f)          │ PPT技术亮点/      │
 *   │                        │ iBeacon_SetEmitParam                │ iBeaconB_operate.c│
 *   │ 线性扫频状态机(非阻塞)  │ LFM 线性调频的思想                   │ iBeacon_SetEmitParam│
 *   │ 按键=模拟PPS触发发射    │ Device_OnEchoPps/PPS触发信标发射     │ main.c/PPS_OUT_EN│
 *   │ 参数结构体存内部Flash   │ USE_STMFLASH=1，SysParam_Save/Load  │ main.h/flash_if.c│
 *   │ 硬件IWDG + 主循环喂狗   │ IWDG_Feed() 在 OnEchoRTC() 里调用    │ main.c(OnEchoRTC)│
 *   │ 命令分发switch(继承)    │ OnEchoPCLINK 的大switch              │ main.c           │
 *   └───────────────────────┴───────────────────────────────────┴──────────────────┘
 *
 * 器件：无源蜂鸣器(PB1/TIM3_CH4引脚位置，但本实验用软件翻转GPIO而非硬件PWM)
 *       ⚠️ 有源蜂鸣器只能发固定音，听不出扫频——但发射时序、按键触发、看门狗
 *          这些逻辑依然可以验证：EMIT期间 LED 会同步快闪，肉眼也能确认节拍。
 *
 * 协议在实验二基础上扩展了 6 条命令（$地址,命令,参数1,参数2*）：
 *   $1,4,f1,f2*   设置扫频起止频率(Hz)
 *   $1,5,width*   设置脉冲宽度(ms)
 *   $1,6,0,0*     手动触发一次发射(相当于模拟PPS)
 *   $1,7,0,0*     保存当前参数到内部Flash
 *   $1,8,0,0*     从Flash重新加载参数
 *   $1,9,0,0*     测试看门狗：故意死循环，~3秒后自动复位(危险但直观的演示)
 ************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>

/*======================= 引脚定义 =======================*/
#define LED_PORT     GPIOC
#define LED_PIN      GPIO_Pin_13
#define KEY_PORT     GPIOA
#define KEY_PIN      GPIO_Pin_0            // 按键：本实验含义变成"模拟PPS，触发一次发射"
#define BUZZ_PORT    GPIOB
#define BUZZ_PIN     GPIO_Pin_1            // 无源蜂鸣器（TIM3_CH4引脚位置，软件翻转驱动）

/*======================= 通信对象（同实验二）=======================*/
#define COMM_BUF_SIZE  64
typedef struct
{
    uint8_t  rxFlag;
    uint8_t  ready;
    uint16_t idx;
    char     buf[COMM_BUF_SIZE];
} COMM_OBJECT;
COMM_OBJECT g_comm;

/*======================= 系统状态（骨架，来自实验一二）=======================*/
volatile uint32_t g_msTick   = 0;
volatile uint8_t  g_1HzFlag  = 0;
volatile uint8_t  g_bKeyFlag = 0;
volatile uint32_t g_lastKeyMs = 0;

uint32_t g_loopCnt = 0, g_loopMax = 1;
uint8_t  g_cpuUse  = 0;
uint32_t g_linkIdle = 0;                   // 多久没收到上位机命令了（协议级活性）

/*===================================================================*/
/*  一、参数结构体 + 内部Flash掉电保存                                   */
/*  对应固件 main.h 的 USE_STMFLASH=1（真固件参数就是存内部Flash的）     */
/*===================================================================*/
// F103C8T6 = 64KB Flash，页大小1KB，取最后一页存参数，远离代码区，安全
#define PARAM_FLASH_ADDR   ((uint32_t)0x0800FC00)
#define PARAM_MAGIC        ((uint16_t)0xBEAC)     // 校验字：判断Flash里是不是我们存过的有效数据

typedef struct//读参数时先检查 magic 是否匹配——匹配说明数据可靠，不匹配则放弃损坏数据，改用默认值。
{
    uint16_t magic;      // = PARAM_MAGIC 才认为数据有效，否则视为"从没存过"，用默认值，magic 存储的是一个预设的常量（比如 0x5A5A、0xA5A5、0x1234 等），定义在别处
    uint16_t f1;         // 扫频起始频率(Hz)
    uint16_t f2;         // 扫频终止频率(Hz)
    uint16_t widthMs;    // 发射脉冲宽度(ms)
    uint16_t blinkMs;    // 空闲时LED闪烁周期(ms)
    uint16_t ledMode;    // 0灭 1常亮 2闪
    uint16_t myAddr;     // 本机通信地址
} SysParam_t;
SysParam_t g_param;

static void SysParam_SetDefault(void)
{
    g_param.magic   = PARAM_MAGIC;
    g_param.f1      = 800;     // 800Hz ~ 1600Hz 之间做扫频，人耳能听出音调变化
    g_param.f2      = 1600;
    g_param.widthMs = 300;
    g_param.blinkMs = 500;
    g_param.ledMode = 2;
    g_param.myAddr  = 1;
}

// Flash 是"内存映射"的：读的时候直接当普通指针解引用即可，不需要调用任何库函数
static void SysParam_Load(void)
{
    const SysParam_t* p = (const SysParam_t*)PARAM_FLASH_ADDR;
    if (p->magic == PARAM_MAGIC) g_param = *p;   // 校验字对得上，说明是我们存的合法数据
    else                          SysParam_SetDefault();  // 否则(出厂全FF)用默认值
}

// 写 Flash 必须先解锁、擦除整页、再逐个半字(16位)编程、最后上锁
// 对应固件 flash_if.c 里的写入流程；erase 期间 CPU 会被总线自动暂停几十毫秒，属正常现象
static void SysParam_Save(void)
{
    uint16_t* src = (uint16_t*)&g_param;
    uint8_t i, n = sizeof(g_param) / 2;

    FLASH_Unlock();
    FLASH_ErasePage(PARAM_FLASH_ADDR);
    for (i = 0; i < n; i++)
        FLASH_ProgramHalfWord(PARAM_FLASH_ADDR + i * 2, src[i]);//它的作用就是往 Flash 里写入一个 16 位的数据
    /*void FLASH_ProgramHalfWord(uint32_t Address, uint16_t Data);是函数原型，data是16位的
    因为 Flash 是按字节编址的，而 FLASH_ProgramHalfWord 每次写入 2 个字节*/
    FLASH_Lock();
}

/*===================================================================*/
/*  二、蜂鸣器"类DDS"发射：TIM3定时中断手动翻转GPIO                     */
/*  对应固件：FPGA 直接产生 CW/LFM 可配置波形；这里没有FPGA，            */
/*  就用 MCU 定时器中断模拟同样的思想——每隔"半周期"翻转一次电平          */
/*===================================================================*/
// 频率→半周期(us)的换算，和 PPT/固件里的 FreqDiv = FPGA_CLOCK/(2·f) 是同一个公式，
// 只是我们把 FPGA_CLOCK 换成了 TIM3 的 1MHz 计数时钟
static void Buzz_SetFreqHz(uint32_t freqHz)//用 TIM3 定时器产生一个方波（高低电平交替），通过改变方波的频率，让蜂鸣器发出不同音调的声音。
{
    uint32_t halfPeriodUs;
    if (freqHz < 50)   freqHz = 50;      // 下限保护：太低会导致ARR太大甚至溢出
    if (freqHz > 20000) freqHz = 20000;  // 上限保护：人耳/蜂鸣器上限附近
    halfPeriodUs = 500000UL / freqHz;    // 500000 = 1,000,000 / 2，即 FreqDiv 公式
    //UL 是 C 语言里一个后缀，用来告诉编译器："这是一个 unsigned long（无符号长整型）类型的常量"
    //我们知道T=1/f，这个是一个周期的秒数，因为TIM定时器是没1微秒计数一次的，所以就变成1000000/f=周期的微秒数，除以2就是半周期的微秒数了。
    TIM3->ARR = (uint16_t)(halfPeriodUs - 1);//ARR是自动重装载寄存器，定时器计数到这个值就会溢出，触发中断。减1是因为计数从0开始。每次中断翻转一次电平，正好是半个周期
}

static void Buzzer_TIM_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    gpio.GPIO_Pin = BUZZ_PIN; gpio.GPIO_Mode = GPIO_Mode_Out_PP; gpio.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(BUZZ_PORT, &gpio);
    GPIO_ResetBits(BUZZ_PORT, BUZZ_PIN);           // 上电先静音

    tb.TIM_Prescaler   = (SystemCoreClock / 1000000) - 1;  // 72MHz/72=1MHz，计数一次=1us，把 72 MHz 的系统时钟分频成 1 MHz，实际上就是分频72次
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period      = 999;                              // 占位值，触发发射时会被覆盖，计数到999会产生中断
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM3, &tb);

    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    nvic.NVIC_IRQChannel = TIM3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;   // 类比固件"FPGA中断"给较高优先级(发射链要准时)
    nvic.NVIC_IRQChannelSubPriority = 1;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    //没有调用 TIM_Cmd(TIM3, ENABLE)
    // 注意：这里不使能TIM3计数器，等 Emit_Trigger() 触发发射时才打开——不发射时定时器完全不跑
}

// TIM3每次"溢出"（走完一个半周期）就翻转一次蜂鸣器电平——方波就是这么"手工"产生的
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)//TIM_IT_Update 是定时器的 "更新中断标志"，它表示：定时器计数到了 ARR 设定的值，正准备从 0 重新开始计数。
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        GPIO_WriteBit(BUZZ_PORT, BUZZ_PIN, (BitAction)(1 - GPIO_ReadOutputDataBit(BUZZ_PORT, BUZZ_PIN)));
    }
}

/*===================================================================*/
/*  三、发射状态机：非阻塞扫频（对应固件 LFM 线性调频思想）              */
/*===================================================================*/
typedef enum { EMIT_IDLE = 0, EMIT_RUNNING } EmitState;//定义了一个枚举模型，里面有两个可能的值，IDLE和RUNNING
volatile EmitState g_emitState   = EMIT_IDLE;//声明了一个全局变量 g_emitState，类型是 EmitState，初始值是IDLE
volatile uint32_t  g_emitStartMs = 0;//这个变量用来记录蜂鸣器开始发声的时刻（毫秒数），以便计算已经响了多久，到了设定的时长就自动停止。

// 触发一次发射：相当于固件里"PPS秒脉冲到来，触发信标发射"的那个动作
static void Emit_Trigger(void)
{
    if (g_emitState == EMIT_RUNNING) { printf(">> EMIT busy, ignored\r\n"); return; } // 上一次没发完，忽略新触发
    g_emitStartMs = g_msTick;//记录下“触发发射这一刻的毫秒时间”，存到 g_emitStartMs 里
    g_emitState   = EMIT_RUNNING;
    Buzz_SetFreqHz(g_param.f1);
    TIM_SetCounter(TIM3, 0);//TIM_SetCounter() 是 STM32 标准外设库里的一个函数，它的作用是直接把定时器的计数器（CNT）寄存器手动设置为指定值。意思是把TIM3的值强行归零
    TIM_Cmd(TIM3, ENABLE);
    printf(">> EMIT start f1=%u f2=%uHz width=%ums\r\n", g_param.f1, g_param.f2, g_param.widthMs);
}

// 每次主循环都检查一次：发射进行中就按线性规律更新频率，时间到就停止（全程不阻塞CPU）
static void Task_Emit(void)
{
    uint32_t elapsed;
    int32_t  f;
    if (g_emitState != EMIT_RUNNING) return;

    elapsed = g_msTick - g_emitStartMs;
    if (elapsed >= g_param.widthMs)
    {
        TIM_Cmd(TIM3, DISABLE);
        GPIO_ResetBits(BUZZ_PORT, BUZZ_PIN);       // 停止后拉低，确保静音
        g_emitState = EMIT_IDLE;
        printf(">> EMIT done\r\n");
        return;
    }
    // 线性扫频：f(t) = f1 + (f2-f1)*t/width —— 这就是"软件版LFM"
    f = (int32_t)g_param.f1 + (int32_t)(g_param.f2 - g_param.f1) * (int32_t)elapsed / (int32_t)g_param.widthMs;
    Buzz_SetFreqHz((uint32_t)f);
}

/*===================================================================*/
/*  四、硬件看门狗 IWDG                                                */
/*  对应固件 OnEchoRTC() 里的 IWDG_Feed()：正常每圈喂狗，                */
/*  一旦程序跑飞进死循环、不再喂狗，硬件会在超时后强制复位系统           */
/*===================================================================*/
static void IWDG_Setup(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);//解除 IWDG 寄存器的写保护。IWDG 的寄存器默认是只读的，防止误操作。写入前必须先解锁。
    IWDG_SetPrescaler(IWDG_Prescaler_64);   // LSI(~40kHz)/64 ≈ 625Hz，每计数一次≈1.6ms，设置预分频器
    IWDG_SetReload(1875);                    // 1875 × 1.6ms ≈ 3秒超时 看门狗计数器从 1875 开始递减，减到 0 时触发复位。
    IWDG_ReloadCounter();//“喂狗”——把计数器重新加载为重装载值（1875）。相当于重置倒计时，让看门狗重新从 3 秒开始倒数
    IWDG_Enable();//使能
}

/*======================= printf 重定向到 USART1 =====================*/
int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}

/*======================= 组帧状态机（同实验二，未改动）================*/
uint8_t Comm_GetFrameMsg(char rx, COMM_OBJECT* c)
{
    if (rx == '$')
    {
        c->rxFlag = 1; c->idx = 0; c->ready = 0;
    }
    else if (c->rxFlag == 1 && rx != '*' && c->idx < COMM_BUF_SIZE - 1)
    {
        c->buf[c->idx++] = rx;
    }
    else if (c->rxFlag == 1 && rx == '*')
    {
        c->buf[c->idx] = '\0';
        c->rxFlag = 0; c->ready = 1;
    }
    if (c->idx >= COMM_BUF_SIZE - 1) { c->rxFlag = 0; c->idx = 0; c->ready = 0; }
    return c->ready;
}

/*======================= 命令分发（在实验二基础上扩展 4~9）===========*/
void Comm_OnFrame(COMM_OBJECT* c)//p1和p2本身没有固定的含义，它们的具体含义完全取决于当前指令
{
    int addr, cmd, p1 = 0, p2 = 0, n;

    c->ready = 0;
    n = sscanf(c->buf, "%d,%d,%d,%d", &addr, &cmd, &p1, &p2);
    if (n < 2) { printf("$ERR,bad frame:[%s]*\r\n", c->buf); return; }
    if (!(addr == 0 || addr == g_param.myAddr)) return;    // 地址过滤：广播或本机地址才响应

    g_linkIdle = 0;

    switch (cmd)
    {
    case 1:  // 设置空闲LED闪烁周期
        if (p1 >= 10) { g_param.blinkMs = (uint16_t)p1; g_param.ledMode = 2; }
        printf("$%d,%d,ok,blink=%ums*\r\n", g_param.myAddr, cmd, g_param.blinkMs);
        break;

    case 2:  // 查询状态
        printf("$%d,%d,t=%us,cpu=%u%%,led=%u,blink=%ums,emit=%u-%uHz/%ums,state=%s*\r\n",
               g_param.myAddr, cmd, g_msTick / 1000, g_cpuUse, g_param.ledMode, g_param.blinkMs,
               g_param.f1, g_param.f2, g_param.widthMs,
               g_emitState == EMIT_RUNNING ? "EMIT" : "idle");
        break;

    case 3:  // 设置LED模式 0灭 1常亮 2闪
        if (p1 <= 2) g_param.ledMode = (uint16_t)p1;
        printf("$%d,%d,ok,led=%u*\r\n", g_param.myAddr, cmd, g_param.ledMode);
        break;

    case 4:  // 设置扫频起止频率(Hz)：对应固件 iBeacon_SetEmitParam 的 uFreqL/uFreqH
        if (p1 >= 50 && p1 <= 20000) g_param.f1 = (uint16_t)p1;
        if (p2 >= 50 && p2 <= 20000) g_param.f2 = (uint16_t)p2;
        printf("$%d,%d,ok,f1=%u,f2=%u*\r\n", g_param.myAddr, cmd, g_param.f1, g_param.f2);
        break;

    case 5:  // 设置发射脉冲宽度(ms)：对应固件 SIG_PARAM.uWidth
        if (p1 >= 20 && p1 <= 5000) g_param.widthMs = (uint16_t)p1;
        printf("$%d,%d,ok,width=%ums*\r\n", g_param.myAddr, cmd, g_param.widthMs);
        break;

    case 6:  // 手动触发一次发射：相当于用命令模拟一次PPS
        Emit_Trigger();
        printf("$%d,%d,ok,triggered*\r\n", g_param.myAddr, cmd);
        break;

    case 7:  // 保存当前参数到Flash
        SysParam_Save();
        printf("$%d,%d,ok,saved to flash*\r\n", g_param.myAddr, cmd);
        break;

    case 8:  // 从Flash重新加载参数
        SysParam_Load();
        printf("$%d,%d,ok,loaded f1=%u f2=%u width=%u*\r\n",
               g_param.myAddr, cmd, g_param.f1, g_param.f2, g_param.widthMs);
        break;

    case 9:  // 危险演示：故意死循环不喂狗，验证硬件看门狗~3秒后自动复位
        printf("$%d,%d,ok,hanging now (IWDG resets in ~3s)...*\r\n", g_param.myAddr, cmd);
        while (1) { }
        break;   // 永远到不了这里

    default:
        printf("$%d,%d,unknown cmd*\r\n", g_param.myAddr, cmd);
        break;
    }
}

/*======================= 初始化：GPIO/串口/按键EXTI ===================*/
static void BSP_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);

    gpio.GPIO_Pin = LED_PIN; gpio.GPIO_Mode = GPIO_Mode_Out_PP; gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_SetBits(LED_PORT, LED_PIN);

    gpio.GPIO_Pin = KEY_PIN; gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(KEY_PORT, &gpio);
}

static void BSP_USART1_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_9;  gpio.GPIO_Mode = GPIO_Mode_AF_PP;       gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_10; gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate = 115200; usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1; usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &usart);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);

    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 2;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

static void BSP_KeyEXTI_Init(void)
{
    EXTI_InitTypeDef exti;
    NVIC_InitTypeDef nvic;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

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
void SysTick_Handler(void)
{
    g_msTick++;
    if (g_msTick % 1000 == 0) g_1HzFlag = 1;
}

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        char c = (char)USART_ReceiveData(USART1);
        Comm_GetFrameMsg(c, &g_comm);
    }
    USART_ClearFlag(USART1, USART_FLAG_ORE | USART_FLAG_FE | USART_FLAG_NE);
}

// 按键：本实验里含义是"模拟PPS秒脉冲"——按一下就触发一次发射，而不再是切换LED模式
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        if (g_msTick - g_lastKeyMs > 200) { g_lastKeyMs = g_msTick; g_bKeyFlag = 1; }
        EXTI_ClearITPendingBit(EXTI_Line0);
    }
}

/*======================= 后台任务（主循环里调用）====================*/
// LED：发射期间快闪(可视化提示，有源蜂鸣器听不出扫频也能靠它确认节拍)；空闲时按模式工作
static void Task_Led(void)
{
    static uint32_t last = 0;
    if (g_emitState == EMIT_RUNNING)
    {
        if (g_msTick - last >= 60)
        {
            last = g_msTick;
            GPIO_WriteBit(LED_PORT, LED_PIN, (BitAction)(1 - GPIO_ReadOutputDataBit(LED_PORT, LED_PIN)));
        }
        return;
    }
    if (g_param.ledMode == 0)       GPIO_SetBits(LED_PORT, LED_PIN);
    else if (g_param.ledMode == 1)  GPIO_ResetBits(LED_PORT, LED_PIN);
    else if (g_msTick - last >= g_param.blinkMs)
    {
        last = g_msTick;
        GPIO_WriteBit(LED_PORT, LED_PIN, (BitAction)(1 - GPIO_ReadOutputDataBit(LED_PORT, LED_PIN)));
    }
}

// 按键事件：触发一次发射（模拟PPS）
static void Task_OnKey(void)
{
    if (g_bKeyFlag)
    {
        g_bKeyFlag = 0;
        printf(">> KEY (simulate PPS)\r\n");
        Emit_Trigger();
    }
}

static void Task_OnComm(void)
{
    if (g_comm.ready) Comm_OnFrame(&g_comm);
}

static void Task_HeartBeat(void)
{
    if (g_1HzFlag)
    {
        uint32_t cnt = g_loopCnt;
        g_loopCnt = 0; g_1HzFlag = 0;
        if (cnt > g_loopMax) g_loopMax = cnt;
        g_cpuUse = 100 - (uint8_t)((uint64_t)cnt * 100 / g_loopMax);

        if (g_linkIdle < 0xFFFF) g_linkIdle++;//防止整数溢出
        printf("[HB] t=%us cpu=%u%% led=%u emit=%u-%uHz/%ums state=%s link=%s\r\n",
               g_msTick / 1000, g_cpuUse, g_param.ledMode,
               g_param.f1, g_param.f2, g_param.widthMs,
               g_emitState == EMIT_RUNNING ? "EMIT" : "idle",
               g_linkIdle > 5 ? "offline" : "online");
    }
}

/*======================= 主函数 ====================================*/
int main(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);//让systick每1ms触发中断一次，配置完之后，系统就会每 1ms 自动触发一次 SysTick_Handler() 中断

    BSP_GPIO_Init();
    BSP_USART1_Init();
    BSP_KeyEXTI_Init();
    Buzzer_TIM_Init();
    IWDG_Setup();

    SysParam_Load();     // 上电自动从Flash读参数；读不到合法数据（magic对不上）就用默认值

    printf("\r\n==== F103 Lab4: Desktop Beacon  addr=%d ====\r\n", g_param.myAddr);
    printf("emit f1=%u f2=%uHz width=%ums | KEY=simulate PPS trigger\r\n",
           g_param.f1, g_param.f2, g_param.widthMs);
    printf("try: $1,6,0,0*  (trigger emit)   $1,7,0,0* (save)   $1,9,0,0* (watchdog demo)\r\n");

    while (1)
    {
        Task_Led();
        Task_OnKey();
        Task_OnComm();
        Task_Emit();          // 非阻塞扫频状态机
        Task_HeartBeat();

        g_loopCnt++;
        IWDG_ReloadCounter(); // 正常每圈喂狗；cmd=9 演示会让程序卡死在上面，不再执行到这里 → 硬件复位
    }
}
