/************************************************************************
 * 实验三 · 传感采集链（ADC+DMA+滑动滤波 与 软件I2C读MPU6050）
 * 平台：STM32F103C8T6（蓝丸） + 标准外设库(SPL)
 *
 * 复刻信标固件两条最核心的数据通路：
 *   ┌────────────────────┬─────────────────────────────┬─────────────────┐
 *   │ 本实验              │ 对应真固件                   │ 文件            │
 *   ├────────────────────┼─────────────────────────────┼─────────────────┤
 *   │ ADC + DMA 循环搬运  │ ADC_InitSingleDMA / 双缓冲   │ ADC_operate.c   │
 *   │ 缓冲区滑动平均滤波   │ GetLiPowerVoltage 的平均    │ ADC_operate.c   │
 *   │ 采样→滤波→标定→限幅 │ 传感器处理标准流水线         │ ADC_operate.c   │
 *   │ 软件I2C读数字传感器 │ P_I2C_Start/SendByte/ReadByte│ ms5837.c        │
 *   │ 出厂换算/系数还原    │ MS5837_Getdata 的温压公式   │ ms5837.c        │
 *   └────────────────────┴─────────────────────────────┴─────────────────┘
 *
 * 两个"被测对象"：
 *   - 电位器 → PA1(ADC1_IN1)，演示 ADC+DMA+滤波（模拟量链）
 *   - MPU6050 → PB8(SCL)/PB9(SDA) 软件I2C，演示数字传感器链（同 ms5837 手法）
 *
 * 输出：每秒经 USART1(115200) 打印一行 ADC电压 + 加速度/角速度/温度
 *
 * 说明：为避开 MicroLIB 的 printf 浮点坑，全部用整数(毫/厘单位)打印。
 ************************************************************************/
//**I2C 总线空闲时,SCL 和 SDA 都是高电平(H)**
//**I2C 总线空闲时,SCL 和 SDA 都是高电平(H)**
//**I2C 总线空闲时,SCL 和 SDA 都是高电平(H)**
/*I2C 协议的铁律是:SDA 只能在 SCL=L 期间变化,SDA 的值只在 SCL=H 期间才是"有效、可读"的。*/
#include "stm32f10x.h"
#include <stdio.h>
#include "oled.h"          // 复用实验一的 OLED 驱动：和 MPU6050 共用 PB8/PB9 这条 I2C 总线

/*======================= 引脚 =======================*/
#define LED_PORT    GPIOC
#define LED_PIN     GPIO_Pin_13
#define I2C_PORT    GPIOB
#define I2C_SCL     GPIO_Pin_8          // 软件 I2C 时钟（和实验一 OLED 同一条总线）
#define I2C_SDA     GPIO_Pin_9          // 软件 I2C 数据

/*======================= 系统状态（骨架）=======================*/
volatile uint32_t g_msTick  = 0;
volatile uint8_t  g_1HzFlag = 0;
uint32_t g_blinkMs = 500;

/*======================= ADC + DMA 缓冲 =======================*/
// DMA 把 ADC 转换结果源源不断搬进这个数组（循环模式，写满回头继续）
// 对应固件 ADC_InitSingleDMA 里的 pBuf1
#define ADC_BUF_LEN   64
volatile uint16_t g_adcBuf[ADC_BUF_LEN];   // DMA 自动填充，CPU 只管读

/*======================= printf 重定向 =======================*/
int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);//ch它本质上是你要发送的那个字符的 ASCII 码
    //把 ch 这个字符的数据写入 USART1 的发送数据寄存器（DR）。写入后，USART 硬件就会自动把这个字节通过 TX 引脚发送出去。
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}
/*printf("Hello") 
    ↓
   逐个字符调用 fputc
    ↓
fputc('H') → 发送 H → 返回 'H' 
fputc('e') → 发送 e → 返回 'e'
fputc('l') → 发送 l → 返回 'l'
fputc('l') → 发送 l → 返回 'l'
fputc('o') → 发送 o → 返回 'o'
    ↓
printf 根据返回值判断是否都发送成功了*/
/*===================================================================*/
/*  一、软件模拟 I2C（带 ACK 读取）—— 完全照搬 ms5837.c 的时序          */
/*  和实验一 OLED 的区别：这次要"读"数据，所以多了 WaitAck / ReadByte   */
/*===================================================================*/
#define SCL_H()   GPIO_SetBits(I2C_PORT, I2C_SCL)
#define SCL_L()   GPIO_ResetBits(I2C_PORT, I2C_SCL)
#define SDA_H()   GPIO_SetBits(I2C_PORT, I2C_SDA)
#define SDA_L()   GPIO_ResetBits(I2C_PORT, I2C_SDA)
#define SDA_READ()  GPIO_ReadInputDataBit(I2C_PORT, I2C_SDA)  // 开漏模式下可直接读线电平

static void i2c_dly(void){ volatile uint16_t i = 10; while(i--); }

static void Soft_I2C_Init(void)  // 注意：不能叫 I2C_Init，那是库里硬件I2C的函数名，会和 stm32f10x_i2c.h 撞名
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    g.GPIO_Pin = I2C_SCL | I2C_SDA;
    g.GPIO_Mode = GPIO_Mode_Out_OD;          // 开漏：写1=释放(由上拉拉高)，写0=拉低；释放后能读线
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C_PORT, &g);
    SCL_H(); SDA_H();//应该置于释放状态
}

static void I2C_Start(void){ SDA_H(); SCL_H(); i2c_dly(); SDA_L(); i2c_dly(); SCL_L(); i2c_dly(); }
static void I2C_Stop (void){ SCL_L(); SDA_L(); i2c_dly(); SCL_H(); i2c_dly(); SDA_H(); i2c_dly(); }

// 等从机应答：释放SDA，给一个时钟，读回0=ACK / 1=NACK（对应 P_I2C_Wait_Ack）
static uint8_t I2C_WaitAck(void)
{
    uint8_t ack;
    SDA_H(); i2c_dly();
    SCL_H(); i2c_dly();
    ack = SDA_READ();//这一步是在等从机拉下呢，一般是在这之前SCL动八次发一个八位的数据，然后这个是第九次的SCL变动
    SCL_L(); i2c_dly();
    return ack;                              // 0 表示从机收到了
}
// 主机回应答(继续读)/不应答(读到此为止)
static void I2C_Ack (void){ SCL_L(); SDA_L(); i2c_dly(); SCL_H(); i2c_dly(); SCL_L(); i2c_dly(); }
/*主机回复应答的情况和等从机应答的情况反过来，从机在第九个时钟的时候选择拉高SDA，这个时候如果主机拉低了SDA那就表示应答了*/
static void I2C_NAck(void){ SCL_L(); SDA_H(); i2c_dly(); SCL_H(); i2c_dly(); SCL_L(); i2c_dly(); }

// 发 1 字节，MSB 先行（对应 P_I2C_Send_Byte）
static void I2C_SendByte(uint8_t d)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        if (d & 0x80) SDA_H(); else SDA_L();
        i2c_dly(); SCL_H(); i2c_dly(); SCL_L(); i2c_dly();
        d <<= 1;
    }
}
// 读 1 字节；ack=1 读完回ACK(还要继续读)，ack=0 回NACK(最后一字节)（对应 P_I2C_Read_Byte）
static uint8_t I2C_ReadByte(uint8_t ack)
{
    uint8_t i, d = 0;
    SDA_H();                                 // 释放 SDA，交给从机驱动
    for (i = 0; i < 8; i++)
    {
        d <<= 1;
        SCL_H(); i2c_dly();
        if (SDA_READ()) d |= 0x01;
        SCL_L(); i2c_dly();
    }
    if (ack) I2C_Ack(); else I2C_NAck();
    return d;
}
/*SCL: ‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_     (连续8个脉冲,一位一个)
      1  2  3  4  5  6  7  8
SDA: 每一位的值,只在对应SCL=H的那个窗口里被采样*/

/*===================================================================*/
/*  二、MPU6050 驱动（基于上面的软件 I2C）                              */
/*===================================================================*/
#define MPU_ADDR_W   0xD0                    // 0x68<<1，写，I2C通信时的地址
#define MPU_ADDR_R   0xD1                    // 读
#define MPU_PWR_MGMT_1   0x6B//电源管理寄存器 1
#define MPU_SMPLRT_DIV   0x19//采样率分频寄存器
#define MPU_CONFIG       0x1A//配置寄存器
#define MPU_GYRO_CONFIG  0x1B//陀螺仪配置寄存器
#define MPU_ACCEL_CONFIG 0x1C//加速度计配置寄存器
#define MPU_ACCEL_XOUT_H 0x3B//加速度计 X 轴数据高字节寄存器
#define MPU_WHO_AM_I     0x75//“我是谁”寄存器

static void MPU_WriteReg(uint8_t reg, uint8_t val)
{
    I2C_Start();
    I2C_SendByte(MPU_ADDR_W); I2C_WaitAck();
    I2C_SendByte(reg);        I2C_WaitAck();
    I2C_SendByte(val);        I2C_WaitAck();
    I2C_Stop();
}
// 从 reg 开始连读 n 个字节（先写寄存器地址，再"重复起始"切到读模式 —— 标准寄存器读）
static void MPU_ReadBytes(uint8_t reg, uint8_t* buf, uint8_t n)
{
    uint8_t i;
    I2C_Start();
    I2C_SendByte(MPU_ADDR_W); I2C_WaitAck();
    I2C_SendByte(reg);        I2C_WaitAck();
    I2C_Start();                              // 重复起始
    I2C_SendByte(MPU_ADDR_R); I2C_WaitAck();
    for (i = 0; i < n; i++)
        buf[i] = I2C_ReadByte(i < n - 1);    // 最后一个字节回 NACK
    I2C_Stop();
}
static uint8_t MPU_ReadReg(uint8_t reg){ uint8_t v; MPU_ReadBytes(reg, &v, 1); return v; }

static uint8_t MPU_Init(void)
{
    MPU_WriteReg(MPU_PWR_MGMT_1, 0x80);  { volatile uint32_t d=300000; while(d--); } // 复位
    MPU_WriteReg(MPU_PWR_MGMT_1, 0x00);      // 唤醒，用内部时钟
    MPU_WriteReg(MPU_SMPLRT_DIV, 0x07);      // 采样分频
    MPU_WriteReg(MPU_CONFIG,     0x06);      // 低通滤波 ~5Hz
    MPU_WriteReg(MPU_GYRO_CONFIG, 0x18);     // 陀螺 ±2000°/s
    MPU_WriteReg(MPU_ACCEL_CONFIG,0x00);     // 加速度 ±2g
    return MPU_ReadReg(MPU_WHO_AM_I);        // 应返回 0x68，用来确认连上了
}

// 读全部：加速度3轴、温度、角速度3轴（共14字节）
static void MPU_GetData(int16_t acc[3], int16_t gyro[3], int16_t* temp)
{
    uint8_t b[14];
    MPU_ReadBytes(MPU_ACCEL_XOUT_H, b, 14);
    acc[0]  = (int16_t)((b[0]  << 8) | b[1]);
    acc[1]  = (int16_t)((b[2]  << 8) | b[3]);
    acc[2]  = (int16_t)((b[4]  << 8) | b[5]);
    *temp   = (int16_t)((b[6]  << 8) | b[7]);
    gyro[0] = (int16_t)((b[8]  << 8) | b[9]);
    gyro[1] = (int16_t)((b[10] << 8) | b[11]);
    gyro[2] = (int16_t)((b[12] << 8) | b[13]);
}

/*===================================================================*/
/*  三、ADC + DMA（电位器 PA1）—— 对应 ADC_operate.c                  */
/*===================================================================*/
static void ADC_DMA_Init(void)
{
    GPIO_InitTypeDef gpio;
    ADC_InitTypeDef  adc;
    DMA_InitTypeDef  dma;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);     // F103 的 DMA 时钟在 AHB
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);                      // ADC时钟=72/6=12MHz(≤14MHz)

    // PA1 模拟输入
    gpio.GPIO_Pin = GPIO_Pin_1; gpio.GPIO_Mode = GPIO_Mode_AIN; gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    // --- DMA1 通道1（ADC1 固定走这条）：外设→内存，循环模式 ---
    DMA_DeInit(DMA1_Channel1);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;      // 源 = ADC 数据寄存器（地址固定）
    dma.DMA_MemoryBaseAddr     = (uint32_t)g_adcBuf;       // 目的 = 我们的缓冲区
    dma.DMA_DIR                = DMA_DIR_PeripheralSRC;    // 外设→内存
    dma.DMA_BufferSize         = ADC_BUF_LEN;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;// 外设地址不变（一直读 DR）
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;     // 内存地址自增（逐个存进数组）
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord; // ADC是12位→按16位搬
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Circular;        // 循环：写满自动回头（关键！CPU不用管）
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &dma);
    DMA_Cmd(DMA1_Channel1, ENABLE);

    // --- ADC1：连续转换 + DMA ---
    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;                       // 单通道，不扫描
    adc.ADC_ContinuousConvMode = ENABLE;                  // 连续转换：转完一个立刻转下一个
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &adc);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1, ADC_SampleTime_55Cycles5); // PA1=通道1
    ADC_DMACmd(ADC1, ENABLE);                             // ADC 转换完触发 DMA 搬运
    ADC_Cmd(ADC1, ENABLE);

    // F103 的 ADC 必须先校准（F4 不用）
    ADC_ResetCalibration(ADC1);   while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);   while (ADC_GetCalibrationStatus(ADC1));

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);              // 启动连续转换，之后全自动
}

// 滑动平均：把 DMA 缓冲区整体求平均（对应固件 GetLiPowerVoltage 的环形平均）
// 这就是"采样(DMA)→滤波(平均)→标定(转电压)"流水线
static uint16_t ADC_GetAvgRaw(void)
{
    uint32_t sum = 0; uint16_t i;
    for (i = 0; i < ADC_BUF_LEN; i++) sum += g_adcBuf[i];
    return (uint16_t)(sum / ADC_BUF_LEN);
}

/*===================================================================*/
/*  骨架：LED / SysTick / USART                                       */
/*===================================================================*/
void SysTick_Handler(void){ g_msTick++; if (g_msTick % 1000 == 0) g_1HzFlag = 1; }//所以 SysTick_Handler() 每隔 1 毫秒 就会被调用一次。

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
int main(void)
{
    int16_t acc[3], gyro[3], temp;
    uint8_t who;
    uint16_t raw; uint32_t mv;

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);//想要 SysTick 每隔 1 毫秒（1ms）产生一次中断。

    BSP_LED_USART_Init();
    ADC_DMA_Init();
    Soft_I2C_Init();
    OLED_Init();                          // OLED 与 MPU6050 共用 PB8/PB9 这条 I2C 总线
    OLED_ShowString(0, 0, "F103 Lab3");
    OLED_ShowString(0, 2, "Sensor Chain");

    printf("\r\n==== F103 Lab3: Sensor Chain (ADC+DMA & MPU6050) ====\r\n");
    who = MPU_Init();
    if (who == 0x68) { printf("MPU6050 OK (WHO_AM_I=0x%02X)\r\n", who);  OLED_ShowString(0, 4, "MPU6050 OK  "); }
    else             { printf("MPU6050 NOT found (WHO_AM_I=0x%02X) -- check wiring/AD0\r\n", who);  OLED_ShowString(0, 4, "MPU6050 ERR "); }

    while (1)
    {
        // LED 非阻塞闪烁（骨架还在）
        static uint32_t last = 0;
        if (g_msTick - last >= g_blinkMs)
        {
            last = g_msTick;
            GPIO_WriteBit(LED_PORT, LED_PIN, (BitAction)(1 - GPIO_ReadOutputDataBit(LED_PORT, LED_PIN)));
        }

        if (g_1HzFlag)                       // 每秒读一次所有传感器并打印
        {
            g_1HzFlag = 0;

            // --- ADC 链：DMA缓冲平均 → 转电压(mV) ---
            raw = ADC_GetAvgRaw();
            mv  = (uint32_t)raw * 3300 / 4096;          // 标定：12位、参考3.3V → 毫伏

            // --- MPU6050 链：读原始 → 整数换算 ---
            MPU_GetData(acc, gyro, &temp);
            // 加速度 ±2g(16384 LSB/g) → 毫g；陀螺 ±2000(16.4 LSB/dps) → 0.1°/s；温度 → 0.01℃
            printf("ADC: raw=%4u  V=%lu mV  |  ACC(mg): x=%d y=%d z=%d  |  GYR(0.1dps): x=%ld y=%ld z=%ld  |  T=%ld(0.01C)\r\n",
                   raw, (unsigned long)mv,
                   (int)((int32_t)acc[0]  * 1000 / 16384),//加速度的量程是2g，灵敏度为 16384 LSB/g，也就是说这时候每 1g 的加速度，传感器输出数字值 16384，但是乘1000意思是输出的是mg
                   (int)((int32_t)acc[1]  * 1000 / 16384),
                   (int)((int32_t)acc[2]  * 1000 / 16384),
                   (long)((int32_t)gyro[0] * 100 / 164),//MPU6050 陀螺仪在 ±2000°/s 量程下，灵敏度为 16.4 LSB/(°/s)，也就是说1度/s的角速度情况下，传感器输出为16.4
                   (long)((int32_t)gyro[1] * 100 / 164),//(单位：0.1°/s)
                   (long)((int32_t)gyro[2] * 100 / 164),
                   (long)((int32_t)temp * 100 / 340 + 3653));//也就是说temp是0的时候为36.53度

            // --- 同步显示到 OLED（和串口同一份数据；OLED 就在同一条 I2C 总线上）---
            {
                char line[20];
                int azmg = (int)((int32_t)acc[2]  * 1000 / 16384);     // Z轴加速度(mg)，平放≈1000
                int gz   = (int)((int32_t)gyro[2] * 100  / 164);       // Z轴角速度(0.1dps)
                int tc   = (int)((int32_t)temp    * 100  / 340 + 3653);// 温度(0.01℃)
                sprintf(line, "ADC:%4lu mV  ", (unsigned long)mv); OLED_ShowString(0, 0, line);
                //把字符串 "ADC: 1234 mV " 写入到 line 这个字符数组里。
                sprintf(line, "AccZ:%5d mg",   azmg);              OLED_ShowString(0, 2, line);
                sprintf(line, "GyrZ:%6d   ",   gz);                OLED_ShowString(0, 4, line);
                sprintf(line, "Temp:%5d cC",   tc);                OLED_ShowString(0, 6, line);
            }
        }
    }
}
