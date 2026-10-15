/************************************************************************
 * SSD1306 OLED 驱动（软件模拟 I2C）实现
 * 对照 USER/ms5837.c：起始/停止/发字节的时序逻辑完全是一回事。
 ************************************************************************/
#include "oled.h"
#include "oledfont.h"

#define OLED_ADDR   0x78        // SSD1306 I2C 写地址 (7位 0x3C << 1)

/*--- 引脚电平宏（软件 I2C 用开漏输出，靠模块自带上拉拉高）---*/
#define SCL_H()  GPIO_SetBits(OLED_SCL_PORT,  OLED_SCL_PIN)
#define SCL_L()  GPIO_ResetBits(OLED_SCL_PORT, OLED_SCL_PIN)
#define SDA_H()  GPIO_SetBits(OLED_SDA_PORT,  OLED_SDA_PIN)
#define SDA_L()  GPIO_ResetBits(OLED_SDA_PORT, OLED_SDA_PIN)

// 软件延时，撑开 I2C 时序（72MHz 下这点空转足够 SSD1306 用）
static void I2C_Delay(void){ volatile uint16_t i = 8; while(i--); }
/*① volatile 关键字（非常关键）
作用：告诉编译器“这个变量可能会被意想不到的方式改变，请不要优化它”。

如果不加：因为 i 没有在别处被使用，编译器发现循环 while(i--) 对程序结果没有任何影响（只是浪费CPU时间），就会直接把整个循环优化掉（删掉），导致这函数变成空函数，没有任何延时效果。

加了 volatile：编译器会老老实实地生成汇编指令，每次从内存读取 i 的值，减1，写回内存，再判断。这 8 次循环实实在在地被执行了。*/

/*--- 软件 I2C 基本时序（和 ms5837.c 的 P_I2C_Start/Stop/Send_Byte 一一对应）---*/
static void I2C_Start(void)
{
    SDA_H(); SCL_H(); I2C_Delay();
    SDA_L(); I2C_Delay();           // SCL 高时 SDA 下跳 = 起始
    SCL_L(); I2C_Delay();
}
static void I2C_Stop(void)
{
    SCL_L(); SDA_L(); I2C_Delay();
    SCL_H(); I2C_Delay();
    SDA_H(); I2C_Delay();           // SCL 高时 SDA 上跳 = 停止
}
// 发 1 字节，MSB 先行。SSD1306 只写不读，这里产生应答时钟但不校验 ACK（write-only 简化）
static void I2C_SendByte(uint8_t dat)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80) SDA_H(); else SDA_L();//哦哦哦我知道了，就是单纯的1是高，0是低，这个SDA是传输数据的线
        I2C_Delay();
        SCL_H(); I2C_Delay();
        SCL_L(); I2C_Delay();
        dat <<= 1;
    }
    SCL_H(); I2C_Delay();           // 第 9 个时钟（ACK 位，不读取）
    SCL_L(); I2C_Delay();
}

// 写一个字节到 SSD1306：cmd=0 命令(控制字0x00)，cmd=1 数据(控制字0x40)
//[START] | [从机地址] | [控制字节] | [数据字节] | [STOP]
/*发送命令：控制字节 = 0x00（0b0000 0000）

发送数据：控制字节 = 0x40（0b0100 0000）*/
static void OLED_WR_Byte(uint8_t dat, uint8_t cmd)
{
    I2C_Start();
    I2C_SendByte(OLED_ADDR);
    I2C_SendByte(cmd ? 0x40 : 0x00);
    I2C_SendByte(dat);
    I2C_Stop();
}

// 设置显示起始位置：x=列(0~127)，y=页(0~7) 接下来我要从第 x 列、第 y 页开始写数据
/*       列地址 (x): 0 ~ 127
       ────────────────────►
  ┌────┬────┬────┬────┬────┐
  │ P0 │    │    │    │    │  ← 页地址 (y): 第0页 (行0~7)
  ├────┼────┼────┼────┼────┤
  │ P1 │    │    │    │    │  ← 页地址 (y): 第1页 (行8~15)
  ├────┼────┼────┼────┼────┤
  │ P2 │    │    │    │    │  ← 页地址 (y): 第2页 (行16~23)
  ├────┼────┼────┼────┼────┤
  │ ...│    │    │    │    │
  ├────┼────┼────┼────┼────┤
  │ P7 │    │    │    │    │  ← 页地址 (y): 第7页 (行56~63)
  └────┴────┴────┴────┴────┘*/
static void OLED_Set_Pos(uint8_t x, uint8_t y)
{
    OLED_WR_Byte(0xB0 + y, 0);
    OLED_WR_Byte(((x & 0xF0) >> 4) | 0x10, 0);//设置列地址高 4 位
    OLED_WR_Byte(x & 0x0F, 0);//设置低四位
}

/*--------------------------- 对外接口 ---------------------------*/
void OLED_Clear(void)
{
    uint8_t page, col;
    for (page = 0; page < 8; page++)
    {
        OLED_Set_Pos(0, page);
        for (col = 0; col < 128; col++)
            OLED_WR_Byte(0x00, 1);
    }
}

void OLED_ShowChar(uint8_t x, uint8_t y, char chr)
{
    uint8_t i, c = (uint8_t)chr - ' ';      // 字库从空格(0x20)起
    if ((uint8_t)chr < ' ' || (uint8_t)chr > '~') c = 0;

    OLED_Set_Pos(x, y);                      // 上半页 8 列
    for (i = 0; i < 8; i++) OLED_WR_Byte(F8X16[c * 16 + i], 1);
    /*F8X16 表示：每个字符占据 8 列宽 × 16 行高 的像素点阵。

每个字符由 16 个字节 的数据来描述：

前 8 个字节：字符的 上半部分（第 0 ~ 7 行）

后 8 个字节：字符的 下半部分（第 8 ~ 15 行）*/
    OLED_Set_Pos(x, y + 1);                  // 下半页 8 列
    for (i = 0; i < 8; i++) OLED_WR_Byte(F8X16[c * 16 + i + 8], 1);
}
/*把传入的字符（如 'A'）转成字库索引

把屏幕定位到指定位置（页 y，列 x）

从字库中取出这个字符的 16 个字节描述数据

前 8 个字节发送到上半页，后 8 个字节发送到下半页

屏幕上就出现了一个 8×16 像素的字符
假设我们要显示 'A'：

计算索引偏移： c = 'A' - ' ' = 65 - 32 = 33

取上半部分（8个字节）：

起始位置：33 * 16 + 0 = 第528个字节开始

取 F8X16[528] ~ F8X16[535]（对应上面的行0~7）

取下半部分（8个字节）：

起始位置：33 * 16 + 8 = 第536个字节开始

取 F8X16[536] ~ F8X16[543]（对应上面的行8~15）*/
void OLED_ShowString(uint8_t x, uint8_t y, const char *str)
{
    while (*str)
    {
        OLED_ShowChar(x, y, *str++);
        x += 8;                              // 每字符宽 8 像素
        if (x > 120) { x = 0; y += 2; }      // 自动换行（一行占 2 页）屏幕宽度是 128 像素
    }
}

void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len)
{
    char buf[12];
    int i, n = 0;
    if (num == 0) buf[n++] = '0';
    while (num) { buf[n++] = '0' + num % 10; num /= 10; }//一位一位拆解
    // 右对齐：不足 len 位前面补空格
    for (i = 0; i < len - n; i++) { OLED_ShowChar(x, y, ' '); x += 8; }
    for (i = n - 1; i >= 0; i--) { OLED_ShowChar(x, y, buf[i]); x += 8; }//反向打印
}

void OLED_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    // SCL/SDA 配成开漏输出（软件 I2C，依赖 OLED 模块板载上拉电阻）
    gpio.GPIO_Pin   = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_OD;//开漏输出 可以把开漏输出想象成一个只能接地的按钮开关
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    SCL_H(); SDA_H();

    // 上电稍等，等 SSD1306 内部稳定
    { volatile uint32_t d = 200000; while(d--); }

    // SSD1306 128x64 标准初始化序列
    OLED_WR_Byte(0xAE, 0);  // 关显示
    OLED_WR_Byte(0x20, 0); OLED_WR_Byte(0x10, 0); // 寻址模式
    OLED_WR_Byte(0xB0, 0);
    OLED_WR_Byte(0xC8, 0);  // 行扫描方向（上下翻转用 0xC0）
    OLED_WR_Byte(0x00, 0); OLED_WR_Byte(0x10, 0);
    OLED_WR_Byte(0x40, 0);
    OLED_WR_Byte(0x81, 0); OLED_WR_Byte(0xFF, 0); // 对比度 0xFF 是最亮，0x00 是最暗
    OLED_WR_Byte(0xA1, 0);  // 列方向（左右翻转用 0xA0）
    OLED_WR_Byte(0xA6, 0);  // 正常显示
    OLED_WR_Byte(0xA8, 0); OLED_WR_Byte(0x3F, 0); // 多路复用 1/64
    OLED_WR_Byte(0xA4, 0);
    OLED_WR_Byte(0xD3, 0); OLED_WR_Byte(0x00, 0);
    OLED_WR_Byte(0xD5, 0); OLED_WR_Byte(0xF0, 0);
    OLED_WR_Byte(0xD9, 0); OLED_WR_Byte(0x22, 0);
    OLED_WR_Byte(0xDA, 0); OLED_WR_Byte(0x12, 0);
    OLED_WR_Byte(0xDB, 0); OLED_WR_Byte(0x20, 0);
    OLED_WR_Byte(0x8D, 0); OLED_WR_Byte(0x14, 0); // 电荷泵开 果模块没有外部升压电路，必须开启内部电荷泵（0x14），否则屏幕永远不亮
    OLED_WR_Byte(0xAF, 0);  // 开显示

    OLED_Clear();
}
