
#include "ADC_operate.h"
#include "IO_operate.h"

#if USE_STM32H7XX
#include "STM32H7_operate.h"
ADC_HandleTypeDef  m_hadc1;
#endif

bool  m_bAdcRdy = false;				// AD初始化状态
int   m_nCal = 0;						// 当前采集值对应的数组下标，当前读的是第几个采集值
float m_fLiPowerVoltage = 0;			// 系统电池电压显示
float m_fVolt[VoltageBuf_MaxSize];// 存电压的数组，大小是VoltageBuf_MaxSize

void ADC_InitSingleDMA(bool bEn, uint32_t* pBuf1, u32 uBufSize);					// 设置单DMA工作模式
void ADC_InitDoubleDMA(bool bEn, uint32_t* pBuf1, uint32_t* pBuf2, u32 uBufSize);	// 设置双缓冲DMA工作模式
//传入两个参数： G_Pin → 用哪个 GPIO 引脚 nCh → 用 ADC 的哪个通道
bool ADC1_CH_INIT(uint16_t G_Pin, uint8_t nCh)
{
	// ADC1_CH*_INIT
#if USE_STM32F1XX || USE_STM32F4XX
	GPIO_InitTypeDef GPIO_InitStructure = {0};//全部置零，防止有脏数据
	ADC_InitTypeDef  ADC_InitStructure  = {0};
#endif
	
#if USE_STM32F1XX	
	/* Enable ADC1 and GPIOA clock */
	RCC_ADCCLKConfig(RCC_PCLK2_Div8);		/* ADCCLK = PCLK2/8 */
	if (nCh < 8)
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);
	else return false;

	GPIO_InitStructure.GPIO_Pin  = G_Pin;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	/* ADC1 configuration */
	ADC_InitStructure.ADC_Mode 			= ADC_Mode_Independent;					// ADC独立转换模式，相对于双重模式
	ADC_InitStructure.ADC_ScanConvMode 	= DISABLE;								// 禁止扫描模式，扫描模式用于多通道采集
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;							// 单次转换模式
	ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;		// 不使用外部触发转换
	ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;				// 采集数据右对齐
	ADC_InitStructure.ADC_NbrOfChannel  = 1;									// 要转换的通道数目
	ADC_Init(ADC1, &ADC_InitStructure);
	
	/* ADC1 regular channel configuration */ 
	// AD采样率为 ADCCLK/(239.5+12.5)
	ADC_RegularChannelConfig(ADC1, nCh, 1, ADC_SampleTime_239Cycles5);

	/* Enable ADC1 */
	ADC_Cmd(ADC1, ENABLE);

	/* Enable ADC1 reset calibaration register */   
	ADC_ResetCalibration(ADC1);
	/* Check the end of ADC1 reset calibration register */
	while(ADC_GetResetCalibrationStatus(ADC1));

	/* Start ADC1 calibaration */
	ADC_StartCalibration(ADC1);
	/* Check the end of ADC1 calibration */
	while(ADC_GetCalibrationStatus(ADC1));
	return true;
	
#elif USE_STM32F4XX
	assert_param(IS_GPIO_PIN(G_Pin));//安全检查，确认传进来的引脚参数是合法的
	
	/* Enable ADC1 and GPIOC clock */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);//GPIO 在 AHB1，ADC1 在 APB2
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
	
	GPIO_InitStructure.GPIO_Pin  = G_Pin;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;//模拟输入模式，因为要读的是模拟电压值，不是高低电平。
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL ;//不上拉不下拉
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	/* ADC1 configuration */
	ADC_InitStructure.ADC_Resolution			= ADC_Resolution_12b;// 12位精度，能分辨 0~4095 个刻度
	ADC_InitStructure.ADC_ScanConvMode			= DISABLE;// 不扫描多个通道，只用一个
	ADC_InitStructure.ADC_ContinuousConvMode	= DISABLE;// 不连续转换，手动触发一次转一次
	ADC_InitStructure.ADC_ExternalTrigConv		= 0;								// 不使用外部触发转换
	ADC_InitStructure.ADC_ExternalTrigConvEdge 	= ADC_ExternalTrigConvEdge_None;	// 触发模式：外部触发禁止
	ADC_InitStructure.ADC_DataAlign 			= ADC_DataAlign_Right;				// 数据右对齐，比如 0000 1111 1111 1111
	ADC_InitStructure.ADC_NbrOfConversion 		= 1;								// 设置进行规则转换的ADC通道数目，此处为1个通道
	ADC_Init(ADC1, &ADC_InitStructure);
	// ADC_DMAAccessMode_1

	/* ADC1 regular channels configuration */ 
	// 如果系统时钟频率比较高，则采样周期值必须调大，否则会引起测量不准确
	ADC_RegularChannelConfig(ADC1, nCh, 1, ADC_SampleTime_480Cycles);//采样周期设了 480 个时钟周期
//用哪个ADC 用第几个通道 转换顺序 采样周期
	/* Enable ADC1 DMA since ADC1 is the Master*/
	// ADC_DMACmd(ADC1, ENABLE); 
	
	/* Enable ADC1 */
	ADC_TempSensorVrefintCmd(ENABLE);	// 使能温度传感器
	ADC_Cmd(ADC1, ENABLE);
	
	return true;
#else
	return false;
#endif
}

bool ADC1_CH_DEINIT()//是INIT反过来
{
	// ADC1_CH*_DEINIT
#if USE_STM32F1XX	
	/* Enable ADC1 and GPIOA clock */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, DISABLE);

	/* Disable ADC1 */
	ADC_Cmd(ADC1, DISABLE);
#elif USE_STM32F4XX
	/* Enable ADC1 and GPIOC clock */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, DISABLE);
	
	/* Disable ADC1 */
	ADC_TempSensorVrefintCmd(DISABLE);	// 使能温度传感器
	ADC_Cmd(ADC1, DISABLE);
#endif
	return false;
}

bool ADC1_CH_INIT_AUTO(bool bEn, uint16_t G_Pin, uint8_t nCh, uint32_t* pBuf1, uint32_t* pBuf2, u32 uBufSize, u8 uSpeed)//uspeed是采样速度
{
	// ADC1_CH*_INIT_AUTO，设定为自动采集
#if USE_STM32F1XX
	GPIO_InitTypeDef GPIO_InitStructure;
	ADC_InitTypeDef  ADC_InitStructure;					// ADC的一般初始化配置
#elif USE_STM32F4XX
	GPIO_InitTypeDef GPIO_InitStructure;
	ADC_InitTypeDef  ADC_InitStructure;					// ADC的一般初始化配置
	ADC_CommonInitTypeDef ADC_CommonInitStructure;		// ADC初始化配置包括模式，时钟分频，采样周期等
#endif
	
#if USE_STM32F1XX	
	/* Enable ADC1 and GPIOA clock */
	RCC_ADCCLKConfig(RCC_PCLK2_Div8);		/* ADCCLK = PCLK2/8 */
	if (nCh < 8)
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);
	else if (nCh >= 8 && nCh < 15)
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOC, ENABLE);
	else
		return false;

	GPIO_InitStructure.GPIO_Pin = G_Pin;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	/* ADC1 configuration */
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;					// ADC独立转换模式，相对于双重模式
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;						// 禁止扫描模式，扫描模式用于多通道采集
	ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;					// 连续转换模式
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;	// 不使用外部触发转换
	ADC_InitStructure.ADC_ExternalTrigConv = 0;							// 外部触发通道，使用软件触发时，此值随便赋值即可
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;				// 采集数据右对齐
	ADC_InitStructure.ADC_NbrOfChannel = 1;								// 要转换的通道数目
	ADC_Init(ADC1, &ADC_InitStructure);
	
	/* ADC1 regular channel configuration */ 
	// AD采样率为 ADCCLK/(239.5+12.5)
	ADC_RegularChannelConfig(ADC1, nCh, 1, ADC_SampleTime_239Cycles5);

	/* Enable ADC1 */
	ADC_Cmd(ADC1, ENABLE);

	/* Enable ADC1 reset calibaration register */   
	ADC_ResetCalibration(ADC1);
	/* Check the end of ADC1 reset calibration register */
	while(ADC_GetResetCalibrationStatus(ADC1));

	/* Start ADC1 calibaration */
	ADC_StartCalibration(ADC1);
	/* Check the end of ADC1 calibration */
	while(ADC_GetCalibrationStatus(ADC1));
	return true;
	
#elif USE_STM32F4XX
	assert_param(IS_GPIO_PIN(G_Pin));
	
	if (bEn)//这是在开启ADC1的自动采集功能，配置ADC1为连续采集模式，并且配合单缓冲DMA把数据搬到pBuf1里
	{
		/* Enable ADC1 and GPIOC clock */
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
		
		GPIO_InitStructure.GPIO_Pin  = G_Pin;//外部传进来的
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;//专门给 ADC 采集电压用
		GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL ;
		GPIO_Init(GPIOA, &GPIO_InitStructure);
		
		/* ADC1 configuration */
		ADC_InitStructure.ADC_Resolution   = ADC_Resolution_12b;
		ADC_InitStructure.ADC_ScanConvMode = DISABLE;
		ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;								// 允许连续采集
		ADC_InitStructure.ADC_ExternalTrigConv     = 0;									// 不使用外部触发转换
		ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;		// 触发模式
		ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;							// 数据右对齐
		ADC_InitStructure.ADC_NbrOfConversion = 1;										// 设置进行规则转换的ADC通道数目，此处为1个通道
		ADC_Init(ADC1, &ADC_InitStructure);
		
		
		/* ADC Common 结构体 参数 初始化  ADC_Init       → 配置 ADC1 自己私有的参数
ADC_CommonInit → 配置 ADC 时钟这种公共参数（即使只有一个ADC也得走这里） */
		ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;						// 独立ADC模式
		ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div8;						// 时钟，PCLK2/8 = xxM 超过36M准确度会下降  ADC 挂在 APB2 上，所以分的是 APB2 的时钟，不是 AHB1 的
		ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;			// 禁止DMA直接访问模式       
		ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_20Cycles;	// 采样时间间隔       两次采样之间间隔20个周期 	
		ADC_CommonInit(&ADC_CommonInitStructure); 										// 初始化ADC Common 结构体      
	  
		/* ADC1 regular channels configuration */ 
		// 如果系统时钟频率比较高，则采样周期值必须调大，否则会引起测量不准确
		// 采样率 = PCLK2/8/(12+ADC_SampleTime_XXCycles)
		ADC_RegularChannelConfig(ADC1, nCh, 1, uSpeed);//其实uspeed本质也是采样时间
		ADC_ClearFlag(ADC1, ADC_FLAG_STRT|ADC_FLAG_OVR);	// 必须清除开始和结束标志
		
		ADC_InitSingleDMA(bEn, pBuf1, uBufSize);			// 设置单缓冲模式 配合连续采集开启了单缓冲 DMA，数据自动搬到 pBuf1 缓冲区里
		
		/* Enable ADC1 */
		ADC_TempSensorVrefintCmd(ENABLE);	// 使能温度传感器
		ADC_Cmd(ADC1, ENABLE);				// 使能ADC1
	}
	else//是在关闭ADC1的自动采集功能，回到最初的单次采集模式
	{
		/* Disable ADC1 and GPIOC clock */
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, DISABLE);
		
		GPIO_InitStructure.GPIO_Pin = G_Pin;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
		GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL ;
		GPIO_Init(GPIOA, &GPIO_InitStructure);   
		
		/* ADC1 regular channels configuration */ 
		// 如果系统时钟频率比较高，则采样周期值必须调大，否则会引起测量不准确
		ADC_RegularChannelConfig(ADC1, nCh, 1, uSpeed);
			
		ADC_InitSingleDMA(bEn, pBuf1, uBufSize);
		
		/* Enable ADC1 */
		ADC_TempSensorVrefintCmd(DISABLE);	// 禁用温度传感器
		ADC_Cmd(ADC1, DISABLE);				// 禁用ADC1
	}
	return true;
#else
	return false;
#endif
}

float GetADC1Voltage(void)
{
	/* 由于没有采用外部触发，所以使用软件触发ADC转换 */ 
	u16 uAdc;
	float fVoltage;
	
#if USE_STM32F1XX
	u32 uOverTime = 0x00000FFFF;
	ADC_SoftwareStartConvCmd(ADC1, ENABLE);
	//检查制定ADC标志位置1与否 ADC_FLAG_EOC 转换结束标志位
	while(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET && uOverTime > 0) 
	{
		if ( ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) )
		{
			ADC_ClearFlag(ADC1, ADC_FLAG_EOC); break;
		}
		else uOverTime--;
	};
	ADC_SoftwareStartConvCmd(ADC1, DISABLE);
#elif USE_STM32F4XX
	u32 uOverTime = 0x000FFFFFF;// 超时计数器
	ADC_SoftwareStartConv(ADC1);//就是用软件手动触发一次ADC开始转换，因为之前配置里关了外部触发，所以得自己动手
	//检查制定ADC标志位置1与否 ADC_FLAG_EOC 转换结束标志位
	while(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET && uOverTime > 0) //如果转换没完成且没超时
	{//就是在空转等待，这种方式叫轮询，就是 CPU 一直盯着 ADC，等它转换完，什么都不能干了，所以才有了中断和DMA这种更高级的方式来避免CPU空转等待
		if ( ADC_GetFlagStatus(ADC1, ADC_FLAG_OVR) )//检查是否溢出
		{
			ADC_ClearFlag(ADC1, ADC_FLAG_OVR); break;//如果溢出就清除溢出标志位并跳出循环，避免死循环
		}
		else uOverTime--;
	}
#endif

#if USE_STM32F1XX || USE_STM32F4XX
	uAdc = ADC_GetConversionValue(ADC1); 		// 得到转换后的值			
	ADC_ClearFlag(ADC1, ADC_FLAG_EOC);			// 清除转换结束中断标志	
#elif USE_STM32H7XX
	uAdc = STM32H7_Get_ADCValue();				// 转换并得到ADC的数据
#endif
	
	fVoltage = (float)uAdc;
	fVoltage = fVoltage*(float)ADC_REF_U/4096.0f;
	return fVoltage;
}

bool ADC1_Initialize(uint32_t nCh, bool bInitBuf)//// 封装后，只需要传通道号
{
	// AD initialize, ADC1 1-16 channel
#if USE_STM32F1XX || USE_STM32F4XX
	switch (nCh)
	{
		case 0 :  m_bAdcRdy = ADC1_CH_INIT(GPIO_Pin_0,  nCh); m_bAdcRdy = true; break;
		case 1 :  m_bAdcRdy = ADC1_CH_INIT(GPIO_Pin_1,  nCh); m_bAdcRdy = true; break;
		case 2 :  m_bAdcRdy = ADC1_CH_INIT(GPIO_Pin_2,  nCh); m_bAdcRdy = true; break;
		case 3 :  m_bAdcRdy = ADC1_CH_INIT(GPIO_Pin_3,  nCh); m_bAdcRdy = true; break;
		case 4 :  m_bAdcRdy = ADC1_CH_INIT(GPIO_Pin_4,  nCh); m_bAdcRdy = true; break;
		case 5 :  m_bAdcRdy = ADC1_CH_INIT(GPIO_Pin_5,  nCh); m_bAdcRdy = true; break;
		case 6 :  m_bAdcRdy = ADC1_CH_INIT(GPIO_Pin_6,  nCh); m_bAdcRdy = true; break;
		case 7 :  m_bAdcRdy = ADC1_CH_INIT(GPIO_Pin_7,  nCh); m_bAdcRdy = true; break;
		default : m_bAdcRdy = false;
	}
	/*switch(nCh)        // 拿着 nCh 去匹配
{
    case 0:        // nCh == 0 走这里
        ...
        break;     // 做完跳出去
    
    case 1:        // nCh == 1 走这里
        ...
        break;
    
    case 2:        // nCh == 2 走这里
        ...
        break;
    
    default:       // 都没匹配上走这里
        ...
}*/
#elif USE_STM32H7XX
	m_bAdcRdy = STM32H7_Init_ADC(nCh); 
#endif

	if ( m_bAdcRdy && bInitBuf)//如果 bInitBuf=true，初始化完就立刻采集一批电压值填满 m_fVolt[] 数组，这样后续读电压的时候缓冲区里已经有数据了，不会读到空值
	{
		int i;
		m_nCal = 0;	delay(5);
		for (i = 0; i < VoltageBuf_MaxSize; i++)
		{
			m_fVolt[i] = GetADC1Voltage();	delay(2);
		}
	}
	return m_bAdcRdy;
}

bool ADC1_DeInitialize()
{
	if ( m_bAdcRdy )
	{
		ADC1_CH_DEINIT();
		m_bAdcRdy = false;
	}
	return m_bAdcRdy;
}

float GetVoltage(void)
{
	// get voltage, not use limit set
	if ( m_bAdcRdy )
	{
		return GetADC1Voltage();
	}
	return 0.0f;
}

float GetLiPowerVoltage(void)
{
	/* 由于没有采用外部触发，所以使用软件触发ADC转换 */ 
	if ( m_bAdcRdy )
	{
		int i;
		float fVoltage, fData = 0;
		fVoltage = GetADC1Voltage();
		if ( fVoltage <= 0 ) return -1;
	
		// 对转化结果进行平滑滤波，提高精度
		m_fVolt[m_nCal++] = fVoltage;
		for (i=0; i<VoltageBuf_MaxSize; i++)
		{
			fData += m_fVolt[i];
		}//ADC 读到分压后的电压，然后加起来
		m_fLiPowerVoltage = (fData/(float)INPUT_DIVIDE_U) / (float)VoltageBuf_MaxSize;//INPUT_DIVIDE_U是分压系数，是0-1的值，把电压值从分压后的值还原成实际电压值
		if (m_nCal == VoltageBuf_MaxSize) m_nCal = 0;
		if(m_fLiPowerVoltage > VoltLi_UpLimit) m_fLiPowerVoltage = VoltLi_UpLimit;//限定幅度
		
		return m_fLiPowerVoltage;
	}
	return -1;
}

float GetLiPowerVoltCorrect(float fVolt, float fCoef1, float fCoef2)//这个函数是对电压做线性校正，补偿硬件误差
{/*比例误差：测到的值和真实值成比例偏差
比如真实7.4V，测出来总是7.2V，差了一个固定比例
→ 用 fCoef1 修正

偏移误差：测到的值总是多或少一个固定值
比如真实7.4V，测出来总是7.6V，多了0.2V
→ 用 fCoef2 修正*/
	// 利用1阶线性拟合修正的电压测量结果
	m_fLiPowerVoltage = fVolt*fCoef1 + fCoef2;
	return m_fLiPowerVoltage;
}

float GetLiChargePercent(void)//把电池电压换算成电量百分比
{
	// 获得当前充电的百分比
	float fPercent = 0;
	if (m_fLiPowerVoltage > VoltLi_LowLimit && m_fLiPowerVoltage < VoltLi_UpLimit - 0.2f)
	{
		fPercent = (m_fLiPowerVoltage - VoltLi_LowLimit)/(VoltLi_UpLimit-VoltLi_LowLimit)*100.0f;
	}
	else if (m_fLiPowerVoltage <= VoltLi_LowLimit)
	{
		fPercent = 0;
	}
	else
	{
		fPercent = 100;
	}
	// 避免零值的存在
	if (fPercent < 1) fPercent = 1;
	
	return fPercent;
}

void ShowBatteryStatus(void)
{
	// show Li-on battery power status
	int nPercent = GetLiChargePercent();
	if (nPercent < 100)
	{
		SetArmLedG(true);	// 为了显示充电状态，在连接时，根据电压点亮红绿灯
		SetArmLedR(true);
		delay(nPercent*9);//点亮越多亮的时间越久
		SetArmLedG(false);
		SetArmLedR(false);
	}
	else
	{
		SetArmLedG(true);	// // 绿灯常亮，不灭
	}
	m_stLedTrig.bShowBat = false;// 显示完了，把触发标志清掉
}

void ShowBatteryStatusLedG(void)
{
	// show Li-on battery power status, Only Green LED
	int nPercent = GetLiChargePercent();
	if (nPercent < 100)
	{
		SetArmLedG(true);	// 为了显示充电状态，在连接时，根据电压点亮绿灯
		delay(nPercent*9);
		SetArmLedG(false);
	}
	else
	{
		SetArmLedG(true);	// 充满电后，直接点亮绿灯
	}
	m_stLedTrig.bShowBat = false;
}
	
void ADC_InitAutoAcq(bool bEn, uint8_t nCh, uint32_t* pBuf1, uint32_t* pBuf2, u32 uBufSize, u8 uSpeed)
{
	// 初始化某一通道的AD为连续自动采集，采样率在函数内设定
	// AD initialize, ADC1 1-16 channel
	switch (nCh)
	{
	#if USE_STM32F1XX || USE_STM32F4XX
		case 0  : m_bAdcRdy = ADC1_CH_INIT_AUTO(bEn, GPIO_Pin_0,  nCh, pBuf1, pBuf2, uBufSize, uSpeed); break;
		case 1  : m_bAdcRdy = ADC1_CH_INIT_AUTO(bEn, GPIO_Pin_1,  nCh, pBuf1, pBuf2, uBufSize, uSpeed); break;
		case 2  : m_bAdcRdy = ADC1_CH_INIT_AUTO(bEn, GPIO_Pin_2,  nCh, pBuf1, pBuf2, uBufSize, uSpeed); break;
		case 3  : m_bAdcRdy = ADC1_CH_INIT_AUTO(bEn, GPIO_Pin_3,  nCh, pBuf1, pBuf2, uBufSize, uSpeed); break;
		case 4  : m_bAdcRdy = ADC1_CH_INIT_AUTO(bEn, GPIO_Pin_4,  nCh, pBuf1, pBuf2, uBufSize, uSpeed); break;
		case 5  : m_bAdcRdy = ADC1_CH_INIT_AUTO(bEn, GPIO_Pin_5,  nCh, pBuf1, pBuf2, uBufSize, uSpeed); break;
		case 6  : m_bAdcRdy = ADC1_CH_INIT_AUTO(bEn, GPIO_Pin_6,  nCh, pBuf1, pBuf2, uBufSize, uSpeed); break;
		case 7  : m_bAdcRdy = ADC1_CH_INIT_AUTO(bEn, GPIO_Pin_7,  nCh, pBuf1, pBuf2, uBufSize, uSpeed); break;
		default : return;
	#endif
	}
}

void ADC_InitSingleDMA(bool bEn, uint32_t* pBuf1, u32 uBufSize)
{
	// 设置单DMA工作模式
#if USE_STM32F4XX
	DMA_InitTypeDef DMA_InitStructure;           		// DMA初始化结构体
	if (bEn)
	{
		/* DMA Iint 结构体参数 初始化 */
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);			// 开启DMA时钟
		
		DMA_DeInit(DMA2_Stream4);  	// see datasheet table43
		while (DMA_GetCmdStatus(DMA2_Stream4) != DISABLE){}
			
		DMA_InitStructure.DMA_PeripheralBaseAddr 	=(u32)&ADC1->DR;					// 外设基址为：ADC 数据寄存器地址    
		DMA_InitStructure.DMA_Memory0BaseAddr 		=(u32)pBuf1;						// 存放ADC转换值的数组地址(存放ADC转换值的寄存器对应输出数据到数组)
		DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;     	// 数据传输方向为外设到存储器  
		/*PeripheralToMemory  → 外设到内存（咱们用的，ADC→缓冲区）
        MemoryToPeripheral  → 内存到外设（比如内存数据发到SPI）
        MemoryToMemory      → 内存到内存（纯数据拷贝）*/
		DMA_InitStructure.DMA_BufferSize 			= uBufSize;           				// 缓冲区大小为，指一次传输的数据量
		DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;		// 外设寄存器只有一个，地址不用递增
		/*Disable  → 外设地址固定不动（咱们用的，ADC寄存器就一个）
        Enable   → 每次传输后外设地址+1（读多个外设寄存器时用）*/
		DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;         	// 地址增量变化
		/*Enable   → 每次传输后内存地址+1（咱们用的，依次存到数组）
        Disable  → 内存地址固定不动（一直覆盖同一个位置）*/
		DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_HalfWord;	// 外设数据大小为半字，即两个字节，STM32是32位的，所以一个字是32位，半字是16位
		/*Byte      → 8位
        HalfWord  → 16位（咱们用的，ADC是12位结果存在16位里）
        Word      → 32位*/
		DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_HalfWord;		// 存储器数据大小也为半字，跟外设数据大小相同
		DMA_InitStructure.DMA_Mode 					= DMA_Mode_Normal;		 			// 非循环传输模式,减小数据处理的压力
		/*Normal    → 传完uBufSize个数据就停（咱们用的）
        Circular  → 传完自动从头开始循环，不停传*/
		DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;				// DMA 传输通道优先级为高，当使用一个DMA通道时，优先级设置不影响
		/*Low       → 低优先级
        Medium    → 中优先级
        High      → 高优先级（咱们用的）
        VeryHigh  → 最高优先级*/
		DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable; 			// 禁止DMA FIFO，使用直连模式   FIFO 模式更适合需要批量传输、追求总线效率的场景
		/*Disable  → 直连模式（咱们用的，来一个传一个）
        Enable   → FIFO模式（攒一批再传）*/
		DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;		// FIFO 大小，FIFO模式禁止时，这个不用配置  
		DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
		/*Single   → 每次传输一个数据（咱们用的）
		Incremental → 每次传输4个数据
		WrappedIncremental → 每次传输4个数据，传完了自动回到第一个*/
		DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;  
		
		DMA_InitStructure.DMA_Channel = DMA_Channel_0;					// 选择 DMA 通道
		DMA_Init(DMA2_Stream4, &DMA_InitStructure);						// 初始化DMA流
		DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TCIF4);
		DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TEIF4);
		DMA_ITConfig(DMA2_Stream4, DMA_IT_TC, ENABLE);
		DMA_Cmd(DMA2_Stream4, ENABLE);									// 使能DMA流，必须放在ADC使能前
		
		/* Enable ADC1 DMA since ADC1 is the Master*/
		ADC_DMARequestAfterLastTransferCmd(ADC1,ENABLE); 				// 源数据变化时开启DMA传输
		ADC_DMACmd(ADC1, ENABLE); 
	}
	else
	{
		/* DMA Iint 结构体参数 初始化 */
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, DISABLE);			// 关闭DMA时钟
		
		DMA_InitStructure.DMA_PeripheralBaseAddr 	=(u32)&ADC1->DR;					// 外设基址为：ADC 数据寄存器地址    
		DMA_InitStructure.DMA_Memory0BaseAddr 		=(u32)pBuf1;						// 存放ADC转换值的数组地址(存放ADC转换值的寄存器对应输出数据到数组)
		DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;     	// 数据传输方向为外设到存储器  
		DMA_InitStructure.DMA_BufferSize 			= uBufSize;           				// 缓冲区大小为，指一次传输的数据量
		DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;		// 外设寄存器只有一个，地址不用递增
		DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;         	// 地址增量变化
		DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_HalfWord;	// 外设数据大小为半字，即两个字节，STM32是32位的，所以一个字是32位，半字是16位
		DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_HalfWord;		// 存储器数据大小也为半字，跟外设数据大小相同
		DMA_InitStructure.DMA_Mode 					= DMA_Mode_Normal;		 			// 循环传输模式 （ADC要不断采集数据，所以要循环模式）
		DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;				// DMA 传输通道优先级为高，当使用一个DMA通道时，优先级设置不影响
		DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable; 			// 禁止DMA FIFO，使用直连模式 
		DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;		// FIFO 大小，FIFO模式禁止时，这个不用配置  
		DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
		DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;  
		
		DMA_InitStructure.DMA_Channel = DMA_Channel_0;					// 选择 DMA 通道
		DMA_Init(DMA2_Stream4, &DMA_InitStructure);						// 初始化DMA流，
		DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TCIF4);// 清除传输完成标志
		DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TEIF4);// 清除传输错误标志
		DMA_ITConfig(DMA2_Stream4, DMA_IT_TC, DISABLE);//DMA_ITConfig 中断配置 ENABLE，传完一批数据通知CPU DISABLE，不需要通知 配置DMA工作完了要不要通知CPU
		DMA_Cmd(DMA2_Stream4, DISABLE);									// 使能DMA流，必须放在ADC使能前

		/* Disable ADC1 DMA since ADC1 is the Master*/
		ADC_DMARequestAfterLastTransferCmd(ADC1,DISABLE); 				// 源数据变化时开启DMA传输
		ADC_DMACmd(ADC1, DISABLE); 
	}	
#endif
}

void ADC_InitDoubleDMA(bool bEn, uint32_t* pBuf1, uint32_t* pBuf2, u32 uBufSize)
{
	// 设置双缓冲DMA工作模式
#if USE_STM32F4XX
	DMA_InitTypeDef DMA_InitStructure;           		// DMA初始化结构体
	if (bEn)
	{
		/* DMA Iint 结构体参数 初始化 */
		RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);			// 开启DMA时钟
		
		DMA_DeInit(DMA2_Stream4);  	// see datasheet table43
		while (DMA_GetCmdStatus(DMA2_Stream4) != DISABLE){}
			
		DMA_InitStructure.DMA_PeripheralBaseAddr 	=(u32)&ADC1->DR;					// 外设基址为：ADC 数据寄存器地址    
		DMA_InitStructure.DMA_Memory0BaseAddr 		=(u32)pBuf1;						// 存放ADC转换值的数组地址(存放ADC转换值的寄存器对应输出数据到数组)
		DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;     	// 数据传输方向为外设到存储器  
		DMA_InitStructure.DMA_BufferSize 			= uBufSize;           				// 缓冲区大小为，指一次传输的数据量
		DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;		// 外设寄存器只有一个，地址不用递增
		DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;         	// 地址增量变化
		DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_HalfWord;	// 外设数据大小为半字，即两个字节，STM32是32位的，所以一个字是32位，半字是16位
		DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_HalfWord;		// 存储器数据大小也为半字，跟外设数据大小相同
		DMA_InitStructure.DMA_Mode 					= DMA_Mode_Circular;		 		// 非循环传输模式,减小数据处理的压力
		DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;				// DMA 传输通道优先级为高，当使用一个DMA通道时，优先级设置不影响
		DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable; 			// 禁止DMA FIFO，使用直连模式 
		DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;		// FIFO 大小，FIFO模式禁止时，这个不用配置  
		DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
		DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;  
		DMA_DoubleBufferModeConfig(DMA2_Stream4, (uint32_t)pBuf2, DMA_Memory_0);		// 设置第二个缓冲区，并指定第一个缓冲区首先填充 DMA_Memory_0 是指定先填哪个缓冲区
		/*DMA_Memory_0 → pBuf1（第一个缓冲区）
DMA_Memory_1 → pBuf2（第二个缓冲区）*/
		DMA_DoubleBufferModeCmd(DMA2_Stream4, ENABLE);
		
		DMA_InitStructure.DMA_Channel = DMA_Channel_0;					// 选择 DMA 通道
		DMA_Init(DMA2_Stream4, &DMA_InitStructure);						// 初始化DMA流
		DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TCIF4);
		DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TEIF4);
		DMA_ITConfig(DMA2_Stream4, DMA_IT_TC, ENABLE);
		DMA_Cmd(DMA2_Stream4, ENABLE);									// 使能DMA流，必须放在ADC使能前
		
		/* Enable ADC1 DMA since ADC1 is the Master*/
		ADC_DMARequestAfterLastTransferCmd(ADC1,ENABLE); 				// 源数据变化时开启DMA传输
		ADC_DMACmd(ADC1, ENABLE); 
		
	}
	else
	{
		DMA_InitStructure.DMA_PeripheralBaseAddr 	=(u32)&ADC1->DR;					// 外设基址为：ADC 数据寄存器地址    
		DMA_InitStructure.DMA_Memory0BaseAddr 		=(u32)pBuf1;						// 存放ADC转换值的数组地址(存放ADC转换值的寄存器对应输出数据到数组)
		DMA_InitStructure.DMA_DIR 					= DMA_DIR_PeripheralToMemory;     	// 数据传输方向为外设到存储器  
		DMA_InitStructure.DMA_BufferSize 			= uBufSize;           				// 缓冲区大小为，指一次传输的数据量
		DMA_InitStructure.DMA_PeripheralInc 		= DMA_PeripheralInc_Disable;		// 外设寄存器只有一个，地址不用递增
		DMA_InitStructure.DMA_MemoryInc 			= DMA_MemoryInc_Enable;         	// 地址增量变化
		DMA_InitStructure.DMA_PeripheralDataSize 	= DMA_PeripheralDataSize_HalfWord;	// 外设数据大小为半字，即两个字节，STM32是32位的，所以一个字是32位，半字是16位
		DMA_InitStructure.DMA_MemoryDataSize 		= DMA_MemoryDataSize_HalfWord;		// 存储器数据大小也为半字，跟外设数据大小相同
		DMA_InitStructure.DMA_Mode 					= DMA_Mode_Circular;		 		// 循环传输模式 （ADC要不断采集数据，所以要循环模式）
		DMA_InitStructure.DMA_Priority 				= DMA_Priority_High;				// DMA 传输通道优先级为高，当使用一个DMA通道时，优先级设置不影响
		DMA_InitStructure.DMA_FIFOMode 				= DMA_FIFOMode_Disable; 			// 禁止DMA FIFO，使用直连模式 
		DMA_InitStructure.DMA_FIFOThreshold 		= DMA_FIFOThreshold_HalfFull;		// FIFO 大小，FIFO模式禁止时，这个不用配置  
		DMA_InitStructure.DMA_MemoryBurst 			= DMA_MemoryBurst_Single;
		DMA_InitStructure.DMA_PeripheralBurst 		= DMA_PeripheralBurst_Single;  
		DMA_DoubleBufferModeConfig(DMA2_Stream4, (uint32_t)pBuf2, DMA_Memory_0);		// 设置第二个缓冲区，并指定第一个缓冲区首先填充
		DMA_DoubleBufferModeCmd(DMA2_Stream4, DISABLE);
		
		DMA_InitStructure.DMA_Channel = DMA_Channel_0;					// 选择 DMA 通道
		DMA_Init(DMA2_Stream4, &DMA_InitStructure);						// 初始化DMA流
		DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TCIF4);
		DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TEIF4);
		DMA_ITConfig(DMA2_Stream4, DMA_IT_TC, DISABLE);
		DMA_Cmd(DMA2_Stream4, DISABLE);									// 使能DMA流，必须放在ADC使能前	
		
		/* Disable ADC1 DMA since ADC1 is the Master*/
		ADC_DMARequestAfterLastTransferCmd(ADC1,DISABLE); 				// 源数据变化时开启DMA传输
		ADC_DMACmd(ADC1, DISABLE);
	}
#endif
}

void ADC_SwitchSingleDMABuf(uint32_t* pBuf1, u32 uBufSize)
{
	// 在不改变配置的情况下，切换缓冲区
#if USE_STM32F4XX
	u32 uOvTime = 0x01FFFFFF;
	DMA_InitTypeDef DMA_InitStructure;           		// DMA初始化结构体
	
	DMA_DeInit(DMA2_Stream4);  							// see datasheet table43
	while (DMA_GetCmdStatus(DMA2_Stream4) != DISABLE && uOvTime-- > 0){}
		
	DMA_InitStructure.DMA_PeripheralBaseAddr =(u32)&ADC1->DR;					// 外设基址为：ADC 数据寄存器地址    
	DMA_InitStructure.DMA_Memory0BaseAddr 	 =(u32)pBuf1;						// 存放ADC转换值的数组地址(存放ADC转换值的寄存器对应输出数据到数组)
	DMA_InitStructure.DMA_DIR 				 = DMA_DIR_PeripheralToMemory;     	// 数据传输方向为外设到存储器  
	DMA_InitStructure.DMA_BufferSize 		 = uBufSize;           				// 缓冲区大小为，指一次传输的数据量
	DMA_InitStructure.DMA_PeripheralInc 	 = DMA_PeripheralInc_Disable;		// 外设寄存器只有一个，地址不用递增
	DMA_InitStructure.DMA_MemoryInc 		 = DMA_MemoryInc_Enable;         	// 地址增量变化
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;	// 外设数据大小为半字，即两个字节，STM32是32位的，所以一个字是32位，半字是16位
	DMA_InitStructure.DMA_MemoryDataSize 	 = DMA_MemoryDataSize_HalfWord;		// 存储器数据大小也为半字，跟外设数据大小相同
	DMA_InitStructure.DMA_Mode 				 = DMA_Mode_Normal;		 			// 非循环传输模式,减小数据处理的压力
	DMA_InitStructure.DMA_Priority 			 = DMA_Priority_High;				// DMA 传输通道优先级为高，当使用一个DMA通道时，优先级设置不影响
	DMA_InitStructure.DMA_FIFOMode 			 = DMA_FIFOMode_Disable; 			// 禁止DMA FIFO，使用直连模式 
	DMA_InitStructure.DMA_FIFOThreshold 	 = DMA_FIFOThreshold_HalfFull;		// FIFO 大小，FIFO模式禁止时，这个不用配置  
	DMA_InitStructure.DMA_MemoryBurst 		 = DMA_MemoryBurst_Single;
	DMA_InitStructure.DMA_PeripheralBurst 	 = DMA_PeripheralBurst_Single;  
	
	DMA_InitStructure.DMA_Channel = DMA_Channel_0;		// 选择 DMA 通道
	DMA_Init(DMA2_Stream4, &DMA_InitStructure);			// 初始化DMA流
		
	DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TCIF4);
	DMA_ClearITPendingBit(DMA2_Stream4, DMA_IT_TEIF4);
	DMA_ITConfig(DMA2_Stream4, DMA_IT_TC, ENABLE);
	DMA_Cmd(DMA2_Stream4, ENABLE);						// 使能DMA流，必须放在ADC使能前
	
	/* Enable ADC1 DMA since ADC1 is the Master*/
	ADC_DMARequestAfterLastTransferCmd(ADC1,ENABLE); 	// 源数据变化时开启DMA传输
	ADC_DMACmd(ADC1, ENABLE); 
	
	ADC_ClearFlag(ADC1, ADC_FLAG_STRT|ADC_FLAG_OVR);	// 必须清除开始和结束标志
	ADC_SoftwareStartConv(ADC1);						// 必须软件启动ADC采集
#endif
}

