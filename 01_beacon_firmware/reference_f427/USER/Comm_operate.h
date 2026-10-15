#ifndef __COMM_OPERATE_H
#define __COMM_OPERATE_H

#include "main.h"
#include "Gpio.h"

//////////////////////////////////////////////////////////////////////////

#define COMM1		1
#define COMM2		2
#define COMM3		3
#define COMM4		4
#define COMM5		5
#define COMM6		6
#define COMM7		7

// if need use flow control mode of half-duplex RS485, should define control IO
#define COMM1_RS485			0
#define COMM2_RS485			1
#define COMM3_RS485			0
#define COMM4_RS485			0
#define COMM6_RS485			0
#define COMM7_RS485			0

#define	COMM1_USE_TX_PIN	1		// Comm1 是否使用Tx管脚定义，该管脚可以用于通用IO
#define	COMM2_USE_TX_PIN	1		// Comm2 是否使用Tx管脚定义，该管脚可以用于通用IO
#define	COMM2_USE_RX_PIN	1		// Comm2 是否使用Rx管脚定义，该管脚可以用于通用IO
#define COMM3_AF_PD8_9		1		// Comm3 管脚复用定义，仅对STM32F4XX有效，正常是PB10、PB11，映射到PD8、PD9

#define RXBUFFER_SIZE 		1
#define MAX_COMM_BUF_SIZE 	256

#define COMM1_RS485_CTRL	0
#define COMM2_RS485_CTRL	0
#define COMM3_RS485_CTRL	0
#define COMM4_RS485_CTRL	0
#define COMM6_RS485_CTRL	0
#define COMM7_RS485_CTRL	0

#if COMM1_RS485_CTRL
#define COMM1_RS485_DE_GPIO			GPIO_Pin_8
#define COMM1_RS485_DE_GPIO_PORT    GPIOA
#define COMM1_RS485_DE				GPIO_SetBits(COMM1_RS485_DE_GPIO_PORT, COMM1_RS485_DE_GPIO);
#define COMM1_RS485_RE				GPIO_ResetBits(COMM1_RS485_DE_GPIO_PORT, COMM1_RS485_DE_GPIO);
#endif

#if COMM3_RS485_CTRL
#define COMM3_RS485_DE_GPIO			GPIO_Pin_10
#define COMM3_RS485_DE_GPIO_PORT	GPIOD
#define COMM3_RS485_DE				GPIO_SetBits(COMM3_RS485_DE_GPIO_PORT, COMM3_RS485_DE_GPIO);
#define COMM3_RS485_RE				GPIO_ResetBits(COMM3_RS485_DE_GPIO_PORT, COMM3_RS485_DE_GPIO);
#endif

typedef enum 
{
	/*baud = (bits/sec)
	0 = 50	    6 = 600	    12 = 9600	
	1 = 75	    7 = 1200	13 = 19200	
	2 = 110 	8 = 1800	14 = 38400
	3 = 134.5	9 = 2400	15 = 57600
	4 = 150	    10 = 4800	16 = 115200
	5 = 300	    11 = 7200	*/
	tBaud50	= 0,
	tBaud75,
	tBaud110,
	tBaud134_5,
	tBaud150,
	tBaud300,
	tBaud600,
	tBaud1200,
	tBaud1800,
	tBaud2400,
	tBaud4800,
	tBaud7200,
	tBaud9600,
	tBaud19200,
	tBaud38400,
	tBaud57600,
	tBaud115200,
	tBaud230400,
	tBaud460800,
	tBaud921600,
}TypeDef_COMM_BAUD;

typedef struct 
{
	bool 	bEnable;
	bool	bRdy;
	bool	bRS485;
	u8		nCommSn;
	u8		nCommBaud;		// 串口波特率枚举量
	u16		nDelay;			// RS485 echo delay time
	u16		nRFlag;
	u16		uCommTx;
	u16		uCommRx;
	USART_TypeDef* USARTn;
#if USE_STM32H7XX
	UART_HandleTypeDef* pUart;
#endif
	char	chRx[8];
	char	chTxBuf[MAX_COMM_BUF_SIZE];
	char	chRxBuf[MAX_COMM_BUF_SIZE];
}COMM_OBJECT;

typedef enum
{
	COMM_FLAG_RX_FULL	= 0x01,
	COMM_FLAG_RX2_FULL	= 0x02,
}COMM_FLAG_TYPE;

extern COMM_OBJECT*	m_pComm1;
extern COMM_OBJECT*	m_pComm2;
extern COMM_OBJECT*	m_pComm3;
extern COMM_OBJECT*	m_pComm4;
extern COMM_OBJECT*	m_pComm6;
extern COMM_OBJECT*	m_pComm7;

extern bool m_bComm1TxDmaEnd;
extern bool m_bComm2TxDmaEnd;
extern bool m_bComm3TxDmaEnd;

#if USE_STM32H7XX
extern UART_HandleTypeDef m_huart1;
extern UART_HandleTypeDef m_huart2;
extern UART_HandleTypeDef m_huart3;
extern UART_HandleTypeDef m_huart4;
extern UART_HandleTypeDef m_huart6;
extern UART_HandleTypeDef m_huart7;
extern DMA_HandleTypeDef m_hdma_usart1_rx;
#endif

void Comm1_Config(int nBaud, bool bEn);				// 配置串口，此函数改变 COMM_OBJECT 对象中的波特率参数值
void Comm2_Config(int nBaud, bool bEn);				// 配置串口，此函数改变 COMM_OBJECT 对象中的波特率参数值
void Comm3_Config(int nBaud, bool bEn);				// 配置串口，此函数改变 COMM_OBJECT 对象中的波特率参数值

#if USE_STM32F4XX || USE_STM32H7XX
void Comm4_Config(int nBaud, bool bEn);				// 配置串口，此函数改变 COMM_OBJECT 对象中的波特率参数值  
void Comm6_Config(int nBaud, bool bEn);				// 配置串口，此函数改变 COMM_OBJECT 对象中的波特率参数值  
void Comm7_Config(int nBaud, bool bEn);				// 配置串口，此函数改变 COMM_OBJECT 对象中的波特率参数值  
#endif

u8  UART_SetCommBaud(int nComm, uint32_t nBaud); 	// 设置串口波特率，此函数不改变 COMM_OBJECT 对象中的波特率参数值，
													// 如果nBaud < 1 则根据COMM_OBJECT 对象中的波特率参数值重新设置对象波特率参数值
USART_TypeDef* UART_CommSnToUSARTn(int nComm);		// 通过串口的枚举值返回 串口结构体对象 
int UART_USARTnToCommSn( USART_TypeDef* USARTn);	// 通过串口的串口结构体对象返回 串口序号  
int UART_GetRealBaud(u8 nEnumBaud);					// 根据串口的枚举值返回波特率（实际数值）
u8  UART_GetEnumBaud(u32 nRealBaud);				// 根据串口的波特率（实际数值）返回枚举值
// int fputc(int ch, FILE *f);	

// 由于RS485型接口的问题，此处不再允许调用单字节发送函数
void Comm1_PutStr(char * str);
void Comm2_PutStr(char * str);
void Comm3_PutStr(char * str);
void Comm1_PutBuf(char * buf, u32 nBuf);
void Comm2_PutBuf(char * buf, u32 nBuf);
void Comm3_PutBuf(char * buf, u32 nBuf);

#if USE_STM32F4XX || USE_STM32H7XX
void Comm4_PutStr(char * str);
void Comm4_PutBuf(char * buf, u32 nBuf);
void Comm6_PutStr(char * str);
void Comm6_PutBuf(char * buf, u32 nBuf);
void Comm7_PutStr(char * str);
void Comm7_PutBuf(char * buf, u32 nBuf);
#endif

void Comm1_DMA_TxIntInit(bool bEn);
void Comm1_DMA_RxIntInit(bool bEn);
void Comm1_DMA_TxConfig(uint32_t *BufferSRC, uint32_t BufferSize);
void Comm1_DMA_RxConfig(bool bEn, uint8_t  *BufferDST, uint32_t BufferSize);
void Comm1_DMA_DBuf_RxConfig(bool bEn, uint8_t *BufferDST1, uint8_t *BufferDST2, uint32_t BufferSize);
							
void Comm2_DMA_TxConfig(uint32_t *BufferSRC, uint32_t BufferSize);

void Comm3_DMA_TxIntInit(bool bEn);
void Comm3_DMA_RxIntInit(bool bEn);		
void Comm3_DMA_TxConfig(uint32_t *BufferSRC, uint32_t BufferSize);
void Comm3_DMA_RxConfig(uint8_t  *BufferDST, uint32_t BufferSize);

#if USE_STM32F4XX || USE_STM32H7XX
void Comm6_DMA_RxIntInit(bool bEn);
void Comm6_DMA_RxConfig(uint8_t *BufferDST, uint32_t BufferSize);
void Comm6_DMA_DBuf_RxConfig(bool bEn, uint8_t *BufferDST1, uint8_t *BufferDST2, uint32_t BufferSize);
#endif

void Comm_Config(int nComm, int nBaud, bool bEn);					// Init comm
void Comm_Config2(int nComm, int nBaud, bool bEn, bool bVirtual);	// Init comm, if bVirtual = true, only generate buffer

void Comm_Enable(int nComm, bool bEn);		// enable special comm
void Comm_EnableTxOnly(int nComm);			// 串口仅发送使能	
void Comm_ClearParam(int nComm);			// clear indicate comm param

bool Comm_GetFrameMsg(char rx, COMM_OBJECT* pComm);	
bool Comm_GetFullFrameMsg(char rx, COMM_OBJECT* pComm);	
bool Comm_GetData(char rx, COMM_OBJECT* pComm);	
void Comm_SendTxBuf(COMM_OBJECT* pComm);			// Send COMM_OBJECT TxBuf data, data length is decide by uCommTx
void Comm_SendMsg(COMM_OBJECT* pComm);				// Send string, data lenth is decide by string length
COMM_OBJECT* Comm_GetObject(int nComm);				// Get COMM_OBJECT by comm sn

#endif


