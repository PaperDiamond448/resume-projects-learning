
#include "PCLink_operate.h"
#include "iSBL_operate.h"
#include "FPGA_operate.h"
#include "iBeaconB_operate.h"

#if	PSENSOR_MS5837
#include "ms5837.h"
#elif PSENSOR_MPM3801
#include "mpm3801.h"
#endif

#if DEVICE_SUBTYPE/100 == iBeaconB

void iBeacon_InitDefaultParam(void)
{
	g_stSys.nSampleRate = POS_Rate1;
	g_stSys.nSampleGain = POS_G0;
	
	g_stSys.m_fDepthGate = 0.2;				// 深度设置阈值 0.2m
	g_stSys.m_fDepthErrCoef	 = 0;			// 深度阈值修正值
	
	g_stSys.nDeadGate	= 1024;				// 死区时间，单位：1uS
	g_stSys.nCatchGate	= 256;				// 捕获时间，单位：1uS	
}

void iBeacon_Initialize(void)
{
	// 设备初始化
	
}

int  iBeacon_SetSampleRate(u8 uNew)
{
	// 设置采样速率
	int result = 0;
	return result;
}

int  iBeacon_SetSampleGain(u8 uNew)
{	
	// 设置信标增益
	int result = 0;
//	if (uNew > 0x03)
//	{
//		GPIO_SetBits(AMP_GPIO_PORT, AMP_A0_GPIO);
//		GPIO_SetBits(AMP_GPIO_PORT, AMP_A1_GPIO);
//		if (m_bAcqStart)
//		{
//			// 当已经开始调节FGPA控制的增益放大器时，默认ARM控制的增益调节到最大
//			// 然后再调节FPGA的增益
//			FPGA_SetSampleGain(uNew - 0x03);
//		}
//	}
//	else
//	{
//		if(uNew&0x01)
//			GPIO_SetBits(AMP_GPIO_PORT, AMP_A0_GPIO);
//		else
//			GPIO_ResetBits(AMP_GPIO_PORT, AMP_A0_GPIO);	
//		
//		if(uNew&0x02)
//			GPIO_SetBits(AMP_GPIO_PORT, AMP_A1_GPIO);
//		else
//			GPIO_ResetBits(AMP_GPIO_PORT, AMP_A1_GPIO);	
//	}
	return result;
}

void iBeacon_SetEmitParam(void)//把信号参数转成fpga能理解的格式
{
	// 设置发射参数	
	double f1, f2, T, FreqDiv, StepDiv;
	u32 uType, uFreqDiv, uStepDiv;
	SIG_PARAM* pParam;
	
	//typedef struct
	//{
	//	u8	nType;		// 信号类型
	//	u32 uWidth;		// 脉冲宽度（单位：us）
	//	u32 uPeriod;	// 脉冲间隔（单位：ms）
	//	u32 uFreqL;		// 起始频率（单位：Hz）
	//	u32 uFreqH;		// 终止频率（单位：Hz）
	//}SIG_PARAM;
	
	// 将读取的数据转换成FPGA需要的格式，注意FPGA的系统主频
	pParam = &g_stSys.Emit_Param[0];	// 应答信号发射参数

	if ( pParam )
	{
		// 近似按照等差数列的方式计算频率步进
		// (1/(2f1)+1/(2f2))*a/2 = T
		// 1/(2f2)+(a-1)*dT = 1/(2f1)
		// 其中假设a为偶数等差数列的项数
		// T为脉冲宽度
		// dT为时间步进
		f1 = (double)pParam->uFreqL;
		f2 = (double)pParam->uFreqH;
		T = (double)pParam->uWidth;
		
		FreqDiv = 1/f1*0.5*FPGA_CLOCK;	
		uFreqDiv = (u32)FreqDiv;			// 初始频率分频系数
		
		if (pParam->nType == SIGNAL_CW)
		{
			uType = 0;						// FPGA 内部 CW 为 0
			uStepDiv = 0;
		}
		else if(pParam->nType == SIGNAL_LFM)
		{
			uType = 1;						// FPGA 内部 LFM 为 1
			T = T/1000000.0f;
			StepDiv = (0.5/f1-0.5/f2)/(2*T/(0.5f/f1+0.5/f2)-1.0);
			uStepDiv = (u32)(StepDiv*FPGA_CLOCK);	
		}
		else return;
		
		FPGA_CMD_Write(FPGA_ADDR_SIG_TYPE, uType);
		FPGA_CMD_Write(FPGA_ADDR_PULSE_WIDTH1, (u16)(pParam->uWidth>>16));
		FPGA_CMD_Write(FPGA_ADDR_PULSE_WIDTH2, (u16)pParam->uWidth);
		FPGA_CMD_Write(FPGA_ADDR_FREQ_WORD1, (u16)(uFreqDiv>>16));
		FPGA_CMD_Write(FPGA_ADDR_FREQ_WORD2, (u16)uFreqDiv);
		FPGA_CMD_Write(FPGA_ADDR_STEP_WORD1, (u16)(uStepDiv>>16));
		FPGA_CMD_Write(FPGA_ADDR_STEP_WORD2, (u16)uStepDiv);
		delay(1);
	}	
}

void iBeacon_OnEchoRtc(void)
{
	// 响应RTC中断处理
	MS5837_Getdata();				// 采集温度压力数据
	m_fPValue = MS5837_GetDepth();
	m_fTValue = MS5837_GetTemperature();
	
	if ( m_fPValue > 0.2f) 
	{
	//	Device_SetPpsOutEn(true);
		m_stLedTrig.bShineG = true;	// 绿灯闪烁表示运行状态
	}
	else
	{
	//	Device_SetPpsOutEn(false);
		m_stLedTrig.bShineR = true;	// 红灯闪烁表示待机状态
	}
}

int iBeacon_Decode_Debug(void* pStCmd, char* cmd)
{
	// 译码并响应调试指令
	int result = 0;
	COMM_COMMAND* pCmd = (COMM_COMMAND*)pStCmd;
	pCmd->lParam = pCmd->wParam;
	if (pCmd->wParam == DEBUG_NULL)
	{
		if (m_bStartStatus) Acq_SetRun(false);
		SysParam_Load();							// 重新加载参数，避免因为调试造成参数错误
		result = 1;
	}
	else if (pCmd->wParam == DEBUG_OP3)
	{
		if (!m_bStartStatus) Acq_SetRun(true);		
		result = 1;
	}
	else if (pCmd->wParam == DEBUG_OP4)
	{
		COMM_COMMAND stCmd;
		float fNoiseLevel;
	
		result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[ ,]%f",
				&stCmd.nAddr, &stCmd.nCmd, &stCmd.wParam, &stCmd.lParam,
				&fNoiseLevel);
			
		if (result >= 5)
		{	
			g_stSys.m_fNoiseLevel = fNoiseLevel;
			SysParam_Save(); result = 1;				// 保存参数
		}
	}
	return result;
}

int iBeacon_SetMode(u32 wParam, u32 lParam)
{
	// 设置工作模式
	int result = 0;
//	iBeaconB_SetRelayMode(wParam); 
	g_stSys.uMode = wParam; result = 1;		// 设置继电器工作模式
	return result;
}

#endif	// DEVICE_SUBTYPE/100 == iBeaconB
