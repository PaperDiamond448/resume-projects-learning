/**
  ******************************************************************************
  * @file    flash_if.h 
  * @author  MCD Application Team
  * @version V1.1.0
  * @date    07-October-2011
  * @brief   Header for flash_if.c module
  ******************************************************************************
  * @attention
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2011 STMicroelectronics</center></h2>
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __FLASH_IF_H
#define __FLASH_IF_H

/* Includes ------------------------------------------------------------------*/
#include "main.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////
//用户根据自己的需要设置
// STM32F103RET6	512K
// STM32F103RBT6	128K
// STM32F427VG		1024K
// STM32F429VG		1024K
// STM32H743VI		2048K
#define STM32_FLASH_SIZE 		1024UL 	 		// 所选STM32的FLASH容量大小(单位为K)
#define STM32_FLASH_WREN 		1             	// 使能FLASH写入(0，不使能; 1，使能)
#define STM32_FLASH_SECTOR_SIZE	0x20000UL		// 128*1024 定义需要保存参数的FLASH块的字节数

//STM32 FLASH的起始地址
#define STM32_FLASH_BASE		0x08000000UL 
#if USE_STM32F4XX
#define STM32_FLASH_END         0x080FFFFFUL	// Flash 结束地址（1MB）
#elif USE_STM32H7XX
#define STM32_FLASH_END         0x080FFFFFUL	// Flash 结束地址 BANK1（1MB）
#endif
#define FLASH_WAITETIME  		50000U          // FLASH等待超时时间

//STM32 FLASH用户数据存储地址
#define FLASH_SAVE_ADDR  STM32_FLASH_BASE + STM32_FLASH_SIZE*1024 - 2048*16

#if USE_STM32F4XX	
//STM32F4 FLASH 扇区的起始地址
#define ADDR_FLASH_SECTOR_0     ((u32)0x08000000) 	//扇区0起始地址, 16 Kbytes  
#define ADDR_FLASH_SECTOR_1     ((u32)0x08004000) 	//扇区1起始地址, 16 Kbytes  
#define ADDR_FLASH_SECTOR_2     ((u32)0x08008000) 	//扇区2起始地址, 16 Kbytes  
#define ADDR_FLASH_SECTOR_3     ((u32)0x0800C000) 	//扇区3起始地址, 16 Kbytes  
#define ADDR_FLASH_SECTOR_4     ((u32)0x08010000) 	//扇区4起始地址, 64 Kbytes  
#define ADDR_FLASH_SECTOR_5     ((u32)0x08020000) 	//扇区5起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_6     ((u32)0x08040000) 	//扇区6起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_7     ((u32)0x08060000) 	//扇区7起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_8     ((u32)0x08080000) 	//扇区8起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_9     ((u32)0x080A0000) 	//扇区9起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_10    ((u32)0x080C0000) 	//扇区10起始地址,128 Kbytes  
#define ADDR_FLASH_SECTOR_11    ((u32)0x080E0000) 	//扇区11起始地址,128 Kbytes  
#elif USE_STM32H7XX
//STM32H7 FLASH 扇区的起始地址
//BANK1
#define ADDR_FLASH_SECTOR_0_BANK1     ((uint32_t)0x08000000) //扇区0起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_1_BANK1     ((uint32_t)0x08020000) //扇区1起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_2_BANK1     ((uint32_t)0x08040000) //扇区2起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_3_BANK1     ((uint32_t)0x08060000) //扇区3起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_4_BANK1     ((uint32_t)0x08080000) //扇区4起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_5_BANK1     ((uint32_t)0x080A0000) //扇区5起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_6_BANK1     ((uint32_t)0x080C0000) //扇区6起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_7_BANK1     ((uint32_t)0x080E0000) //扇区7起始地址, 128 Kbytes  

//BANK2
#define ADDR_FLASH_SECTOR_0_BANK2     ((uint32_t)0x08100000) //扇区0起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_1_BANK2     ((uint32_t)0x08120000) //扇区1起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_2_BANK2     ((uint32_t)0x08140000) //扇区2起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_3_BANK2     ((uint32_t)0x08160000) //扇区3起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_4_BANK2     ((uint32_t)0x08180000) //扇区4起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_5_BANK2     ((uint32_t)0x081A0000) //扇区5起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_6_BANK2     ((uint32_t)0x081C0000) //扇区6起始地址, 128 Kbytes  
#define ADDR_FLASH_SECTOR_7_BANK2     ((uint32_t)0x081E0000) //扇区7起始地址, 128 Kbytes  
#endif

u8 FLASH_If_Erase(u16 uSector);	// 按照指定扇区序号，擦除Flash， 例如：FLASH_Sector_0 —— FLASH_Sector_11

uint8_t STMFLASH_Write(u32 WriteAddr, u32* pBuffer, u32 NumToWrite);	// 从指定地址开始写入指定长度的数据
uint8_t STMFLASH_Read( u32 ReadAddr,  u32* pBuffer, u32 NumToRead);		// 从指定地址开始读出指定长度的数据

#endif /* __FLASH_IF_H */

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
