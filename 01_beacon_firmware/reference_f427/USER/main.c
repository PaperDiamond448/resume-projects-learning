
#include "main.h"
#include "Nvic.h"

#if USE_STM32F1XX
#include "nvic.h"
#include "stm32f10x_it.h"
#endif

#if USE_STM32F4XX
#include "stm32f4xx_it.h"
#endif

#if USE_STM32H7XX
#include "stm32h7xx_it.h"
#endif

#if USE_DS1302
#include "DS1302_operate.h"
#elif USE_DS3231
#include "DS3231_operate.h"
#endif

#include "PCLink_operate.h"

bool m_bInitDone   		= false;	// 系统初始化完成标志
bool m_bForceLoad  		= false;	// 强制加载系统参数
bool m_bForceStore 		= false;	// 强制保存系统参数
bool m_bForceSysClk		= false;	// 强制设置系统时钟
bool m_bForceInitParam	= false;	// 强制系统参数重新初始化
bool m_bForceInitRtcEx	= false;	// 强制从外部RTC更新系统时钟
bool m_bCpuIdleCorrect	= false;	// 修正 CPU 空闲率计数器系数标志，启动后置 1，将开始自动校准
bool m_bStartStatus		= false;	// 系统运行状态

#if USE_HSE// 定义是否使用片外HSE时钟
u32  m_nSysClkTest      = CLK_128MHz;//外部高速时钟
#else
u32  m_nSysClkTest      = CLK_HSI128MHz;//内部高速时钟
#endif
u32  m_nSysClkType 		= 0;		// 当前设置的时钟配置
u32  m_uMemUse     		= 0;		// 内存占用率
u32	 m_uCpuIdle    		= 0;		// CPU 空闲率计数器
u32  m_uCpuIdleStep		= 77;		// CPU 空闲状态步进值，由无关操作决定
float m_fCpuIdle   		= 0.0f;		// CPU空闲率（百分比，0~100%）
RTC_COUNTER_TypeDef 	m_hRtcCnt;  //// RTC中断计数器结构体

//////////////////////////////////////////////////////////////////////////
// 如果需要更改堆栈和堆的大小，在startup_stm32f427x.s文件中修改
// 如果要更改系统时钟，在 stm32f1xx.h中修改 HSE_VALUE
// 如果要更改系统时钟，在 stm32f4xx.h中修改 HSE_VALUE
// 如果要更改系统时钟，在 stm32h7xx_hal_conf.h中修改 HSE_VALUE
// 系统主频可以通过全局变量 SystemCoreClock 查看

int main(void)
{
	m_hRtcCnt.uAcqIdle 	    = 0;// 采集空闲计数器，检查fpga是否正常工作，fpga正常响应的话就清零
	m_hRtcCnt.uPPSIdle 	    = 0;//秒脉冲（PPS）监测计数器
	m_hRtcCnt.uAcqRstCnt    = 0;//采集复位计时器，开始采集的时候清零，递增时机是采集异常需要复位的时候，记录复位次数
	m_hRtcCnt.uWatchDog     = 0;//看门狗计数器，监控系统是否卡死，防止程序跑飞，清零时机：收到任何上位机命令。增长时机：每次RTC秒中断的时候递增
	m_hRtcCnt.uPCLinkIdle   = 0; 
	
	// 由于初始化有顺序要求，以下函数的顺序不要调整
	Init_All_Periph();					// 系统初始化
	Device_Initialize();				// 初始化设备相关配置参数
	PCLINK_DebugInfo();					// 输出主频，debugComm等信息
	Device_DebugInfo();					// 输出设备初始化时的debug信息
	NVIC_Config();						// 初始化中断，需要在所有模块初始化完成后配置
	m_bInitDone = true;					// 为避免中断等出错，必须在初始化完成后，再响应各种中断
	SetArmLedR(false);
	
	while(1)
	{
		// 由于带延时闪烁或者双闪的过程及其耗时，插入在中断中会影响某些功能
		// 因此利用轮询的方式进行闪灯操作
		LedShine();
		Device_OnEchoMain();
		if ( m_bForceLoad )
		{
			m_bForceLoad = false;
			SysParam_Load();			// 强制系统重新加载参数 读取Flash中存储的设备配置参数
		}
		else if ( m_bForceStore )
		{
			m_bForceStore = false;
			SysParam_Save();			// 强制系统保存系统参数
		}
		else if ( m_bForceInitParam )
		{
			SysParam_Load();
			m_bForceInitParam = false;	// 强制系统重新加载默认参数
		}
		else if ( m_bForceSysClk )
		{
			SetSystemClock(m_nSysClkTest, false);
			m_bForceSysClk = false;
		}
		else if ( m_bForceInitRtcEx )
		{
			// 由于系统启动时仅初始化并更新时间，如果连续采集时，时间累积误差会增大
			// 因此每次采集结束，更新一次RTC时间，以保证文件存储时间对应为最新时间值
		#if USE_DS1302
			Ds1302UpdateRTC();					// 读取DS1302的时间数据并更新RTC
			m_nRtcYMD = m_stDsTime.uYMD;
			m_nRtcHMS = m_stDsTime.uHMS;
		#elif USE_DS3231
			Ds3231UpdateRTC();					// 读取DS3231的时间数据并更新RTC
			m_nRtcYMD = m_stDsTime.uYMD;
			m_nRtcHMS = m_stDsTime.uHMS;
		#endif
			m_bForceInitRtcEx = false;
		}
		
		m_uCpuIdle += m_uCpuIdleStep;	// while 循环内所消耗的CPU频率计数值
	}
}

// LED灯闪灯控制
void LedShine()
{
	if (m_stLedTrig.bShineR)		ShineRed();
	else if (m_stLedTrig.bShineG)	ShineGreen();
	else if (m_stLedTrig.bShineB)	ShineBlue();
	else if (m_stLedTrig.bShineRR)	ShineRR();
	else if (m_stLedTrig.bShineGG)	ShineGG();
	else if (m_stLedTrig.bShineRG)	ShineRG();
	else if (m_stLedTrig.bShineGB)	ShineGB();
}

// RTC wakeup interrupt process function
void OnEchoRTC(void)
{
	// 响应RTC时钟中断
	if ( m_bInitDone ) 
	{
		// 计算CPU空闲率
		m_fCpuIdle = (float)m_uCpuIdle;
		m_fCpuIdle = m_fCpuIdle*100.0f/(float)SystemCoreClock;//空闲率(%) = (实际空闲计数 / 满载时最大计数) × 100%
		m_fCpuIdle = m_fCpuIdle > 100 ? m_fCpuIdle/2 : m_fCpuIdle;//除以2是个经验性的修正，粗暴的拉回范围罢了。。
		m_uCpuIdle = 0;
		if ( m_bCpuIdleCorrect )//启动的时候是1
		{
			// 使能校准后，通过计算和修正，获得校准后的步进值
			// 校准过程由于其它所有操作被跳过，此处要喂狗（函数内有 USE_IWDG 判断)
			IWDG_Feed();	// 看门狗喂狗 OnEchoRTC()	
			if ( m_fCpuIdle > 100 && m_uCpuIdleStep > 10 )
				m_uCpuIdleStep--;
			else if ( m_fCpuIdle < 98 && m_uCpuIdleStep < 1000 )
				m_uCpuIdleStep++;
			return;
		}
		
		if ( !m_bCpuIdleCorrect && m_nRtcHMS%10 == 0 )//没在校准的时候，每10秒更新一次内存占用率
			m_uMemUse = my_mem_perused(SRAMIN);
	}
	
	Device_OnEchoRtc();//非校准模式流程完整，主循环自己会喂，不需要额外处理。
}

// Comm1 echo process function
void OnEchoComm1(void)
{
	OnEchoPCLINK(m_pComm1);
}

// Comm2 echo process function
void OnEchoComm2(void)
{
	// 响应串口2中断
	OnEchoPCLINK(m_pComm2);
}

// Comm3 echo process function
void OnEchoComm3(void)
{
	// 响应串口3中断
	OnEchoPCLINK(m_pComm3);
}

// Comm6 echo process function
void OnEchoComm6(void)
{
	// 响应串口6中断
#if DEVICE_TYPE == tDeviceIPowerProbe
	Device_OnEchoLCD(m_pComm6);
#else
	OnEchoPCLINK(m_pComm6);
#endif
}

void OnEchoPCLINK(void* pObj)
{
	// 响应计算机连接命令
	// 命令格式：
	// $A,B,C,D*
	// A	int32整数 设备地址；0->通配地址
	// B	int32整数	命令号
	// C	U32整数 参数1
	// D	U32整数 参数2
	COMM_COMMAND stCmd;
	COMM_OBJECT* pComm = (COMM_OBJECT*)pObj;
	if ( pComm == 0 || !m_bInitDone ) return;//或，两个有一个真的即可
	//pComm == 0 不是检查结构体里的某个成员，而是检查指针本身，检查的是指针是否为NULL，防止程序崩溃，也就是说是否指向了有效的结构体实例，为0就是有误
	if ( pComm->bRdy && PCLINK_DecodeCmd(pComm->chRxBuf, &stCmd) )//把 $1,2,3,4* 这样的字符串解析成结构体 stCmd，拿到地址、命令号、参数
	{
		m_hRtcCnt.uWatchDog   = 0;						// 任意指令复位看门狗
		m_hRtcCnt.uPCLinkIdle = 0;						// 任意指令复位PCLinkIdle计数器

		if ( !(stCmd.nAddr == 0 || stCmd.nAddr == g_stSys.nAddr) ) return;
		//stCmd.nAddr == 0 里的 0 是个特殊地址，代表广播地址，所有设备都响应这个地址的命令，当然也包括了本机地址，所以这个条件是允许的。
		//STCMD是发射的地址，G_STSYS.NADDR是本机地址，如果发来的命令地址既不是广播地址，也不是本机地址，那么这个命令就不响应，直接返回。
		if ( stCmd.nCmd == Device_Cmd_Link )
		{
			// 设备串口连接命令,返回设备ID等参数
			m_bPcConnect = true;						// 标志上位机物理接口连接状态
			m_bPcStatus  = true;						// 标志上位机软件接口联机状态
			PCLINK_Decode_Link(pComm->chRxBuf);			// 译码设备状态参数，解析连接参数
			
			PCLINK_Echo_Link(stCmd);					// 回复设备初始化信息
			PCLINK_Echo_Info(true);						// 回复设备RTC等，可不止RTC信息
		}
		else				// 允许未连接的情况下接收其它指令
		{
			switch ( stCmd.nCmd )
			{
			case Device_Cmd_Delink				: 	PCLINK_Decode_DeLink(stCmd);							// 断开设备连接
													break;
			case Device_Cmd_Get_Info			: 	PCLINK_Echo_Info(false); 								// 获取设备信息
													break;
			case Device_Cmd_Set_Rtc				: 	PCLINK_Decode_SetRTC(pComm->chRxBuf);					// 设置系统时间 年月日 星期 时分秒 
													break;
			case Device_Cmd_Acq_Start			: 	PCLINK_Decode_DAQ_Start(stCmd, pComm->chRxBuf);			// 译码设备开始采集命令参数
													break;
			case Device_Cmd_Acq_Stop			:	PCLINK_Decode_DAQ_Stop(stCmd);							// 译码设备停止采集命令参数
													break;
			case Device_Cmd_Get_TestData		:	PCLINK_Decode_GetValue(stCmd, pComm->chRxBuf);			// 响应获取测试数据
													break;
			case Device_Cmd_Set_Mode			:	PCLINK_Decode_SetMode(stCmd);							// 设置工作模式
													break;
			case Device_Cmd_Set_Addr			:	PCLINK_Decode_SetAddr(stCmd);							// 设置本机地址
													break;
			case Device_Cmd_Set_IP				:	PCLINK_Decode_SetIP(pComm->chRxBuf);					// 设置IP地址
													break;
			case Device_Cmd_Set_Id				:	PCLINK_Decode_SetId(stCmd);								// 设置本机ID
													break;
			case Device_Cmd_Set_Sn				:	PCLINK_Decode_SetSn(stCmd);								// 设置本机传感器序号
													break;
			case Device_Cmd_Set_SampleCh		:	PCLINK_Decode_SetSampleCh(stCmd);						// 设置采样通道
													break;
			case Device_Cmd_Set_SampleRate		:	PCLINK_Decode_SetSampleRate(stCmd);						// 设置采样率
													break;
			case Device_Cmd_Set_SampleGain		:	PCLINK_Decode_SetSampleGain(stCmd, pComm->chRxBuf);		// 设置增益
													break;
			case Device_Cmd_Set_SampleFilter	:	PCLINK_Decode_SetSampleFilter(stCmd, pComm->chRxBuf);	// 设置滤波器参数
													break;
			case Device_Cmd_Set_SampleUnit		:	PCLINK_Decode_SetSampleUnit(stCmd);						// 设置数据单位
													break;
			case Device_Cmd_Set_WorkBand		:	PCLINK_Decode_SetWorkBand(stCmd);						// 设置设备工作带宽
													break;
			case Device_Cmd_Set_PowerTime		: 	PCLINK_Decode_SetPowerTime(pComm->chRxBuf);				// 设置上电采集模式及时间
													break;
			case Device_Cmd_Set_PwOffTime		: 	PCLINK_Decode_SetPwOffTime(pComm->chRxBuf);				// 设置下电采集模式及时间
													break;
			case Device_Cmd_Set_CommBaud		:	PCLINK_Decode_SetCommBaud(stCmd);						// 设置串口波特率
													break;	
			case Device_Cmd_Set_AcqLogTime		: 	PCLINK_Decode_SetAcqLogTime(stCmd);						// 设置采样时长
													break;
			case Device_Cmd_Set_AcqStartTime	:	PCLINK_Decode_SetAcqStartTime(stCmd);					// 设置开始采集时间
													break;
			case Device_Cmd_Set_Sensor			: 	PCLINK_Decode_SetSensor(stCmd, pComm->chRxBuf);			// 设置传感器参数
													break;			
			case Device_Cmd_Set_ErrCoef			:	PCLINK_Decode_SetErrCoef(pComm->chRxBuf);				// 设置误差修正系数
													break;
			case Device_Cmd_SyncCmd_Set			:	PCLINK_Decode_SyncRecvSet(stCmd);						// 设置同步时钟基准参数
													break;
			case Device_Cmd_SyncCmd_Msg			: 	PCLINK_Decode_SyncRecvMsg(&stCmd);						// 响应同步采集消息指令
													break;
			case Device_Cmd_Set_PowerEn			:	PCLINK_Decode_SetPowerEn(stCmd);						// 设置设备上电参数
													break;
			case Device_Cmd_Set_PowerDelay		:	PCLINK_Decode_SetPowerDelay(stCmd);						// 设置设备上电延迟参数
													break;
			case Device_Cmd_Set_TrigGate		:	PCLINK_Decode_SetTrigGate(pComm->chRxBuf);				// 设置触发门限参数
													break;
			case Device_Cmd_Response			:	PCLINK_Decode_Response(stCmd);							// 响应消息回执
													break;
			case Device_Cmd_Set_Param			:	PCLINK_Decode_SetParam(stCmd, pComm->chRxBuf);			// 设置参数
													break;
			case Device_Cmd_Get_Param			:	PCLINK_Decode_GetParam(stCmd, pComm->chRxBuf);			// 响应参数
													break;	
			case Device_Cmd_SaveParam			:	PCLINK_Decode_SaveParam(stCmd);							// 保存配置参数
													break;	
			case Device_Cmd_Init_Param			:	PCLINK_Decode_InitParam(stCmd);							// 初始化设备配置参数
													break;	
			case Device_Cmd_Debug				:	PCLINK_Decode_Debug(stCmd, pComm->chRxBuf);				// 上位机调试指令
													break;
			case Device_Cmd_WatchDog			:	PCLINK_Decode_WatchDog(stCmd);							// 响应看门狗喂狗消息
													break;
			case Device_Cmd_Set_CommTalk		:	PCLINK_Decode_SetCommTalkMode(stCmd);					// 响应切换至声通模式
													break;
			case Device_Cmd_Set_LimitParam		:	PCLINK_Decode_SetLimitParam(pComm->chRxBuf);			// 设置限制参数
													break;	
			case Device_Cmd_Get_MasterStatus 	:   PCLINK_Decode_GetMasterStatus(stCmd, pComm->chRxBuf);	// 响应主机状态回复消息
													break;
			case Device_Cmd_Set_Calibrate		:	PCLINK_Decode_SetCalibrate(stCmd);						// 设置校准
													break;
			case Device_Cmd_Set_Relay			:	PCLINK_Decode_SetPwRelay(stCmd, pComm->chRxBuf);		// 设置继电器开启
													break;
			case Device_Cmd_Set_EmitEn			:	PCLINK_Decode_SetEmitEn(stCmd);							// 设置继电器开启
													break;
			case Device_Cmd_RecvData			:	PCLINK_Decode_RecvData(stCmd, pComm->chRxBuf);			// 接收特殊配置数据
													break;
			case Device_Cmd_Set_ArrayUnit		:	PCLINK_Decode_SetArrayUnit(stCmd);						// 设置阵元参数
													break;	
			case Device_Cmd_Set_ArrayUnit_Addr	:	PCLINK_Decode_SetArrayUnitAddr(stCmd, pComm->chRxBuf);	// 设置阵元地址参数
													break;	
			case Device_Cmd_SD_SetCardType		:	PCLINK_Decode_SD_SetCardType(stCmd);					// 设置设备内置存储卡型号
													break;			
			case Device_Cmd_Set_DeviceVersion	:	PCLINK_Decode_SetVersion(pComm->chRxBuf);				// 设置设备版本号
													break;
			case Device_Cmd_Set_DeviceProtect	:	PCLINK_Decode_SetProtect(stCmd);						// 设置设备拆机保护模式
													break;
			case Device_Cmd_Set_DeviceEncrypt	:	PCLINK_Decode_SetEncrypt(stCmd, pComm->chRxBuf);		// 设置设备数据加密模式
													break;
			case Device_Cmd_Set_Compass			:	PCLINK_Decode_SetCompassParam(pComm->chRxBuf);			// 设置电子罗盘附件参数
													break;
			default	:	if ( stCmd.nCmd >= 30 && stCmd.nCmd < 100 )				// 对PC发送的SD卡操作命令进行处理
						{
							PCLINK_Decode_SD(pComm->chRxBuf, &stCmd);
						}
						else if ( stCmd.nCmd >= 200 && stCmd.nCmd < 250 )		// 判断指令是否是同步控制指令操作
						{
							PCLINK_Decode_TimeRef(pComm->chRxBuf, &stCmd);
						}
			break;
			}
		}

		pComm->uCommRx = 0;
		pComm->bRdy = false;
	}
}

