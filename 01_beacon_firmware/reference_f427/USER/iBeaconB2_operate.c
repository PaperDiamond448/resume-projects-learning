
#include "PCLink_operate.h"
#include "iSBL_operate.h"
#include "FPGA_operate.h"
#include "iBeaconB2_operate.h"

#if DEVICE_SUBTYPE/100 == iBeaconB2

bool m_bCodeRecv = false;
int  m_nCodeResult = 0;						// 接收机解码输出
int  m_nCodeWDogCnt = 0;					// 译码校验等待时间窗口

void iBeacon_InitDefaultParam(void)
{
	g_stSys.uMode		= 0x00;				// 继电器触发模式
	g_stSys.uRelaySwTime[0] = 1;			// 继电器触发时间
	g_stSys.uRelaySwTime[1] = 1;			// 继电器触发时间
	g_stSys.uRelaySwTime[2] = 1;			// 继电器触发时间
	g_stSys.uRelaySwTime[3] = 1;			// 继电器触发时间
	g_stSys.uRelaySwTime[4] = 1;			// 继电器触发时间
	g_stSys.nSampleGain = POS_G0;
	
	g_stSys.nDeadGate	= 1000;				// 死区时间，单位：1mS
	g_stSys.nCatchGate	= 2048;				// 捕获时间，单位：1uS
	g_stSys.uDetectMode = 0;
	
	g_stSys.m_fNoiseLevel = 0.25;			// 噪声增益门限调节值，默认0.25V
	
	g_stSys.Detect_Param[0][0] = SIGNAL_CW;
	g_stSys.Detect_Param[1][0] = 21000;		// 21kHz
	g_stSys.Detect_Param[2][0] = 21000;			
	
	g_stSys.Detect_Param[0][1] = SIGNAL_CW;
	g_stSys.Detect_Param[1][1] = 24000;		// 24kHz
	g_stSys.Detect_Param[2][1] = 24000;		
	
	g_stSys.Detect_Param[0][2] = SIGNAL_CW;
	g_stSys.Detect_Param[1][2] = 27000;		// 27kHz
	g_stSys.Detect_Param[2][2] = 27000;	
	
	g_stSys.Emit_Param[0].nType 	= SIGNAL_CW;
	g_stSys.Emit_Param[0].uWidth 	= 5000;			// 脉冲宽度5mS
	g_stSys.Emit_Param[0].uPeriod 	= 100000;		// 发射间隔默认100mS
	g_stSys.Emit_Param[0].uFreqL 	= 33333;		// 发射起始频率
	g_stSys.Emit_Param[0].uFreqH 	= 33333;		// 发射终止频率
	
}

int iBeacon_SetMode(u32 wParam, u32 lParam)
{
	// 设置工作模式
	int result = 0;
	iBeacon_SetRelayMode(wParam); 			// 给继电器控制板发送消息配置继电器工作模式
	g_stSys.uMode = wParam; result = 1;		// 设置继电器工作模式
	return result;
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
	if (uNew > 0x03)
	{
		GPIO_SetBits(AMP_GPIO_PORT, AMP_A0_GPIO);
		GPIO_SetBits(AMP_GPIO_PORT, AMP_A1_GPIO);
		if (m_bAcqStart)
		{
			// 当已经开始调节FGPA控制的增益放大器时，默认ARM控制的增益调节到最大
			// 然后再调节FPGA的增益
			FPGA_SetSampleGain(uNew - 0x03);
		}
	}
	else
	{
		if(uNew&0x01)
			GPIO_SetBits(AMP_GPIO_PORT, AMP_A0_GPIO);
		else
			GPIO_ResetBits(AMP_GPIO_PORT, AMP_A0_GPIO);	
		
		if(uNew&0x02)
			GPIO_SetBits(AMP_GPIO_PORT, AMP_A1_GPIO);
		else
			GPIO_ResetBits(AMP_GPIO_PORT, AMP_A1_GPIO);	
	}
	return result;
}

void iBeacon_SetRelayMode(bool bInit)
{
	// 设置继电器工作模式
	sprintf(m_pComm1->chTxBuf, "-> $0,%d,%d,%d*\n",	Device_Cmd_Set_Mode, g_stSys.uMode, bInit);
	PCLINK_SendMsg(m_pComm1);
}

void iBeacon_SetRelay(u8 uCh, u8 uSec)
{
	// 设置继电器控制状态	
	if (m_pComm1)
	{
		u8 uS;
		if (g_stSys.uMode&0x01) 
			uS = 0;					// 继电器保持模式
		else	
			uS = uS < 1 ? 1 : uSec;	// 继电器脉冲触发模式，时长最小不能小于1秒
		
		if ( uCh >= 1 && uCh <= MAX_RELAY_NUM)
		{
			COMM_OBJECT* pComm = PCLINK_GetCommObj();	
			g_stSys.uRelaySwTime[uCh-1] = uSec;
			
			sprintf(m_pComm1->chTxBuf, "-> $0,%d,%d,%d*\n",	Device_Cmd_Set_Relay, uCh, uSec);
			PCLINK_SendMsg(m_pComm1);
			ShineBlue();				// 正确接收并发送指令后蓝灯闪亮
			
			// 给主控软件发送消息，提示接收到指令	
			if (pComm)
			{
				sprintf(m_pCommObject->chTxBuf, "->$%d,%d,%d,%d,ok*\r\n",
					g_stSys.nAddr, Device_Cmd_Response, uCh, 1);
				delay(5); PCLINK_SendMsg(m_pCommObject);
			}
		}
	}
}

void iBeacon_SetEmitParam(void)
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
	float fNoise;
	m_stLedTrig.bShineG = true;		
	fNoise = FPGA_GetNoiseAvgData();	// 读取噪声有效值
	Device_CalcNoiseGate(fNoise);		// 计算和设置门限
	
	if ( m_nCodeWDogCnt > 0) 
	{
		m_nCodeWDogCnt--;
		if ( m_nCodeWDogCnt <= 1 ) 
			m_nCodeResult = 0;			// 超时清零
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
		if (m_bAcqStart) Acq_SetRun(false);
		SysParam_Load();							// 重新加载参数，避免因为调试造成参数错误
		result = 1;
	}
	else if (pCmd->wParam == DEBUG_OP3)
	{
		if (!m_bAcqStart) Acq_SetRun(true);		
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

void iBeacon_Echo_SetRelayInfo(void)
{
	// 回应继电器设置参数
	int i=0;
	COMM_OBJECT* pComm = PCLINK_GetCommObj();
	if (!m_bPcStatus && pComm == 0) return;

#if DEVICE_SUBTYPE/100 == iBeaconB2
	for (i=0; i<MAX_RELAY_NUM; i++)
	{
	sprintf(pComm->chTxBuf, "-> $%d,%d,%d,%d*\n",
		g_stSys.nAddr, Device_Cmd_Set_Relay,	i+1, g_stSys.uRelaySwTime[i]);
		PCLINK_SendMsg(pComm);delay(5);
	}
#endif
}
	
#endif	// DEVICE_SUBTYPE/100 == iBeaconB2
