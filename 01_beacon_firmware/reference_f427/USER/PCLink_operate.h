#ifndef __PCLINK_operate_H
#define __PCLINK_operate_H

#include "main.h"
#include <math.h>
#include "IO_operate.h"
#include "ADC_operate.h"
#include "RTC_operate.h"
#include "Comm_operate.h"
#include "aes.h"			// AES加密算法库

#if USE_FATFS
#include "SD_operate.h"
#endif

#if USE_NET
#include "Net_operate.h"
#endif

#if USE_GPS
#include "Gps.h"
#endif

#define DATA_HEADER_KEYWORD		0x5555aaaa
#define DATA_END_KEYWORD		0xffffaaaa

// 数据文件上传缓冲区参数定义
#define MAX_LOAD_BUF			1024//单次上传的数据包大小
#if USE_STM32F1XX
#define MAX_LOAD_BUF_BLOCK		4		// 对STM32F103，不能建立大缓存，值的大小要根据所选器件设定
#elif USE_STM32F4XX
#define MAX_LOAD_BUF_BLOCK		8		// 上位机程序已经改进，这个值可以根据处理器内存大小更改
#elif USE_STM32H7XX
#define MAX_LOAD_BUF_BLOCK		32		// 上位机程序已经改进，这个值可以根据处理器内存大小更改
#endif

#define MAX_HEADER_BUF_SIZE		512 	// 文件头缓存的大小
#define MAX_PRELOAD_BUF			MAX_LOAD_BUF*MAX_LOAD_BUF_BLOCK//设置缓冲区大小，上传 SD 卡文件时，一次读 8KB 数据到缓冲区，然后分批发给上位机。缓冲区越大，传输效率越高，但占用内存也越多。

#define _MAX_PATH   260 	/* max. length of full pathname *///完整路径最大长度
#define _MAX_DRIVE  3   	/* max. length of drive component盘符长度 */
#define _MAX_DIR    256 	/* max. length of path component目录路径最大长度 */
#define _MAX_FNAME  256 	/* max. length of file name component 文件名最大长度*/
#define _MAX_EXT    256 	/* max. length of extension component 扩展名最大长度*/
//exp：完整路径： "0:/DATA/2024/20240501_123456.DAT"
//          └─┬─┘ └─┬─┘ └────┬────┘ └──┬──┘
//         _MAX_DRIVE _MAX_DIR  _MAX_FNAME _MAX_EXT
#if USE_STM32F1XX//定义芯片的Flash大小
	#define MAX_ROM_SIZE	256
#elif USE_STM32F4XX || USE_STM32H7XX
	#define MAX_ROM_SIZE	2048
#endif

#define POWER_RELAY_LIMIT		0		// 电源继电器数量限制

#define tDevice_Null			0
#define tDeviceCRIO_Relay		13		// cRIO 继电器供电控制板
#define tDeviceIListenS			14		// 自容式水听器，单通道版
#define	tDeviceIListenM			15		// 自容式水听器，多通道版
#define	tDeviceIListenN			16		// 多通道数据网络采集板
#define tDeviceIListenZ			17		// 多通道数据网络采集器-Zynq版本
#define	tDeviceILogger			20		// 基于STM32F103的记录设备
#define	tDeviceILoggerPresure	21		// 自容式压力传感器
#define	tDeviceShortBaseLine	30		// 短基线定位系统
#define tDeviceIBeacon			31		// 短基线定位系统信标，应答器   是这个
#define tDeviceIDeckUnit		32		// 应答器甲板单元
#define	tDeviceIPowerAmp		40		// 数控声源发射系统
#define	tDeviceSeaDaq			50		// 多通道阵列数据采集系统
#define tDeviceIPowerCtrl		60		// 应用于潜标系统的继电器开关控制板
#define tDeviceITimeRef			70		// 时钟基准控制板
#define tDeviceITimeRefModel	71		// 时钟模块
#define tDeviceIUTM				80		// 测厚设备
#define tDeviceIUDM				90		// 测深设备
#define	tDeviceIPowerProbe		100		// 单通道功率测量探头
#define tDeviceISigCond			110		// 信号调理器
#define tDeviceISensor			120		// 传感器模块
#define tDeviceIBuoy			130		// 浮标集成传感器天线，可能包含各种传感器，如GPS，气象仪等

#define DEVICE_TYPE		 tDeviceIBeacon	// 定义设备类型，通过修改 DEVICE_TYPE，同一套代码可以编译出不同产品的固件。

/*************************************************************************************************/
#if DEVICE_TYPE == tDevice_Null || DEVICE_TYPE == tDeviceIBuoy
	#include "Device_operate.h"
#elif DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceIListenN || DEVICE_TYPE == tDeviceIPowerProbe
	#include "FPGA_operate.h"
	#include "iListen_operate.h"
#elif DEVICE_TYPE == tDeviceIUTM || DEVICE_TYPE == tDeviceIUDM
	#include "FPGA_operate.h"
	#include "iEmit_operate.h"
	#include "iListenNE_operate.h"
#elif DEVICE_TYPE == tDeviceILogger || DEVICE_TYPE == tDeviceILoggerPresure		// 基于STM32F103的记录设备		
	#include "Acq_operate.h"
	#include "Device_operate.h"
#elif DEVICE_TYPE == tDeviceShortBaseLine	|| DEVICE_TYPE == tDeviceIBeacon	// 短基线及其信标
	#include "iSBL_operate.h"
#elif DEVICE_TYPE == tDeviceIDeckUnit			// 应答器甲板单元
	#include "iDeckUnit_operate.h"
	#include "FPGA_operate.h"
#elif DEVICE_TYPE == tDeviceIPowerCtrl			// 应用于潜标系统的继电器开关控制板 
	#include "iPowerCtrl_operate.h"
#elif DEVICE_TYPE == tDeviceIPowerAmp			// 潜标声源发射系统
	#include "iEmit_operate.h"
#elif DEVICE_TYPE == tDeviceITimeRef			// 时统单元设备
	#include "iTimeRef_operate.h"
#elif DEVICE_TYPE == tDeviceITimeRefModel		// 时钟模块设备
	#include "GPSDO_operate.h"
#elif DEVICE_TYPE == tDeviceISensor				// 传感器模块 
	#include "iSensor_operate.h"
#elif DEVICE_TYPE == tDeviceISigCond			// 多通道信号调理器设备
	#include "iSignalCond.h"
	#include "FPGA_operate.h"
#endif
/*************************************************************************************************/

#ifdef USE_ATOMIC
#define ATOMIC_SA45			1
#define ATOMIC_SA33			2
#define ATOMIC_SA31			3
#define ATOMIC_X72			4
#define ATOMIC_PRS10		5
#define ATOMIC_STM_Rb		11
#define ATOMIC_STM_Rb_H		12		
#define ATOMIC_MODEL4_V1	401		// 以 X72 为基准的原子钟时统终端
#define ATOMIC_MODEL5_V1	501		// 以 PRS10 为基准的原子钟时统终端
#define ATOMIC_MODEL5_V2	511		// 以 PRS10 为基准的原子钟时统单元，用于基阵同步
#define ATOMIC_MODEL12_V1	1201	// 以 STM_Rb_H 为基准的原子钟时统终端
#define OCXO_MODEL			100		// 恒温晶振模块
#define OCXO_MODEL_V1		10001	// 恒温晶振模块 V1，主控的程序，自带FPGA
#define TCXO_MODEL			101		// 温补晶振模块
#define TCXO_MODEL_V1		10101	// 温补晶振模块 V1，主控的程序
#endif

typedef struct COMM_COMMAND
{
	int nAddr, nCmd;//设备地址 命令号
	unsigned long wParam, lParam;//两个参数
	bool bCmdHold;//命令是否需要保持
}COMM_COMMAND;//命令结构体标准格式

#define DATA_PACK_HEADER_SIZE	48
typedef struct DATA_PACK_STRUCT		// 文件数据传输过程中使用的数据帧，数据包的结构，上位机下载 SD 卡里的采集数据文件。


{
	u32 uHeader;//包头
	u32 uPackLen;					// 数据包的大小，或者说长度
	int nAddr, nCmd;//地址 命令号
	unsigned long wParam, lParam;//参数
	u32 uDataBytes;					// 数据的大小
	u32 uCrcType;					// 校验类型
	u32 uCrc;						// 校验值
	int nBlock;						// 当前数据缓冲区分块数
	int iBlock;						// 当前数据包对应的缓冲区数据块号码
	u32 uCrcSum;					// 命令部分的校验和
	char chCurLoadBuf[MAX_LOAD_BUF];//定义一个固定大小的字节数组，当作临时存放数据的缓冲区
}DATA_PACK_STRUCT;				

typedef struct RT_DATA_PACK_HEADER_STRUCT//实时数据的包头，设备连续采集时，每帧数据前面都带这个头，告诉上位机"这是哪个设备、什么类型的数据"
{
	u32 uHeader;//包头
	u32 uPackLen;					// 数据包的大小
	int nAddr, nCmd;
	unsigned long wParam, lParam;
	int nDeviceType;				// 设备类型
	int nDevictSubtype;				// 设备子类型（V1/V2版本？
	int nID;						// 设备ID
	u32 uParam1, uParam2;
	u32 uParam3, uParam4;
	u32 uParam5, uParam6;
}RT_DATA_PACK_HEADER_STRUCT;		// 实时传输的数据头

typedef struct SLEEP_CTRL_STRUCT//休眠控制结构，管理设备的低功耗休眠
{
	bool bSleep;//是否处于休眠状态
	bool bInitDone;					// 是否完成单次计时
	u32  uSleepNum;					// 休眠时间设定值
	u32  uSleepCnt;					// 休眠时间计数器
}SLEEP_CTRL_STRUCT;					// 休眠控制参数结构体

typedef struct SIG_PARAM//信号参数结构，描述一个发射波形的所有参数
{
	u8  nSn;						// 信号序号，第几个预设波形？
	u8	nType;						// 信号类型
	u32 uWidth;						// 脉冲宽度（单位：us）
	u32 uPeriod;					// 脉冲间隔（单位：us）
	u32 uFreqL;						// 起始频率（单位：Hz）
	u32 uFreqH;						// 终止频率（单位：Hz
	u32 uLoops;						// 当前信号发射循环次数， >= 1 为循环次数， = 0 表示无限循环
}SIG_PARAM;

typedef struct RELAY_CTRL_STRUCT//继电器控制结构，控制电源继电器何时闭合/断开
{
	bool bEn;						// 电源继电器上电时的使能状态
	bool bInitOn;					// 电源继电器上电时的状态，= true 闭合
	u8   uPwMode;					// 电源继电器上电模式，默认延迟上电
	u32  uPwTimeYMD;				// 电源继电器上电延迟时间，年月日
	u32  uPwTimeHMS;				// 电源继电器上电延迟时间，时分秒
	u32  uHoldTime;					// 电源继电器上电保持时间，单位：秒
}RELAY_CTRL_STRUCT;					// 继电器控制参数结构体

// 系统配置参数定义
typedef struct SYSTEM_PARAM
{
	u8 bInit;						// 参数初始化配置标志
	u8 nModify;						// 参数修改标志
	float fProgramVersion;			// 设备版本号
	u32 uProgramTime;				// 设备程序烧录时间
	u32 uMode;						// 设备工作模式，扩展工作模式为32位数
	u16 uCpuStep;					// CPU 空闲时间计数步进值
	u8  nAddr;						// 设备地址
	int nID;						// 设备ID号，用于记录设备的出厂参数
	int nSn;						// 设备附属传感器序号
	u8  IP1, IP2, IP3, IP4;			// 设备IP地址
	u8  nPwMode, nPwOffMode;		// 设备上电模式
	int nPwYMD, nPwHMS;				// 开始采集时间参数
	int nPwOffYMD, nPwOffHMS;		// 停止采集时间参数
	
	u16 uSampleTime;				// 单次采集时间长度，单位分钟，1~65535分钟，uSampleTime=0表示连续采集
	u16 uSampleGap;					// 每次采集间隔时间，单位分钟，1~65535分钟，nSampleGap =0无效
	
	u8  nExCommBaud;				// 扩展串口波特率
	u8  nDataType;					// 数据格式（枚举量）
	u8	uCardType;					// 定义内存卡的容量（枚举量）
	u32	uSaveParam;					// 单个文件保存的数据量参数（扇区数，或者秒数）
	u8  nSampleRate;				// 采样率
	u8  nSampleGain;				// 增益配置
	u16 nSampleFilter;				// 滤波器参数
	u8  nSampleUnit;				// 数值单位
	u8  nSampleCh;					// 采样通道数
	u32 uSampleChSel;				// 当前所选择的采集通道参数
	
	float fSensitivity;				// 传感器灵敏度
	float fLiVoltCoef1;				// 锂电池电压测量修正系数，采用1阶拟合的方式进行修正
	float fLiVoltCoef2;				// fLiVolt = fLiVoltCoef1*fLiVoltTest + fLiVoltCoef2;
	float fLiVoltGateOff;			// 为了避免锂电池组过放，设置过放关闭电压阈值
	
#if DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceIListenN
	u8 bDeviceProtect;				// 设备防拆机保护状态
	u8 bDeviceEncrypt;				// 设备数据加密状态
	short nSampleGainQty;			// 增益档位数
	short nSampleGainStep;			// 增益档位步进值
	short nSampleGainOrigin;		// 增益定标初始值
	u16 nFftOrder;					// FFT 阶数
	u16 nTimeRefModelType;			// 时钟基准模块型号
	u16 bTimeRefEnable;				// 时钟模块使能标志
	u16 nNetPwTime;					// 供电后网口上电保持时间，单位：秒
	u16 nSysPwTime;					// 供电后延迟上电等待时间，单位：秒
	float fAccelerometer;			// 加速度计灵敏度
	float fPSensorScale;			// 压力传感器量程
	float fPSensorZero;				// 压力传感器零点值
	float fTSensorCoef1;			// 温度传感器修正参数
	float fTSensorCoef2;
	u8  nEncryptMode;				// 加密模式
	u8  nEncryptKey[AES_KEYLEN];	// 加密秘钥
	u16 nArrayUnit;					// 阵列采集的采集单元个数
	#if POWER_RELAY_LIMIT
	RELAY_CTRL_STRUCT stRelayCtrl[POWER_RELAY_LIMIT];	// 电源继电器控制参数，根据设备安装的继电器附件数量限制值确定
	#endif
#elif DEVICE_TYPE == tDeviceIPowerAmp
	u32 uDeviceWorkBandH;			// 设备工作带宽限制
	u32 uDeviceWorkBandL;			// 设备工作带宽限制
	u16 nTimeRefModelType;			// 时钟模块型号
	u16 bTimeRefEnable;				// 时钟模块使能标志
	u16 bTimeRefPpsCatchEn;			// 时钟模块自动同步 
	float m_fPulseWidthLimitMax;	// 发射脉宽限制参数，单位：S
	float m_fPulsePeriodLimitMax;	// 发射周期限制参数，单位：S
	float m_fPulseDutyRatio;		// 发射信号占空比
	float m_fDepthGate;				// 深度压力控制门限
	float m_fDepthErrCoef;			// 深度压力控制门限零点修正值
	u16 nNetPwTime;					// 网口上电保持时间
	u16 uGroups;					// 发射信号的配置数量
	SIG_PARAM Emit_Param[MAX_EMIT_PARAM_GROUP];		// 序号，信号类型，脉冲宽度（单位：us），脉冲发射间隔（单位：us），起始频率（单位：Hz），终止频率（单位：Hz）
#elif DEVICE_TYPE == tDeviceShortBaseLine || DEVICE_TYPE == tDeviceIBeacon
	short nSampleGainStep;			// 增益档位步进值
	short nSampleGainOrigin;		// 增益定标初始值
	u8  uDetectMode;				// 检测模式
	u16 nDeadGate;					// 检测死区时间
	u16 nCatchGate;					// 捕获鉴宽时间
	float m_fDepthGate;				// 深度压力控制门限
	float m_fDepthErrCoef;			// 深度压力控制门限零点修正值
	float m_fDetectGate;			// 检测信号门限
	float m_fNoiseLevel;			// 背景噪声增益调节门限参数
	SIG_PARAM Emit_Param[MAX_EMIT_PARAM_GROUP];		// 序号，信号类型，脉冲宽度（单位：us），脉冲发射间隔（单位：us），起始频率（单位：Hz），终止频率（单位：Hz）
	u32 Detect_Param[3][MAX_DETECT_PARAM_GROUP];	// 检测信号参数，3行数据，分别为信号类型，起始频率（单位：Hz），终止频率（单位：Hz）
	#if MAX_RELAY_NUM > 0
	u8  uRelaySwTime[MAX_RELAY_NUM];				// 继电器保持时间
	#endif
#elif DEVICE_TYPE == tDeviceIDeckUnit
	short nSampleGainStep;			// 增益档位步进值
	short nSampleGainOrigin;		// 增益定标初始值
	u8  uDetectMode;				// 检测模式
	u16 nDeadGate;					// 检测死区时间
	u16 nCatchGate;					// 捕获鉴宽时间
	u16	uCrcTimes;					// 校验次数
	float m_fDepthGate;				// 深度压力控制门限
	float m_fDepthErrCoef;			// 深度压力控制门限零点修正值
	float m_fDetectGate;			// 检测信号门限
	float m_fNoiseLevel;			// 背景噪声增益调节门限参数
	SIG_PARAM Emit_Param[MAX_EMIT_PARAM_GROUP];		// 5行数据，分别为信号类型，脉冲宽度（单位：us），脉冲发射间隔（单位：us），起始频率（单位：Hz），终止频率（单位：Hz）
	u32 Detect_Param[3][MAX_DETECT_PARAM_GROUP];	// 检测信号参数，3行数据，分别为信号类型，起始频率（单位：Hz），终止频率（单位：Hz）
#elif DEVICE_TYPE == tDeviceIUTM || DEVICE_TYPE == tDeviceIUDM
	u32 uAcqLogTime;		// 采集时间，单位：mS
	u32 uGroups;			// 发射信号的配置数量
	u32 Emit_Param[5][1];	// 5行数据，分别为信号类型，脉冲宽度（单位：us），脉冲发射间隔（单位：us），起始频率（单位：Hz），终止频率（单位：Hz）
#elif DEVICE_TYPE == tDeviceIPowerProbe
	float m_fCurrCoef[5];	// 电流误差修正系数数组
	float m_fVoltCoef[5];	// 电压误差修正系数数组
#elif DEVICE_TYPE == tDeviceITimeRef || DEVICE_TYPE == tDeviceITimeRefModel
	u32	m_uFreqTuneL;		// 时钟模块频率的电压调节值或者频率调节值
	int m_nFreqTuneH;
	u32 m_uPpsWidth;		// PPS秒脉冲脉宽参数
	u16 m_uDisciplineSpeed;	// 驯服时间窗速率，单位：n倍速
	u32 m_uPpsTrigSec[2];	// 触发秒参数
	u32	m_uPpsTrigMin[2];	// 触发分钟参数
	u8  m_nSlaveMode;		// 时统单元外挂设备模式参数
	u8  m_nSlaveRate;		// 时统单元外挂设备采样率参数
	u8  m_nSlaveGain;		// 时统单元外挂设备增益参数
#elif DEVICE_TYPE == tDeviceISensor
	float m_fTCoef[5];		// 温度误差修正系数数组
#elif DEVICE_TYPE == tDeviceISigCond
	u8 m_nGain[32];			// 增益
	int m_nHPass;			// 滤波器下限截止频率
	int m_nLPass;			// 滤波器上限截止频率
#elif DEVICE_TYPE == tDeviceIPowerCtrl
	u32 nSampleFileLength;	// 采样文件时长，s
	float m_fDepthGate;		// 深度压力控制门限
	float m_fDepthErrCoef;	// 深度压力控制门限零点修正值
#elif DEVICE_TYPE == tDeviceILogger
	u8  m_uFuncEn1;			// 设备功能使能寄存器1
	u8  m_uFuncEn2;			// 设备功能使能寄存器2
	u8  m_uFuncEn3;			// 设备功能使能寄存器3
	u8  m_uFuncEn4;			// 设备功能使能寄存器4
	u32 m_uSampleRate2;		// 采样率，按照mS为单位
	u16	m_uPwRelayTime[8];	// 继电器延迟开关时间参数，单位：分钟 0~65535
	u32 nSensorType;		// 传感器类型
	u8  nSensorUnit;		// 传感器数量
	u32	nSensorBaud;		// 传感器串口波特率
#endif
}SYSTEM_PARAM;

enum TypeDef_DEVICE_COMMAND
{
	// 0~99保留为全局命令
	Device_Cmd_Link = 0,//建立连接
	Device_Cmd_Set_Addr,				// 1  Set device address设置设备地址
	Device_Cmd_Set_Id,					// 2  Set device ID 设置ID
	Device_Cmd_Set_Sn,					// 3  Set device sensor Sn
	Device_Cmd_Set_Rtc,					// 4  Set device RTC time设置时钟
	Device_Cmd_Acq_Start,				// 5  start开始采集
	Device_Cmd_Acq_Stop,				// 6  stop停止采集
	Device_Cmd_Set_PowerEn,				// 7  enable device power on
	Device_Cmd_Debug,					// 8  debug output调试模式
	Device_Cmd_SaveParam,				// 9  save configure parameter
	Device_Cmd_SendData,				// 10 send special format measure data to user
	Device_Cmd_WatchDog,				// 11 send watchdog warning
	Device_Cmd_Response,				// 12 message reveive response
	Device_Cmd_SendOver,				// 13 send special format measure data over
	Device_Cmd_Delink,					// 14 device delink command
	Device_Cmd_RecvData,				// 15 device recv special format data
	Device_Cmd_Set_PowerDelay,			// 16 设备上电启动延迟时间参数设置指令
	Device_Cmd_Set_TrigSignal,			// 17 发送触发信号
	Device_Cmd_Init_Param,				// 18 初始化设备参数
	Device_Cmd_Set_DeviceVersion,		// 19 设置设备版本号
	Device_Cmd_Set_DeviceProtect,		// 20 设置设备保护状态
	Device_Cmd_Set_DeviceEncrypt,		// 21 设置设备数据加密模式
	
	Device_Cmd_SD_Format = 30,			// 30 format the sd card
	Device_Cmd_SD_OpenDir,				// 31 open special dir of sd card
	Device_Cmd_SD_LoadFile,				// 32 upload special file 
	Device_Cmd_SD_DelFile,				// 33 delete special file
	Device_Cmd_SD_DelDir,				// 34 delete special folder
	Device_Cmd_SD_FName,				// 35 send dir name or filename
	Device_Cmd_SD_OpenFile,				// 36 open special file
	Device_Cmd_SD_CloseFile,			// 37 close current file
	Device_Cmd_SD_LoadData,				// 38 load data through net port
	Device_Cmd_SD_SendData,				// 39 send data through net port
	Device_Cmd_SD_SetCardType,			// 40 设置存储卡类型
	Device_Cmd_SD_ErrInfo,				// 41 从设备返回的存储卡错误信息
	Device_Cmd_SD_NewFile,				// 42 在设备内部新建一个文件
	Device_Cmd_SD_NewDir,				// 43 在设备内部新建一个文件夹
	Device_Cmd_SD_WriteFile,			// 44 请求写入数据到新建文件
	
	Device_Cmd_Get_Info = 100,			// 100 Get device Information
	Device_Cmd_Set_PowerTime,			// 101 设置上电时间，包括上电模式
	Device_Cmd_Set_SampleRate,			// 102 
	Device_Cmd_Set_SampleGain,			// 103
	Device_Cmd_Set_SampleFilter,		// 104
	Device_Cmd_Set_SampleUnit,			// 105
	Device_Cmd_Set_Mode,				// 106 Set device work mode
	Device_Cmd_Set_IP,					// 107 Set device IP address
	Device_Cmd_Set_Param,				// 108 Set param
	Device_Cmd_Get_Param,				// 109 Get param
	Device_Cmd_Set_AcqLogTime,			// 110 设置采集文件时长参数
	Device_Cmd_Set_PcPowerEn,			// 111 Set Pc power enable
	Device_Cmd_Set_PcSoftEn,			// 112 Set Pc Soft enable
	Device_Cmd_Set_SampleCh,			// 113 Set sample channels parameter
	Device_Cmd_Set_AcqStartTime,		// 114 设置同步采集起始时间
	Device_Cmd_Set_CommBaud,			// 115 设置串口波特率
	Device_Cmd_Set_Calibrate,			// 116 设置校准操作
	Device_Cmd_Set_Relay,				// 117 设置选定继电器开关状态
	Device_Cmd_Set_EmitEn,				// 118 设置脉冲发射使能
	Device_Cmd_Get_TestData,			// 119 获得测试结果
	Device_Cmd_Set_Sensor,				// 120 设置传感器参数
	Device_Cmd_Set_ErrCoef,				// 121 设置误差修正参数
	Device_Cmd_Set_TrigGate,			// 122 设置特定设备的工作触发门限
	Device_Cmd_Set_CommTalk,			// 123 设置声通接收模式
	Device_Cmd_Get_FaultInfo,			// 124 错误信息
	Device_Cmd_Set_LimitParam,			// 125 设置限制参数
	Device_Cmd_Get_MasterStatus,		// 126 获取主机状态
	Device_Cmd_Get_SlaveStatus,			// 127 获取从机状态
	Device_Cmd_Set_ArrayUnit,			// 128 设置阵元参数
	Device_Cmd_Set_ArrayUnit_Addr,		// 129 设置阵元地址参数
	Device_Cmd_Set_WorkBand,			// 130 设置设备工作带宽
	Device_Cmd_Get_SensorData,			// 131 获取传感器参数
	Device_Cmd_Set_PwOffTime,			// 132 设置自动下电时间，只有绝对时间模式
	Device_Cmd_Set_PowerReset,			// 133 设置接收该指令的控制器复位系统电源
	Device_Cmd_Set_PowerSleep,			// 134 设置接收该指令的控制器关闭系统电源进入休眠状态
	Device_Cmd_Set_Compass,				// 135 设置电子罗盘参数
	Device_Cmd_Set_TdSensor,			// 136 设置TD传感器参数

	Device_Cmd_Atomic_Sync_Set	= 200,	// 200 时钟模块配置参数设置 
	Device_Cmd_Atomic_Sync_PPS,			// 201 时钟模块秒脉冲和输入秒脉冲同步
	Device_Cmd_Atomic_Set_Cnt,			// 202 Set the inner counter of atomic clock
	Device_Cmd_Atomic_Get_Cnt,			// 203 Get the inner counter value of atomic clock
	Device_Cmd_Atomic_Freq_Adj,			// 204 调节时钟基准频率参数
	Device_Cmd_Atomic_Freq_Latch,		// 205 保存当前调节频率参数
	Device_Cmd_Atomic_Set_PPS,			// 206 设置时钟模块PPS脉冲信号输出特性参数
	Device_Cmd_Atomic_Set_PPS_Trig,		// 207 设置PPS触发时间参数
	Device_Cmd_Atomic_Discipline,		// 208 驯服时钟基准
	Device_Cmd_Atomic_DisciplineSet,	// 209 驯服时钟基准时的参数设置
	Device_Cmd_Atomic_DisciplineInfo,	// 210 驯服时钟基准过程回复消息
	Device_Cmd_Atomic_DisciplineRes,	// 211 驯服时钟基准时的误差测量参数
	Device_Cmd_Atomic_Link,				// 212 时钟基准模块连接指令
	Device_Cmd_Atomic_PwEn,				// 213 时钟基准模块供电使能指令

	Device_Cmd_SyncCmd_RTC 	= 250,		// 250 给同步设备或者时钟模块发送RTC时间配置指令
	Device_Cmd_SyncCmd_Set,				// 251 给同步设备发送时钟基准配置指令
	Device_Cmd_SyncCmd_Msg,				// 252 给同步节点设备发送时钟控制消息指令（用于有线或者无线同步节点）
	Device_Cmd_SyncCmd_Time,			// 253 给同步设备发送时间码
	Device_Cmd_SyncCmd_Start,			// 254 给同步设备发送采集(或者发射等)开始时间指令
	Device_Cmd_SyncCmd_Reset,			// 255 给同步设备发送系统复位指令
	Device_Cmd_SyncCmd_Stop,			// 256 给同步设备发送采集(或者发射等)停止指令
	Device_Cmd_SyncCmd_AutoCtrl,		// 257 同步终端与计算机之间的交互信息

	Device_Cmd_Set_Timing_Delay = 300,	// Set timing device(master mode) sample clock delay register

	Device_Cmd_Get_CompassData  = 350,	// Get compass information
	Device_Cmd_Set_CompassPower,		// Set compass power
	
	// LCD operate command 400~499
	Device_Cmd_LCD_Start 	    = 400,	// 400 LCD start
	Device_Cmd_LCD_ChangePage,			// 401 LCD change page，LCD换页后回执的消息
	Device_Cmd_LCD_SetPage 		= 450,	// 450 设置LCD页面指令
	
};

enum TypeDef_SD_Error//错误码
{
#if !USE_FATFS || (USE_FATFS && _FATFS >= 68300)
	// @brief  SDIO specific error defines  
	SD_CMD_CRC_FAIL                    = (1), /*!< Command response received (but CRC check failed) */
	SD_DATA_CRC_FAIL                   = (2), /*!< Data bock sent/received (CRC check Failed) */
	SD_CMD_RSP_TIMEOUT                 = (3), /*!< Command response timeout命令响应超时 */
	SD_DATA_TIMEOUT                    = (4), /*!< Data time out数据传输超时 */
	SD_TX_UNDERRUN                     = (5), /*!< Transmit FIFO under-run */
	SD_RX_OVERRUN                      = (6), /*!< Receive FIFO over-run */
	SD_START_BIT_ERR                   = (7), /*!< Start bit not detected on all data signals in widE bus mode */
	SD_CMD_OUT_OF_RANGE                = (8), /*!< CMD's argument was out of range.*/
	SD_ADDR_MISALIGNED                 = (9), /*!< Misaligned address */
	SD_BLOCK_LEN_ERR                   = (10), /*!< Transferred block length is not allowed for the card or the number of transferred bytes does not match the block length */
	SD_ERASE_SEQ_ERR                   = (11), /*!< An error in the sequence of erase command occurs.*/
	SD_BAD_ERASE_PARAM                 = (12), /*!< An Invalid selection for erase groups */
	SD_WRITE_PROT_VIOLATION            = (13), /*!< Attempt to program a write protect block 写保护违规*/
	SD_LOCK_UNLOCK_FAILED              = (14), /*!< Sequence or password error has been detected in unlock command or if there was an attempt to access a locked card */
	SD_COM_CRC_FAILED                  = (15), /*!< CRC check of the previous command failed */
	SD_ILLEGAL_CMD                     = (16), /*!< Command is not legal for the card state非法命令 */
	SD_CARD_ECC_FAILED                 = (17), /*!< Card internal ECC was applied but failed to correct the data */
	SD_CC_ERROR                        = (18), /*!< Internal card controller error */
	SD_GENERAL_UNKNOWN_ERROR           = (19), /*!< General or Unknown error */
	SD_STREAM_READ_UNDERRUN            = (20), /*!< The card could not sustain data transfer in stream read operation. */
	SD_STREAM_WRITE_OVERRUN            = (21), /*!< The card could not sustain data programming in stream mode */
	SD_CID_CSD_OVERWRITE               = (22), /*!< CID/CSD overwrite error */
	SD_WP_ERASE_SKIP                   = (23), /*!< only partial address space was erased */
	SD_CARD_ECC_DISABLED               = (24), /*!< Command has been executed without using internal ECC */
	SD_ERASE_RESET                     = (25), /*!< Erase sequence was cleared before executing because an out of erase sequence command was received */
	SD_AKE_SEQ_ERROR                   = (26), /*!< Error in sequence of authentication. */
	SD_INVALID_VOLTRANGE               = (27),
	SD_ADDR_OUT_OF_RANGE               = (28),
	SD_SWITCH_ERROR                    = (29),
	SD_SDIO_DISABLED                   = (30),
	SD_SDIO_FUNCTION_BUSY              = (31),
	SD_SDIO_FUNCTION_FAILED            = (32),
	SD_SDIO_UNKNOWN_FUNCTION           = (33),

	// @brief  Standard error defines   这些枚举项没有写值，会自动递增
	SD_INTERNAL_ERROR, 
	SD_NOT_CONFIGURED,
	SD_REQUEST_PENDING, 
	SD_REQUEST_NOT_APPLICABLE, 
	SD_INVALID_PARAMETER,  
	SD_UNSUPPORTED_FEATURE,  
	SD_UNSUPPORTED_HW,  
	SD_ERROR,  
	SD_OK = 0,
#endif	// !USE_FATFS
	// 自定义错误信息
	SD_OPEN_FAULT	= 1000,
};

enum TypeDef_SD_CARD_CONTENT //SD卡容量枚举，128M是1，往下自动递增
{
	SD_128M = 1,
	SD_256M,SD_512M,
	SD_1G,SD_2G,SD_4G,SD_8G,
	SD_16G,SD_32G,SD_64G,SD_128G,SD_256G,SD_512G,
	SD_1T,SD_1T5,SD_2T,SD_3T,SD_4T,
};

enum TypeDef_SD_FORMAT_CMD//SD卡格式化命令枚举
{
	SDOP_NC 	= 0,  //无操作，空闲
	SDOP_FMT 	= 1, //格式化
	SDOP_DEL,    //删除数据
};

enum TypeDef_FILE_UPLOAD_STATUS//文件上传状态枚举
{
	Upload_OK = 0,//上传成功
	Upload_End,//上传结束
	Upload_Reload,			// 重新上传校验错误的数据块	
	Upload_Newload,			// 加载新的缓冲区数据并上传
	Upload_Err_File,		// 文件信息错误
	Upload_Err_LoadData,	// 从SD卡中加载数据出错
	Upload_Err_CreateBuf,	// 建立缓冲区错误
	Upload_Err_OpenFile,	// 打开文件错误
	Upload_Err_CloseFile,	// 关闭文件错误
	Upload_Err_iBlock,		// 接收第 iBlock 块参数错误
	Upload_Fast_OK,			// 接收每个Block数据的快速模式
};

enum TypeDef_FOLDER_OPEN_STATUS//文件夹遍历状态枚举
{	
	OpenDir_OK = 0,//成功打开文件夹或成功读取一个文件
	OpenDir_KeepOn,//继续遍历
	OpenDir_Over,//遍历结束
};

enum TypeDef_POWER_MODE_TYPE//电源/采集模式枚举，这个枚举决定了设备什么时候开始工作、怎么工作
{
	// 采集模式设定
	PwMode_RealTime = 0,			// 默认系统上电采集
	PwMode_Relative = 1,			// 设定从上电开始之后的相对时间开始采集，
	PwMode_Absolute = 2,			// 设定从上电开始之后的绝对时间开始采集，真实时间
	PwMode_RealTimeInterval = 3,	// 设定上电启动间隔采集模式，立即开始，但每隔一段时间采一次
	PwMode_RelativeInterval = 4,	// 设定相对时间启动间隔采集模式，上电后延迟一段时间，然后间隔采集
	PwMode_AbsoluteInterval = 5,	// 设定绝对时间启动间隔采集模式，等到指定时间开始，然后间隔采集
	PwMode_RealTimeSchedule = 6,	// 设定时间表模式，按照小时,分钟为单位，进行间歇采集，比如每天9:00-17:00工作
	PwMode_RelativeSchedule = 7,//相对时间的时间表模式
	PwMode_AbsoluteSchedule = 8,
};

enum TypeDef_DATA_CRC_TYPE//数据校验类型枚举
{
	CRC_NONE = 0,//无检验
	CRC_SUM16,//16位累加和，简单检验用
	CRC_CRC8,//8位CRC，用于小数据包
	CRC_CRC16,//通用数据传输
	CRC_CRC32,//文件传输
	CRC_CCITT16,//CCITT标准CRC16，用于通信协议
	CRC_DNP16,//DNP协议CRC16，工业协议
	CRC_KERMIT16,//Kermit协议CRC16，文件传输
};

enum TypeDef_FILE_DATA_TYPE//文件数据类型枚举
{
	DATA_FLOAT = 0,//单精度浮点数
	DATA_U32,  DATA_U24, DATA_U16, DATA_U8,	// 无符号整数，32位。。		
	DATA_32,   DATA_24,  DATA_16,  DATA_8,	// 有符号
	DATA_ASCII = 9,			// 字符型
	DATA_INT   = 10,		// 整数型
	DATA_DOUBLE = 11,		// 双精度数
	DATA_HEX   = 12,		// 16进制型
	// 2025.08.03 扩展出LittleEnd模式数据存储类型，
	// 以兼容个别类型设备数据存储的特殊性
	DATA_U32L, DATA_U24L, DATA_U16L,	// 无符号，LittleEnd
	DATA_32L,  DATA_24L,  DATA_16L,		// 有符号，LittleEnd
	
};

enum TypeDef_DATA_VALUE_UNIT_TYPE//数据单位类型枚举，水听器采集到的原始数据是 ADC 的二进制数值（比如 0~4095），不是真正的物理量，得转换一下成电压
{
	Unit_Bin = 0,		// 2进制，需要上位机根据转换系数进行转换
	Unit_Volt,			// 采样数值单位，V，电压值
};

enum TypeDef_Signal_Type//信号波形类型枚举
{
	SIGNAL_CW  = 1,
	SIGNAL_LFM = 2,
	SIGNAL_SIN = 3,		// 正弦波
	SIGNAL_TRIANGLE,	// 三角波
	SIGNAL_SAWTOOTH,	// 锯齿波
	SIGNAL_FILE  = 6,//从文件读取的任意波形
	SIGNAL_NOISE = 7,//噪声
	SIGNAL_SLFM  = 8,	// 对称阶梯调频信号
};

enum TypeDef_CMD_SENSOR_SETTYPE//传感器设置类型枚举，因为传感器出厂时都有个体差异，需要校准，实际压力 = 测量值 × 系数1 + 系数2
{
	// Device_Cmd_Set_Sensor 指令对应的下一级设置类型
	SENSOR_SetType 	= 0,		// 设置传感器类型
	SENSOR_Set_Relay,			// 设置继电器控制器参数
	SENSOR_Set_PSensor_ErrCoef,	// 设置压力传感器修正参数
	SENSOR_Set_TSensor_ErrCoef,	// 设置温度传感器修正参数
	SENSOR_Set_VSensor_ErrCoef,	// 设置板载电池电压测量修正系数
};		

enum TypeDef_ENCRYPT_MODE//数据加密模式枚举
{
	ENCRYPT_NULL,//不加密
	ENCRYPT_AES_CTR,			// AES-CTR	128位加密
};

enum TypeDef_SET_COMPASS_MODE	// 设置电子罗盘参数，电子罗盘（磁力计）用于测量方向（航向角）
{
	COMPASS_Set_Addr,			// 设置电子罗盘的地址
	COMPASS_Set_CommBaud,		// 设置电子罗盘的波特率
};

enum TypeDef_SET_RELAY_MODE		// 设置继电器参数
{
	RELAY_InitParam  = 1,		// 继电器初始化参数，第一次上电时配置继电器
	RELAY_Set_Param  = 2,		// 设置继电器参数
	RELAY_GlobalRun  = 3,		// 启动继电器
	RELAY_GlobalStop = 4,		// 停止继电器
	RELAY_SigleRun   = 5,		// 启动单继电器
	RELAY_SigleStop  = 6,		// 停止单继电器
	RELAY_SigleSwitchOn   = 7,	// 单继电器闭合
	RELAY_SigleSwitchOff  = 8,	// 单继电器开路
};

extern SYSTEM_PARAM g_stSys;//全局系统参数
extern COMM_OBJECT* m_pCommDebug;	// debug串口
extern COMM_OBJECT*	m_pCommObject;	// 必须初始化方可使用上位机串口通讯
extern RT_DATA_PACK_HEADER_STRUCT m_hRtDataPackHead;

extern bool m_bPcConnect;			// 标志上位机物理接口连接状态，参数掉电不保存，网线插没插/串口连没连）
extern bool m_bPcStatus;			// 标志上位机软件接口联机状态，参数掉电不保存，上位机软件发送了"连接"命令并完成握手
extern bool m_bUpdateInit;			// 标志更新初始化状态
extern bool m_bNetSendFlag;			// 网口发送状态，由于缓冲区不够大，当此状态有效时，表示发送缓冲区里的数据未发送完毕
extern bool m_bFileUpload;			// 上传文件状态
extern bool m_bFileDownload;		// 下载文件状态
extern bool m_bSdFormat;			// 接收到格式化指令状态
extern bool m_bSdDelDir;			// 接收到删除文件夹指令状态
extern bool m_bSdDeleting;			// 正在执行删除状态
extern bool m_bTimer4Enable;		// 电源指示灯定时器启动状态

extern int  m_uTimer4IntCnt;		// 电源指示灯定时器计数值
extern char*  m_pNetBufTemp;		// 网口发送缓冲区的指针
extern int  m_nNetBufSize;			// 网口发送缓冲区的剩余数据发送量

extern char  m_chFileHeader[MAX_HEADER_BUF_SIZE];	// 文件头缓存
extern char* m_pFileHeaderDBuf;						// 文件头数据区地址，必须在初始化文件头时进行同步赋值

extern u32 m_uAtomicCnt;			// 同步采集时，用于代替时钟基准计时器的计数器
extern u32 m_uPwTimeCnt;			// 上电定时器，只在相对时间定时上电使用，用于记录历时时间（秒）
extern u32 m_uPwTimeSetYMD;			// 上电时间设定值：年月日 绝对时间 20171010	相对时间 < 3天		
extern u32 m_uPwTimeSetHMS;			// 上电时间设定值：时分秒 绝对时间 195959	相对时间 总秒数

float CharToFloat(char* buf, int nBuf);	//从字节缓冲区→还原成数据（收数据时用）
float U16ToFloat(char* buf, int nBuf);	
float U24ToFloat(char* buf, int nBuf);	
unsigned short CharToUShort(char* buf, int nBuf);	// BigEnd 模式，大端，高位字节在前
int IntToChar(char* buf, int nBuf, int nData);		// BigEnd 模式，从数据→拆成字节缓冲区（发数据时用）
int U16ToChar(char* buf, int nBuf, u16 uData);		// BigEnd 模式
int U32ToChar(char* buf, int nBuf, u32 uData);		// BigEnd 模式
int FloatToChar(char* buf, int nBuf, float fData);	// BigEnd 模式
int ShortToChar(char* buf, int nBuf, short nData);	// BigEnd 模式
int U16ToChar2(char* buf, int nBuf, u16 uData);		// LittleEnd 模式，低位字节在前，无非是提供了两种方式to char
int U32ToChar2(char* buf, int nBuf, u32 uData);		// LittleEnd 模式
/************************************************************************/
/*以下函数为公共函数*/
/************************************************************************/

void SysParam_Load(void);					// 读取Flash中存储的设备配置参数，Flash掉电不丢数据，类似于硬盘
void SysParam_Save(void);					// 将设备配置参数写入Flash中存储
bool GetConfigParam(char* pBuf, int nLen);	// 从配置文件读取配置参数数据，pbuf：存放读取数据的缓冲区指针。nlen是缓冲区的大小
bool SetConfigParam(void);					// 将配置参数数据写入配置文件
int  GetSizeOfData(int nDataType);			// 根据数据类型，获得该类型对应的字节数
//上位机连接管理
void PCLINK_Init(void);						// 初始化Debug串口，上位机接口
void PCLINK_DeInit(void);					// 退出连接时，清理
void PCLINK_DebugInfo(void);				// 程序初始化时的消息打印输出
//定时器回调，OnTimer 说明这是定时器中断触发的回调函数，不是主动调用的，而是定时器到点了硬件自动调用。
void Device_OnTimer2(void);					// 文件读取控制定时器，Timer2 每隔X毫秒触发  →  自动调用 Device_OnTimer2()  →  处理文件读取进度
void Device_OnTimer4(void);					// 充电电压LED控制定时器，Timer4 每隔X毫秒触发  →  自动调用 Device_OnTimer4()  →  控制LED闪烁

COMM_OBJECT* PCLINK_SetCommObj(COMM_OBJECT* pCommObj); //设置，初始化时，造好串口对象，通过Set存进去
COMM_OBJECT* PCLINK_GetCommObj(void); //获取，其他地方需要用串口时，通过Get取出来

void PCLINK_SendMsg(COMM_OBJECT* pComm);							// 发送消息给上位机		
void PCLINK_Send_PcPwEn(bool bEn);									// 发送指令上电或者下电
void PCLINK_Send_PcSoftEn(bool bEn);								// 通过上位机串口发送指令软件启动或者关闭
void PCLINK_Send_DebugSoftEn(bool bEn);								// 通过 Debug串口发送指令软件启动或者关闭
void PCLINK_Send_WatchDog(void);									// 发送看门狗警告指令，等待回应复位看门狗
void PCLINK_Send_SleepCmd(bool bSleep, u32 uSec);					// 给设备发送休眠指令
void PCLINK_Send_GetValue(int nAddr, int wParam, int lParam);		// 给设备发送获取数据指令
//send是主动给上位机发指令
void PCLINK_Echo(COMM_COMMAND stCmd);								// 响应上位机消息，无特殊处理，直接回应4字段指令
void PCLINK_EchoDebug(COMM_COMMAND stCmd);							// 响应 Debug消息，无特殊处理，直接回应4字段指令
void PCLINK_EchoASC(int nCmd, u32 wParam, u32 lParam);				// 响应上位机消息，无特殊处理，回应ASCII指令
void PCLINK_EchoHex(int nCmd, u32 wParam, u32 lParam);				// 响应上位机消息，无特殊处理，回应2进制指令
void PCLINK_Echo_Link(COMM_COMMAND stCmd);							// 回应设备连接消息							
void PCLINK_Echo_Info(bool bInit);									// 回应设备参数消息, bInit = 1 初始化后第一条指令info指令 
void PCLINK_Echo_SetIP(COMM_COMMAND stCmd);							// 回应设备IP参数
void PCLINK_Echo_SetDeviceVersion(void);							// 回应设备版本信息
void PCLINK_Echo_SetSampleGain(COMM_COMMAND stCmd);					// 回应设备增益参数（多参数）
void PCLINK_Echo_SetPowerTime(void);								// 回应设备上电模式、时间参数
void PCLINK_Echo_SetPwOffTime(void);								// 回应设备下电模式、时间参数
void PCLINK_Echo_SetSensor(COMM_COMMAND stCmd);						// 回应设备传感器参数
void PCLINK_Echo_Response(bool bEcho);								// 发送握手消息, bEcho = false 发送方， bEcho = true 接收方回执
void PCLINK_Echo_SyncRecvSet(void);									// 回应同步采集设备的时钟基准参数
void PCLINK_Echo_SetTrigGate(void);									// 回应设备工作触发门限参数
void PCLINK_Echo_SetEncrypt(void);									// 回应设备设置数据加密参数
//Echo 系列 → 收到指令后回复
int PCLINK_DecodeCmd(char* rxBuf, COMM_COMMAND* stCmd);				// 仅解析地址，指令号，参数1，参数2
int PCLINK_Decode_Link(char* cmd);									// 设置设备连接状态
int PCLINK_Decode_DeLink(COMM_COMMAND stCmd);						// 设置设备断开连接
int PCLINK_Decode_Debug(COMM_COMMAND stCmd, char* cmd);				// 调试指令
int PCLINK_Decode_DAQ_Start(COMM_COMMAND stCmd, char* cmd);			// 开始采集
int PCLINK_Decode_DAQ_Stop(COMM_COMMAND stCmd);						// 停止采集
int PCLINK_Decode_WatchDog(COMM_COMMAND stCmd);						// 处理看门狗复位指令
int PCLINK_Decode_SetVersion(char* cmd);							// 设置设备程序版本号等参数信息
int PCLINK_Decode_SetProtect(COMM_COMMAND stCmd);					// 设置设备防拆除保护状态
int PCLINK_Decode_SetEncrypt(COMM_COMMAND stCmd, char* cmd);		// 设置设备数据加密状态
int PCLINK_Decode_SetAddr(COMM_COMMAND stCmd);						// 设置设备地址
int PCLINK_Decode_SetMode(COMM_COMMAND stCmd);						// 设置工作模式
int PCLINK_Decode_SetIP(char* cmd);									// 设置设备IP参数
int PCLINK_Decode_SetId(COMM_COMMAND stCmd);						// 设置设备ID参数
int PCLINK_Decode_SetSn(COMM_COMMAND stCmd);						// 设置设备传感器序号
int PCLINK_Decode_SetRTC(char* cmd);    							// 设置系统实时时钟
int PCLINK_Decode_SetPowerEn(COMM_COMMAND stCmd);					// 设置设备供电后保持上电状态时间参数，用于某些允许指令激活的设备
int PCLINK_Decode_SetPowerDelay(COMM_COMMAND stCmd);				// 设置设备供电后延迟开启电源时间参数
int PCLINK_Decode_SetPowerTime(char* cmd);							// 设置设备上电模式、时间参数
int PCLINK_Decode_SetPwOffTime(char* cmd);							// 设置设备下电模式、时间参数
int PCLINK_Decode_SetSampleCh(COMM_COMMAND stCmd);					// 设置采样通道
int PCLINK_Decode_SetSampleRate(COMM_COMMAND stCmd);				// 设置采样率
int PCLINK_Decode_SetSampleGain(COMM_COMMAND stCmd, char* cmd);		// 设置增  益
int PCLINK_Decode_SetSampleFilter(COMM_COMMAND stCmd, char* cmd);	// 设置滤波器参数
int PCLINK_Decode_SetSampleUnit(COMM_COMMAND stCmd);				// 设置输出结果单位

int PCLINK_Decode_SetAcqLogTime(COMM_COMMAND stCmd);				// 设置采样时长参数
int PCLINK_Decode_SetAcqStartTime(COMM_COMMAND stCmd);				// 设置同步采集起始时间
int PCLINK_Decode_SetCommBaud(COMM_COMMAND stCmd);					// 设置PCLINK串口波特率
int PCLINK_Decode_SetCalibrate(COMM_COMMAND stCmd);					// 设置设备校准
int PCLINK_Decode_SetPwRelay(COMM_COMMAND stCmd, char* cmd);		// 设置继电器配置参数
int PCLINK_Decode_SetEmitEn(COMM_COMMAND stCmd);					// 设置发射使能状态
int PCLINK_Decode_SetSensor(COMM_COMMAND stCmd, char* cmd);			// 设置传感器参数
int PCLINK_Decode_SetErrCoef(char* cmd);							// 设置传感器误差修正参数
int PCLINK_Decode_SetCompassParam(char* cmd);						// 设置设备集成的电子罗盘附件参数
int PCLINK_Decode_SetTdSensorParam(char* cmd);						// 设置设备集成的TD传感器附件参数
int PCLINK_Decode_SetTrigGate(char* cmd);							// 设置设备工作触发门限参数
int PCLINK_Decode_SetParam(COMM_COMMAND stCmd, char* cmd);			// 设置设备相关参数
int PCLINK_Decode_GetParam(COMM_COMMAND stCmd, char* cmd);			// 响应设置相关参数
int PCLINK_Decode_SaveParam(COMM_COMMAND stCmd);					// 响应保存参数回执
int PCLINK_Decode_InitParam(COMM_COMMAND stCmd);					// 响应初始化设备配置
int PCLINK_Decode_GetValue(COMM_COMMAND stCmd, char* cmd);			// 响应获取数据
int PCLINK_Decode_Response(COMM_COMMAND stCmd);						// 响应消息回执
int PCLINK_Decode_SetCommTalkMode(COMM_COMMAND stCmd);				// 设置声通模式
int PCLINK_Decode_SetLimitParam(char* cmd);							// 设置限制参数
int PCLINK_Decode_GetMasterStatus(COMM_COMMAND stCmd, char* cmd);	// 响应获取主机状态参数
int PCLINK_Decode_RecvData(COMM_COMMAND stCmd, char* cmd);			// 接收数据
int PCLINK_Decode_SetArrayUnit(COMM_COMMAND stCmd);					// 设置阵元参数
int PCLINK_Decode_SetArrayUnitAddr(COMM_COMMAND stCmd, char* cmd);	// 设置阵元地址参数
int PCLINK_Decode_SetWorkBand(COMM_COMMAND stCmd);					// 设置设备工作带宽

int PCLINK_Decode_TimeRef(char* cmd, COMM_COMMAND* pStCmd);			// 响应时钟模块消息
int PCLINK_Decode_SyncRecvSet(COMM_COMMAND stCmd);					// 设置同步采集设备的时钟基准参数
int PCLINK_Decode_SyncRecvMsg(COMM_COMMAND* pStCmd);				// 响应同步采集消息指令
//Decode 系列 → 解析并执行指令，收到原始字节流，先解析出是什么指令，再执行对应的操作。
bool PCLINK_SendData(char* buf, u32 nBuf);							// 上传数据，在网口模式下，数据量需要小于缓冲区大小
bool PCLINK_SendData2(void);										// 上传数据，在网口模式下，发送缓冲区剩余的数据
bool PCLINK_RecvData(char* buf, u32 nBuf);							// 下传数据，在网口模式下接收 DATA_PACK_STRUCT 结构的数据 
/************************************************************************/
// 以下函数仅在自容式存储设备中使用，SD卡文件系统操作
int PCLINK_Decode_SD(char* cmd, COMM_COMMAND* pCmd);				// 解码SD卡命令
int PCLINK_Decode_SD_SetCardType(COMM_COMMAND stCmd);				// 根据指令参数，设置存储卡类型参数
#if USE_FATFS
void PCLINK_Echo_SD_DelDir(int nStatus, FRESULT lParam);			// 格式化响应消息，0 删除失败， 1 删除成功， 2 正在删除
void PCLINK_Echo_SD_Format(int nStatus, FRESULT lParam);			// 格式化响应消息，0 格式化失败， 1 格式化完成， 2 正在格式化

int PCLINK_Decode_SD_OpenDir(char* cmd, COMM_COMMAND* pCmd);		// 打开指定文件夹
int PCLINK_Decode_SD_DelFile(char* cmd, COMM_COMMAND* pCmd);		// 删除指定文件，指定全路径文件名
int PCLINK_Decode_SD_DelDir(char* cmd, COMM_COMMAND* pCmd);			// 删除指定文件夹，指定全路径文件夹名
int PCLINK_Decode_SD_NewFile(char* cmd, COMM_COMMAND* pCmd);		// 新建指定文件，指定全路径文件名（但是是在当前路径下建立文件）
int PCLINK_Decode_SD_NewDir(char* cmd, COMM_COMMAND* pCmd);			// 新建指定文件夹，指定全路径文件夹名（但是是在当前路径下建立文件夹）
int PCLINK_Decode_SD_Format(COMM_COMMAND* pCmd);					// 格式化磁盘
int PCLINK_Decode_SD_OpenFile(char* cmd, COMM_COMMAND* pCmd);		// 打开指定上传的文件
int PCLINK_Decode_SD_CloseFile(COMM_COMMAND* pCmd);					// 关闭当前上传的文件
int PCLINK_Decode_SD_ReadFile(COMM_COMMAND* pCmd);					// 根据指令参数，发送文件缓冲区的数据块
int PCLINK_Decode_SD_WriteFile(COMM_COMMAND* pCmd);					// 根据指令参数，对新建文件进行数据写入
void PCLINK_PreLoadFileData(void);									// 通过 systick 定时器周期调用，在下载文件过程中填充数据缓冲区
void PCLINK_ReleaseFileBuf(void);									// 释放上传文件建立的缓冲区
#endif

/************************************************************************/
void SetPwTimeCnt(int nYMD, int nHMS, u32 uTime, u32 uGap);			// 根据参数设置上电参数计数器

bool IsPwModeInterval(void);		// 判断是否是间歇上电采集模式

void PCLINK_InitCmdHeader(char* buf, int nBuf, int nCmd, u32 wParam, u32 lParam);			// 初始化2进制传输的实时采集数据包包头
void PCLINK_UpdateCmdHeader(char* buf, int nBuf, int nCmd, u32 wParam, u32 lParam);			// 仅更新2进制传输的实时采集数据包包头部分参数
void PCLINK_UpdateCmdHeader2(char* buf, int nBuf, float fParam1, float fParam2);			// 仅更新2进制传输的实时采集数据包包头部分参数

/************************************************************************/
// 以下函数在使用GPS时使用
bool Is_CMD_GPS(char* cmd, COMM_COMMAND* pCmd);			// 判断是否接收的是NEMA格式的GPS数据

/************************************************************************/
// 以下函数为独立看门狗调用使用
void IWDG_Init(int nSec);		// IWDG初始化
void IWDG_Feed(void);  			// 喂狗函数

/************************************************************************/
char* FmtFloat(char* buf, int nFraction, float fData);	// 格式化浮点数

/************************************************************************/
// ARM在线更新程序
// 1、在工程属性设置对话框中选择 user 标签页；
// 2、在下边的 After Build/Rebuild 中的1# 添加语句：fromelf.exe --bin -o XX.bin "#L"
// 	  其中 XX.bin 改为对应工程名的二进制文件，例如：iListen.bin 

#define USERID       "admin"
#define PASSWORD     "123456"
#define LOGIN_SIZE   (15+ sizeof(USERID) + sizeof(PASSWORD))
typedef  void (*pFunction)(void);

bool CheckFlashUpdateStatus(void);						// 判断并处理FLASH更新状态
void iap_load_app(u32 appxaddr);						// 更新程序函数

/************************************************************************/

#endif



