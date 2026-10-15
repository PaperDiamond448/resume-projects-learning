#ifndef __SPI_H
#define __SPI_H

#include "main.h"

#define USE_SPI1_PORTB		0		// 使用PortB，PB3、PB4、PB5替代 PortA

extern bool m_bSPI1Init;
extern bool m_bSPI4Init; 

#if USE_STM32H7XX
extern SPI_HandleTypeDef m_hSpi1;
extern SPI_HandleTypeDef m_hSpi4;
extern DMA_HandleTypeDef m_hdma_spi1_rx;
extern DMA_HandleTypeDef m_hdma_spi4_rx;
#endif

void SPI1_Init(bool bInit);			// 初始化SPI1口 
u8  SPI1_ReadWriteByte(u8 TxData);	// SPI1总线读写一个字节
u16 SPI1_ReadWriteWord(u16 TxData);	// SPI1总线读写一个字	
u16 SPI1_ReadWrite3Byte(u8 uAddr, u16 uData);// SPI1总线读写3字，第1字节是读写地址，数据为16位

void SPI4_Init(bool bInit);		 	// 初始化SPI4口
u8  SPI4_ReadWriteByte(u8 TxData);	// SPI4总线读写一个字节
u16 SPI4_ReadWriteWord(u16 TxData);	// SPI4总线读写一个字
u16 SPI4_ReadWrite3Byte(u8 uAddr, u16 uData);// SPI4总线读写3字，第1字节是读写地址，数据为16位

void SPI5_Init(bool bInit);		 	// 初始化SPI5口
u8  SPI5_ReadWriteByte(u8 TxData);	// SPI5总线读写一个字节
u16 SPI5_ReadWriteWord(u16 TxData);	// SPI5总线读写一个字
u16 SPI5_ReadWrite3Byte(u8 uAddr, u16 uData);// SPI5总线读写3字，第1字节是读写地址，数据为16位

void SPI1_Init_Slave(bool bInit);	// 初始化SPI1口的Slave模式
void SPI1_DMA_DBuf_RxConfig(bool bEn, uint8_t *BufferDST1, uint8_t *BufferDST2, uint32_t BufferSize);

void SPI4_Init_Slave(bool bInit);	// 初始化SPI4口的Slave模式
void SPI4_DMA_DBuf_RxConfig(bool bEn, uint8_t *BufferDST1, uint8_t *BufferDST2, uint32_t BufferSize);
#endif

