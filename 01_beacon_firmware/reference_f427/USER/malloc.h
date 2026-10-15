#ifndef __MALLOC_H
#define __MALLOC_H

#include "main.h"

#ifndef NULL
#define NULL 0
#endif

#if USE_STM32F1XX
#define SRAMIN		0		// 内部内存池数组序号
#define SRAMEX		1		// 外部内存池数组序号

#define SRAMBANK	2 		// 定义支持的SRAM块数

//mem1内存参数设定.mem1完全处于内部SRAM里面
#define MEM1_BLOCK_SIZE			128  	  						// 内存块大小 字节
#define MEM1_MAX_SIZE			4*1024  						// 最大管理内存
#define MEM1_ALLOC_TABLE_SIZE	MEM1_MAX_SIZE/MEM1_BLOCK_SIZE 	// 内存表大小

//mem2内存参数设定.mem2的内存池处于外部SRAM里面,其他的处于内部SRAM里面
#define MEM2_BLOCK_SIZE			128  	  						// 内存块大小 字节
#define MEM2_MAX_SIZE			4*1024  						// 最大管理内存
#define MEM2_ALLOC_TABLE_SIZE	MEM2_MAX_SIZE/MEM2_BLOCK_SIZE 	// 内存表大小

#elif USE_STM32F4XX
//定义三个内存池
#define SRAMIN		0		// 内部内存池
#define SRAMEX		1		// 外部内存池
#define SRAMCCM		2		// CCM内存池(此部分SRAM仅仅CPU可以访问！！！)

#define SRAMBANK	3 		// 定义支持的SRAM块数

//mem1内存参数设定,mem1完全处于内部SRAM里面
#define MEM1_BLOCK_SIZE		128  		// 内存块大小为128字节
#define MEM1_MAX_SIZE		160*1024 	// 最大管理内存
#define MEM1_ALLOC_TABLE_SIZE MEM1_MAX_SIZE/MEM1_BLOCK_SIZE  //内存表大小

//mem2内存参数设定,mem2处于外部SRAM里面
#define MEM2_BLOCK_SIZE		256  		// 内存块大小为256字节
#define MEM2_MAX_SIZE		200*1024 	// 最大管理内存 200k
#define MEM2_ALLOC_TABLE_SIZE MEM2_MAX_SIZE/MEM2_BLOCK_SIZE  //内存表大小

//mem3内存参数设定,mem3处于CCM,用于管理CCM(特别注意,这部分SRAM,仅CPU可以访问)
#define MEM3_BLOCK_SIZE		128  		// 内存块大小为 128字节
#define MEM3_MAX_SIZE		32*1024 	// 最大管理内存 32k
#define MEM3_ALLOC_TABLE_SIZE MEM3_MAX_SIZE/MEM3_BLOCK_SIZE  //内存表大小

#elif USE_STM32H7XX
#define USE_SRAMEX 	0
#if USE_SRAMEX
//定义六个内存池
#define SRAMIN	 	0		//AXI内存池,AXI共512KB 
#define SRAMEX   	1		//外部内存池(SDRAM), SDRAM共32MB
#define SRAM12		2		//SRAM1/2/3内存池,SRAM1+SRAM2,共256KB
#define SRAM4		3		//SRAM4内存池,SRAM4共64KB
#define SRAMDTCM 	4		//DTCM内存池,DTCM共128KB,此部分内存仅CPU和MDMA(通过AHBS)可以访问!!!!
#define SRAMITCM 	5		//ITCM内存池,DTCM共64 KB,此部分内存仅CPU和MDMA(通过AHBS)可以访问!!!!

#define SRAMBANK 	6		//定义支持的SRAM块数.	
#else
//定义五个内存池
#define SRAMIN	 	0		//AXI内存池,AXI共512KB 
#define SRAM12		1		//SRAM1/2/3内存池,SRAM1+SRAM2,共256KB
#define SRAM4		2		//SRAM4内存池,SRAM4共64KB
#define SRAMDTCM 	3		//DTCM内存池,DTCM共128KB,此部分内存仅CPU和MDMA(通过AHBS)可以访问!!!!
#define SRAMITCM 	4		//ITCM内存池,ITCM共64 KB,此部分内存仅CPU和MDMA(通过AHBS)可以访问!!!!

#define SRAMBANK 	5		//定义支持的SRAM块数.	
#endif	// USE_SRAMEX

//mem1内存参数设定.mem1是H7内部的AXI内存.
#define MEM1_BLOCK_SIZE			128  	  						//内存块大小为128字节
#define MEM1_MAX_SIZE			300*1024  						//最大管理内存300K, H7的AXI内存总共512KB
#define MEM1_ALLOC_TABLE_SIZE	MEM1_MAX_SIZE/MEM1_BLOCK_SIZE 	//内存表大小

//mem2内存参数设定.mem2是外部的SDRAM内存
#define MEM2_BLOCK_SIZE			128 	  						//内存块大小为128字节
#define MEM2_MAX_SIZE			28912 *1024  					//最大管理内存28912K,外扩SDRAM总共32MB,LTDC占了2MB,还剩30MB.
#define MEM2_ALLOC_TABLE_SIZE	MEM2_MAX_SIZE/MEM2_BLOCK_SIZE 	//内存表大小
		 
//mem3内存参数设定.mem3是H7内部的SRAM1+SRAM2内存
#define MEM3_BLOCK_SIZE			128  	  						//内存块大小为128字节
#define MEM3_MAX_SIZE			200 *1024  						//最大管理内存200K, H7的SRAM1+SRAM2共256KB
#define MEM3_ALLOC_TABLE_SIZE	MEM3_MAX_SIZE/MEM3_BLOCK_SIZE 	//内存表大小
		 
//mem4内存参数设定.mem4是H7内部的SRAM4内存
#define MEM4_BLOCK_SIZE			128  	  						//内存块大小为128字节
#define MEM4_MAX_SIZE			60 *1024  						//最大管理内存60K, H7的SRAM4共64KB
#define MEM4_ALLOC_TABLE_SIZE	MEM4_MAX_SIZE/MEM4_BLOCK_SIZE 	//内存表大小
		 
//mem5内存参数设定.mem5是H7内部的DTCM内存,此部分内存仅CPU和MDMA可以访问!!!!!!
#define MEM5_BLOCK_SIZE			128  	  						//内存块大小为128字节
#define MEM5_MAX_SIZE			64 *1024  						//最大管理内存120K,H7的DTCM共128KB
#define MEM5_ALLOC_TABLE_SIZE	MEM5_MAX_SIZE/MEM5_BLOCK_SIZE 	//内存表大小
		 
//mem6内存参数设定.mem6是H7内部的ITCM内存,此部分内存仅CPU和MDMA可以访问!!!!!!
#define MEM6_BLOCK_SIZE			128  	  						//内存块大小为128字节
#define MEM6_MAX_SIZE			60 *1024  						//最大管理内存60K,H7的ITCM共64KB
#define MEM6_ALLOC_TABLE_SIZE	MEM6_MAX_SIZE/MEM6_BLOCK_SIZE 	//内存表大小

#endif	// USE_STM32F4XX || USE_STM32H7XX

//内存管理控制器
struct _m_mallco_dev
{
	void (*init)(u8);  			// 初始化
	u8 (*perused)(u8); 			// 内存使用率
	u8 *membase[SRAMBANK]; 		// 内存池,管理SRAMBANK个区域的内存
	u32 *memmap[SRAMBANK];  	// 内存管理状态表
	u8 memrdy[SRAMBANK];   		// 内存管理是否就绪
};

/***********************************************/
// 用户外部调用函数
/***********************************************/
void my_mem_init(u8 memx);				 		// 内存管理初始化函数(外/内部调用)
u8 my_mem_perused(u8 memx);				 		// 获得内存使用率(外/内部调用) 

void myfree(u8 memx,void *ptr);  				// 内存释放(外部调用)
void *mymalloc(u8 memx,u32 size);				// 内存分配(外部调用)
void *myrealloc(u8 memx,void *ptr,u32 size);	// 重新分配(外部调用)
/***********************************************/
#endif


