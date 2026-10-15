
/************************************************************************/
// 更新时间：2025.11.04
// 2025.10.14 修改：MS5837_GetDepth 计算值错误
/************************************************************************/

#include "ms5837.h"
#include "Gpio.h"

#define MS5837_WRADDR             0xEC
#define MS5837_RDADDR             0xED

#define MS5837_RESET              0x1E
#define MS5837_ADC_READ           0x00
#define MS5837_PROM_READ          0xA0
#define MS5837_CONVERT_D1_2048    0x46	
#define MS5837_CONVERT_D2_2048    0x56
#define MS5837_CONVERT_D1_4096    0x48	
#define MS5837_CONVERT_D2_4096    0x58
#define MS5837_CONVERT_D1_8192    0x4A	
#define MS5837_CONVERT_D2_8192    0x5A

#define DELAY_PRAM		2	// 延时参数

uint16_t  c1_senst1;	// C1 压力灵敏度 SENS|T1		
uint16_t  c2_offt1;		// C2 压力补偿  OFF|T1
uint16_t  c3_tcs;		// C3 温度压力灵敏度系数 TCS
uint16_t  c4_tcc;		// C4 温度系数的压力补偿 TCO
uint16_t  c5_tref;		// C5 参考温度 T|REF
uint16_t  c6_tempsens;	// C6 温度系数的温度 TEMPSENS
uint32_t  C[10];		// 误差修正系数数组

float m_fM5837T, m_fM5837P;		// 压力传感器的压力值，温度值

const float Pa = 100.0f;
const float bar = 0.001f;
const float mbar = 1.0f;

// IIC private function 
void P_I2C_Initialize(void);	// 由于F4xx系列已经占用了I2C_Init函数，因此初始化函数名称有所不同
void P_I2C_SDA_IN(void);
void P_I2C_SDA_OUT(void);
void P_I2C_Start(void);
void P_I2C_Stop(void);
void P_I2C_Ack(void);
void P_I2C_NAck(void);
u8   P_I2C_Wait_Ack(void);
void P_I2C_Send_Byte(u8 txd);
u8   P_I2C_Read_Byte(u8 ack);

/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
** 函数名称: Delay_nMs_nUs
** 功能描述: 延时nms, 或者nus (可通过仿真来判断他的准确度)
** 参数描述：time (us) 注意time<65535				 
:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
void P_Delay_nMs(uint16_t time)  //软件延时，用空循环来"浪费"时间
{ 
	uint16_t i;
    u32 j;
    u32 nLoop = SystemCoreClock/1000;//SystemCoreClock 是系统主频，比如 168MHz 的 F4 就是 168000000
	for(i=0;i<time;i++)
		for(j=0;j<nLoop;j++);
}
void P_Delay_nUs(uint16_t time)  
{ 
	uint16_t i;
    u32 j;
    u32 nLoop = SystemCoreClock/200000;
	for(i=0;i<time;i++)
		for(j=0;j<nLoop;j++);
}

/////////////////////////////////////////////////////////////////////
// MS5837压力传感器控制函数

void MS5837_Init(void)
{	 
	u8 i, inth, intl; 
	P_I2C_Initialize();		// 初始化IIC接口
	
	// 复位MS5837
	P_I2C_Start();
	P_I2C_Send_Byte(0xEC);// MS5837 的 I2C 地址（写模式）
	P_I2C_Wait_Ack();
	P_I2C_Send_Byte(0x1E);	// 发送复位命令
	P_I2C_Wait_Ack();
    P_I2C_Stop();	
	//就是通过 I2C 发一个 0x1E 命令，让传感器重置到初始状态，然后等 50ms 让它启动好。
	P_Delay_nMs(50);       	// 复位后延时（注意这个延时是一定必要的，可以缩短但似乎不能少于20ms）
	 
	for (i=1;i<=6;i++) //读取 6 个出厂校准系数— 从传感器内部 PROM 读出来，存到 C[1]~C[6]
	{
		P_I2C_Start();
		P_I2C_Send_Byte(0xEC);
		P_I2C_Wait_Ack();
		//// 发送读命令 0xA2, 0xA4, 0xA6, 0xA8, 0xAA, 0xAC
		P_I2C_Send_Byte(0xA0 + (i*2));
		P_I2C_Wait_Ack();
		P_I2C_Stop();
		P_Delay_nUs(5);
		P_I2C_Start();
		P_I2C_Send_Byte(0xEC+0x01);  	//进入接收模式
		P_Delay_nUs(5);
		P_I2C_Wait_Ack();
		inth = P_I2C_Read_Byte(1);  	//带ACK的读数据，高字节
		P_Delay_nUs(5);
		intl = P_I2C_Read_Byte(0); 		//最后一个字节NACK，低字节
		P_I2C_Stop();
		C[i] = (((uint16_t)inth << 8) | intl);//拼成16位
	}
	c1_senst1	= C[1];		// C1 压力灵敏度 SENS|T1	
	c2_offt1	= C[2];		// C2 压力补偿  OFF|T1
	c3_tcs		= C[3];		// C3 温度压力灵敏度系数 TCS	
	c4_tcc		= C[4];		// C4 温度系数的压力补偿 TCO
	c5_tref		= C[5];		// C5 参考温度 T|REF	
	c6_tempsens	= C[6];		// C6 温度系数的温度 TEMPSENS
}
/*验证刚才读回来的 6 个校准系数有没有传输错误或损坏。MS5837 出厂时会在 PROM 里存一个 4 位校验值，读回数据后重新算一遍 CRC，和存的值对比，一致就说明数据是好的。*/
unsigned char crc4(unsigned int n_prom[]) // n_prom defined as 8x unsigned int (n_prom[8])
{
	int cnt;				// simple counter
	unsigned int n_rem=0; 	// crc remainder
	unsigned char n_bit;
	n_prom[0]=((n_prom[0]) & 0x0FFF); 	// 把原来存的 CRC 值清掉（高4位置0）
	n_prom[7]=0; 						// 辅助位清0
	for (cnt = 0; cnt < 16; cnt++) 		// 遍历8个16位数据，每次处理1字节，共16字节
	{ 
		// choose LSB or MSB
		if (cnt%2==1) n_rem ^= (unsigned short) ((n_prom[cnt>>1]) & 0x00FF);//低字节
		else n_rem ^= (unsigned short) (n_prom[cnt>>1]>>8);//高字节
		//// 对每一位做 CRC 多项式运算
		for (n_bit = 8; n_bit > 0; n_bit--)
		{
			if (n_rem & (0x8000)) n_rem = (n_rem << 1) ^ 0x3000;
			else n_rem = (n_rem << 1);
		}
	}
	n_rem= ((n_rem >> 12) & 0x000F); 	// final 4-bit remainder is CRC code
	return (n_rem ^ 0x00);
}

/*读d1,d2*/
unsigned long MS583703BA_getConversion(uint8_t command)
{
	unsigned long conversion = 0;
	u8 temp[3];
//发转换命令
	P_I2C_Start();
	P_I2C_Send_Byte(MS5837_WRADDR); 	//写地址
	P_I2C_Wait_Ack();
	P_I2C_Send_Byte(command); 			//写转换命令
	P_I2C_Wait_Ack();
	P_I2C_Stop();
	P_Delay_nMs(DELAY_PRAM);// 等待传感器 ADC 转换完成
	
	P_I2C_Start();
	P_I2C_Send_Byte(MS5837_WRADDR); 	//写地址
	P_I2C_Wait_Ack();
	P_I2C_Send_Byte(0);					//开始读序列
	P_I2C_Wait_Ack();
	P_I2C_Stop();
	P_Delay_nMs(DELAY_PRAM);
	
	P_I2C_Start();
	P_I2C_Send_Byte(MS5837_RDADDR);		//进入接收模式
	P_I2C_Wait_Ack();
	temp[0] = P_I2C_Read_Byte(1);		//带ACK的读数据  bit 23-16
	temp[1] = P_I2C_Read_Byte(1);		//带ACK的读数据  bit 8-15
	temp[2] = P_I2C_Read_Byte(0);		//带NACK的读数据 bit 0-7
	/*ACK（应答）：主机读完一个字节后回一个 ACK，意思是"收到了，继续发"
NACK（非应答）：主机读完最后一个字节回一个 NACK，意思是"收到了，可以停了"*/
	P_I2C_Stop();

	conversion = (unsigned long)temp[0] * 65536 + (unsigned long)temp[1] * 256 + (unsigned long)temp[2];
	return conversion;
}

void MS5837_Getdata(void) 
{
	uint32_t D1, D2;
	int32_t TEMP, dT=0;
	float TEMP2, P2;
	
	int64_t SENS = 0;
	int64_t OFF = 0;
	float SENSi = 0;
	float OFFi = 0;
	float Ti = 0;
	float OFF2 = 0;
	float SENS2 = 0;
	
	// 按照功耗和精度的取舍，设定不同的转换精度并获取数据
	D1 = MS583703BA_getConversion(MS5837_CONVERT_D1_2048);
	D2 = MS583703BA_getConversion(MS5837_CONVERT_D2_2048);

	dT = D2-(uint32_t)C[5]*256l;//// 实际温度和参考温度的差值，加个l是长整型的意思。。
	
	SENS = (int64_t)C[1]*32768l+((int64_t)C[3]*dT)/256l;
	OFF = (int64_t)C[2]*65536l+((int64_t)C[4]*dT)/128l;
	// P = (D1*SENS/(2097152l)-OFF)/(8192l);

	TEMP = 2000l+(int64_t)dT*C[6]/8388608LL;// 初步温度，比如 2500 表示 25.00℃

	if((TEMP/100)<20)
	{
		Ti = (3*(int64_t)dT*(int64_t)dT)/(8589934592LL);
		OFFi = (3*(TEMP-2000)*(TEMP-2000))/2;
		SENSi = (5*(TEMP-2000)*(TEMP-2000))/8;
		if((TEMP/100)<-15)
		{
			OFFi = OFFi+7*(TEMP+1500l)*(TEMP+1500l);
			SENSi = SENSi+4*(TEMP+1500l)*(TEMP+1500l);
		}
	}
	else if((TEMP/100)>=20)
	{
		Ti = 2*(dT*dT)/(137438953472LL);
		OFFi = (1*(TEMP-2000)*(TEMP-2000))/16;
		SENSi = 0;
	}
	
	OFF2 = OFF-OFFi;
	SENS2 = SENS-SENSi;

	TEMP2 = (TEMP-Ti)/100;
	P2 = (((D1*SENS2)/2097152l-OFF2)/8192l)/10;

	m_fM5837P	= P2;		// 返回数据单位：mBar  压力
	m_fM5837T	= TEMP2;	// 返回数据单位：摄氏度   温度
}

float MS5837_GetPressure(void)
{
	MS5837_Getdata();
	return m_fM5837P;	
}

float MS5837_GetDepth(void)
{
	// 不调用函数，直接返回压力值转换的深度值
	float depth = m_fM5837P/100000.0f*10.0f;
	return depth;	
}

float MS5837_GetTemperature(void)
{
	// 不调用函数，直接返回温度值
	return m_fM5837T;
}

/****************************************************************************
* Function Name  : IIC_Config
* Description    : 初始化GPIO.
* Input          : None
* Output         : None
* Return         : None
****************************************************************************/
void P_I2C_Initialize()
{
#if USE_STM32F1XX
	RCC_APB2PeriphClockCmd(P_SCL_APB2Periph, ENABLE);
	RCC_APB2PeriphClockCmd(P_SDA_APB2Periph, ENABLE);
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);		// 由于PB3和PB4和JTAG复用，因此此处需要特殊设置
	GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
	
	GPIO_SetPin(P_SCL_PORT, P_I2C_SCL, GPIO_Mode_Out_PP, GPIO_Speed_50MHz);
	GPIO_SetPin(P_SDA_PORT, P_I2C_SDA, GPIO_Mode_Out_PP, GPIO_Speed_50MHz);
	
#elif USE_STM32F4XX
	RCC_AHB1PeriphClockCmd(P_SCL_AHB1Periph , ENABLE);
	RCC_AHB1PeriphClockCmd(P_SDA_AHB1Periph , ENABLE);

	GPIO_SetOut(P_SCL_PORT, P_I2C_SCL, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
	GPIO_SetOut(P_SDA_PORT, P_I2C_SDA, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);
	/*不封装的话是这样：GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin   = P_I2C_SCL;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(P_SCL_PORT, &GPIO_InitStructure);*/
#endif

	P_I2C_SCL_H;//作用就是让SCL和SDA是高电平，I2C 协议规定总线空闲时两根线都必须是高电平
	P_I2C_SDA_H;
}

/*******************************************************************************
* 函 数 名         : P_I2C_SDA_IN
* 函数功能		   : SDA输入配置	   
* 输    入         : 无
* 输    出         : 无
*******************************************************************************/
void P_I2C_SDA_IN(void)
{
#if USE_STM32F1XX
	GPIO_SetPin(P_SDA_PORT, P_I2C_SDA, GPIO_Mode_IPU, GPIO_Speed_50MHz);
#elif USE_STM32F4XX
	GPIO_SetIn(P_SDA_PORT,	 P_I2C_SDA, GPIO_OType_PP, GPIO_Speed_50MHz, GPIO_PuPd_UP);//本质就是把out换成in了
#endif
}

/*******************************************************************************
* 函 数 名         : P_I2C_SDA_OUT
* 函数功能		   : SDA输出配置	   
* 输    入         : 无
* 输    出         : 无
*******************************************************************************/
void P_I2C_SDA_OUT(void)
{
#if USE_STM32F1XX
	GPIO_SetPin(P_SDA_PORT, P_I2C_SDA, GPIO_Mode_Out_PP, GPIO_Speed_50MHz);
#elif USE_STM32F4XX
	GPIO_SetOut(P_SDA_PORT, P_I2C_SDA, GPIO_OType_PP,    GPIO_Speed_50MHz, GPIO_PuPd_UP);
#endif
}

//产生起始信号
void P_I2C_Start(void)
{
    P_I2C_SDA_OUT();//要产生 Start/Stop 这种时序信号，必须先切成输出模式才行。
	
	P_I2C_SDA_H;
	P_I2C_SCL_H; P_Delay_nUs(5);
	P_I2C_SDA_L; P_Delay_nUs(5);
	P_I2C_SCL_L; P_Delay_nUs(5);
}//起始：SCL 高电平期间，SDA 从高变低

//产生停止信号
void P_I2C_Stop(void)
{
   P_I2C_SDA_OUT();

   P_I2C_SCL_L;
   P_I2C_SDA_L; P_Delay_nUs(5);
   P_I2C_SCL_H; P_Delay_nUs(5);
   P_I2C_SDA_H; P_Delay_nUs(5);
}//stop：SCL 高电平期间，SDA 从低变高
//其他时候（传数据时）SCL 高电平期间 SDA 是不允许变化的，所以这两个信号才能被识别出来。

//主机产生应答信号ACK SCL 高电平期间 SDA 不变 → 正常数据/ACK
void P_I2C_Ack(void)
{
   P_I2C_SCL_L;
   P_I2C_SDA_OUT();
   P_I2C_SDA_L; P_Delay_nUs(5);
   P_I2C_SCL_H; P_Delay_nUs(5);
   P_I2C_SCL_L;
}

//主机不产生应答信号NACK
void P_I2C_NAck(void)
{
   P_I2C_SCL_L;
   P_I2C_SDA_OUT();
   P_I2C_SDA_H; P_Delay_nUs(5);
   P_I2C_SCL_H; P_Delay_nUs(5);
   P_I2C_SCL_L;
}
/*I2C 协议规定，在 SCL 高电平期间读取 SDA 的值：

SDA 低 = ACK，意思是"收到了，继续"
SDA 高 = NACK，意思是"收到了，停止"*/


//等待从机应答信号
//返回值：1 接收应答失败
//		  0 接收应答成功
u8 P_I2C_Wait_Ack(void)
{
	u8 tempTime = 0;
	P_I2C_SDA_IN();
	P_I2C_SDA_H; P_Delay_nUs(5);
	P_I2C_SCL_H; P_Delay_nUs(5);

	while(GPIO_ReadInputDataBit(P_SDA_PORT,P_I2C_SDA))//P_SDA_PORT → SDA 引脚在哪个端口，P_I2C_SDA → SDA 是这个端口的哪个引脚
	{//SDA为高电平的时候就走括号里的内容，如果是0则直接跳出循环，说明收到了 ACK
		if(tempTime++ > 250)
		{
			P_I2C_Stop(); return 1;
		}	 
	}

	P_I2C_SCL_L;
	return 0;
}

//I2C 发送一个字节
void P_I2C_Send_Byte(u8 txd)
{
	u8 i=0;

	P_I2C_SDA_OUT();
	P_I2C_SCL_L;			// 拉低时钟开始数据传输

	for(i=0;i<8;i++)
	{		
		if((txd&0x80)>0) 	// 0x80  1000 0000
		{
			P_I2C_SDA_H; P_Delay_nUs(5);//这一位是1就拉高SDA
		}
		else
		{
			P_I2C_SDA_L; P_Delay_nUs(5);//是0的话就拉低SDA
		}
        txd<<=1;
        P_I2C_SCL_H; P_Delay_nUs(5);//从机在 SCL 高电平期间读取 SDA的值，所以每发送一位数据后都要拉高 SCL 让从机读到当前位的值
        P_I2C_SCL_L; P_Delay_nUs(5);
	} 
}

//I2C 读取一个字节
u8 P_I2C_Read_Byte(u8 ack)
{
   u8 i=0,receive=0;

   P_I2C_SDA_IN();
   for(i=0;i<8;i++)
   {
   		P_I2C_SCL_L; P_Delay_nUs(5);
		P_I2C_SCL_H;
		receive<<=1;//所以左移的作用是把已经收到的位往高位挪，给新来的位腾出最低位的位置
		if(GPIO_ReadInputDataBit(P_SDA_PORT,P_I2C_SDA))
		   receive++;//receive++ 就是往最低位写入当前读到的 1
		P_Delay_nUs(5);	
   }

   	if(ack==0)
	   	P_I2C_NAck();
	else
		P_I2C_Ack();

	return receive;
}

/*********************************************************************************************************
** End of File
*********************************************************************************************************/
