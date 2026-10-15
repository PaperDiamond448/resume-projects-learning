
#include "PCLink_operate.h"
#include "iSBL_operate.h"
#include "FPGA_operate.h"

#if	PSENSOR_MS5837
#include "ms5837.h"
#elif PSENSOR_MPM3801
#include "mpm3801.h"
#endif

#if USE_COMPASS && COMPASS_TYPE == COMPASS_DCM250B
#include "DCM250B_operate.h"
#elif USE_COMPASS && COMPASS_TYPE == COMPASS_LCAxx
#include "LCAxx_operate.h"
#endif

bool m_bPpsTrig			= false;
bool m_bPpsLock			= false;		// 恒温晶振秒脉冲锁定状态
bool m_bSendTrig		= false;
bool m_bDebugGainSet	= false;		// 调试时固定增益参数，用于接收电路测试，true 调试时固定增益，false 自动增益控制

bool m_bPwOn		 	= false;		// 唤醒上电状态
bool m_bWatch 			= false;		// 值班状态
bool m_bSelfTest	 	= false;		// 自检状态
bool m_bSelfTestPass 	= false;		// 自检通过状态
u8   m_nSelfTestCnt  	= 0;			// 自检时间计数器

float m_fVoltSysPw = 0;					// 系统电源电压
float m_fVoltLiPw  = 0;					// 锂电池组电压
float m_fHeading, m_fPitch, m_fRoll;	// 电子罗盘传感器的方位角，俯仰角，翻滚角
float m_fPValue, m_fTValue;				// 压力传感器的压力值，温度值

/*************************************************************************************************/
// 背景噪声处理相关参数
// 背景噪声积分长度控制在 15秒，则滑动窗长度约为 64
u8 m_uGainSwDelayCnt = 0;		// 增益切换延迟计数器
u8 m_uNoiseSlideWinCnt = 0;		// 背景噪声滑动窗滑动计数值	
float m_fNoiseMean[NOISE_WIN];	// 每个数据窗计算出的噪声均值
float m_fNoiseVoltSAvg;		// 当前噪声电压有效值（非滑动窗均值）
float m_fNoiseAvgVolt;			// 背景噪声电压均值
float  m_fAvgBuf[RECV_DATA_BLOCK] = {0};		// 每个数据块的均值数据缓冲区
float  m_fSlipeBuf[RECV_DATA_BLOCK*2] = {0};	// 数据块噪声均值数据滑动缓冲区

float m_fFilterCoef0 = 0.0119583991836254;
// 噪声平滑滤波器系数
float m_fFilterCoef[NOISE_WIN] = 
	{	0.0008820477381123,-0.0000000000000000,-0.0010690965495653,-0.0022954635716931,-0.0036345287535327,-0.0050272947588109,-0.0064015598997062,-0.0076740009944734,\
		-0.0087530791338388,-0.0095426783209489,-0.0099463382329398,-0.0098718987633580,-0.0092363384619548,-0.0079705640489827,-0.0060238958643408,-0.0033679957506123, \
		0.0000000000000000, 0.0040553497031068, 0.0087447411290753, 0.0139866016718436, 0.0196727308849916, 0.0256712350212193, 0.0318306745812766, 0.0379852699021686, \
		0.0439609503086938, 0.0495819828426811, 0.0546778800863535, 0.0590902654246683, 0.0626793697091019, 0.0653298462030884, 0.0669556204234655, 0.0675035365825716, \
		0.0669556204234655, 0.0653298462030884, 0.0626793697091019, 0.0590902654246683, 0.0546778800863535, 0.0495819828426811, 0.0439609503086938, 0.0379852699021686, \
		0.0318306745812766, 0.0256712350212193, 0.0196727308849916, 0.0139866016718436, 0.0087447411290753, 0.0040553497031068, 0.0000000000000000,-0.0033679957506123,\
		-0.0060238958643408,-0.0079705640489827,-0.0092363384619548,-0.0098718987633580,-0.0099463382329398,-0.0095426783209489,-0.0087530791338388,-0.0076740009944734,\
		-0.0064015598997062,-0.0050272947588109,-0.0036345287535327,-0.0022954635716931,-0.0010690965495653,-0.0000000000000000, 0.0008820477381123, 0.0119583991836254,};
/*************************************************************************************************/
		
void Acq_SetRun(bool bRun)
{
#if DEVICE_SUBTYPE/100 == iPositionT3 || \
	DEVICE_SUBTYPE/100 == iBeaconR    || DEVICE_SUBTYPE/100 == iBeaconR2
	if ( bRun == m_bStartStatus ) return;
	if ( bRun )
	{
		SetSystemClock(SYSCLK_ACQ, false);			// 由于主要计算量均在FPGA，ARM降频以减小功耗
		Acq_ClrParam();								// 清空采集参数

		// 等待系统上电并且稳定后再配置FPGA参数
		FPGA_Init();								// 初始化FPGA管脚
		Device_Decode_SetMode(g_stSys.uMode, 0);	// 设置工作模式
		Device_SetSampleRate(g_stSys.nSampleRate);	// 设置采样率
		Device_SetSampleGain(g_stSys.nSampleGain);	// 设置增益或者测试模式
		Device_SetDeadGate(g_stSys.nDeadGate);		// 设定死区时间
		Device_SetCatchGate(g_stSys.nCatchGate);	// 设置捕获时间
		Device_SetEmitParam();						// 设置发射参数
		Device_SetDetectParam();					// 设置检测信号参数

		printf(" Start acquire.\r\n");
		FPGA_IntEn(true);							// 使能FPGA中断准备开始采集
		FPGA_SetAcqEn(true);						// 发送指令，FPGA开始采集，使能时清零FPGA中断向量
	}
	else
	{
		FPGA_SetAcqEn(false);						// 发送指令，FPGA停止采集
		FPGA_IntEn(false);							// 关闭FPGA采集中断
		Acq_ClrParam();								// 清空采集参数
		printf(" Stop acquire.\r\n");
	}
#endif
}

void Acq_ClrParam(void)
{
	int i = 0;
	m_bPpsTrig = false;
	m_nFpgaInt = ACQ_PROC_NULL;
	m_fPValue  = 0;	m_fTValue = 0;
	m_fHeading = 0; m_fPitch   = 0;	m_fRoll   = 0;
	
	m_hRtcCnt.uAcqIdle = 0;
	m_hRtcCnt.uPPSIdle = 0;
	m_hRtcCnt.uAcqRstCnt = 0;
	
	for(i=0; i<RECV_DATA_BLOCK*2; i++)
	{
		// 设置背景噪声平滑滤波器的初始值
		m_fSlipeBuf[i] = PRO_GATE_INIT*0.5f;	
	}
}

void Device_InitDefaultParam(void)
{
	// 设备参数初始化
#if DEVICE_SUBTYPE/100 == iPositionT1	|| DEVICE_SUBTYPE/100 == iPositionT2	|| DEVICE_SUBTYPE/100 == iPositionT3
	iPosition_InitDefaultParam();
#elif DEVICE_SUBTYPE/100 == iBeaconB	|| DEVICE_SUBTYPE/100 == iBeaconB2		|| \
	  DEVICE_SUBTYPE/100 == iBeaconR 	|| DEVICE_SUBTYPE/100 == iBeaconR2
	iBeacon_InitDefaultParam();
#endif
}

void Device_DebugInfo(void)
{	
	int nSampGain = Device_GetRealSampleGain(g_stSys.nSampleGain);	
	printf(" Acquire signal conditioning circuit fixed gain is %d(dB).\r\n", nSampGain);
}

void Device_InitializeComm(void)
{
	// 设备初始化串口模块
#if DEVICE_SUBTYPE/100 == iPositionT1

#elif DEVICE_SUBTYPE/100 == iPositionT2
	Comm1_Config(57600, true);				// 串口1——Debug PCLINK串口
	Comm2_Config(9600,  true);				// 串口2——倾角传感器串口
	Comm3_Config(115200, true);				// 串口3——网关单元至基元和通讯机串口
#elif DEVICE_SUBTYPE/100 == iPositionT3
	Comm2_Config(9600, true);				// 串口2——PCLINK串口
	Comm6_Config(115200, true);				// 串口6——Debug 串口
#elif DEVICE_SUBTYPE/100 == iPositionC
	
#elif DEVICE_SUBTYPE/100 == iBeaconB
	
#elif DEVICE_SUBTYPE/100 == iBeaconB2
	Comm1_Config(19200, true);				// 串口1——继电器控制器通讯串口
	Comm2_Config(115200, true);				// 串口2——PCLINK串口
	Comm6_Config(115200, true);				// 串口6——Debug 串口
#elif DEVICE_SUBTYPE/100 == iBeaconR
	Comm1_Config(9600, true);				// 串口1——倾角传感器串口
	Comm_Config(PCLINK_Comm, PCLINK_CommBaud, true);	// PCLINK串口，开关盒串口
	Comm_Config(DEBUG_Comm, DEBUG_Comm_Baud, true);		// Debug 串口
#elif DEVICE_SUBTYPE/100 == iBeaconR2
	Comm1_Config(USE_SPORT_BAUD, false);	// 串口1——FPGA到ARM数据传输串口
	Comm3_Config(9600, true);				// 串口3——PCLINK串口，倾角传感器串口，
#endif
	PCLINK_Init();							// 初始化通讯和调试接口
}

void Device_Initialize(void)
{
	// 设备初始化
	int i;
	SetSystemClock(SYSCLK_ACQ, false);			// 设置系统时钟
	Device_InitializeComm();					// 设备初始化串口
	
#if DEVICE_SUBTYPE/100 == iBeaconB	|| DEVICE_SUBTYPE/100 == iBeaconB2	|| \
	DEVICE_SUBTYPE/100 == iBeaconR	|| DEVICE_SUBTYPE/100 == iBeaconR2
	iBeacon_Initialize();	
#endif
	
	g_stSys.nSampleGain = 0;								// 上电时设置增益为0
	g_stSys.m_fDetectGate = PRO_GATE_INIT;					// 预置检测门限初始值
	m_fNoiseAvgVolt = PRO_GATE_INIT/PRO_DETECT_COEF;		// 预置检测噪声均值，防止滤波器溢出
	for(i=0; i<NOISE_WIN; i++)	m_fNoiseMean[i] = g_stSys.m_fDetectGate/2;
}

void Device_OnDaqStart(void)
{
	// 开始采集
#if DEVICE_SUBTYPE/100 == iPositionT1	|| DEVICE_SUBTYPE/100 == iPositionT2	|| DEVICE_SUBTYPE/100 == iPositionT3
	Acq_SetRun(true);
#endif
}

void Device_OnDaqStop(void)
{
	// 停止采集
#if DEVICE_SUBTYPE/100 == iPositionT1	|| DEVICE_SUBTYPE/100 == iPositionT2	|| DEVICE_SUBTYPE/100 == iPositionT3
	Acq_SetRun(false);
#endif
}

void Device_OnEchoLink(void)
{
	// 响应设备连接事件
	
}

void Device_OnEchoMain(void)
{
	// 主程序main函数循环调用，响应低速事件
#if DEVICE_SUBTYPE/100 == iPositionT1	|| DEVICE_SUBTYPE/100 == iPositionT2	|| DEVICE_SUBTYPE/100 == iPositionT3
	iPosition_OnEchoMain();
#endif
}

void Device_OnEchoSensor(void* pCommObj)
{
	// 响应设备消息
#if DEVICE_SUBTYPE/100 == iBeaconR || DEVICE_SUBTYPE/100 == iBeaconR2
	iBeacon_OnEchoSensor( pCommObj );
#endif
}

void Device_OnEchoAcq(void)
{
#if DEVICE_SUBTYPE/100 == iPositionT
	iPosition_OnEchoAcq();
#elif DEVICE_SUBTYPE/100 == iBeaconR || DEVICE_SUBTYPE/100 == iBeaconR2
	iBeacon_OnEchoAcq();
#endif
}

void Device_OnEchoRtc(void)//RTC每秒中断
{
	// RTC interrupt operate function
	if (!m_bInitDone) return;
	RTC_FreshTime();				// 更新系统时间值

#if DEVICE_SUBTYPE/100 == iPositionC
	iPositionC_OnEchoRtc();
#elif DEVICE_SUBTYPE/100 == iBeaconB	|| DEVICE_SUBTYPE/100 == iBeaconB2	|| \
	  DEVICE_SUBTYPE/100 == iBeaconR 	|| DEVICE_SUBTYPE/100 == iBeaconR2
	iBeacon_OnEchoRtc();
#endif
}

void Device_OnEchoInt(void)
{
	// 响应中断处理
#if DEVICE_SUBTYPE/100 == iPositionT3
	m_nFpgaInt = iPosition_OnEchoInt();
#else
	m_nFpgaInt = FPGA_AcqProc();
	m_hRtcCnt.uAcqIdle   = 0;				// 采集过程中每次FPGA中断清零一次
	m_hRtcCnt.uAcqRstCnt = 0;				// 保持正常检测处理模式	
#endif	
}

void Device_OnEchoPps(void)//PPS，明明这里面啥也没有
{
	// PPS interrupt operate funciton
	if (!m_bInitDone) return;
#if DEVICE_SUBTYPE/100 == iPositionC	|| DEVICE_SUBTYPE/100 == iBeaconB
	if (m_bStartStatus)
	{
		m_bPpsTrig 	= true;//// 置"该发射"标志
		m_bSendTrig	= true;// 触发上报
	}
	else
	{
		m_bPpsTrig 	= false;
		m_bSendTrig	= false;
	}
#endif	
}

int Device_OnEchoGps(void* pCommObj)
{
	int nResult = 0;
#if USE_GPS

#endif
	return nResult;
}

void Device_OnEchoComm1(void* pCommObj)
{
	// 响应串口1中断
#if DEVICE_SUBTYPE/100 == iBeaconR
	iBeacon_OnEchoSensor( pCommObj );
#endif
}

void Device_OnEchoComm2(void* pCommObj)
{
	// 响应串口2中断
#if DEVICE_SUBTYPE/100 == iPositionT2
	iPositon_OnEchoSensor( pCommObj );
#elif DEVICE_SUBTYPE/100 == iPositionT3
	OnEchoPCLINK(pCommObj);
#endif
}

void Device_OnEchoComm3(void* pCommObj)
{
	// 响应串口3中断
#if DEVICE_SUBTYPE/100 == iBeaconR2
	iBeacon_OnEchoSensor( pCommObj );
#endif
}

void Device_OnEchoComm1DMA(void)
{
	// 响应串口2DMA中断
#if DEVICE_SUBTYPE/100 == iBeaconR2
	if ( m_bStartStatus )
	{
		// 由于使用串口DMA传输数据，因此需要在DMA中断中清除接收中断
		FPGA_CMD_Write(FPGA_ADDR_INT_CLR, 0xFFFF); 	// 清除检测中断标志位
		Device_OnEchoAcq();							// 调用处理函数，对接收的数据进行处理
	}
#endif
}

void Device_OnEchoAdDMA(void)
{
	// 响应AD-DMA中断
#if DEVICE_SUBTYPE/100 == iBeaconR || DEVICE_SUBTYPE/100 == iBeaconR2
	if ( m_bStartStatus )
	{
		iBeacon_OnEchoAdDMA();
	}
#endif
}

void Device_OnEchoSpiDMA(void)
{
	// 响应SPI-DMA中断

}

void Device_Echo_Link(char* pBuf)
{
	// 响应设备连接指令
	// 数据采用拼接方式
	assert_param(pBuf);
	sprintf(pBuf, "%d,%d,%d,%d,%d,%d,%.2f,%.2f,ok*\n",
		DEVICE_TYPE,          DEVICE_SUBTYPE,//设备类型
		g_stSys.nSampleRate,  g_stSys.nSampleGain,//采样率 增益
		g_stSys.nDeadGate,    g_stSys.nCatchGate,//死区时间 捕获时间
		g_stSys.m_fDepthGate, g_stSys.m_fDepthErrCoef);//水深门限 水深误差系数
}

void Device_Echo_Info(bool bInit)
{
	// 响应设备参数消息, bInit = 1 初始化后第一条指令info指令 
	COMM_OBJECT* pComm = PCLINK_GetCommObj();
	if (!m_bPcStatus && pComm == 0) return;//没联机且通信对象不存在则不发送信息

#if DEVICE_SUBTYPE/100 == iPositionT2	|| DEVICE_SUBTYPE/100 == iPositionT3
	sprintf(pComm->chTxBuf, "-> $%d,%d,%d,%d,%d,%d,%d,%0.1f,%0.1f,%0.1f,%0.1f,%0.1f,%0.3f,%0.3f,%0.3f*\n",
		g_stSys.nAddr, Device_Cmd_Get_Info,	bInit, m_bPcStatus,	
		m_bStartStatus, m_bPpsLock, g_stSys.nSampleGain, 
		m_fPValue, m_fTValue, m_fHeading, m_fPitch, m_fRoll, 
		m_fNoiseVoltSAvg, m_fNoiseAvgVolt, g_stSys.m_fNoiseLevel);
#elif DEVICE_SUBTYPE/100 == iBeaconB    || DEVICE_SUBTYPE/100 == iBeaconB2	|| \
	  DEVICE_SUBTYPE/100 == iBeaconR    || DEVICE_SUBTYPE/100 == iBeaconR2
	sprintf(pComm->chTxBuf, "-> $%d,%d,%d,%d,%d,%d,%d,%0.1f,%0.1f,%0.1f,%0.1f,%0.1f,%0.3f,%0.3f,%0.3f,%0.3f,%0.3f*\n",
		g_stSys.nAddr, Device_Cmd_Get_Info,	bInit, m_bPcStatus,	
		m_bStartStatus, m_bPpsLock, g_stSys.nSampleGain, //是否启动 PPS锁定状态 增益
		m_fPValue, m_fTValue, m_fHeading, m_fPitch, m_fRoll, //P T 以及航向角等
		m_fNoiseVoltSAvg, m_fNoiseAvgVolt, g_stSys.m_fNoiseLevel, m_fVoltSysPw, m_fVoltLiPw);
#elif DEVICE_SUBTYPE/100 == iPositionC
	
#endif
	delay(100); PCLINK_SendMsg(pComm);

	if (bInit)
	{
		Device_Decode_GetParam(0);			// 发送发射和检测信号信息参数
	#if DEVICE_SUBTYPE/100 == iBeaconB2
		iBeacon_Echo_SetRelayInfo();		// 发送继电器配置参数
	#endif
	}
}

int Device_Decode_Link(char* cmd)
{
	// 响应设备连接命令 
	int result = 0;
#if DEVICE_SUBTYPE/100 == iPositionT3 || DEVICE_SUBTYPE/100 == iBeaconB
	Acq_SetRun(false);						// 停止运行
#elif DEVICE_SUBTYPE/100 == iBeaconR  || DEVICE_SUBTYPE/100 == iBeaconR2
	iBeacon_SelfTestEn(false);				// 和上位机连接时，停止自检
#endif
	return result;
}

int Device_Decode_Debug(void* pStCmd, char* cmd)
{
	// 响应设备调试命令
	int result = 0;
	COMM_COMMAND* pCmd = (COMM_COMMAND*)pStCmd;	assert_param(pStCmd);
#if DEVICE_SUBTYPE/100 == iPositionT3
	result = iPosition_Decode_Debug(pStCmd, cmd);	
#elif DEVICE_SUBTYPE/100 == iBeaconB    || DEVICE_SUBTYPE/100 == iBeaconB2	|| \
	  DEVICE_SUBTYPE/100 == iBeaconR    || DEVICE_SUBTYPE/100 == iBeaconR2
	result = iBeacon_Decode_Debug(pStCmd, cmd);	
#endif
	if ( result ) PCLINK_Echo(*pCmd);
	return result;
}

int Device_Decode_DAQ_Start(char* cmd)
{
	// 响应开始采集命令
	int result = 0;
	u32 uMode; int nRate, nGain;
	COMM_COMMAND stCmd;
	stCmd.nAddr = g_stSys.nAddr;
	stCmd.nCmd  = Device_Cmd_Acq_Start;
	stCmd.wParam = 0; stCmd.lParam = 0;
	
	result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%u%*[ ,]%d%*[ ,]%d",
		&stCmd.nAddr, &stCmd.nCmd, &uMode, &nRate, &nGain);
	
	if (result >= 5)
	{	
		g_stSys.uMode = uMode;
		g_stSys.nSampleRate = nRate;
		g_stSys.nSampleGain = nGain;
		result = 1;
	}
	return result;
}

int Device_Decode_SetMode(u32 wParam, u32 lParam)
{
	int result = 0;
#if DEVICE_SUBTYPE/100 == iPositionT1	|| DEVICE_SUBTYPE/100 == iPositionT2	|| DEVICE_SUBTYPE/100 == iPositionT3
	result = iPosition_SetMode(wParam, lParam);
#elif DEVICE_SUBTYPE/100 == iBeaconB2	|| \
	  DEVICE_SUBTYPE/100 == iBeaconR    || DEVICE_SUBTYPE/100 == iBeaconR2
	result = iBeacon_SetMode(wParam, lParam);
#else
	g_stSys.uMode = wParam;	result = 1;
#endif
	return result;
}

int Device_Decode_SetParam(void* pStCmd, char* cmd)
{
	// 响应设置参数指令
	int result = 0;
	COMM_COMMAND* pCmd = (COMM_COMMAND*)pStCmd;
	
	if (pCmd->wParam == PARAM_DEAD_GATE)
	{
		// 设定死区时间，单位0.1mS
		g_stSys.nDeadGate = pCmd->lParam;
		Device_SetDeadGate(pCmd->lParam);
		result = 1;
	}
	else if (pCmd->wParam == PARAM_CATCH_GATE)
	{
		// 设定捕获时间，单位1uS
		g_stSys.nCatchGate = pCmd->lParam;
		Device_SetCatchGate(pCmd->lParam);
		result = 1;
	}
	else if (pCmd->wParam == PARAM_DEPTH_GATE)
	{
		// 设定深度触发门限，单位：米（水深）
		g_stSys.m_fDepthGate = (float)pCmd->lParam;
		result = 1;
	}
	else if (pCmd->wParam == PARAM_DEPTH_ERR_COEF)
	{
		// 设定深度触发门限修正值，单位：米（水深）
		g_stSys.m_fDepthErrCoef = (float)pCmd->lParam;
		result = 1;
	}
	else if (pCmd->wParam == PARAM_EMIT_PARAM)
	{
		// 设定发射信号参数
		COMM_COMMAND stCmd;
		SIGNAL_PARAM hParam;
	
		result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[ ,]%d%*[ ,]%d%*[ ,]%u%*[ ,]%u%*[ ,]%u%*[ ,]%u",
				&stCmd.nAddr, &stCmd.nCmd, &stCmd.wParam, &stCmd.lParam,
				&hParam.nSn,     &hParam.nType,   
				&hParam.uWidth,  &hParam.uPeriod,
				&hParam.uFreqL,  &hParam.uFreqH);
			
		if (result >= 10 && hParam.nSn < MAX_EMIT_PARAM_GROUP)
		{
			COMM_OBJECT* pComm = PCLINK_GetCommObj();
			
			g_stSys.Emit_Param[hParam.nSn].nType 	= hParam.nType;
			g_stSys.Emit_Param[hParam.nSn].uWidth 	= hParam.uWidth;
			g_stSys.Emit_Param[hParam.nSn].uPeriod 	= hParam.uPeriod;
			g_stSys.Emit_Param[hParam.nSn].uFreqL 	= hParam.uFreqL;
			g_stSys.Emit_Param[hParam.nSn].uFreqH 	= hParam.uFreqH;
			
			if (pComm)
			{
				sprintf(m_pCommObject->chTxBuf, "->$%d,%d,%d,%d,%d,%d,%d,%u,%u,%u,%u,ok*\r\n",
					g_stSys.nAddr, Device_Cmd_Get_Param, PARAM_EMIT_PARAM, 1,
					MAX_EMIT_PARAM_GROUP, hParam.nSn,
					g_stSys.Emit_Param[hParam.nSn].nType,
					g_stSys.Emit_Param[hParam.nSn].uWidth,
					g_stSys.Emit_Param[hParam.nSn].uPeriod,
					g_stSys.Emit_Param[hParam.nSn].uFreqL,
					g_stSys.Emit_Param[hParam.nSn].uFreqH);
				
				delay(5); PCLINK_SendMsg(m_pCommObject);
			#if DEVICE_SUBTYPE/100 == iPositionT2 || DEVICE_SUBTYPE/100 == iPositionT3
				iPosition_SetEmitParam();
			#elif DEVICE_SUBTYPE/100 == iBeaconB    || DEVICE_SUBTYPE/100 == iBeaconB2   || \
				  DEVICE_SUBTYPE/100 == iBeaconR    || DEVICE_SUBTYPE/100 == iBeaconR2
				iBeacon_SetEmitParam();				// 写入发射参数到FPGA
			#endif
				SysParam_Save(); return 1;			// 保存参数后直接返回
			}
		}
	}
	else if (pCmd->wParam == PARAM_DETECT_PARAM)
	{
		// 设定发射信号参数
		COMM_COMMAND stCmd;
		u32 uDetectSigType, uSel, uDetectFreq1, uDetectFreq2;
		COMM_OBJECT* pComm = PCLINK_GetCommObj();
	
		result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[ ,]%u%*[ ,]%u%*[ ,]%u%*[ ,]%u",
				&stCmd.nAddr, &stCmd.nCmd, &stCmd.wParam, &stCmd.lParam,
				&uSel, &uDetectSigType, &uDetectFreq1, &uDetectFreq2);
			
		if (result >= 8)
		{
			bool bSet = false;
			if (uDetectSigType == SIGNAL_CW || uDetectSigType == SIGNAL_LFM)
			{
				if (uSel < MAX_DETECT_PARAM_GROUP)
				{
					g_stSys.Detect_Param[0][uSel] = uDetectSigType;
					g_stSys.Detect_Param[1][uSel] = uDetectFreq1;
					g_stSys.Detect_Param[2][uSel] = uDetectFreq2;
					bSet = true;
				}
			}
			
			if (bSet && pComm)
			{
				sprintf(m_pCommObject->chTxBuf, "->$%d,%d,%d,%d,%d,%u,%u,%u,%u,ok*\r\n",
					g_stSys.nAddr, Device_Cmd_Set_Param, PARAM_DETECT_PARAM,  1,
					MAX_DETECT_PARAM_GROUP, uSel, uDetectSigType, uDetectFreq1, uDetectFreq2);
				
				delay(5); PCLINK_SendMsg(m_pCommObject);
				SysParam_Save(); return 1;			// 保存参数后直接返回
			}
		}
	}
	else if (pCmd->wParam == PARAM_RELAY_SW_TIME)
	{
		COMM_COMMAND stCmd;
		u32 uRelaySn, uSwTime;
		COMM_OBJECT* pComm = PCLINK_GetCommObj();
	
		result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[ ,]%u%*[ ,]%u",
				&stCmd.nAddr, &stCmd.nCmd, &stCmd.wParam, &stCmd.lParam,
				&uRelaySn, &uSwTime);
			
		if (result >= 6)
		{	
			if (pComm)
			{
				sprintf(m_pCommObject->chTxBuf, "->$%d,%d,%d,%d,%u,%u,ok*\r\n",
					g_stSys.nAddr, Device_Cmd_Set_Param, PARAM_RELAY_SW_TIME,  1,
					uRelaySn, uSwTime);
				
				delay(5); PCLINK_SendMsg(m_pCommObject);
			}
			SysParam_Save(); return 1;			// 保存参数后直接返回
		}
		else result = 0;
	}

	if (result)
	{
		PCLINK_Echo(*pCmd); SysParam_Save(); 
	}
	return result;
}

int Device_Decode_GetParam(void* pStCmd)
{
	// 响应设置发送发射信号信息
	int result = 0;
#if DEVICE_SUBTYPE/100 == iBeaconB		|| DEVICE_SUBTYPE/100 == iBeaconB2   || \
	DEVICE_SUBTYPE/100 == iBeaconR		|| DEVICE_SUBTYPE/100 == iBeaconR2
	COMM_OBJECT* pComm = PCLINK_GetCommObj();
	if (pComm)
	{
		int i;
		delay(5);
		for(i=0;i<MAX_EMIT_PARAM_GROUP;i++)
		{
			sprintf(m_pCommObject->chTxBuf, "->$%d,%d,%d,%d,%d,%d,%u,%u,%u,%u,%u,ok*\r\n",
				g_stSys.nAddr, Device_Cmd_Get_Param, PARAM_EMIT_PARAM, 1,
				MAX_EMIT_PARAM_GROUP, i,
				g_stSys.Emit_Param[i].nType,
				g_stSys.Emit_Param[i].uWidth,
				g_stSys.Emit_Param[i].uPeriod,
				g_stSys.Emit_Param[i].uFreqL,
				g_stSys.Emit_Param[i].uFreqH);
			PCLINK_SendMsg(m_pCommObject); delay(5);
		}
		
		delay(5);
		for(i=0;i<MAX_DETECT_PARAM_GROUP;i++)
		{
			sprintf(m_pCommObject->chTxBuf, "->$%d,%d,%d,%d,%d,%d,%u,%u,%u,ok*\r\n",
				g_stSys.nAddr, Device_Cmd_Set_Param, PARAM_DETECT_PARAM, 1,
				MAX_DETECT_PARAM_GROUP, i, 
				g_stSys.Detect_Param[0][i], 
				g_stSys.Detect_Param[1][i], 
				g_stSys.Detect_Param[2][i]);
			PCLINK_SendMsg(m_pCommObject); delay(5);
		}
		result = 1;
	}
#endif
	return result;
}

int Device_Decode_SetSampleRate(void* pStCmd)
{
	// 响应解码保存信号参数
	int result = 0;

	return result;
}

int  Device_Decode_SetSampleGain(u32 wParam, u32 lParam)
{
	// 响应译码设置增益参数
	int result = 0;

	return result;
}

int Device_Decode_SetEmitEn(bool bEn)
{
	// 响应发射使能命令
	int result = 0;

	return result;
}

int Device_Decode_SetRelay(u32 wParam , u32 lParam)
{
	// 响应设置继电器命令
	int result = 0;
#if DEVICE_SUBTYPE/100 == iPositionT1
	if (stCmd.wParam == 1)	Device_SetRelay(stCmd.lParam);
	result = 1;
#elif DEVICE_SUBTYPE/100 == iBeaconB2
	iBeacon_SetRelay(wParam, lParam);
	SysParam_Save(); result = 1;
#endif
	return result;
}

int Device_Decode_GetMasterStatus(void* pCmd, char* cmd)
{
	// 响应获取主机状态参数
	int result = 0;
#if DEVICE_SUBTYPE/100 == iBeaconR || DEVICE_SUBTYPE/100 == iBeaconR2
	if (!m_bSelfTestPass)
	{
		// 发送回执消息，自检未通过
		PCLINK_EchoASC(Device_Cmd_Get_MasterStatus, MStatus_Err, 0);
	}
	else if (m_bSelfTestPass)
	{
		PCLINK_EchoASC(Device_Cmd_Get_MasterStatus, MStatus_Good, 0);
	}
	result = 1;
#endif
	return result;
}

int Device_Decode_RecvData(void* pStCmd, char* cmd)
{
	// 接收用于相关处理的发射脉冲信号数据
#if DEVICE_SUBTYPE/100 == iPositionT2
	// 接收到此指令时，直接将数据通过串口3发送给各个阵元，更新阵元内部数据
	if (m_pComm3)
	{
		int i=0;
		m_pComm3->chTxBuf[0] = '$';
		m_pComm3->chTxBuf[1] = ',';
		while(*cmd != '*' && i<MAX_COMM_BUF_SIZE-2)
		{
			m_pComm3->chTxBuf[i+2] = *(cmd+i);
			i++;
		}
		m_pComm3->chTxBuf[i+2] = '*';			// 补上‘*’
		Comm3_PutBuf(m_pComm3->chTxBuf, i+2);	// 发送数据
	}
	
#elif DEVICE_SUBTYPE/100 == iPositionC
	COMM_COMMAND* pCmd = (COMM_COMMAND*)pStCmd;
	assert_param(pStCmd);

	int nAddr, nCmd, nType, nLen, nBlock, nResult;
	char chData[128];
	
	if ( pCmd->wParam == DATA_16 && pCmd->lParam == 128)
	{
		int i;
		nResult = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%d%*[ ,]%d%*[ ,]%d%*[ ,]%128c",
			&nAddr, &nCmd,
			&nType,	&nLen, &nBlock, chData);

		// 解析数据并保存到相关数据中
		if (nResult >= 6 && nBlock * (stCmd.lParam / 2 + 1) < PULSE_DATA_BLOCK * FPGA_BUF_SIZE)
		{
			int nPos = nBlock * (pCmd->lParam / GetSizeOfData(pCmd->wParam));
			for(i=0; i<stCmd.lParam/2; )
			{
				unsigned int nData;
				char buf[4] = {0, 0, 0, 0};
				buf[0] = chData[i];
				buf[1] = chData[i+1];
				sscanf(buf, "%x", &nData);
				m_dataPulse[nPos+i] = (u16)nData;
			}
		}
		return 1;
	}
#endif
	return 0;
}

int Device_Decode_SetCalibrate(void* pStCmd)
{
	// 响应校准命令
	int result = 0;

	return result;
}

int Device_GetRealSampleRate(u8 uRate)
{
	switch (uRate)
	{
		case 1 : return 1;
		case 2 : return 2;
		default : return 0;
	}		
}

int	Device_GetRealSampleGain(u8 uGain)
{
	int result = 0;
#if DEVICE_SUBTYPE/100 == iPositionT1
	result = iPositionT_GetRealSampleGain(uGain);
#endif
	return result;
}

void Device_CalcNoiseGate(float fNoise)
{
	// 计算噪声门限参数，输入参数为当前噪声均值
#ifdef DEBUG_SELF_TEST
	if (m_bSelfTest)
	{
		Device_SetSampleGain(0x01); return;				// 自检时增益参数设置
	}
#endif
	if (fNoise <= 0.0001f || fNoise > 10.0f) return;	// 剔除过小的错误值，实测本底噪声可达0.1mV
	else m_fNoiseVoltSAvg = fNoise;						// 更新背景噪声短时积分均值参数
	
	if (m_uNoiseSlideWinCnt < NOISE_WIN)
	{
		m_fNoiseMean[m_uNoiseSlideWinCnt++] = fNoise;
	}
	else
	{
		// 当填充满滑动窗时，进行先入后出滑动
		int i;
		for (i=0; i<NOISE_WIN-1; i++)
			m_fNoiseMean[i] = m_fNoiseMean[i+1];

		if (fNoise < 2.0f) 
			m_fNoiseMean[NOISE_WIN-1] = fNoise;		// 新数据压入堆栈
		else
			m_fNoiseMean[NOISE_WIN-1] = 2.0f;		// 限制过大的峰值信号，减小对平滑窗的影响
		
		// 对噪声均值数组中的数据进行带通滤波，减少数据干扰
		// 采用n阶FIR滤波器，低通0.1Hz，不加窗
		m_fNoiseAvgVolt = fNoise*m_fFilterCoef0;	// 对输出y赋初值
	
		for(i = 0; i < NOISE_WIN; i++) 				// 计算滤波器输出值
			m_fNoiseAvgVolt += m_fFilterCoef[i] * m_fNoiseMean[i];

		if ( !m_bDebugGainSet )
		{
		#if DEVICE_SUBTYPE/100 == iBeaconR
			if (!m_bStartStatus && g_stSys.nSampleGain > 3) 
				g_stSys.nSampleGain = 0x03;			// 值班状态，增益参数复位
		#endif

			if ( m_uGainSwDelayCnt > 0 ) m_uGainSwDelayCnt--;
			// 增益调节时，增加速度减慢，减小速度加快，避免快速调节增益时带来的干扰
			if ( m_fNoiseAvgVolt < g_stSys.m_fNoiseLevel && fNoise < g_stSys.m_fNoiseLevel )		
			{
				// 调整增益值增加，增加速度由 m_uGainSwDelayCnt 控制
				#if DEVICE_SUBTYPE/100 == iBeaconR2
				// 取消最大增益档位，注意，最大档位为7，不能超过此值
				if((m_bStartStatus && g_stSys.nSampleGain < 6)) 
				#else
				// 取消最大增益档位，注意，最大档位为6，不能超过此值
				if((m_bStartStatus && g_stSys.nSampleGain < 5) || (!m_bStartStatus && g_stSys.nSampleGain < 3)) 
				#endif
				{
					if ( m_uGainSwDelayCnt <= 0 )	// 增益增加，每6秒调整一次
						g_stSys.nSampleGain++;		// 调整增益增加
				#if DEVICE_SUBTYPE/100 == iBeaconR || DEVICE_SUBTYPE/100 == iBeaconR2
					m_bCatch = false;
					m_bCatchHold = false;			// 为了避免增益切换时的干扰问题，清零捕获信号
				#endif
				}
			}
			else if ( m_fNoiseAvgVolt > g_stSys.m_fNoiseLevel*2 || fNoise > g_stSys.m_fNoiseLevel*2)
			{
				if(g_stSys.nSampleGain > 0) 
				{
					g_stSys.nSampleGain--;			// 调整增益
					m_uGainSwDelayCnt = 6;			// 赋值增益增加延迟等待计数器，防止快速增加增益
				#if DEVICE_SUBTYPE/100 == iBeaconR || DEVICE_SUBTYPE/100 == iBeaconR2
					m_bCatch     = false;
					m_bCatchHold = false;			// 为了避免增益切换时的干扰问题，清零捕获信号
				#endif
				}
			}
		}
		Device_SetSampleGain(g_stSys.nSampleGain);	// 自适应调节增益参数
		
		m_fNoiseAvgVolt = m_fNoiseAvgVolt < 0.1f ? 0.1f : m_fNoiseAvgVolt;	// 强制提高门限，避免噪声过低容易误触发
		g_stSys.m_fDetectGate = m_fNoiseAvgVolt*PRO_DETECT_COEF;			// 更新检测门限，噪声门限是噪声均值的 PRO_DETECT_COEF 倍	
	}
}

void Device_SetPpsOutEn(bool bEn)
{
	// 使能或者禁止FPGA输出PPS秒脉冲至同步设备
#ifdef PPS_OUT_EN_GPIO
	// 控制信号与PPS负脉冲信号或逻辑输出，因此高电平禁止
	if (bEn)
		GPIO_ResetBits(PPS_OUT_EN_GPIO_PORT, PPS_OUT_EN_GPIO);
	else
		GPIO_SetBits(PPS_OUT_EN_GPIO_PORT, PPS_OUT_EN_GPIO);
#endif
}

void Device_SetDeadGate(u16 uNew)
{
	// 设置检测盲区
#if DEVICE_SUBTYPE/100 == iPositionT1
	FPGA_SetDeadGate(uNew*128);		// 单位：128uS/bit
#elif DEVICE_SUBTYPE/100 == iPositionT2
	if (m_pComm3)
	{
		//  给设备的通用地址发送消息，且不需要回应
		sprintf(m_pComm3->chTxBuf, "$%d,%d,%d,%d*\n",
		0, Device_Cmd_Set_Param, PARAM_DEAD_GATE, uNew);
		Comm_SendMsg(m_pComm3);
	}
#elif DEVICE_SUBTYPE/100 == iPositionT3 || DEVICE_SUBTYPE/100 == iPositionC  || \
	  DEVICE_SUBTYPE/100 == iBeaconB    || DEVICE_SUBTYPE/100 == iBeaconB2
	FPGA_SetDeadGate(uNew);			// 单位：0.1mS
#elif DEVICE_SUBTYPE/100 == iBeaconR    || DEVICE_SUBTYPE/100 == iBeaconR2
	FPGA_SetDeadGate(uNew);			// 单位：1mS
#endif	
}

void Device_SetCatchGate(u16 uNew)
{
	// 设置检测脉宽参数
#if DEVICE_SUBTYPE/100 == iPositionT1
	FPGA_SetCatchGate(uNew);
#elif DEVICE_SUBTYPE/100 == iPositionT2
	if (m_pComm3)
	{
		//  给设备的通用地址发送消息，且不需要回应
		sprintf(m_pComm3->chTxBuf, "$%d,%d,%d,%d*\n",
		0, Device_Cmd_Set_Param, PARAM_CATCH_GATE, uNew);
		Comm_SendMsg(m_pComm3);
	}
#elif DEVICE_SUBTYPE/100 == iPositionT3 || DEVICE_SUBTYPE/100 == iPositionC  || \
	  DEVICE_SUBTYPE/100 == iBeaconB    || DEVICE_SUBTYPE/100 == iBeaconB2   || \
	  DEVICE_SUBTYPE/100 == iBeaconR    || DEVICE_SUBTYPE/100 == iBeaconR2
	FPGA_SetCatchGate(uNew);
#endif	
}

void Device_SetEmitParam(void)				
{
	// 设置发射信号参数
#if DEVICE_SUBTYPE/100 == iPositionT3
	iPosition_SetEmitParam();
#elif DEVICE_SUBTYPE/100 == iPositionC4
	iPositionC_SetEmitParam();
#elif DEVICE_SUBTYPE/100 == iBeaconB 	|| DEVICE_SUBTYPE/100 == iBeaconB2	|| \
	  DEVICE_SUBTYPE/100 == iBeaconR 	|| DEVICE_SUBTYPE/100 == iBeaconR2
	iBeacon_SetEmitParam();
#endif
}

void Device_SetDetectParam()
{
	// 设置检测信号参数
#if DEVICE_SUBTYPE/100 == iPositionT3
	iPosition_SetDetectParam();
#endif
}

int Device_SetSampleRate(u8 uNew)
{
	// 设置采样速率
	int result = 0;
#if DEVICE_SUBTYPE/100 == iPositionT1	|| DEVICE_SUBTYPE/100 == iPositionT2
	result = iPositionT_SetSampleRate();
#endif
	return result;
}

int Device_SetSampleGain(u8 uNew)
{
	int result = 0;
#if DEVICE_SUBTYPE/100 == iPositionC 
	result = iPositionC_SetSampleGain(uNew);
#elif DEVICE_SUBTYPE/100 == iBeaconB2 	|| DEVICE_SUBTYPE/100 == iBeaconR || DEVICE_SUBTYPE/100 == iBeaconR2
	result = iBeacon_SetSampleGain(uNew);
#endif
	return result;
}

int Device_SetRelay(u32 wParam, u32 lParam)
{
	// 设置继电器
	int result = 0;
#if DEVICE_SUBTYPE/100 == iPositionT1
//	if (stCmd.wParam == 1)
//		Device_SetRelay(stCmd.lParam);
	result = 1；
#endif
	return result;
}

/*****************************************************************************/

