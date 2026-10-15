/**
  ******************************************************************************
  * @file    flash_if.c 
  * @author  MCD Application Team
  * @version V1.1.0
  * @date    07-October-2011
  * @brief   This file provides high level routines to manage internal Flash 
  *          programming (erase and write). 
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

/* Includes ------------------------------------------------------------------*/
#include "flash_if.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

// 获取某个地址所在的flash扇区
// addr:flash地址
// 返回值:0~11,即addr所在的扇区
uint16_t STMFLASH_GetFlashSector(u32 addr)
{
#if USE_STM32F4XX	
	if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_1)return FLASH_Sector_0;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_2)return FLASH_Sector_1;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_3)return FLASH_Sector_2;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_4)return FLASH_Sector_3;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_5)return FLASH_Sector_4;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_6)return FLASH_Sector_5;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_7)return FLASH_Sector_6;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_8)return FLASH_Sector_7;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_9)return FLASH_Sector_8;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_10)return FLASH_Sector_9;
	else if(addr >= STM32_FLASH_BASE && addr<ADDR_FLASH_SECTOR_11)return FLASH_Sector_10; 
	return FLASH_Sector_11;	
#elif USE_STM32H7XX
	if(addr < ADDR_FLASH_SECTOR_1_BANK1)return FLASH_SECTOR_0;
	else if(addr < ADDR_FLASH_SECTOR_2_BANK1)return FLASH_SECTOR_1;
	else if(addr < ADDR_FLASH_SECTOR_3_BANK1)return FLASH_SECTOR_2;
	else if(addr < ADDR_FLASH_SECTOR_4_BANK1)return FLASH_SECTOR_3;
	else if(addr < ADDR_FLASH_SECTOR_5_BANK1)return FLASH_SECTOR_4;
	else if(addr < ADDR_FLASH_SECTOR_6_BANK1)return FLASH_SECTOR_5;
	else if(addr < ADDR_FLASH_SECTOR_7_BANK1)return FLASH_SECTOR_6;
	return FLASH_SECTOR_7;	
#endif
}

#if STM32_FLASH_WREN	// 如果使能了写使能

/**
  * @brief  This function does an erase of all user flash area
  * @param  uSector: sector of user flash area
  * @retval 0: user flash area successfully erased
  *         1: error occurred
  */
u8 FLASH_If_Erase(u16 uSector)
{
	/* Device voltage range supposed to be [2.7V to 3.6V], the operation will be done by word */ 
#if USE_STM32F4XX	
	/* STM32F40X STM32F41X
	扇区 0  0x0800 0000 - 0x0800 3FFF 	16 KB
	扇区 1  0x0800 4000 - 0x0800 7FFF 	16 KB
	扇区 2  0x0800 8000 - 0x0800 BFFF 	16 KB
	扇区 3  0x0800 C000 - 0x0800 FFFF 	16 KB
	扇区 4  0x0801 0000 - 0x0801 FFFF 	64 KB
	扇区 5  0x0802 0000 - 0x0803 FFFF 	128 KB
	扇区 6  0x0804 0000 - 0x0805 FFFF 	128 KB
	扇区 7  0x0806 0000 - 0x0807 FFFF 	128 KB
	扇区 8  0x0808 0000 - 0x0809 FFFF 	128 KB
	扇区 9  0x080A 0000 - 0x080B FFFF 	128 KB
	扇区 10 0x080C 0000 - 0x080D FFFF 	128 KB
	扇区 11 0x080E 0000 - 0x080F FFFF 	128 KB
	
	// STM32F42X STM32F43X
	扇区 12 0x0810 0000 - 0x0810 3FFF 	16 KB
	扇区 13 0x0810 4000 - 0x0810 7FFF 	16 KB
	扇区 14 0x0810 8000 - 0x0810 BFFF 	16 KB
	扇区 15 0x0810 C000 - 0x0810 FFFF 	16 KB
	扇区 16 0x0811 0000 - 0x0811 FFFF 	64 KB
	扇区 17 0x0812 0000 - 0x0813 FFFF 	128 KB
	扇区 18 0x0814 0000 - 0x0815 FFFF 	128 KB
	扇区 19 0x0816 0000 - 0x0817 FFFF 	128 KB
	扇区 20 0x0818 0000 - 0x0819 FFFF 	128 KB
	扇区 21 0x081A 0000 - 0x081B FFFF 	128 KB
	扇区 22 0x081C 0000 - 0x081D FFFF 	128 KB
	扇区 23 0x081E 0000 - 0x081F FFFF 	128 KB
	*/
	FLASH_Status status = FLASH_COMPLETE;
	u32 uFlashAddr = 0;
	if ( IS_FLASH_SECTOR(uSector) )
	{
		switch ( uSector )
		{
			case FLASH_Sector_0	 : uFlashAddr = 0x08000000; break;	/* 16 Kbyte */
			case FLASH_Sector_1	 : uFlashAddr = 0x08004000; break;	/* 16 Kbyte */
			case FLASH_Sector_2	 : uFlashAddr = 0x08008000; break;	/* 16 Kbyte */
			case FLASH_Sector_3	 : uFlashAddr = 0x0800C000; break;	/* 16 Kbyte */
			case FLASH_Sector_4	 : uFlashAddr = 0x08010000; break;	/* 64 Kbyte */
			case FLASH_Sector_5	 : uFlashAddr = 0x08020000; break;	/* 128 Kbyte */
			case FLASH_Sector_6	 : uFlashAddr = 0x08040000; break;	/* 128 Kbyte */
			case FLASH_Sector_7	 : uFlashAddr = 0x08060000; break;	/* 128 Kbyte */
			case FLASH_Sector_8	 : uFlashAddr = 0x08080000; break;	/* 128 Kbyte */
			case FLASH_Sector_9	 : uFlashAddr = 0x080A0000; break;	/* 128 Kbyte */
			case FLASH_Sector_10 : uFlashAddr = 0x080C0000; break;	/* 128 Kbyte */
			case FLASH_Sector_11 : uFlashAddr = 0x080E0000; break;	/* 128 Kbyte */

			case FLASH_Sector_12 : uFlashAddr = 0x08100000; break;	/* 16 Kbyte */
			case FLASH_Sector_13 : uFlashAddr = 0x08104000; break;	/* 16 Kbyte */
			case FLASH_Sector_14 : uFlashAddr = 0x08108000; break;	/* 16 Kbyte */
			case FLASH_Sector_15 : uFlashAddr = 0x0810C000; break;	/* 16 Kbyte */
			case FLASH_Sector_16 : uFlashAddr = 0x08110000; break;	/* 64 Kbyte */
			case FLASH_Sector_17 : uFlashAddr = 0x08120000; break;	/* 128 Kbyte */
			case FLASH_Sector_18 : uFlashAddr = 0x08140000; break;	/* 128 Kbyte */
			case FLASH_Sector_19 : uFlashAddr = 0x08160000; break;	/* 128 Kbyte */
			case FLASH_Sector_20 : uFlashAddr = 0x08180000; break;	/* 128 Kbyte */
			case FLASH_Sector_21 : uFlashAddr = 0x081A0000; break;	/* 128 Kbyte */
			case FLASH_Sector_22 : uFlashAddr = 0x081C0000; break;	/* 128 Kbyte */
			case FLASH_Sector_23 : uFlashAddr = 0x081E0000; break;	/* 128 Kbyte */
			default: return 1; // 无效扇区，返回错误
		}
	}
	else
    {
        return 1; // 扇区非法，返回错误
    }
	
	if ( IS_FLASH_ADDRESS(uFlashAddr) )
    {
        FLASH_Unlock();
        FLASH_DataCacheCmd(DISABLE); // 擦除前禁用数据缓存
        status = FLASH_EraseSector(uSector, VoltageRange_3); // 擦除指定扇区
        FLASH_DataCacheCmd(ENABLE);  // 擦除后启用缓存
        FLASH_Lock();
        
        if (status != FLASH_COMPLETE)
        {
            return 1; // 擦除失败
        }
        return 0; // 擦除成功
    }
#endif
	return 1;	
}

/**
// 特别注意:因为STM32F4，STM32H7的扇区实在太大,没办法本地保存扇区数据,
//          写地址如果非0XFF,那么会先擦除整个扇区且不保存扇区数据.所以
//          写非0XFF的地址,将导致整个扇区数据丢失.建议写之前确保扇区里
//          没有重要数据,最好是整个扇区先擦除了,然后慢慢往后写. 
// 本函数对OTP区域也有效! 可以用来写OTP区!
// OTP区域地址范围:0X1FFF7800~0X1FFF7A0F
 * @brief  从指定 Flash 地址写入指定字数的数据
 * @param  WriteAddr : 起始地址（必须 4 字节对齐）
 * @param  pBuffer   : 数据接收缓冲区（不能为空）
 * @param  NumToWrite: 写入的字（4 字节）数
 * @retval 0:成功; 1:地址非法; 2:参数非法;
 */
uint8_t STMFLASH_Write(u32 WriteAddr, u32* pBuffer, u32 NumToWrite)	
{ 
#if USE_STM32F4XX	
	FLASH_Status status = FLASH_COMPLETE;
	int nSector = STMFLASH_GetFlashSector(WriteAddr);	// 获取要访问的Flash扇区号
	if( WriteAddr<STM32_FLASH_BASE||WriteAddr%4 ) 		// 非法地址
		return 1;								
	
	if(pBuffer == NULL || NumToWrite == 0) // 空指针/无数据，返回错误
        return 2;
	
	FLASH_Unlock();							// 解锁 
	FLASH_DataCacheCmd(DISABLE);			// FLASH擦除期间,必须禁止数据缓存
 		
	u32 addrx   = WriteAddr;				// 写入的起始地址
	u32 endaddr = WriteAddr+NumToWrite*4;	// 写入的结束地址
	if(addrx < 0X1FFF0000)					// 只有主存储区,才需要执行擦除操作!!
	{
		status = FLASH_EraseSector(STMFLASH_GetFlashSector(addrx),VoltageRange_3);	// VCC=2.7~3.6V之间!!
		if( status != FLASH_COMPLETE ) 		// 发生错误
		{
			return 2;  						// 失败
		}			
	}
	if( status == FLASH_COMPLETE )
	{
		while( WriteAddr < endaddr )		// 写数据
		{
			if(FLASH_ProgramWord(WriteAddr, *pBuffer) != FLASH_COMPLETE)	// 写入数据
			{ 
				break;						// 写入异常
			}
			else
			{
				WriteAddr += 4;				// 写入正常
				pBuffer++;
			}
		} 
	}
	FLASH_DataCacheCmd(ENABLE);				// FLASH擦除结束,开启数据缓存
	FLASH_Lock();							// 上锁
	return 0;  								// 成功
#elif USE_STM32H7XX
	FLASH_EraseInitTypeDef FlashEraseInit = {0};
    HAL_StatusTypeDef FlashStatus = HAL_OK;
    uint32_t SectorError = 0;
    uint32_t addrx   = WriteAddr;
    uint32_t endaddr = WriteAddr + NumToWrite * 4;  // 按uint32_t计算结束地址
	
	// 1. 地址合法性检查（必须是Flash地址+4字节对齐）
    if ((WriteAddr < STM32_FLASH_BASE) || (WriteAddr >= (STM32_FLASH_BASE + 0x200000)) || (WriteAddr % 4 != 0))
    {
        return 1;  // 地址非法
    }
    if (pBuffer == NULL || NumToWrite == 0)
    {
        return 1;  // 参数非法
    }
    
 	// 2. 解锁Flash
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return 2;  // 解锁失败
    }
    
    // 3. 批量擦除需要的扇区（优化：先计算所有需要擦除的扇区，避免逐地址判断）
    uint32_t start_sector = (addrx - STM32_FLASH_BASE) / STM32_FLASH_SECTOR_SIZE;
    uint32_t end_sector   = (endaddr - 1 - STM32_FLASH_BASE) / STM32_FLASH_SECTOR_SIZE;	// 向上取整到扇区

    for (uint32_t sector = start_sector; sector <= end_sector; sector++)
    {
        // 检查扇区是否已全为0xFFFFFFFF，避免重复擦除
        uint32_t sector_addr = STM32_FLASH_BASE + sector * STM32_FLASH_SECTOR_SIZE;
        uint8_t is_erased = 1;
        for (uint32_t i = 0; i < STM32_FLASH_SECTOR_SIZE; i += 4)
        {
            if (*(volatile uint32_t*)(sector_addr + i) != 0xFFFFFFFF)
            {
                is_erased = 0;
                break;
            }
        }
        if (!is_erased)
        {
            FlashEraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;		// 擦除类型，扇区擦除 
            FlashEraseInit.Sector 	 = sector;
            FlashEraseInit.Banks 	 = FLASH_BANK_1;  				// 若用BANK2，需根据地址判断
            FlashEraseInit.NbSectors = 1;
            FlashEraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;

            if (HAL_FLASHEx_Erase(&FlashEraseInit, &SectorError) != HAL_OK)
            {
                HAL_FLASH_Lock(); return 2;  						// 擦除失败
            }
            // 擦除后刷新缓存
            SCB_CleanInvalidateDCache();
            SCB_InvalidateICache();
        }
    }

    // 4. 按32字节（FlashWord）批量写入
    while (addrx < endaddr)
    {
        // 每次写32字节（8个uint32_t），需确保剩余数据足够；不足时补0（可选）
        if ((endaddr - addrx) >= 32)
        {
            FlashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addrx, (uint64_t)pBuffer);
            addrx += 32;
            pBuffer += 8;
        }
        else
        {
            // 处理不足32字节的情况（可选：补0后写入，或返回错误）
            uint32_t temp_buf[8] = {0};
            memcpy(temp_buf, pBuffer, (endaddr - addrx));
            FlashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addrx, (uint64_t)temp_buf);
            addrx = endaddr;
        }

        if (FlashStatus != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 3;  // 写入失败
        }
    }

    // 5. 写入后刷新缓存
    SCB_CleanInvalidateDCache();
    SCB_InvalidateICache();

    // 6. 锁定Flash
    HAL_FLASH_Lock();
    return 0;  // 成功
#else
    return 1;  // 非F4、H7平台
#endif
} 

/**
 * @brief  从指定 Flash 地址读取指定字数的数据
 * @param  ReadAddr : 起始地址（必须 4 字节对齐）
 * @param  pBuffer  : 数据接收缓冲区（不能为空）
 * @param  NumToRead: 读取的字（4 字节）数
 * @retval 0:成功; 1:地址非法; 2:参数非法;
 */
uint8_t STMFLASH_Read(u32 ReadAddr, u32* pBuffer, u32 NumToRead)   	
{
	// 1. 参数合法性检查
    if (pBuffer == NULL || NumToRead == 0)
    {
        return 2; // 空指针或读取长度为 0
    }

    // 2. 地址合法性检查：必须在 Flash 范围内 + 4 字节对齐
    if ((ReadAddr < STM32_FLASH_BASE) || (ReadAddr > STM32_FLASH_END) || (ReadAddr % 4 != 0))
    {
        return 1; // 地址非法
    }
    // 检查结束地址是否越界
    uint32_t end_addr = ReadAddr + NumToRead * 4;
    if (end_addr > (STM32_FLASH_END + 1))
    {
        return 1; // 读取越界
    }

    // 3. 刷新 D-Cache：避免读取缓存中的旧数据（H7 系列必加）
	#if USE_STM32H7XX
    SCB_InvalidateDCache_by_Addr((uint32_t *)ReadAddr, NumToRead * 4);
	#endif

    // 4. 循环读取 Flash 数据
    uint32_t faddr = ReadAddr;
    for (uint32_t i = 0; i < NumToRead; i++)
    {
        // 使用 volatile 确保每次直接读取内存（Flash），禁止编译器优化
        pBuffer[i] = *(__IO uint32_t *)faddr;
        faddr += 4;
    }

    return 0; // 读取成功
}

#endif

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
