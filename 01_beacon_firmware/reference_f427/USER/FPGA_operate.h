#ifndef __FPGA_OPERATE_H
#define __FPGA_OPERATE_H

#include "main.h"

#ifndef UNIT
typedef unsigned int	UINT;
#endif

#define Bank1_SRAM1_ADDR  ((uint32_t)0x60000000)  
#define Bank1_SRAM2_ADDR  ((uint32_t)0x64000000)  
#define Bank1_SRAM3_ADDR  ((uint32_t)0x68000000)

// FPGA内部寄存器定义
// 读取数据地址
#define FPGA_ADDR_INT_REG			(uint16_t)0x0001*2
#define FPGA_ADDR_BUF_BASE          (uint16_t)0x1000*2	// 限制FPGA内部存储器的大小必须小于 2^12 = 4096
#define FPGA_ADDR_PACK_SN1			(uint16_t)0x0010*2
#define FPGA_ADDR_PACK_SN2			(uint16_t)0x0011*2
#define FPGA_ADDR_PACK_SN3			(uint16_t)0x0012*2
#define FPGA_ADDR_PACK_SN4			(uint16_t)0x0013*2
#define FPGA_ADDR_DOA_T1			(uint16_t)0x0014*2
#define FPGA_ADDR_DOA_T2			(uint16_t)0x0015*2

// 写入数据地址
#define	FPGA_ADDR_ACQ_ENABLE		(uint16_t)0x0001*2
#define	FPGA_ADDR_SAMPLE_GAIN		(uint16_t)0x0002*2
#define FPGA_ADDR_SAMPLE_RATE		(uint16_t)0x0003*2
#define FPGA_ADDR_INT_CLR           (uint16_t)0x0004*2	// 中断清除
#define FPGA_ADDR_CALC_ENABLE       (uint16_t)0x0005*2	// 校阵使能
#define FPGA_ADDR_EMIT_ENABLE       (uint16_t)0x0006*2	// 发射使能，主要用于校阵控制，不用于定位
#define FPGA_ADDR_DEAD_GATE			(uint16_t)0x0007*2	// 死区时间设定寄存器地址
#define FPGA_ADDR_CATCH_GATE		(uint16_t)0x0008*2	// 脉宽鉴宽时间设定寄存器地址
#define FPGA_ADDR_POS_RATE			(uint16_t)0x0009*2	// 定位速率设定
#define FPGA_ADDR_PPS_ENABLE		(uint16_t)0x000A*2	// PPS输出使能，也是发射脉冲输出使能	
#define FPGA_ADDR_DEPTH				(uint16_t)0x000B*2	// 深度参数，单位0.1m
#define FPGA_ADDR_SIG_TYPE			(uint16_t)0x0011*2	// 发射信号类型，1->CW,2->LFM
#define FPGA_ADDR_PULSE_WIDTH1		(uint16_t)0x0012*2	// 脉冲宽度控制字高字（单位：uS）
#define FPGA_ADDR_PULSE_WIDTH2		(uint16_t)0x0013*2	// 脉冲宽度控制字低字（单位：uS）
#define FPGA_ADDR_FREQ_WORD1        (uint16_t)0x0014*2	// 频率控制字高字
#define FPGA_ADDR_FREQ_WORD2        (uint16_t)0x0015*2	// 频率控制字低字
#define FPGA_ADDR_STEP_WORD1        (uint16_t)0x0016*2	// 频率步进控制字高字
#define FPGA_ADDR_STEP_WORD2        (uint16_t)0x0017*2	// 频率步进控制字低字
#define FPGA_ADDR_FREQ2_WORD1       (uint16_t)0x0018*2	// 频率2控制字高字
#define FPGA_ADDR_FREQ2_WORD2       (uint16_t)0x0019*2	// 频率2控制字低字
#define FPGA_ADDR_STEP2_WORD1       (uint16_t)0x001A*2	// 频率2步进控制字高字
#define FPGA_ADDR_STEP2_WORD2       (uint16_t)0x001B*2	// 频率2步进控制字低字
#define FPGA_ADDR_PERIOD_WORD1      (uint16_t)0x001C*2	// 脉冲周期控制字高字
#define FPGA_ADDR_PERIOD_WORD2      (uint16_t)0x001D*2	// 脉冲周期控制字低字

// 以下对应读写地址，不再区分读和写
#define FPGA_ADDR_DETECT_MODE		(uint16_t)0x0020*2	// 检测方法模式
#define FPGA_ADDR_DETECT_SIG_TYPE	(uint16_t)0x0021*2	// 检测信号类型
#define FPGA_ADDR_DETECT_CLR		(uint16_t)0x0022*2	// 检测状态清除，用于虚警排除
#define FPGA_ADDR_DETECT_F1H		(uint16_t)0x0023*2	// 应答信号频率控制字高字，检测应答信号的NCO频率控制字
#define FPGA_ADDR_DETECT_F1L		(uint16_t)0x0024*2	// 应答信号频率控制字低字
#define FPGA_ADDR_DETECT_F2H		(uint16_t)0x0025*2	// 应答信号频率控制字高字，检测应答信号的NCO频率控制字
#define FPGA_ADDR_DETECT_F2L		(uint16_t)0x0026*2	// 应答信号频率控制字低字
#define FPGA_ADDR_DETECT_F3H		(uint16_t)0x0027*2	// 应答信号频率控制字高字，检测应答信号的NCO频率控制字
#define FPGA_ADDR_DETECT_F3L		(uint16_t)0x0028*2	// 应答信号频率控制字低字

#define FPGA_ADDR_MODE				(uint16_t)0x0040*2	// 信标工作模式
#define FPGA_ADDR_SIG_AVERAGE		(uint16_t)0x0041*2	// 信标工作环境噪声采集信号均值
#define FPGA_ADDR_EMIT_DELAY		(uint16_t)0x0042*2	// 信标捕获信号后延迟发射时间参数
#define FPGA_ADDR_PITCH_PARAM		(uint16_t)0x0050*2	// 信标姿态信息，俯仰角
#define FPGA_ADDR_ROLL_PARAM		(uint16_t)0x0051*2	// 信标姿态信息，翻滚角
#define FPGA_ADDR_PINGPANG_GAP		(uint16_t)0x0052*2	// 高频信标间隔时间参数，（单位：mS）
#define FPGA_ADDR_SIG_READ			(uint16_t)0x0060*2	// 读取数据使能
#define FPGA_ADDR_CATCH_CODE		(uint16_t)0x0061*2	// 解码数据
#define FPGA_ADDR_DOA1H				(uint16_t)0x0062*2	// 时延测量值1，高字
#define FPGA_ADDR_DOA1L				(uint16_t)0x0063*2	// 时延测量值1，低字
#define FPGA_ADDR_DOA2H				(uint16_t)0x0064*2	// 时延测量值2，高字
#define FPGA_ADDR_DOA2L				(uint16_t)0x0065*2	// 时延测量值2，低字

#define FPGA_ADDR_RAM_SEL			(uint16_t)0x0070*2

typedef enum
{
	ACQ_PROC_NULL = 0,
	ACQ_PROC_OK,
	ACQ_PROC_OVERFLOW,
	ACQ_PROC_ERR,
	ACQ_PROC_OVER,
}ACQ_PROC_TypeDef;

extern bool m_bFpgaInitRdy;				// FPGA接口初始化状态
extern u16  m_nFpgaInt;
extern u16  m_nFpgaIntB;
extern u16	m_uFpgaIntReg;	            // FPGA中断寄存器值

/************************************************************************/
// 配置FPGA采集相关设置
/************************************************************************/
void FPGA_Init(void);     
void FPGA_DeInit(void);
void FPGA_IntEn(bool bEn);				// 使能或禁止FPGA与ARM的中断

/************************************************************************/
/* FPGA数据读写控制函数
*/
/************************************************************************/
void FPGA_WriteBuffer(uint16_t* pBuffer, uint32_t WriteAddr, uint32_t NumWordToWrite);
void FPGA_ReadBuffer(uint16_t* pBuffer, uint32_t WriteAddr, uint32_t NumWordToWrite);	
void FPGA_CMD_Write(u16 uAddr, u16 uData);		// 根据地址定义，向FPGA中写入16位数据
u16  FPGA_CMD_Read(u16 uAddr);					// 根据地址定义，从FPGA中读取16位数据
u16  FPGA_GetIntReg(void);						// 从FPGA读取中断寄存器参数
void FPGA_SetAcqEn(bool bEn);					// 设置FPGA采集使能信号
void FPGA_SetCalEn(bool bEn);					// 设置FPGA基阵校准使能
void FPGA_SetSampleRate(u8 nRate);	            // 设置AD的采样速率
void FPGA_SetSampleGain(u8 nGain);	            // 设置AD的增益
void FPGA_SetEmitEn(bool bEn);					// 设置校阵脉冲发射，或者根据压力传感器数值使能发射
void FPGA_SetDeadGate(u16 nGate);				// 设定死区时间
void FPGA_SetCatchGate(u16 nGate);				// 脉宽鉴宽时间
float FPGA_GetNoiseAvgData(void);				// 从FPGA读取噪声平滑滤波器的参数并转换为输出电压
	
int FPGA_AcqProc(void);						    // FPGA采集处理程序

#endif


