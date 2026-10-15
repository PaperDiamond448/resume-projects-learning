#ifndef __iSBL_operate_H
#define __iSBL_operate_H

#include "main.h"

// 信标和短基线及短基线基元子设备整合编号
#define iPositionT1			1		// 2018年维修版本，作用距离200米， 第一版，以ARM为主控的程序
#define iPositionT2			10		// 2020年全新版本，作用距离1000米，第一版，网关主控的程序
#define iPositionT3			11		// 2022年，搜索声呐，主控单元

#define iBeaconB			30		// 2020年，全新版本，短基线信标的程序，包含通讯功能
#define iBeaconB2			31		// 2022年，仅带简易FSK通信功能的信标

#define iBeaconR			40		// 2021年，高低频应答信标的程序
#define iBeaconR2			41		// 2022年，高低频应答信标的程序，新版，国产化

#define iPositionT1_V1		100		// 2018年维修版本，作用距离200米， 第一版，以ARM为主控的程序
#define iPositionT2_V1		1000	// 2020年，全新版本，作用距离1000米，第一版，网关主控的程序
#define iPositionT3_V1		1101	// 2022年，搜索声呐，主控单元程序

#define iBeaconB_V1			3000	// 2026年，全新版本，短基线定位信标，包含通讯功能
#define iBeaconB2_V1		3100	// 2022年，带简易FSK通信功能的信标

#define iBeaconR_V1			4000	// 2021年，高低频应答信标的程序
#define iBeaconR2_V1		4100	// 2022年，高低频应答信标的程序，新版，国产化

#define DEVICE_SUBTYPE		iBeaconB_V1

/*************************************************************************************************/
#if DEVICE_SUBTYPE/100 == iBeaconB
#include "iBeaconB_operate.h"
#elif DEVICE_SUBTYPE/100 == iBeaconB2
#include "iBeaconB2_operate.h"
#elif DEVICE_SUBTYPE/100 == iBeaconR || DEVICE_SUBTYPE/100 == iBeaconR2
#include "iBeaconR_operate.h"
#endif
/*************************************************************************************************/
#define PSENSOR_MS5837			1
#define PSENSOR_MPM3801			0

/*************************************************************************************************/
#define NOISE_WIN				64					// 定义噪声自动门限滑动平均窗阶数

#define PRO_GATE_INIT			0.250f				// 定义初始检测门限值，有效值0.25V
#define PRO_DETECT_COEF			2.000f				// 定义检测门限和噪声均值的比率

/*************************************************************************************************/
// 此部分宏定义是用于数据接口类型和模式
#define USE_FMC_CMD_PORT		1		// ARM和FPGA指令接口使用 FMC  的宏定义

#define USE_SPI_CMD_PORT		0		// ARM和FPGA指令接口使用 SPI  的宏定义
#if USE_SPI_CMD_PORT
#define USE_SPI1_AS_CMD_PORT	1		// ARM和FPGA指令接口使用 SPI1 的宏定义
#define USE_SPI4_AS_CMD_PORT	0		// ARM和FPGA指令接口使用 SPI4 的宏定义
#define USE_SPI5_AS_CMD_PORT	0		// ARM和FPGA指令接口使用 SPI5 的宏定义
#endif

#define USE_SPI_DATA_PORT		0		// ARM和FPGA数据接口使用 SPI  的宏定义
#if USE_SPI_DATA_PORT
#define USE_SPI1_AS_DATA_PORT	0		// ARM和FPGA数据接口使用 SPI1 的宏定义
#define USE_SPI4_AS_DATA_PORT	1		// ARM和FPGA数据接口使用 SPI4 的宏定义
#endif

#define USE_UART_DATA_PORT		0		// ARM和FPGA数据接口使用 串口 的宏定义
#if USE_UART_DATA_PORT
#define USE_COMM1_AS_DATA_PORT	1		// ARM和FPGA数据接口使用 COMM1 的宏定义
#endif
/*************************************************************************************************/

typedef enum
{
	POS_G0 = 0, POS_G1,	POS_G2,	POS_G3,	POS_GEND,
}POS_SAMPLE_GAIN_TypeDef;

typedef enum				// 采样率
{
	POS_SR0 = 0,			// 200kHz
	POS_SR1,				// 400kHz
	POS_SR2,				// 800kHz
	POS_SR3,				// 1000kHz
	POS_SREND,
}POS_SAMPLE_RATE_TypeDef;

typedef enum				// 定位刷新率
{
	POS_Rate1 = 0,			// 1Hz
	POS_Rate2,				// 2Hz
	POS_REND,
}POS_FRESH_RATE_TypeDef;

typedef enum
{
	PARAM_DEAD_GATE = 0,	// 检测死区门限参数
	PARAM_CATCH_GATE,		// 捕获脉宽设定参数
	PARAM_CONST_GATE,		// 固定门限设定参数
	PARAM_DEPTH_GATE,		// 深度触发门限参数
	PARAM_DEPTH_ERR_COEF,	// 深度触发门限修正参数
	PARAM_CELL_ID,			// 设置基元ID
	PARAM_EMIT_PARAM,		// 设置发射信号参数
	PARAM_DETECT_PARAM,		// 设置检测信号参数
	PARAM_RELAY_SW_TIME,	// 设置继电器保持时间参数
}PARAM_SET_TypeDef;

typedef struct
{
	int nSn;				// 信号在配置区的序号
	int	nType;				// 信号类型
	u32 uWidth;				// 脉冲宽度（单位：us）
	u32 uPeriod;			// 脉冲间隔（单位：us）
	u32 uFreqL;				// 起始频率（单位：Hz）
	u32 uFreqH;				// 终止频率（单位：Hz）
	float fAmplitude;		// 输出幅度
}SIGNAL_PARAM;

typedef enum				// debug 模式
{
	DEBUG_NULL = 0,			// 无调试操作
	DEBUG_OP1,				// 调试时，发送原始波形数据
	DEBUG_OP2,				// 调试时，发送FFT处理结果
	DEBUG_OP3,				// 调试时，强制发射脉冲信号
	DEBUG_OP4,				// 调试时，设置噪声增益调节控制门限参数
	DEBUG_OP5,				// 调试时，强制设定调试增益
}DEBUG_operate_TypeDef;

typedef enum 
{
	MStatus_Good = 1,
	MStatus_Err,
}TypeDef_MasterStatus_Type;	// 主机状态消息参数

/*************************************************************************************************/
extern bool m_bDebugGainSet;			// 调试时设定增益参数，用于接收电路测试

extern bool m_bPwOn;					// 唤醒上电状态
extern bool m_bWatch;					// 值班状态
extern bool m_bSelfTest;				// 自检状态
extern bool m_bSelfTestPass;			// 自检通过状态
extern u8 m_nSelfTestCnt;				// 自检时间计数器

extern float m_fVoltSysPw;				// 系统电源电压
extern float m_fVoltLiPw;				// 锂电池组电压
extern float m_fHeading, m_fPitch, m_fRoll;		// 倾角传感器的俯仰角，翻滚角
extern float m_fPValue, m_fTValue;				// 压力传感器的压力值，温度值

extern u8 m_uGainSwDelayCnt;					// 增益切换延迟计数器
extern u8 m_uNoiseSlideWinCnt;					// 背景噪声滑动窗滑动计数值
extern float m_fNoiseMean[NOISE_WIN];			// 每个数据窗计算出的噪声均值
extern float m_fNoiseVoltSAvg;					// 背景噪声电压短时积分有效值（非滑动窗均值）
extern float m_fNoiseAvgVolt;					// 背景噪声电压长时积分有效值（滑动窗均值）
extern float m_fAvgBuf[RECV_DATA_BLOCK];		// 用于进行背景噪声计算的数据块的均值计算结果缓冲区
extern float m_fSlipeBuf[RECV_DATA_BLOCK*2];	// 噪声均值数据滑动缓冲区
extern float m_fFilterCoef0;
extern float m_fFilterCoef[NOISE_WIN];

/*************************************************************************************************/

void Acq_SetRun(bool bRun);						// 使能或停止采集
void Acq_ClrParam(void);						// 清空采集参数

void Device_InitDefaultParam(void);				// 设备参数初始化
void Device_DebugInfo(void);					// 打印输出采集信息
void Device_InitializeComm(void);				// 设备初始化串口，（出于时钟频率变化等原因）
void Device_Initialize(void);					// 设备初始化
void Device_OnDaqStart(void);					// 开始采集
void Device_OnDaqStop(void);					// 停止采集
void Device_OnEchoMain(void);					// 响应低速事件（主程序main函数循环调用）
void Device_OnEchoSensor(void* pCommObj);		// 响应传感器消息				
void Device_OnEchoAcq(void);					// 响应设备采集处理
void Device_OnEchoRtc(void);					// 响应RTC中断处理
void Device_OnEchoInt(void);					// 响应中断处理
void Device_OnEchoPps(void);					// 响应PPS秒脉冲中断处理
int  Device_OnEchoGps(void* pCommObj);			// 响应GPS消息处理
void Device_OnEchoComm1(void* pCommObj);		// 响应串口1中断
void Device_OnEchoComm2(void* pCommObj);		// 响应串口2中断
void Device_OnEchoComm3(void* pCommObj);		// 响应串口3中断
void Device_OnEchoComm1DMA(void);				// 响应串口1-DMA中断
void Device_OnEchoAdDMA(void);					// 响应AD-DMA中断
void Device_OnEchoSpiDMA(void);					// 响应SPI-DMA中断

void Device_Echo_Link(char* pBuf);							// 响应设备连接指令
void Device_Echo_Info(bool bInit);							// 响应设备信息指令, bInit = 1 初始化后第一条指令info指令 

int Device_Decode_Link(char* cmd);							// 响应设备连接命令 
int Device_Decode_Debug(void* pStCmd, char* cmd);			// 响应设备调试命令
int Device_Decode_DAQ_Start(char* cmd);						// 响应开始采集命令
int Device_Decode_SetMode(u32 wParam, u32 lParam);			// 设置工作模式
int Device_Decode_SetParam(void* pStCmd, char* cmd);		// 响应设置参数指令
int Device_Decode_GetParam(void* pStCmd);					// 发送发射信号信息
int Device_Decode_SaveParam(void* pStCmd);					// 响应解码保存信号参数
int Device_Decode_SetSampleRate(void* pStCmd);				// 响应译码设置采样率参数
int Device_Decode_SetSampleGain(u32 wParam, u32 lParam);	// 响应译码设置增  益参数
int Device_Decode_SetEmitEn(bool bEn);						// 响应发射使能命令
int Device_Decode_SetRelay(u32 wParam , u32 lParam);		// 响应设置继电器命令
int Device_Decode_GetMasterStatus(void* pCmd, char* cmd);	// 响应获取主机状态参数
int Device_Decode_RecvData(void* pStCmd, char* cmd);		// 接收用于相关处理的发射脉冲信号数据
int Device_Decode_SetCalibrate(void* pStCmd);				// 响应校准命令

int Device_GetRealSampleRate(u8 uRate);			// 此处两个函数暂时保留
int Device_GetRealSampleGain(u8 uGain);

void Device_CalcNoiseGate(float fNoise);		// 计算噪声门限参数，输入参数为当前噪声均值

/*************************************************************************************************/
// 以下为控制FPGA进行相关操作的设备函数封装，内部有对不同类型子设备的宏定义设计
void Device_SetDeadGate(u16 uNew);				// 设置检测盲区
void Device_SetCatchGate(u16 uNew);				// 设置检测脉宽
void Device_SetEmitParam(void);					// 设置发射信号参数
void Device_SetDetectParam(void);				// 设置检测信号参数
int  Device_SetSampleRate(u8 uNew);				// 设置采样速率
int  Device_SetSampleGain(u8 uNew);				// 设置阵元增益
int  Device_SetRelay(u32 wParam, u32 lParam);	// 设置继电器
/*************************************************************************************************/

#endif	// __iSBL_operate_H

/*
// 使用DSP_LIB库
1、
默认情况下，STM32F4xx的FPU是禁用的，可以通过设置协处理器控制寄存器（CPACR）来开启硬件FPU。
在keil编程环境下，可以通过定义全局宏定义标识符_FPU_PRESENT和_FPU_USED都为1来开启硬件FPU。
其中宏定义标识符_FPU_PRESENT用来确认处理是否带有FPU功能，标识符_FPU_USED用来确定是否开启FPU功能。
实际上，因为STM32F4是带有FPU功能的，所以在stm32f4xx.h头文件中，默认定义 _FPU_PRESENT 为 1。

2、
在工程定义中，添加宏定义
ARM_MATH_CM4, __CC_ARM, ARM_MATH_MATRIX_CHECK, ARM_MATH_ROUNDING, __FPU_PRESENT=1, __FPU_USED =1

3、
添加库文件
将DSP_LIB文件夹放于工程文件夹下，与USER等文件夹并列即可。
然后打开工程，新建DSP_LIB分组，并将arm_cortexM4lf_math.lib添加到工程里面，
并添加DSP_LIB下的include文件夹到头文件包含路径中
然后在调用DSP_LIB函数的文件中包含 arm_math.h

4、
DSP库主要包含以下几个分库：

BasicMathFunctions		–基本数学函数：提供浮点数的各种基本运算函数，如向量加减乘除等运算。
CommonTables 			–arm_common_tables.c文件提供位翻转或相关参数表。
ComplexMathFunctions 	–复杂数学功能，如向量处理，求模运算的。
ControllerFunctions 	–控制功能函数。包括正弦余弦，PID电机控制，矢量Clarke变换，矢量Clarke逆变换等。
FastMathFunctions 		–快速数学功能函数。提供了一种快速的近似正弦，余弦和平方根等相比CMSIS计算库要快的数学函数。
FilteringFunctions 		–滤波函数功能，主要为FIR和LMS（最小均方根）等滤波函数。
MatrixFunctions 		–矩阵处理函数。包括矩阵加法、矩阵初始化、矩阵反、矩阵乘法、矩阵规模、矩阵减法、矩阵转置等函数。
StatisticsFunctions 	–统计功能函数。如求平均值、最大值、最小值、计算均方根RMS、计算方差/标准差等。
SupportFunctions 		–支持功能函数，如数据拷贝，Q格式和浮点格式相互转换，Q任意格式相互转换。
TransformFunctions 		–变换功能。包括复数FFT（CFFT）/复数FFT逆运算（CIFFT）、实数FFT（RFFT）/实数FFT逆运算（RIFFT）、和DCT（离散余弦变换）和配套的初始化函数。

*/

