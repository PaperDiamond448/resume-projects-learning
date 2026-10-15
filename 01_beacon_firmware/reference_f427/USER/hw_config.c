
#include "main.h"
#include "hw_config.h"
#include "IO_operate.h"
#include "PCLINK_operate.h"

#if USE_STM32H7XX
#include "STM32H7_operate.h"
#endif

/**************************************************************************
*Function name:	void Init_All_Periph(void)
*Parameters   :	Null
*Author 	  : 
*Time		  :	
*Description  :	Init_All_Periph
***************************************************************************/
/*硬件初始化文件
├── Init_All_Periph()     // 主初始化函数，顺序调用所有硬件初始化
├── COMM_Configuration()  // 串口通信接口配置
├── SetSystemClock()      // 对外接口：设置系统时钟
└── RCC_Configuration()   // 底层函数：实际配置RCC时钟寄存器*/
void RCC_Configuration(uint32_t type);	// 初始化时钟配置

void Init_All_Periph(void)
{	
#if USE_STM32F4XX
	SystemCoreClockUpdate();			// 更新系统时钟 读取 STM32 的硬件寄存器，计算出当前实际的系统时钟频率，并更新到全局变量 SystemCoreClock 中。
#elif USE_STM32H7XX
	STM32H7_Initial();					// 初始化HAL库等配置
	SystemCoreClockUpdate();			// 更新系统时钟
#endif
	
#if USE_STM32F1XX
	my_mem_init(SRAMIN);				// 初始化内部内存池
#elif USE_STM32F4XX
	my_mem_init(SRAMIN); 				// 初始化内部内存池
	my_mem_init(SRAMCCM); 				// 初始化CCM内存池
#elif USE_STM32H7XX
	my_mem_init(SRAMIN); 				// 初始化AXI内存池,AXI共512KB
	my_mem_init(SRAM12); 				// 初始化SRAM1/2/3内存池,SRAM1+SRAM2,共256KB
	my_mem_init(SRAM4); 				// 初始化SRAM4内存池,SRAM4共64KB
	my_mem_init(SRAMDTCM); 				// 初始化DTCM内存池,DTCM共128KB,此部分内存仅CPU和MDMA(通过AHBS)可以访问!!!!
	my_mem_init(SRAMITCM); 				// 初始化ITCM内存池,DTCM共64 KB,此部分内存仅CPU和MDMA(通过AHBS)可以访问!!!!
#endif
	
	delay(1000);						// 上电延时1秒，以免使用恒温晶振时系统无法正常启动
#if USE_STM32F1XX
	SetSystemClock(CLK_HSI64MHz, 0);	// 初始化时钟后才能进行IO接口的初始化，否则会引起外设工作不正常
#elif USE_STM32F4XX && USE_USB
	SetSystemClock(CLK_HSI64MHz, 0);	// 初始化时钟后才能进行IO接口的初始化，否则会引起外设工作不正常
#elif USE_STM32F4XX && USE_NET
	SetSystemClock(CLK_HSI128MHz, 0);	// 初始化时钟后才能进行IO接口的初始化，否则会引起外设工作不正常
#elif USE_STM32H7XX
	SetSystemClock(CLK_128MHz, 0);		// 初始化时钟后才能进行IO接口的初始化，否则会引起外设工作不正常
#endif

	// 初始化时钟后才能进行IO接口、串口、RTC的初始化，否则会引起外设工作不正常
	GPIO_INIT(); 						// 初始化一般GPIO管脚配置，如LED灯等
	Device_InitializeComm();			// 初始化串口，此处必须强制配置一次，之后将在 SetSystemClock()函数中自动完成
	RTC_Initialize(); delay(100);		// 初始化RTC时钟，必须放在 SetSystemClock()函数之后，否则无RTC模块的操作时钟
	printf("\r\n Hardware initialize over.\r\n");
	
#if (USE_FATFS && USE_FILE && !USE_USB) || (USE_FATFS && !USE_USB)
	Init_SD_Fatfs(true); 				// 初始化SD卡，更新配置参数
#endif
	// 由于 SysParam_Load() 函数中存在配置参数在文件中保存的选项
	// 因此，当使用这一功能是，必须在上一步强制初始化 Fatfs 文件系统
	SysParam_Load();					// 加载设备配置参数
}

void COMM_Configuration(void)//初始化调试口和通讯口
{
	// 初始化串口
	Comm_Config(DEBUG_Comm, DEBUG_Comm_Baud, true);	// Debug信息输出串口
	m_pCommDebug = Comm_GetObject(DEBUG_Comm);//初始化完串口后，拿到对应的串口对象指针
			
	// debug 串口可能和通讯串口不同
	Comm_Config(PCLINK_Comm, PCLINK_CommBaud, true);// 上位机通讯接口
	m_pCommObject = Comm_GetObject(PCLINK_Comm);	// 对应的串口对象指针
}

//************************************
// Method:    配置系统时钟函数
// FullName:  RCC_Configuration
// Access:    public 
// Returns:   void
// Qualifier:
// Parameter: void
//************************************
void RCC_Configuration(uint32_t type)//type是外部传入的
{	
#if USE_STM32F1XX
	RCC_ClocksTypeDef RCC_Clocks;
	if ( type == CLK_SLEEP )
	{
		PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);
	}
	else
	{
	#if USE_HSE	
		// 使用片外无源晶振
		// HSE_VALUE must be 8MHz, please check the hardware or define set
		// 系统外部高速晶振默认选择8MHz晶振, 以下配置均默认按照8MHz晶振配置
		int HSEStartUpStatus = 0;
		RCC_DeInit();					/*将外设RCC寄存器重设为缺省值*/
		RCC_HSEConfig(RCC_HSE_ON);		//RCC_HSE_ON——HSE晶振打开(ON)
		HSEStartUpStatus = RCC_WaitForHSEStartUp();		/*等待HSE起振*/
		if(HSEStartUpStatus == SUCCESS)        								//SUCCESS：HSE晶振稳定且就绪
		{
			if (type == CLK_1MHz)
			{
				RCC_HCLKConfig(RCC_SYSCLK_Div8);  							/*设置AHB时钟（HCLK）*/  
				RCC_PCLK2Config(RCC_HCLK_Div1);  							/*设置高速AHB时钟（PCLK2）*/
				RCC_PCLK1Config(RCC_HCLK_Div2);   							/*设置低速AHB时钟（PCLK1）*/ 
						
				FLASH_SetLatency(FLASH_Latency_2);    						/*设置FLASH存储器延时时钟周期数*/
				FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);		/*选择FLASH预取指缓存的模式*/
				
				/*设置系统时钟（SYSCLK）*/ 
				//RCC_SYSCLKSource_HSE——选择HSE作为系统时钟
				RCC_SYSCLKConfig(RCC_SYSCLKSource_HSE); 
				
				/* 返回用作系统时钟的时钟源*/
				while(RCC_GetSYSCLKSource() != 0x04){}
			}
			else if (
				type == CLK_8MHz  || type == CLK_HSE	|| type == CLK_HSI	 ||
				type == CLK_24MHz || type == CLK_48MHz  || type == CLK_64MHz ||
				type == CLK_HSI48MHz || type == CLK_HSI64MHz)
			{
				if (type == CLK_8MHz || type == CLK_HSE || type == CLK_HSI)
				{
					RCC_PLLConfig(RCC_PLLSource_HSE_Div2, RCC_PLLMul_2); 	/*设置PLL时钟源及倍频系数*/ 
					RCC_HCLKConfig(RCC_SYSCLK_Div1);  						/*设置AHB时钟（HCLK）*/  
					RCC_PCLK2Config(RCC_HCLK_Div1);  						/*设置高速AHB时钟（PCLK2）*/
					RCC_PCLK1Config(RCC_HCLK_Div2);   						/*设置低速AHB时钟（PCLK1）*/  
				}
				if (type == CLK_24MHz)
				{
					RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_3); 	/*设置PLL时钟源及倍频系数*/ 
					RCC_HCLKConfig(RCC_SYSCLK_Div1);  						/*设置AHB时钟（HCLK）*/  
					RCC_PCLK2Config(RCC_HCLK_Div1);  						/*设置高速AHB时钟（PCLK2）*/
					RCC_PCLK1Config(RCC_HCLK_Div2);   						/*设置低速AHB时钟（PCLK1）*/    			
				}
				else if (type == CLK_48MHz || type == CLK_HSI48MHz)
				{
					RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_6); 	/*设置PLL时钟源及倍频系数*/ 
					RCC_HCLKConfig(RCC_SYSCLK_Div1);  						/*设置AHB时钟（HCLK）*/  
					RCC_PCLK2Config(RCC_HCLK_Div1);  						/*设置高速AHB时钟（PCLK2）*/
					RCC_PCLK1Config(RCC_HCLK_Div2);   						/*设置低速AHB时钟（PCLK1）*/    			
				}
				else if (type == CLK_64MHz || type == CLK_HSI64MHz)
				{
					// 实际频率设置为72MHz
					RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9); 	/*设置PLL时钟源及倍频系数*/ 
					RCC_HCLKConfig(RCC_SYSCLK_Div1);  						/*设置AHB时钟（HCLK）*/  
					RCC_PCLK2Config(RCC_HCLK_Div1);  						/*设置高速AHB时钟（PCLK2）*/
					RCC_PCLK1Config(RCC_HCLK_Div2);   						/*设置低速AHB时钟（PCLK1）*/    			
				}
				
				FLASH_SetLatency(FLASH_Latency_2);    						/*设置FLASH存储器延时时钟周期数*/
				FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);		/*选择FLASH预取指缓存的模式*/      

				/*使能PLL */
				RCC_PLLCmd(ENABLE); 

				/*检查指定的RCC标志位(PLL准备好标志)设置与否*/   
				while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET){}

				/*设置系统时钟（SYSCLK）*/ 
				//RCC_SYSCLKSource_PLLCLK——选择PLL作为系统时钟
				RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); 

				/* PLL返回用作系统时钟的时钟源*/
				while(RCC_GetSYSCLKSource() != 0x08){}
			}
			else
			{
				// 默认设置错误时，配置为HSE时钟频率
				RCC_HCLKConfig(RCC_SYSCLK_Div1);  							/*设置AHB时钟（HCLK）*/  
				RCC_PCLK2Config(RCC_HCLK_Div1);  							/*设置高速AHB时钟（PCLK2）*/
				RCC_PCLK1Config(RCC_HCLK_Div2);   							/*设置低速AHB时钟（PCLK1）*/ 
				
				FLASH_SetLatency(FLASH_Latency_2);    						/*设置FLASH存储器延时时钟周期数*/
				FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);		/*选择FLASH预取指缓存的模式*/
				
				/*设置系统时钟（SYSCLK）*/ 
				//RCC_SYSCLKSource_HSE——选择HSE作为系统时钟
				RCC_SYSCLKConfig(RCC_SYSCLKSource_HSE); 
				
				/* 返回用作系统时钟的时钟源*/
				while(RCC_GetSYSCLKSource() != 0x04){}
			}
		}
	#else
		 // 使用片内晶振
		int HSIStartUpStatus = 0;
		RCC_DeInit();			/*将外设RCC寄存器重设为缺省值*/
		RCC_HSICmd(ENABLE);		// 使能 HSI
		while( HSIStartUpStatus != RCC_CR_HSIRDY )
		{	HSIStartUpStatus = RCC->CR & RCC_CR_HSIRDY;	}	// 等待 HSI 就绪
		if (HSIStartUpStatus == RCC_CR_HSIRDY)				/*等待HSI起振*/
		{
			if (type == CLK_1MHz)
			{
				RCC_HCLKConfig(RCC_SYSCLK_Div8);  							/*设置AHB时钟（HCLK）*/  
				RCC_PCLK2Config(RCC_HCLK_Div1);  							/*设置高速AHB时钟（PCLK2）*/
				RCC_PCLK1Config(RCC_HCLK_Div2);   							/*设置低速AHB时钟（PCLK1）*/ 
				
				FLASH_SetLatency(FLASH_Latency_2);    						/*设置FLASH存储器延时时钟周期数*/
				FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);		/*选择FLASH预取指缓存的模式*/
				
				/*设置系统时钟（SYSCLK）*/ 
				//RCC_SYSCLKSource_HSI——选择HSI作为系统时钟
				RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI); 
				
				/* 返回用作系统时钟的时钟源*/
				while(RCC_GetSYSCLKSource() != 0x00){}
			}
			else if (
				type == CLK_8MHz  || type == CLK_HSE	|| type == CLK_HSI	 ||
				type == CLK_24MHz || type == CLK_48MHz  || type == CLK_64MHz ||
				type == CLK_HSI48MHz || type == CLK_HSI64MHz)
			{
				if (type == CLK_8MHz || type == CLK_HSE || type == CLK_HSI)
				{
					RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_2); 	/*设置PLL时钟源及倍频系数*/ 
					RCC_HCLKConfig(RCC_SYSCLK_Div1);  						/*设置AHB时钟（HCLK）*/  
					RCC_PCLK2Config(RCC_HCLK_Div1);  						/*设置高速AHB时钟（PCLK2）*/
					RCC_PCLK1Config(RCC_HCLK_Div2);   						/*设置低速AHB时钟（PCLK1）*/  
				}
				if (type == CLK_24MHz)
				{
					RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_6); 	/*设置PLL时钟源及倍频系数*/ 
					RCC_HCLKConfig(RCC_SYSCLK_Div1);  						/*设置AHB时钟（HCLK）*/  
					RCC_PCLK2Config(RCC_HCLK_Div1);  						/*设置高速AHB时钟（PCLK2）*/
					RCC_PCLK1Config(RCC_HCLK_Div2);   						/*设置低速AHB时钟（PCLK1）*/    			
				}
				else if (type == CLK_48MHz || type == CLK_HSI48MHz)
				{
					RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_12); 	/*设置PLL时钟源及倍频系数*/ 
					RCC_HCLKConfig(RCC_SYSCLK_Div1);  						/*设置AHB时钟（HCLK）*/  
					RCC_PCLK2Config(RCC_HCLK_Div1);  						/*设置高速AHB时钟（PCLK2）*/
					RCC_PCLK1Config(RCC_HCLK_Div2);   						/*设置低速AHB时钟（PCLK1）*/    			
				}
				else if (type == CLK_64MHz || type == CLK_HSI64MHz)
				{
					// 受倍频系数的影响，当使用内置晶振时，最高64M
					RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_16); 	/*设置PLL时钟源及倍频系数*/ 
					RCC_HCLKConfig(RCC_SYSCLK_Div1);  						/*设置AHB时钟（HCLK）*/  
					RCC_PCLK2Config(RCC_HCLK_Div1);  						/*设置高速AHB时钟（PCLK2）*/
					RCC_PCLK1Config(RCC_HCLK_Div2);   						/*设置低速AHB时钟（PCLK1）*/    			
				}
				
				FLASH_SetLatency(FLASH_Latency_2);    						/*设置FLASH存储器延时时钟周期数*/
				FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);		/*选择FLASH预取指缓存的模式*/      

				/*使能PLL */
				RCC_PLLCmd(ENABLE); 

				/*检查指定的RCC标志位(PLL准备好标志)设置与否*/   
				while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET){}

				/*设置系统时钟（SYSCLK）*/ 
				//RCC_SYSCLKSource_PLLCLK——选择PLL作为系统时钟
				RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); 

				/* 返回用作系统时钟的时钟源*/
				while(RCC_GetSYSCLKSource() != 0x08){}
			}
			else
			{
				// 默认设置错误时，配置为HSE时钟频率
				RCC_HCLKConfig(RCC_SYSCLK_Div1);  							/*设置AHB时钟（HCLK）*/  
				RCC_PCLK2Config(RCC_HCLK_Div1);  							/*设置高速AHB时钟（PCLK2）*/
				RCC_PCLK1Config(RCC_HCLK_Div2);   							/*设置低速AHB时钟（PCLK1）*/ 
				
				FLASH_SetLatency(FLASH_Latency_2);    						/*设置FLASH存储器延时时钟周期数*/
				FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);		/*选择FLASH预取指缓存的模式*/
				
				/*设置系统时钟（SYSCLK）*/ 
				//RCC_SYSCLKSource_HSI——选择HSI作为系统时钟
				RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI); 
				
				/* 返回用作系统时钟的时钟源*/
				while(RCC_GetSYSCLKSource() != 0x00){}
			} 
		}
	#endif
	}
	
	SystemCoreClockUpdate();		// 更新系统时钟频率数值  SystemCoreClock
	RCC_GetClocksFreq(&RCC_Clocks);
	m_uPCLK1 = RCC_Clocks.PCLK1_Frequency;
	m_uPCLK2 = RCC_Clocks.PCLK2_Frequency;
#endif
	
#if USE_STM32F4XX
    ErrorStatus HSEStartUpStatus;
    RCC_ClocksTypeDef RCC_ClocksStatus;
    
	if (type == CLK_SLEEP)
	{
		// 进入睡眠模式
		__WFI();//Wait For Interrupt
	}
	else if (type >= CLK_HSI)//用数字范围来区分时钟类型
	{
		RCC_DeInit();			// 恢复系统默认时钟源
		RCC_HSICmd(ENABLE);		// 使能HSI 16MHZ，这部分走HSI分支
		while(RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET) ;// 等待 HSI稳定
		
		RCC_PLLCmd(DISABLE);	// 设置系统PLL前先禁用 先把PLL关掉，才能去改PLL配置
		if ( type == CLK_HSI || type == CLK_HSE)//不经过PLL倍频
		{
			RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);//把HSI（内部16MHz时钟）作为SYSCLK的来源。
			RCC_HCLKConfig(RCC_SYSCLK_Div1);				// DIV1是除1的意思16Mhz，HCLK直接等于SYSCLK
			RCC_PCLK1Config(RCC_HCLK_Div2);	    			// 8Mhz		
			RCC_PCLK2Config(RCC_HCLK_Div1);     			// 16Mhz
		}
		else
		{
			/* Select regulator voltage output Scale 1 mode */
			RCC->APB1ENR |= RCC_APB1ENR_PWREN;//← 给电源模块通电
			PWR->CR |= PWR_CR_VOS;//← 设置电压调节器的输出电压范围，范围1是最高性能模式，适用于高频率运行
 			
			RCC_HCLKConfig(RCC_SYSCLK_Div1);	// HCLK = SYSCLK / 1
			// RCC->CFGR |= RCC_CFGR_HPRE_DIV1;	/* HCLK = SYSCLK / 1*/
			
			// RCC_PLLConfig 设置 PLL输出频率 和 SYSCLK频率
			// 第一个参数选择时钟源
			// 第二个参数 PLL分频数 0~63，HSE时钟经过分频后范围要求限制在1~2MHz
			// 第三个参数 PLL倍频数 192~432，倍频后的VCO时钟频率范围要求限制在192~432 MHz
			// 第四个参数 SYSCLK分频数 2、4、6、8 总时钟频率要求小于180MHz
			// 第五个参数 OTG FS, SDIO and RNG时钟分频数 4~15，如果有OTG，则必须48MHz，SDIO需要小于48MHz

			// RCC_PCLK1Config(RCC_HCLK_Div4);	   		
			// RCC_PCLK2Config(RCC_HCLK_Div2);     
			// TIM2~7,12~14	由PCLK1工作频率决定，当其分频系数为1时，等于HCLK，大于1时，乘以2
			// TIM1,8~11 由PCLK2工作频率决定，当其分频系数为1时，等于HCLK，大于1时，乘以2
			// PCLK1必须小于42MHz
			// PCLK2必须小于84MHz
 
			if (type == CLK_HSI48MHz || type == CLK_48MHz)//这里是要经过PLL倍频的
			/*CLK_HSI48MHz  →  明确指定用HSI时钟源来倍频到48MHz
    			CLK_48MHz     →  只说我要48MHz，不在乎用HSI还是HSE*/
			{
				RCC_PCLK1Config(RCC_HCLK_Div4);	    		// 12Mhz		
				RCC_PCLK2Config(RCC_HCLK_Div2);     		// 24Mhz
				RCC_PLLConfig(RCC_PLLSource_HSI, 16, 192, 4, 8);	// SDIO	24MHz
				//最后这个8是PLLQ——USB/SDIO的时钟分频
			}
			else if (type == CLK_HSI64MHz || type == CLK_64MHz)
			{
				RCC_PCLK1Config(RCC_HCLK_Div4);	    		// 16Mhz		
				RCC_PCLK2Config(RCC_HCLK_Div2);     		// 32Mhz
				RCC_PLLConfig(RCC_PLLSource_HSI, 16, 256, 4, 8);	// SDIO	32MHz
			}
			else if (type == CLK_HSI96MHz || type == CLK_96MHz)
			{
				RCC_PCLK1Config(RCC_HCLK_Div4);	   		 	// 24Mhz		
				RCC_PCLK2Config(RCC_HCLK_Div2);     		// 48Mhz
				RCC_PLLConfig(RCC_PLLSource_HSI, 16, 384, 4, 12);	// SDIO	32MHz
			}
			else if (type == CLK_HSI128MHz || type == CLK_128MHz)
			{
				RCC_PCLK1Config(RCC_HCLK_Div4);	    		// 32Mhz		
				RCC_PCLK2Config(RCC_HCLK_Div2);     		// 64Mhz
				RCC_PLLConfig(RCC_PLLSource_HSI, 16, 256, 2, 8);	// SDIO	32MHz
			}
			else if (type == CLK_HSI160MHz || type == CLK_160MHz)
			{			
				RCC_PCLK1Config(RCC_HCLK_Div4);	    		// 40Mhz		
				RCC_PCLK2Config(RCC_HCLK_Div2);     		// 80Mhz
				RCC_PLLConfig(RCC_PLLSource_HSI, 16, 320, 2, 8);	// SDIO	40MHz
			}
			else
			{
				// 默认主频160MHz
				RCC_PCLK1Config(RCC_HCLK_Div4);	    		// 40Mhz		
				RCC_PCLK2Config(RCC_HCLK_Div2);     		// 80Mhz
				RCC_PLLConfig(RCC_PLLSource_HSI, 16, 320, 2, 8);	// SDIO	40MHz
			}
 
			RCC_PLLCmd(ENABLE);                     		// PLL时钟开启
			/* Wait till the main PLL is ready */
			while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET){ }
 
			/* Enable the Over-drive to extend the clock frequency to 180 Mhz */
			PWR->CR |= PWR_CR_ODEN;// 第一步：请求开启Over-drive
			while((PWR->CSR & PWR_CSR_ODRDY) == 0){	}// 第二步：等它准备好
			PWR->CR |= PWR_CR_ODSWEN;//正式切换到Over-drive
			while((PWR->CSR & PWR_CSR_ODSWRDY) == 0){ }      //完成
			/* Configure Flash prefetch, Instruction cache, Data cache and wait state */
			FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;
 
			RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);  	// 选择PLL时钟为系统时钟
			/* Wait till the main PLL is used as system clock source */
			while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET){ } //确认PLL还在正常运行
			while(RCC_GetSYSCLKSource () != 0x08){}//确认系统时钟真的切过去了
		}
	}
	else if ( type >= CLK_HSE && type < CLK_HSI )
	{	
		RCC_DeInit(); 
		RCC_HSEConfig(RCC_HSE_ON);   //这部分走HSE分支
		HSEStartUpStatus = RCC_WaitForHSEStartUp();

		if(HSEStartUpStatus == SUCCESS)
		{
			// 当外部晶振稳定且就绪时，根据HSE_VALUE的值，确定输入时钟的分频系数
			int nDiv = HSE_VALUE/1000000;			//HSE_VALUE 是外部晶振的频率，单位是Hz，在这是换算成MHz的数值，所以 nDiv 直接就拿去当PLLM用了	
			assert_param(HSE_VALUE > 1000000);				// 外部时钟必须确保频率大于1MHz

			RCC_PLLCmd(DISABLE);
			if (type == CLK_HSE || type == CLK_HSI)//醒着但极度省电，超低速，就是这么设计的。。。
			{
				RCC_SYSCLKConfig(RCC_SYSCLKSource_HSE);
				RCC_HCLKConfig(RCC_SYSCLK_Div8);			// 1.25Mhz
				RCC_PCLK1Config(RCC_HCLK_Div1);	    		// 1.25Mhz	
				RCC_PCLK2Config(RCC_HCLK_Div1);     		// 1.25Mhz
			}
			else
			{
				RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);  // 选择PLL时钟为系统时钟
				RCC_HCLKConfig(RCC_SYSCLK_Div1);    		// 选择系统时钟为HCLK(AHB)时钟

				// RCC_PLLConfig 设置 PLL输出频率 和 SYSCLK频率
				// 第一个参数选择时钟源
				// 第二个参数 PLL分频数 0~63，HSE时钟经过分频后范围要求限制在1~2MHz
				// 第三个参数 PLL倍频数 192~432，倍频后的VCO时钟频率范围要求限制在192~432 MHz
				// 第四个参数 SYSCLK分频数 2、4、6、8 总时钟频率要求小于180MHz
				// 第五个参数 OTG FS, SDIO and RNG时钟分频数 4~15，如果有OTG，则必须48MHz，SDIO需要小于48MHz

				// RCC_PCLK1Config(RCC_HCLK_Div4);	   		
				// RCC_PCLK2Config(RCC_HCLK_Div2);     
				// TIM2~7,12~14	由PCLK1工作频率决定，当其分频系数为1时，等于HCLK，大于1时，乘以2
				// TIM1,8~11 由PCLK2工作频率决定，当其分频系数为1时，等于HCLK，大于1时，乘以2
				// PCLK1必须小于42MHz
				// PCLK2必须小于84MHz

				if (type == CLK_24MHz )
				{
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 6Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div4);     // 6Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv, 192, 8, 4);
				}
				else if (type == CLK_48MHz || type == CLK_HSI48MHz)
				{
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 12Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div2);     // 24Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv/2, 192, 8, 8);
				}
				else if (type == CLK_64MHz || type == CLK_HSI64MHz)
				{
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 16Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div2);     // 32Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv/2, 192, 6, 8);
				}
				else if (type == CLK_96MHz || type == CLK_HSI96MHz)
				{
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 24Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div2);     // 48Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv/2, 192, 4, 8);
				}
				else if (type == CLK_128MHz || type == CLK_HSI128MHz)
				{
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 32Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div2);     // 64Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv, 256, 2, 8);
				}
				else if (type == CLK_144MHz)
				{
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 36Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div2);     // 72Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv, 288, 2, 6);
				}
				else if (type == CLK_160MHz || type == CLK_HSI160MHz)
				{
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 40Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div2);     // 80Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv, 320, 2, 8);
				}
				else if (type == CLK_180MHz)
				{
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 45Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div2);     // 90Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv, 360, 2, 8);
				}
				else if (type == CLK_192MHz)
				{
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 48Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div2);     // 96Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv, 384, 2, 8);
				}
				else
				{
					// 默认主频160MHz
					RCC_PCLK1Config(RCC_HCLK_Div4);	    // 40Mhz		
					RCC_PCLK2Config(RCC_HCLK_Div2);     // 80Mhz
					RCC_PLLConfig(RCC_PLLSource_HSE, nDiv, 320, 2, 8);
				}
				
				RCC_PLLCmd(ENABLE);                     // PLL时钟开启

				while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET){} 
				while(RCC_GetSYSCLKSource () != 0x08){}
			}
		}
	}
	// 为了保证 RCC_GetClocksFreq 得到的数值正确 m_uPCLK1 和 m_uPCLK2
	// 需要修改定义#define HSE_VALUE 为硬件配置的正确时钟频率
	SystemCoreClockUpdate();//时钟配置改变了，SystemCoreClock 这个全局变量还存着旧值，调用这个函数就是重新去读寄存器，把SystemCoreClock更新成当前真实的频率。
	RCC_GetClocksFreq (&RCC_ClocksStatus);//RCC_ClocksStatus 是一个结构体，调用这个函数之后，里面会填满各种频率值
	m_uPCLK1 = RCC_ClocksStatus.PCLK1_Frequency;
	m_uPCLK2 = RCC_ClocksStatus.PCLK2_Frequency;//把APB1和APB2的频率存到全局变量里，因为后续很多地方要用到
#endif
	
#if USE_STM32H7XX
	// 由于H7系列切换时钟频率时间较长，且不能被中断打断
	// 因此切换频率前需要停止所有中断，等切换完频率后再开启
	if (type == CLK_SLEEP)
	{
		// 进入睡眠模式
		__WFI();
	}
	else if (type >= CLK_HSI)
	{
		__disable_irq();	// 禁止全局中断
		STM32H7_Init_HSIClock( type );
		__enable_irq();
	}
	else if ( type >= CLK_HSE && type < CLK_HSI )
	{	
		__disable_irq();	// 禁止全局中断
		STM32H7_Init_HSEClock( type );
		__enable_irq();
	}
	
	SystemCoreClockUpdate();			// 更新系统时钟
	m_uPCLK1 = HAL_RCC_GetPCLK1Freq();
	m_uPCLK2 = HAL_RCC_GetPCLK2Freq();

#endif
}

void SetSystemClock(uint32_t type, bool bShowMsg)//bShowMsg 就是个开关，true就打详细日志
{
	// 设置系统时钟，同时配置串口波特率
	if (m_nSysClkType == type) return;		// 如果当前时钟已经是你要设置的类型了，直接返回，什么都不做
	m_nSysClkType = type;					// 保存当前配置
	
	RCC_Configuration(type);				// 配置时钟参数
	
	if (type != CLK_SLEEP)// 睡眠模式不需要做这些
	{
		delay(100);
		Device_InitializeComm();			// 每次设置系统时钟时，复位和初始化串口

#if DEBUG_PRINTF
		if (bShowMsg)
		{
			/* Output a message on Hyperterminal using printf function */
			PCLINK_DebugInfo();//打印详细的调试信息
		}
		else
		{
			printf(" System core clock is : %dM.\r\n", SystemCoreClock/1000000);
		}
#endif	
	}
}
