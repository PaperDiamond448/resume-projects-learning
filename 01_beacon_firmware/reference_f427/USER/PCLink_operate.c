
#include <stdlib.h>
#include "PCLink_operate.h"

#if USE_STM32F1XX
#include "flash.h"
#endif

#if USE_STM32F4XX || USE_STM32H7XX
#if USE_EEPROM
#include "24lc02.h"
#endif	// USE_EEPROM
#if USE_STMFLASH
#include "flash_if.h"
#endif	// USE_STMFLASH
#if USE_SPIFLASH
#include "w25qxx.h"
#endif	// USE_SPIFLASH
#endif	// USE_STM32F4XX || USE_STM32H7XX

#if USE_STM32H7XX
#include "STM32H7_operate.h"
#endif	// USE_STM32H7XX

#if USE_ATOMIC
#include "TimeSync_operate.h"
#endif

#if USE_DS1302
#include "DS1302_operate.h"
#elif USE_DS3231
#include "DS3231_operate.h"
#endif

// 如果增减 SYSTEM_PARAM 中的参数或者顺序，务必更改初始化校验值
#define EEPROM_INIT_VALUE	0x0D		
#define DATA_CRC_MODE		CRC_NONE

/************************************************************************/
SYSTEM_PARAM g_stSys;
COMM_OBJECT* m_pCommDebug  = 0;	// debug串口
COMM_OBJECT* m_pCommObject = 0;	// 必须初始化方可与上位机通讯（不限于串口）
COMM_COMMAND  m_stHexCmdHold;	// 当前挂起的2进制数据传输操作命令
/************************************************************************/
bool m_bPcConnect 	  = false;	// 标志上位机物理接口连接状态，参数掉电不保存
bool m_bPcStatus  	  = false;	// 标志上位机软件接口联机状态，参数掉电不保存
bool m_bUpdateInit	  = false;	// 标志更新初始化状态
bool m_bFileUpload 	  = false;	// 上传文件状态
bool m_bFileDownload  = false;	// 下载文件状态
bool m_bSdFormat 	  = false;	// 接收到格式化指令状态
bool m_bSdDelDir 	  = false;	// 接收到删除文件夹指令状态
bool m_bSdDeleting 	  = false;	// 正在执行删除状态
bool m_bNetSendFlag	  = false;	// 网口发送状态，由于缓冲区不够大，当此状态有效时，表示发送缓冲区里的数据未发送完毕
bool m_bTimer4Enable  = false;	// 电源指示灯定时器启动状态

int	 m_uTimer4IntCnt  = 0;		// 电源指示灯定时器计数值
char* m_pNetBufTemp   = 0;		// 网口发送缓冲区的指针
int  m_nNetBufSize	  = 0;		// 网口发送缓冲区的剩余数据发送量

u32 m_uFileLoadSectors  = 0;			// 预读数据起始扇区
int m_nPreLoadBufSel    = 0;			// 当前预读缓冲区选择状态，0 or 1 or -1，（-1 代表不选择任何缓冲区）
u16 m_uFileLoadBufSel   = 0;			// 当前预读缓冲区序号 0 or 1
char* m_pFileLoadBuf[2] = { 0, 0 };		// 数据加载缓冲区
bool  m_bFileLoadRdy[2] = { 0, 0 };		// 数据加载缓冲区预读完毕状态
u32 m_uFileLoadBytes[2] = { 0, 0 };		// 数据加载缓冲区预读数据字节数
u32 m_uFileLoadFres[2]  = { 0, 0 };		// 数据加载缓冲区预读文件操作状态，FR_OK = 0 

DATA_PACK_STRUCT* m_pDataPack = 0;				// 文件传输的二进制数据包指针
RT_DATA_PACK_HEADER_STRUCT m_hRtDataPackHead;	// 实时采集的二进制数据包包头
/************************************************************************/
char  m_chFileHeader[MAX_HEADER_BUF_SIZE];	// 文件头缓存
char* m_pFileHeaderDBuf = 0;				// 文件头数据区地址，必须在初始化文件头时进行同步赋值
/************************************************************************/

u32 m_uAtomicCnt = 0;					// 同步采集，用于代替原子钟计时器的计数器
u32 m_uPwTimeCnt = 0;					// 上电定时器，只在相对时间定时上电使用，用于记录历时时间（秒）
u32 m_uPwTimeSetYMD = 0;				// 上电时间设定值：年月日 绝对时间 20171010	相对时间 <=2		
u32 m_uPwTimeSetHMS = 0;				// 上电时间设定值：时分秒 绝对时间 195959 	相对时间 总秒数

/************************************************************************/
/*以下函数禁止外部调用*/
#if USE_FATFS
bool PCLINK_SendHexMsg(DATA_PACK_STRUCT* pPack, int nCmd, u32 wParam);
bool PCLINK_SendHexData(DATA_PACK_STRUCT* pPack, int nCmd, u32 wParam);
void PCLINK_Echo_SD_Upload_Fname(FILINFO* pFt, COMM_COMMAND* stCmd);		// 发送当前检索到的文件或文件夹名

u32 DataPackHeaderCrcSum(DATA_PACK_STRUCT* pPack);							// 计算数据帧包头校验值
int GetCurLoadBufBlocks(u32 uPreLoadBytes);									// 计算当前缓冲区数据分块数	
int GetCurLoadBufBlockBytes(u32 uPreloadBytes, int iBlock);					// 计算当前缓冲区数据块的数据字节数
u32 GetCurLoadBufBlockCrc(DATA_PACK_STRUCT* pPack, void* pBuf, int nLen);	// 计算当前缓冲区数据块的校验结果，并将数据搬移至发送缓冲区中
#endif
/************************************************************************/

void SysParam_Load(void)
{
	// 读取Flash中存储的设备配置参数
	u8 uFlag = 0;
	void* pEEPROM = 0;
#if USE_STM32F1XX
	// 用Flash模拟EEPROM,获取系统配置参数
	int nSize = (sizeof(g_stSys)+1)/2;
	pEEPROM = (u16*)mymalloc(SRAMIN, nSize*2);
	STMFLASH_Read(FLASH_SAVE_ADDR, pEEPROM, nSize);
#elif USE_STM32F4XX || USE_STM32H7XX
	int nSize = sizeof(g_stSys);
	#if USE_FILE//SD卡
		pEEPROM = (u8*)mymalloc(SRAMIN, nSize);
		if ( pEEPROM )
		{
			GetConfigParam((char*)pEEPROM, nSize);			// 从文件中读取配置文件信息
		}
	#elif USE_EEPROM// 外挂EEPROM
		pEEPROM = (u8*)mymalloc(SRAMIN, nSize);//从哪块内存池分配 分配的内存大小
		if ( nSize <= EE_TYPE )
		{
			// 需要判断所读取的参数字节数小于EEPROM的容量
			AT24CXX_Initlize();								// IIC接口初始化
			AT24CXX_Read(0, pEEPROM, nSize);				// 从外挂EEPROM中读取系统参数
		}
	#elif USE_BACKMEM
		pEEPROM = (u8*)mymalloc(SRAMIN, nSize);
		if ( nSize <= MAX_ROM_SIZE )
		{
			// 需要判断所读取的参数字节数小于器件后备存储器的容量
			RTC_BackMemRead(0, pEEPROM, sizeof(g_stSys));	// 从后备存储器中读取系统参数
		}
	#elif USE_STMFLASH
		nSize = sizeof(g_stSys);
		nSize = nSize%4 ? (nSize/4+1) : nSize/4;		// 4字节一个单位  三目运算符：条件 ? 真值 : 假值
		pEEPROM = (u32*)mymalloc(SRAMIN, nSize*4);		// 按照字节数创建缓冲区
		if ( pEEPROM )
		{
			int res = STMFLASH_Read(FLASH_SAVE_ADDR, pEEPROM, nSize);
			if ( res != 0 ) printf(" Flash save error in SysParam_Load().\r\n");
		}
	#endif
	#if USE_SPIFLASH
		W25QXX_Init();									// W25QXX初始化
		while(W25QXX_ReadID() != W25Q128)				// 检测不到W25Q128
		{	delay(1000); ShineRed(); }					// 红灯提示错误
	#endif
#endif
	uFlag = *((u8*)pEEPROM);	// 读取第一个字节，判断是否等于初始化完成标志
	if (uFlag != EEPROM_INIT_VALUE || m_bForceInitParam)//Flash 无效 → 初始化 g_stSys → 写入 Flash
	{
		// Flash未初始化，先初始化
		g_stSys.bInit = EEPROM_INIT_VALUE;
		g_stSys.fProgramVersion = 1.0;//设备版本号
		g_stSys.uProgramTime    = 0;//程序烧录时间
		g_stSys.uCpuStep 		= m_uCpuIdleStep;//CPU空闲时间计数步进值
		g_stSys.nAddr 			= 0;
		g_stSys.nID   			= 0;
		g_stSys.nSn   			= 0;//附属传感器序号
		
		g_stSys.nPwYMD = 0;	g_stSys.nPwHMS = 0;
		g_stSys.uSampleTime = 10; g_stSys.uSampleGap = 5;
		g_stSys.IP1 = 192; g_stSys.IP2 = 168;	g_stSys.IP3 = 1; g_stSys.IP4 = 168;
		
		g_stSys.uMode      		= 0;
		g_stSys.nPwMode    		= PwMode_RealTime;//默认系统上电采集
		g_stSys.nPwOffMode 		= 0;
		g_stSys.nPwYMD	   = 0; g_stSys.nPwHMS	  = 0;
		g_stSys.nPwOffYMD  = 0; g_stSys.nPwOffHMS = 0;
		g_stSys.nExCommBaud  	= tBaud9600;
		g_stSys.nModify    		= 0;
		
		g_stSys.nSampleRate 	= 0;
		g_stSys.nSampleGain 	= 0;
		g_stSys.nSampleCh   	= 1;
		g_stSys.nSampleFilter	= 200;		// 单位： 100Hz
		g_stSys.nSampleUnit		= Unit_Bin;
		g_stSys.nDataType		= DATA_24;
		g_stSys.fSensitivity	= -196;
		g_stSys.fLiVoltCoef1	= 1.0f;
		g_stSys.fLiVoltCoef2	= 0.0f;
		
		m_bForceInitParam = false;			
		Device_InitDefaultParam();			// 初始化设备特有参数
		SysParam_Save();					// 第一次初始化后保存默认参数
	}
	else//如果flash已经初始化过了，就把flash中的数据复制到g_stSys中
	{
		memcpy( (u8*)&g_stSys, (u8*)pEEPROM, sizeof(g_stSys) );//pEEPROM 复制到 g_stSys
	}
	
	// 增加锂电池电压修正参数校验，如果这个值离谱了，就帮它掰回正常值"
	if ( g_stSys.fLiVoltCoef1 > 1.5f || g_stSys.fLiVoltCoef1 < 0.5f )  g_stSys.fLiVoltCoef1 = 1.0f;
	if ( g_stSys.fLiVoltCoef2 > 1.0f || g_stSys.fLiVoltCoef2 < -1.0f ) g_stSys.fLiVoltCoef2 = 0.0f;

	// 更新上电模式和时间相关参数
	SetPwTimeCnt(g_stSys.nPwYMD, g_stSys.nPwHMS, g_stSys.uSampleTime, g_stSys.uSampleGap);	
		
#if USE_FATFS && (DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceILogger)
	Device_UpdateSampleParam();												// 更新采样参数
	Device_InitFileHeader((char*)&m_chFileHeader, MAX_HEADER_BUF_SIZE);		// 初始化文件头缓冲区数据，提高采集数据过程中的速度和效率
#endif
	
	if ( pEEPROM ) myfree(SRAMIN, pEEPROM);		// 释放临时缓存
}

void SysParam_Save(void)//把 g_stSys 这个结构体里的所有参数，保存到 Flash 里
{
	// 将设备配置参数写入Flash中存储
#if USE_STM32F1XX
	int nSize = (sizeof(g_stSys)+1)/2;
	STMFLASH_Write(FLASH_SAVE_ADDR, (u16*)&g_stSys, nSize);
#elif USE_STM32F4XX || USE_STM32H7XX
	#if USE_FILE
		SetConfigParam();	// 将配置数据保存至文件中
	#elif USE_EEPROM
		int nSize = sizeof(g_stSys);
		if ( nSize < EE_TYPE )
		{
			// 需要判断所写入的参数字节数小于EEPROM的容量
			AT24CXX_Write(0, (u8*)&g_stSys, nSize);
		}
	#elif USE_BACKMEM
		int nSize = sizeof(g_stSys);
		if (  nSize < MAX_ROM_SIZE )
		{
			// 需要判断所写入的参数字节数小于器件后备存储器的容量
			RTC_BackMemWrite(0, (u8*)&g_stSys, nSize);
		}
	#elif USE_STMFLASH
		int res=0, retry_cnt=0;
		u32* pEEPROM;	u32* pBuffer;
	
		int nSize = (sizeof(g_stSys) + 3) / 4;		// 4字节为一个写入单位，算出要写几个u32单位  +3是向上取整
		pEEPROM = (u32*)mymalloc(SRAMIN, nSize*4);	// 按照字节数创建缓冲区
		pBuffer = (u32*)&g_stSys;//记住地址

	#define FLASH_SAVE_RETRY_TIMES    3	// 失败重试 3 次
		if ( pEEPROM )
		{
			// 拷贝配置数据
			memset(pEEPROM, 0, nSize*4);//先把缓冲区清零
			memcpy(pEEPROM, pBuffer, sizeof(g_stSys));//再把gstsys复制进去   就是把g_stSys放到了这个新的缓冲区里面

			// ====================== 重试写入 ======================
			while(retry_cnt < FLASH_SAVE_RETRY_TIMES)
			{
				// 写入 Flash
				res = STMFLASH_Write(FLASH_SAVE_ADDR, pEEPROM, nSize);
				if(res == 0)  	// 写入成功
				{
					// 回读校验
					u32 verify_buf[nSize];
					res = STMFLASH_Read(FLASH_SAVE_ADDR, verify_buf, nSize);

					if(res == 0 && memcmp(pEEPROM, verify_buf, sizeof(g_stSys)) == 0)//memory compare函数
					/*memcmp(指针A, 指针B, 字节数)
                它就是把A和B指向的内存，逐字节对比，然后：

                完全一样 → 返回 0
                不一样 → 返回非0*/
						break;	// 写入 + 校验成功
				}
				retry_cnt++; delay(5);	// 简单延时，防止连续操作太快
			}

			// 最终失败判断
			if(retry_cnt >= FLASH_SAVE_RETRY_TIMES)
				printf(" Flash save failed after %d retries !!!\r\n", FLASH_SAVE_RETRY_TIMES);

			myfree(SRAMIN, pEEPROM);
		}
	#endif
#endif

	// 初始化文件头缓冲区数据，提高采集数据过程中的速度和效率
#if USE_FATFS && (DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceILogger)
	Device_UpdateSampleParam();												// 更新采样参数
	Device_InitFileHeader((char*)&m_chFileHeader, MAX_HEADER_BUF_SIZE);		// 初始化文件头缓冲区数据，提高采集数据过程中的速度和效率
#endif
}

bool GetConfigParam(char* pBuf, int nLen)
{
	// 从配置文件读取配置参数数据
	bool bResult = false;
#if USE_FATFS
	if ( IsConfigDataFileExist() )
	{
		UINT bt;
		FIL hFileConfig;	// 文件结构体
		if (f_open(&hFileConfig, "0://Config.dat", FA_READ) == FR_OK)
		{
			// 读取配置参数
			if ( f_read(&hFileConfig, pBuf, nLen, &bt) == FR_OK )		// 读取配置数据
				bResult = true;
			else
				*pBuf = EEPROM_INIT_VALUE + 1;							// 数据读取错误，返回非正常状态值
			f_close(&hFileConfig);
			return bResult;
		}
		printf(" The file for config is read unsuccessful...\n");
	}
#endif
	return bResult;
}

bool SetConfigParam(void)
{
	// 将配置参数数据写入配置文件
	bool bResult = false;
#if USE_FATFS
	if ( IsConfigDataFileExist() )
	{
		UINT bt;
		FIL hFileConfig;	// 文件结构体
		if (f_open(&hFileConfig, "0://Config.dat", FA_WRITE) == FR_OK)
		{
			// 将配置参数写入文件
			if (f_write(&hFileConfig, &g_stSys, sizeof(g_stSys), &bt) == FR_OK)
				bResult = true;
			f_close(&hFileConfig);
			return bResult;
		}
		printf(" The file for config is write unsuccessful...\n");
	}
#endif
	return bResult;
}

int GetSizeOfData(int nDataType)
{
	// 根据数据类型，获得该类型对应的字节数
	switch( nDataType )
	{
		case DATA_FLOAT : return 4;
		case DATA_U32	: return 4;
		case DATA_U24	: return 3;
		case DATA_U16	: return 2;
		case DATA_U8	: return 1;
		case DATA_32	: return 4;
		case DATA_24	: return 3;
		case DATA_16	: return 2;
		case DATA_8		: return 1;
		case DATA_ASCII : return 1;
		case DATA_INT	: return sizeof(int);
		case DATA_DOUBLE: return sizeof(double);
		case DATA_U32L	: return 4;
		case DATA_U24L	: return 3;
		case DATA_U16L	: return 2;
		case DATA_32L	: return 4;
		case DATA_24L	: return 3;
		case DATA_16L	: return 2;
		default : return 0;
	}
}

void PCLINK_Init()
{
#if PCLINK_Comm == COMM1
	m_pCommObject = m_pComm1;		// 对应的串口对象指针
#elif PCLINK_Comm == COMM2
	m_pCommObject = m_pComm2;		// 对应的串口对象指针
#elif PCLINK_Comm == COMM3
	m_pCommObject = m_pComm3;		// 对应的串口对象指针
#elif PCLINK_Comm == COMM4
	m_pCommObject = m_pComm4;		// 对应的串口对象指针
#elif PCLINK_Comm == COMM6
	m_pCommObject = m_pComm6;		// 对应的串口对象指针
#endif	
	
#if DEBUG_Comm == COMM1
	m_pCommDebug = m_pComm1;
#elif DEBUG_Comm == COMM2
	m_pCommDebug = m_pComm2;
#elif DEBUG_Comm == COMM3
	m_pCommDebug = m_pComm3;
#elif DEBUG_Comm == COMM4
	m_pCommDebug = m_pComm4;
#elif DEBUG_Comm == COMM6
	m_pCommDebug = m_pComm6;
#endif
}

void PCLINK_DeInit()
{
	// 退出连接时，清理
	m_bPcConnect = false;
#if USE_NET
#if	USE_UDP_MODE
	udp_client_disconn();
#endif
#endif
#if USE_FATFS
	PCLINK_ReleaseFileBuf();
#endif
}

void PCLINK_DebugInfo(void)
{
	// 程序初始化时的消息打印输出
#if DEBUG_PRINTF
	/* Output a message on Hyperterminal using printf function */
	printf("\n\n");
	printf(" System start initializing.\r\n");
	printf(" ("__DATE__ " - " __TIME__ ").\r\n");
	printf(" System core clock is : %dM.\r\n", SystemCoreClock/1000000);
	if (m_pCommDebug == 0)
		printf(" Error: No UART configured for debug printf.\r\n");
	else
		printf(" Debug printf configured using UART%d.\r\n", m_pCommDebug->nCommSn);
#endif
}

void Device_OnTimer2(void)
{
	// 响应TIMER2定时器
#if USE_FATFS
	if ( m_bFileUpload )
	{
		// 下载文件过程中定时填充数据缓冲区
		PCLINK_PreLoadFileData();
		
		// 此处增加发送超时退出机制，避免上位机无响应时
		// 下位机始终处于上传文件的状态，进而影响上位机再次连接
		if ( m_hRtcCnt.uPCLinkIdle > 10)
			PCLINK_Decode_SD_CloseFile(0);
	}
	else if ( m_bFileDownload )
	{
		if ( m_stHexCmdHold.bCmdHold )
		{
			PCLINK_Decode_SD_WriteFile(&m_stHexCmdHold);	// 将数据写入文件中
			m_stHexCmdHold.bCmdHold = false;				// 清除指令
		}
	}
	else if (m_bSdDelDir || m_bSdFormat)
	{
		// 响应删除文件或文件夹
		OnEchoFormat();
	}
#if DEVICE_TYPE == tDeviceIListenM && DEVICE_SUBTYPE/100 == iListenM_Store
	else if (m_bArrayAcqStart)
	{
		// 如果网口数据缓冲区有未发送完毕的数据，连续发送
		PCLINK_SendData2();		
	}
#endif
	else
#endif
	{
		// 无任何文件操作自动终止定时器2
		TIMER2_Config(0, false);	
	}
}

void Device_OnTimer4(void)
{
	// 响应TIMER4定时器
	if (++m_uTimer4IntCnt >= 100) m_uTimer4IntCnt = 0;
	if ( !m_bPcConnect && !m_bStartStatus )
	{
		// 计算当前充电电量百分比，计算当前计数器值百分比
		// 充满按照90%比例计算，充满前红绿灯保持闪烁状态，避免常亮
		int nPercent = GetLiChargePercent();
		if ( nPercent < 94)
		{
			if (m_uTimer4IntCnt < nPercent)
			{
				// 为了显示充电状态，根据电压点亮红绿灯
				SetArmLedG(true);	
				SetArmLedR(true);
			}
			else
			{
				SetArmLedG(false);	
				SetArmLedR(false);
			}
		}
		else
		{
			// 充满电后，直接点亮绿灯
			SetArmLedG(true);	
			SetArmLedR(false);
		}
	}
	else
	{
		// 联网后不再显示充电状态
		SetArmLedG(false);	
		SetArmLedR(false);
		m_uTimer4IntCnt = 0;
		m_bTimer4Enable = false;
		m_stLedTrig.bShowBat = false;	// 停止显示电量
		TIMER4_Config(0, false);		
	}
}

void PCLINK_SendMsg(COMM_OBJECT* pComm)
{
	// 根据定义的串口发送信息
	int nlen;
	if (pComm == 0) return;
	nlen = strlen(pComm->chTxBuf);
	
	if (nlen > MAX_COMM_BUF_SIZE)
	{
		printf(" Error: can't send command message more than %d byte.\n", MAX_COMM_BUF_SIZE);
		return;
	}
	else if ( nlen <= 0 ) return;
	
#if USE_CRC_CRC16
	if (nlen < MAX_COMM_BUF_SIZE - 4)
	{
		// 必须确保缓冲区有空间容纳校验值
		// 校验$和*之间的数据，并替换 ‘*’结尾字符 为 '#'
		int i;
		bool bStart = false;
		char chCrc[12] = {0};
		unsigned short crc = 0;
		
		for(i=0; i<nlen-1; i++)
		{
			if (pComm->chTxBuf[i] == '$') 
				bStart = true;
			else
			{
				if (bStart && pComm->chTxBuf[i] != '*')
				{
					crc = update_crc_16(crc, pComm->chTxBuf[i]);
				}
				else if (bStart && pComm->chTxBuf[i] == '*')
				{
					pComm->chTxBuf[i] = '#';
					pComm->chTxBuf[i+1] = 0;
					break;
				}
			}			
		}
		nlen = strlen(pComm->chTxBuf);
		sprintf(chCrc, "%04X*\n", crc);
		strcat(pComm->chTxBuf, chCrc);
	}
#endif
#if USE_NET
	Net_SendData(pComm->chTxBuf, nlen);		//  网口不能发送0字节空包
#else
	if (pComm->nCommSn == COMM1) 
		Comm1_PutStr(pComm->chTxBuf);
	else if (pComm->nCommSn == COMM2) 
		Comm2_PutStr(pComm->chTxBuf);
	else if (pComm->nCommSn == COMM3) 
		Comm3_PutStr(pComm->chTxBuf);
#if USE_STM32F4XX || USE_STM32H7XX
	else if (pComm->nCommSn == COMM4) 
		Comm4_PutStr(pComm->chTxBuf);
	else if (pComm->nCommSn == COMM6) 
		Comm6_PutStr(pComm->chTxBuf);
	else if (pComm->nCommSn == COMM7) 
		Comm7_PutStr(pComm->chTxBuf);	
#endif
#endif
}

bool PCLINK_SendData(char* buf, u32 nBuf)
{
	// 上传数据，在网口模式下，数据量需要小于缓冲区大小
#if USE_NET
	if (buf && nBuf > 0)				
	{
		//  网口不能发送0字节空包
		int nSend = Net_SendData(buf, nBuf); return true;
	} else return false;
#else
#if PCLINK_Comm == COMM1
	#if USE_COMM_DMA
	if (m_pComm1->bEnable && buf && nBuf > 0)
	{
		USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE); 
		Comm1_DMA_TxConfig((uint32_t*)buf, nBuf);
		while(!m_bComm1TxDmaEnd);
		USART_DMACmd(USART1, USART_DMAReq_Tx, DISABLE);
		return true;	
	}	
	#else
		Comm1_PutBuf(buf, nBuf); return true;
	#endif
#elif PCLINK_Comm == COMM2
	#if USE_COMM_DMA
	if (m_pComm2->bEnable && buf && nBuf > 0)
	{
		USART_DMACmd(USART2, USART_DMAReq_Tx, ENABLE); 
		Comm2_DMA_TxConfig((uint32_t*)buf, nBuf);
		while(!m_bComm2TxDmaEnd);
		USART_DMACmd(USART2, USART_DMAReq_Tx, DISABLE); 
		return true;
	}
	#else
		Comm2_PutBuf(buf, nBuf); return true;
	#endif
#elif PCLINK_Comm == COMM3
	#if USE_COMM_DMA
	if (m_pComm3->bEnable && buf && nBuf > 0)
	{
		USART_DMACmd(USART3, USART_DMAReq_Tx, ENABLE); 
		Comm3_DMA_TxConfig((uint32_t*)buf, nBuf);
		while(!m_bComm3TxDmaEnd);
		USART_DMACmd(USART3, USART_DMAReq_Tx, DISABLE);
		return true;
	}
	#else
		Comm3_PutBuf(buf, nBuf); return true;
	#endif
#elif PCLINK_Comm == COMM4
	Comm4_PutBuf(buf, nBuf); return true;
#elif PCLINK_Comm == COMM6
	Comm6_PutBuf(buf, nBuf); return true;
#elif PCLINK_Comm == COMM7
	Comm7_PutBuf(buf, nBuf); return true;
#else
	return false;
#endif
#endif
}

bool PCLINK_SendData2(void)
{
	// 上传数据，在网口模式下，发送缓冲区剩余的数据
	// 仅在网口发送缓冲区还有数据未发送的条件下调用
#if USE_NET
	if (m_bPcConnect && m_bStartStatus && m_bNetSendFlag && m_pNetBufTemp && m_nNetBufSize > 0)
	{
		int nSend;								
		m_bNetSendFlag = true;									// 保持发送标志位
		nSend = Net_SendData(m_pNetBufTemp, m_nNetBufSize);					
		if (nSend < m_nNetBufSize)
		{
			m_pNetBufTemp = m_pNetBufTemp + nSend;			// 保存当前的发送缓冲区指针
			m_nNetBufSize = m_nNetBufSize - nSend;			// 保存剩余发送缓冲数据
		}
		else
		{
			m_bNetSendFlag = false;		// 可以完全发送，则清除发送状态标志位
			m_pNetBufTemp = 0;
			m_nNetBufSize = 0;
		}
	}
	else
	{
		m_bNetSendFlag = false;			// 可以完全发送，则清除发送状态标志位
		m_pNetBufTemp = 0;
		m_nNetBufSize = 0;
	}
	return true;
#else
	return false;
#endif
}

bool PCLINK_RecvData(char* buf, u32 nBuf)
{
	// 下传数据，在网口模式下接收 DATA_PACK_STRUCT 结构的数据 
	if ( buf && nBuf > 0 && m_pCommObject)
	{
		char* pBuf = 0;
		int nSize = sizeof(DATA_PACK_STRUCT);
		if ( m_uFileLoadBufSel == 0 && m_pFileLoadBuf[0] )
		{
			pBuf = m_pFileLoadBuf[0];
		}
		else if ( m_uFileLoadBufSel == 1 && m_pFileLoadBuf[1] )
		{
			pBuf = m_pFileLoadBuf[1];
		}
		
		if ( pBuf && m_pCommObject->uCommRx+nBuf <= nSize )
		{
			memcpy(pBuf+m_pCommObject->uCommRx, buf, nBuf);
			m_pCommObject->uCommRx += nBuf;
		}
		else if ( pBuf && m_pCommObject->uCommRx+nBuf > nSize )
		{
			memcpy(pBuf+m_pCommObject->uCommRx, buf, nSize - m_pCommObject->uCommRx );
			m_pCommObject->uCommRx = nSize;
		}
		else
		{
			
		}
		
		if ( m_pCommObject->uCommRx >= nSize )
		{
			DATA_PACK_STRUCT* pPack = (DATA_PACK_STRUCT*)pBuf;
			if ( pPack->uHeader == DATA_HEADER_KEYWORD )
			{
				m_stHexCmdHold.nAddr  = g_stSys.nAddr;
				m_stHexCmdHold.nCmd   = pPack->nCmd;
				m_stHexCmdHold.wParam = pPack->wParam;
				m_stHexCmdHold.lParam = m_uFileLoadBufSel;	// 保存进行存储的缓冲区选择值
				m_stHexCmdHold.bCmdHold	= true;				// 挂起上位机指令，等待设备响应操作
								
				m_pCommObject->bRdy = true;					// 标志数据已接收
				m_uFileLoadBufSel = m_uFileLoadBufSel == 0 ? 1 : 0;		//  切换缓冲区
				return true;
			}
			else
			{
				// 数据包校验错误，请求重传
				m_stHexCmdHold.nAddr  = g_stSys.nAddr;
				m_stHexCmdHold.nCmd   = pPack->nCmd;
				m_stHexCmdHold.wParam = Upload_Reload;
				m_stHexCmdHold.lParam = m_uFileLoadSectors;	// 请求重传的数据起始位置
				m_stHexCmdHold.bCmdHold	= true;				// 挂起上位机指令，等待设备响应操作
			}
		}
	}
	return false;
}

#if USE_FATFS
bool PCLINK_SendHexMsg(DATA_PACK_STRUCT* pPack, int nCmd, u32 wParam)
{
	if (pPack == NULL) return false;
	pPack->nCmd   = nCmd;
	pPack->wParam = wParam;
	pPack->uCrc   = 0;
	pPack->uPackLen = sizeof(DATA_PACK_STRUCT) - MAX_LOAD_BUF;
	pPack->uCrcSum  = DataPackHeaderCrcSum(pPack);
	return PCLINK_SendData((char*)pPack, pPack->uPackLen);			// 发送数据
}

bool PCLINK_SendHexData(DATA_PACK_STRUCT* pPack, int nCmd, u32 wParam)
{
	if (pPack == NULL) return false;
	pPack->nCmd    = nCmd;
	pPack->wParam  = wParam;
	pPack->uCrcSum = DataPackHeaderCrcSum(pPack);
	return PCLINK_SendData((char*)pPack, pPack->uPackLen);			// 发送数据
}
#endif // USE_FATFS

COMM_OBJECT* PCLINK_SetCommObj(COMM_OBJECT* pCommObj)
{
	// 设置 PCLINK 串口对象，返回值是为了对称
	assert_param(pCommObj);
	m_pCommObject = pCommObj;
	return m_pCommObject;
}

COMM_OBJECT* PCLINK_GetCommObj(void)
{
	return m_pCommObject;
}

void PCLINK_Send_PcPwEn(bool bEn)
{
	// 发送指令上电或者下电
	assert_param(m_pCommObject);
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,ok*\n",
		g_stSys.nAddr, Device_Cmd_Set_PcPowerEn, bEn, 0);
	PCLINK_SendMsg(m_pCommObject);
}

void PCLINK_Send_PcSoftEn(bool bEn)
{
	// 发送指令软件启动或者关闭
	assert_param(m_pCommObject);
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,ok*\n",
		g_stSys.nAddr, Device_Cmd_Set_PcSoftEn, bEn, 0);
	PCLINK_SendMsg(m_pCommObject);
}

void PCLINK_Send_DebugSoftEn(bool bEn)
{
	// 发送指令软件启动或者关闭
#if DEVICE_TYPE == tDeviceIPowerCtrl
	// 本指令特定tDeviceIPowerCtrl，且通过Debug方式发送
	printf(" $%d,%d,%d,%d,ok*\n", g_stSys.nAddr, Device_Cmd_Set_PcSoftEn, bEn, 0);
#endif
}

void PCLINK_Send_WatchDog(void)
{
	// 发送看门狗警告指令，等待回应复位看门狗
	assert_param(m_pCommObject);
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,ok*\r\n",
		g_stSys.nAddr, Device_Cmd_WatchDog, m_nRtcYMD, m_nRtcHMS);
	PCLINK_SendMsg(m_pCommObject);
}

void PCLINK_Send_SleepCmd(bool bSleep, u32 uSec)
{
	// 设备发送休眠指令
	// bSleep = true 休眠指定时间然后启动， bSleep = false, 上电工作指定时间然后休眠

}

void PCLINK_Send_GetValue(int nAddr, int wParam, int lParam)
{
	// 给设备发送获取数据指令		
#if DEVICE_TYPE == tDeviceILogger
	assert_param(m_pCommObject);
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d*\r\n",
		nAddr,  Device_Cmd_Get_TestData, wParam, lParam);
	PCLINK_SendMsg(m_pCommObject);
#endif
}

void PCLINK_Echo(COMM_COMMAND stCmd)
{
	if ( m_pCommObject )
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%lu,%lu,ok*\r\n",
			stCmd.nAddr, stCmd.nCmd, stCmd.wParam, stCmd.lParam);
		PCLINK_SendMsg(m_pCommObject);
	}
}

void PCLINK_EchoDebug(COMM_COMMAND stCmd)
{
	// 响应 Debug消息，无特殊处理，直接回应4字段指令
	if ( m_pCommDebug )
	{
		sprintf(m_pCommDebug->chTxBuf, "-> $%d,%d,%lu,%lu,ok*\r\n",
			stCmd.nAddr, stCmd.nCmd, stCmd.wParam, stCmd.lParam);
		PCLINK_SendMsg(m_pCommDebug);
	}
}

void PCLINK_EchoASC(int nCmd, u32 wParam, u32 lParam)
{
	// 发送二进制指令
	if ( m_pCommObject )
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%u,%u,ok*\r\n",
			g_stSys.nAddr, nCmd, wParam, lParam);
		PCLINK_SendMsg(m_pCommObject);
	}
}

void PCLINK_EchoHex(int nCmd, u32 wParam, u32 lParam)
{
	// 发送二进制指令
#if DEVICE_TYPE == tDeviceIListenS 		|| DEVICE_TYPE == tDeviceIListenM	|| DEVICE_TYPE == tDeviceIListenN || \
	DEVICE_TYPE == tDeviceIPowerProbe 	|| DEVICE_TYPE == tDeviceIUTM 		|| DEVICE_TYPE == tDeviceIUDM
	if ( m_pCommObject )
	{
		PCLINK_InitCmdHeader(m_pCommObject->chTxBuf, MAX_RT_DATA_PACK_HEADER, nCmd, wParam, lParam);	// 初始化发送数据包头
		PCLINK_SendData(m_pCommObject->chTxBuf, MAX_RT_DATA_PACK_HEADER);								// 仅发送包头
	}
#endif
}

void PCLINK_Echo_Link(COMM_COMMAND stCmd)//建立连接 → 重置状态 → 打包设备信息 → 发送给上位机，连接的瞬间，只发一次
{//主要内容是设备信息
	char chBuf[MAX_COMM_BUF_SIZE] = {0};
	assert_param(m_pCommObject);
	m_bPcConnect 	= true;		// 仅标志接口已连通
	m_bPcStatus 	= true;		// 设备和上位机程序连接状态
	m_bFileUpload 	= false;//没在上传文件
	m_bFileDownload = false;//没在下载文件
	m_bSdDeleting 	= false;//没在删除文件
	m_bSdDelDir		= false;//没在删除文件夹
	m_bSdFormat 	= false;//没在格式化

	// 响应设备连接消息，把消息头写进发送缓冲区，格式可能是：$1,0,5,2,101,
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%u,%d,", stCmd.nAddr, stCmd.nCmd, g_stSys.nAddr, g_stSys.uMode, g_stSys.nID);

	// 获取设备信息
#if DEVICE_TYPE == tDeviceIListenS 		|| DEVICE_TYPE == tDeviceIListenM	|| DEVICE_TYPE == tDeviceIListenN 	|| \
	DEVICE_TYPE == tDeviceIPowerProbe	|| DEVICE_TYPE == tDeviceIUTM 		|| DEVICE_TYPE == tDeviceIUDM		|| \
	DEVICE_TYPE == tDeviceILogger		|| DEVICE_TYPE == tDeviceISensor	|| DEVICE_TYPE == tDeviceISigCond	|| \
	DEVICE_TYPE == tDeviceShortBaseLine	|| DEVICE_TYPE == tDeviceIBeacon	|| \
	DEVICE_TYPE == tDeviceIPowerAmp		|| DEVICE_TYPE == tDeviceITimeRefModel
	Device_Echo_Link(chBuf);//复杂设备有专门的函数
#else
	sprintf(chBuf, "%d,%d,%d,%d,%d,ok*\r\n", DEVICE_TYPE, DEVICE_SUBTYPE, g_stSys.nPwMode, g_stSys.nPwYMD, g_stSys.nPwHMS);
#endif
	strcat(m_pCommObject->chTxBuf, chBuf);//strcat就是把把源字符串接到目标字符串的屁股后面，strcat(目标字符串, 源字符串)
	//chTxBuf 里已经有了消息头，chBuf 里有设备详细信息
	if (m_pCommObject->bRS485)//RS485 是一种串口通信标准，它允许多台设备连接在同一条总线上进行通信，但由于总线共享的特性，可能会出现数据冲突的情况
	{
		PCLINK_SendMsg(m_pCommObject);delay(50); 	// 由于采用RS485接口的设备可能在回应消息时由于连接指令多余字符的影响造成总线冲突
		PCLINK_SendMsg(m_pCommObject);delay(50);	// 因此连续发送两条指令，以便于上位机能够正确接收回执消息
	}
	else
	{
		PCLINK_SendMsg(m_pCommObject);delay(100);	// 增加连接指令延迟时间，以便于某些设备进行特殊处理
	}
}

void PCLINK_Echo_Info(bool bInit)//连接后可以随时发，上位机想要就问
{//主要内容是设备当前运行状况
	// 响应设备参数消息
	assert_param(m_pCommObject);
#if USE_FATFS//如果设备正在上传文件、下载文件、删文件、格式化SD卡，就直接 return 不回复
	if ( m_bFileUpload || m_bFileDownload || m_bSdDeleting || m_bSdFormat ) return;
#endif
#if DEVICE_TYPE == tDeviceIListenS 	 || DEVICE_TYPE == tDeviceIListenM		|| DEVICE_TYPE == tDeviceILogger || \
	DEVICE_TYPE == tDeviceITimeRef 	 || DEVICE_TYPE == tDeviceITimeRefModel || \
	DEVICE_TYPE == tDeviceISigCond	 || DEVICE_TYPE == tDeviceShortBaseLine || DEVICE_TYPE == tDeviceIBeacon || \
	DEVICE_TYPE == tDeviceIPowerAmp	 || DEVICE_TYPE == tDeviceIPowerCtrl
	Device_Echo_Info(bInit);
#else
	// 此处由 m_bPcConnect 更改为 m_bPcStatus
	// 因为有些类型的接口可能状态为连接，但是没有和上位机程序连通
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,%u,%u,%u,%.2fV*\r\n",
		g_stSys.nAddr, Device_Cmd_Get_Info,	bInit, m_bPcStatus,	m_nRtcYMD, m_nRtcWeek, m_nRtcHMS, m_fLiPowerVoltage);
	PCLINK_SendMsg(m_pCommObject);
#endif
}

void PCLINK_Echo_SetDeviceVersion(void)
{
	// 返回设备版本信息
	assert_param(m_pCommObject);
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%u,%u,%.3f,ok*\r\n", 
		g_stSys.nAddr, Device_Cmd_Set_DeviceVersion, g_stSys.uProgramTime, 0, g_stSys.fProgramVersion);
	PCLINK_SendMsg(m_pCommObject);
}

void PCLINK_Echo_SetIP(COMM_COMMAND stCmd)
{
	// 返回设备IP地址信息
	assert_param(m_pCommObject);
	if (stCmd.wParam == 0)
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%lu,%lu,%u,%u,%u,%u,ok*\r\n",
			stCmd.nAddr, Device_Cmd_Set_IP, stCmd.wParam, stCmd.lParam,
			g_stSys.IP1, g_stSys.IP2, g_stSys.IP3, g_stSys.IP4);
		PCLINK_SendMsg(m_pCommObject);
	}
}

void PCLINK_Echo_SetSampleGain(COMM_COMMAND stCmd)
{
	// 返回设备增益参数
#if DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM
	assert_param(m_pCommObject);
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%lu,%lu,%d,%d,%d,ok*\r\n",
		stCmd.nAddr, Device_Cmd_Set_SampleGain, stCmd.wParam, stCmd.lParam,
		g_stSys.nSampleGainStep, g_stSys.nSampleGainOrigin, GAIN_STEP_QTY);
	PCLINK_SendMsg(m_pCommObject);
#else
	PCLINK_Echo(stCmd); 
#endif
}

void PCLINK_Echo_SetPowerTime(void)
{
	// 返回设备上电模式、时间参数
	if ( m_pCommObject )
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,%d,%u,%u,ok*\r\n",
			g_stSys.nAddr, Device_Cmd_Set_PowerTime, 
			g_stSys.nPwMode, g_stSys.nPwYMD, g_stSys.nPwHMS, g_stSys.uSampleTime, g_stSys.uSampleGap);
		PCLINK_SendMsg(m_pCommObject);
	}
}

void PCLINK_Echo_SetPwOffTime(void)
{
	// 回应设备下电模式、时间参数
	if ( m_pCommObject )
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,%d,ok*\r\n",
			g_stSys.nAddr, Device_Cmd_Set_PwOffTime, 
			g_stSys.nPwOffMode, g_stSys.nPwOffYMD, g_stSys.nPwOffHMS);
		PCLINK_SendMsg(m_pCommObject);
	}
}

void PCLINK_Echo_SetSensor(COMM_COMMAND stCmd)
{
	// 响应设置传感器参数
#if DEVICE_TYPE == tDeviceIListenM
	int nSensor = (int)stCmd.wParam;
	if (stCmd.wParam == 1)
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%.2f*\r\n",
			g_stSys.nAddr, Device_Cmd_Set_Sensor, nSensor, g_stSys.fSensitivity);
		PCLINK_SendMsg(m_pCommObject);
	}
	else if (stCmd.wParam == 2)
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%.2f*\r\n",
			g_stSys.nAddr, Device_Cmd_Set_Sensor, nSensor, g_stSys.fAccelerometer);
		PCLINK_SendMsg(m_pCommObject);
	}
#elif DEVICE_TYPE == tDeviceIPowerProbe
	// 返回参数的精度要求比较高，和其它设备不同
	int nSensor = (int)stCmd.wParam;
	if (nSensor >= 1)
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%.6f*\r\n",
			g_stSys.nAddr, Device_Cmd_Set_Sensor, nSensor, g_stSys.fSensitivity);
		PCLINK_SendMsg(m_pCommObject);
	}
#else
	int nSensor = (int)stCmd.wParam;
	if (nSensor >= 1)
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%.2f*\r\n",
			g_stSys.nAddr, Device_Cmd_Set_Sensor, nSensor, g_stSys.fSensitivity);
		PCLINK_SendMsg(m_pCommObject);
	}
#endif
}

void PCLINK_Echo_Response(bool bEcho)							
{
	// 发送握手消息, bEcho = false 发送方， bEcho = true 接收方回执
	PCLINK_EchoASC(Device_Cmd_Response, 1, bEcho);
}

void PCLINK_Echo_SyncRecvSet(void)				
{
	// 返回设备同步采集设备的时钟基准参数
	// 返回时钟基准器件型号和使能状态（目前版本的使能状态均不支持改变）
#if USE_ATOMIC
	if ( m_pCommObject == NULL ) return;
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,%d,%d*\r\n",
			g_stSys.nAddr, Device_Cmd_SyncCmd_Set, m_bAtomicLinked, m_bAtomicDiscipline, 
			g_stSys.nTimeRefModelType, g_stSys.bTimeRefEnable);
	PCLINK_SendMsg(m_pCommObject);
#endif
}

void PCLINK_Echo_SetTrigGate(void)
{
	// 响应设置设备工作触发门限参数
#if DEVICE_TYPE == tDeviceIPowerAmp	|| DEVICE_TYPE == tDeviceIPowerCtrl
	if ( m_pCommObject == NULL ) return;
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,0,0,%.3f,%.3f*\n",
			g_stSys.nAddr, Device_Cmd_Set_TrigGate, g_stSys.m_fDepthGate, g_stSys.m_fDepthErrCoef);
	PCLINK_SendMsg(m_pCommObject);
#endif
}

void PCLINK_Echo_SetEncrypt(void)
{
	// 回应设备设置数据加密参数
#if defined(EN_DEVICE_ENCRYPT) && EN_DEVICE_ENCRYPT == 1
	uint32_t crc = 0xFFFFFFFF;
	if ( m_pCommObject == NULL ) return;
    for (int i = 0; i < AES_KEYLEN; ++i) {
        crc ^= g_stSys.nEncryptKey[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
	// 增加计算密码CRC的过程，然后通过上位机进行验证，保证密码设置的正确性
	sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,%d,%u*\r\n",
			g_stSys.nAddr, Device_Cmd_Set_DeviceEncrypt, g_stSys.bDeviceEncrypt, g_stSys.nEncryptMode, 1, crc);
	PCLINK_SendMsg(m_pCommObject);
#endif
}

int PCLINK_DecodeCmd(char* rxBuf, COMM_COMMAND* stCmd)//把上位机发过来的一串文本命令，解析成结构体里的各个字段
/*rxBuf  → 接收到的原始字符串，比如 "$1,2,100,200"
stCmd  → 解析结果存到这个结构体里
返回值 → 1=解析成功，0=解析失败*/
{
	char* rxBufb = rxBuf;//先复制一份指针备用，因为后面可能要移动指针位置，不想破坏原来的 rxBuf
	assert_param(rxBuf && stCmd);//确保两个指针都不是NULL，防止后面操作空指针直接崩掉
	
	// 将字符头去掉
	if (rxBuf[0] == '$') rxBufb++;//就是指针往后移一个字符，相当于从第二个字符开始读
	int result = sscanf(rxBufb, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu", //%*[ ,] 是个特殊写法，* 表示读了但不存，[ ,] 表示匹配空格或逗号
						&stCmd->nAddr, &stCmd->nCmd, &stCmd->wParam, &stCmd->lParam);
	if (result >= 4) return 1;//sscanf 的返回值是成功解析了几个字段
	return 0;
}

int PCLINK_Decode_Link(char* cmd)
{
	// 解码设备连接指令，设置设备连接状态
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS	|| DEVICE_TYPE == tDeviceIListenM	   || DEVICE_TYPE == tDeviceIListenN 		 || \
	DEVICE_TYPE == tDeviceILogger	|| DEVICE_TYPE == tDeviceITimeRef	   || DEVICE_TYPE == tDeviceITimeRefModel	 || \
	DEVICE_TYPE == tDeviceIPowerAmp	|| DEVICE_TYPE == tDeviceShortBaseLine || DEVICE_TYPE == tDeviceIBeacon
	result = Device_Decode_Link(cmd);//大部分走这个处理函数，个别设备类型走下面的处理函数
#elif DEVICE_TYPE == tDeviceIPowerCtrl
	result = 1;//电源控制设备直接返回1
#endif
	
	// 2025.08.12 更新
	// 上位机连接时，要从外部RTC更新时钟至内部RTC
	// 以保证上位机连接时读取的时间值是正确的
#if USE_DS1302
	Ds1302UpdateRTC();					// 更新DS1302的时间值
#elif USE_DS3231
	Ds3231UpdateRTC();					// 更新DS3231的时间值
#endif
	
#if USE_NET && USE_UDP_MODE
	udp_client_connect();				// 为了提高UDP传输效率，当接收到连接时，保存并打开连接
#endif
	return result;
}

int PCLINK_Decode_DeLink(COMM_COMMAND stCmd)
{
	// 设置设备断开连接
	m_bPcConnect = false; 
	m_bPcStatus  = false;
	PCLINK_Echo(stCmd); 				// 断开设备连接
#if DEVICE_TYPE == tDeviceIListenS  	|| DEVICE_TYPE == tDeviceIListenM	|| DEVICE_TYPE == tDeviceIPowerAmp
	printf( "PC delink, then system reset.\r\n" );
	NVIC_SystemReset();					// PCLINK_Decode_DeLink()，系统强制复位		
#elif DEVICE_TYPE == tDeviceILogger 
	Device_Decode_DeLink(&stCmd);
#endif
	return 1;
}

int PCLINK_Decode_Debug(COMM_COMMAND stCmd, char* cmd)
{
	// 调试指令
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenM	|| DEVICE_TYPE == tDeviceIListenN || \
	DEVICE_TYPE == tDeviceIPowerAmp	|| DEVICE_TYPE == tDeviceShortBaseLine	 || DEVICE_TYPE == tDeviceIBeacon
	result = Device_Decode_Debug(&stCmd, cmd);
#endif
	return result;
}

int PCLINK_Decode_DAQ_Start(COMM_COMMAND stCmd, char* cmd)
{
	// 开始采集
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS 		|| DEVICE_TYPE == tDeviceIListenM 	|| DEVICE_TYPE == tDeviceIListenN 	|| \
	DEVICE_TYPE == tDeviceIPowerProbe 	|| DEVICE_TYPE == tDeviceIUTM 		|| DEVICE_TYPE == tDeviceIUDM		|| \
	DEVICE_TYPE == tDeviceIPowerAmp		|| DEVICE_TYPE == tDeviceShortBaseLine
	result = Device_Decode_DAQ_Start(cmd);
	if (result && !m_bStartStatus) 
	{
		// 注意顺序，必须在回应后才能开始采集
		delay(5);  PCLINK_Echo(stCmd);		// 此处必须要延时，以兼容RS485模式
		delay(10); Device_OnDaqStart();		// 开始采集，此处必须要延时，等待上位机响应指令
		return 1;							// 此处必须直接返回，避免开始实时采集时返回了字符消息
	}
#elif DEVICE_TYPE == tDeviceITimeRef || DEVICE_TYPE == tDeviceITimeRefModel
	result = Device_OnDaqStart(cmd, &stCmd);
#endif
	if (result) { PCLINK_Echo(stCmd); SysParam_Save(); }
	return result;
}

int PCLINK_Decode_DAQ_Stop(COMM_COMMAND stCmd)
{
	// 停止采集
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS 		|| DEVICE_TYPE == tDeviceIListenM 	|| DEVICE_TYPE == tDeviceIListenN	|| \
	DEVICE_TYPE == tDeviceIPowerProbe 	|| DEVICE_TYPE == tDeviceIUTM 		|| DEVICE_TYPE == tDeviceIUDM 		|| \
	DEVICE_TYPE == tDeviceILogger  		|| DEVICE_TYPE == tDeviceShortBaseLine	|| \
	DEVICE_TYPE == tDeviceITimeRef 		|| DEVICE_TYPE == tDeviceITimeRefModel
	Device_OnDaqStop(); result = 1;			// 停止采集
#elif DEVICE_TYPE == tDeviceIPowerAmp
	Device_OnEmitStop(); result = 1;		// 停止发射
#endif
	if (result) 
	{ delay(100); PCLINK_Echo(stCmd); }		// 延迟回复
	return result;
}

int PCLINK_Decode_WatchDog(COMM_COMMAND stCmd)
{
	// 处理看门狗复位指令
	int result = 0;
#if DEVICE_TYPE == tDeviceIPowerCtrl
	result = Device_Echo_WatchDog(&stCmd);
#else
	if (stCmd.wParam == 0)
	{
		// 上位机连接中断状态
		
	}
	if (stCmd.lParam == 0)
	{
		// 上位机采集停止状态
		
	}
#endif
	return result;
}

int PCLINK_Decode_SetVersion(char* cmd)
{
	// 设置设备版本号
	float fVersion;
	COMM_COMMAND stCmd;
	int result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[ ,]%f",
		&stCmd.nAddr, &stCmd.nCmd, &stCmd.wParam, &stCmd.lParam, &fVersion);
	if ( result >= 5 && fVersion >= 1.0f )
	{
		g_stSys.uProgramTime    = stCmd.wParam;
		g_stSys.fProgramVersion = fVersion;
		PCLINK_Echo_SetDeviceVersion();
		SysParam_Save(); return 1;
	}
	return 0;
}

int PCLINK_Decode_SetProtect(COMM_COMMAND stCmd)
{
	// 设置设备保护状态
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS
	if ( stCmd.wParam == 0x01010101 )
	{
		// stCmd.wParam == 0x01010101时解除保护状态
		g_stSys.bDeviceProtect = stCmd.lParam != 0 ? 1 : 0;					// 必须输入正确的编码才能解锁
		PCLINK_EchoASC(stCmd.nCmd, stCmd.wParam, g_stSys.bDeviceProtect);	// 回复解锁状态信息，通知上位机
		SysParam_Save();													// 保存解锁状态
		result = Device_Decode_SetProtect(g_stSys.bDeviceProtect);			// 执行解锁和锁定操作
	}
#endif
	return result;
}

int PCLINK_Decode_SetEncrypt(COMM_COMMAND stCmd, char* cmd)
{
	// 设置设备数据加密状态
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS
	int nSetKey = 0;
	char key[64] = {0};
	result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[ ,]%d%*[,]%s",
					&stCmd.nAddr, &stCmd.nCmd, &stCmd.wParam, &stCmd.lParam, &nSetKey, key);
	if ( result >= 6 )
	{
		if ( stCmd.lParam == ENCRYPT_AES_CTR && nSetKey == 1 )
		{
			g_stSys.bDeviceEncrypt = stCmd.wParam;
			g_stSys.nEncryptMode   = stCmd.lParam;
			memcpy(g_stSys.nEncryptKey, key, AES_KEYLEN);	// 更新秘钥		
			result = 1;			
		}
	}
	else if ( result >= 4 )
	{
		if ( stCmd.lParam == ENCRYPT_AES_CTR )
		{
			g_stSys.bDeviceEncrypt = stCmd.wParam;
			g_stSys.nEncryptMode   = stCmd.lParam;
			result = 1;
		}
	}
	if ( result == 1 )
	{
		PCLINK_Echo_SetEncrypt();
		SysParam_Save(); 
	}
#endif
	return result;
}

int PCLINK_Decode_SetAddr(COMM_COMMAND stCmd)
{
	// 设置设备地址
	g_stSys.nAddr = (u8)(stCmd.wParam);
	SysParam_Save(); PCLINK_Echo(stCmd);
	return 1;
}

int PCLINK_Decode_SetMode(COMM_COMMAND stCmd)
{
	// 设置工作模式
	int result = 1;					// 默认保存模式参数
	g_stSys.uMode = stCmd.wParam;

#if DEVICE_TYPE == tDeviceIListenS 	|| DEVICE_TYPE == tDeviceIListenM 		|| DEVICE_TYPE == tDeviceILogger	|| \
	DEVICE_TYPE == tDeviceISensor	|| DEVICE_TYPE == tDeviceShortBaseLine 	|| DEVICE_TYPE == tDeviceIBeacon 	|| \
	DEVICE_TYPE == tDeviceITimeRef 	|| DEVICE_TYPE == tDeviceITimeRefModel 	|| \
	DEVICE_TYPE == tDeviceIPowerAmp	|| DEVICE_TYPE == tDeviceIPowerCtrl		|| \
	DEVICE_TYPE == tDeviceIUTM		|| DEVICE_TYPE == tDeviceIUDM
	result = Device_Decode_SetMode(stCmd.wParam, stCmd.lParam);
#endif
	if (result)
	{
		stCmd.lParam = g_stSys.uMode;		// 由于保存参数的时候可能因为配置定义约束，模式会和设置的参数不一致
		PCLINK_Echo(stCmd);					// 因此需要确认模式参数并更新参数后再发送上位机
		m_bForceStore = true;				// main()函数中执行保存
		result = 1;
	}
	return result;
}

int PCLINK_Decode_SetIP(char* cmd)
{
	// 设置IP地址参数
#if USE_NET
	COMM_COMMAND stCmd;
	int d1, d2, d3, d4, result;
	assert_param(cmd);
	// $X,X,0,0,IP1,IP2,IP3,IP4*
	result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[ ,]%d%*[ ,]%d%*[ ,]%d%*[ ,]%d",
		&stCmd.nAddr, &stCmd.nCmd, &stCmd.wParam, &stCmd.lParam, &d1, &d2, &d3, &d4);

	if (result >= 8)
	{
		if (stCmd.wParam == 0)
		{
			g_stSys.IP1 = (u8)d1;	g_stSys.IP2 = (u8)d2;	
			g_stSys.IP3 = (u8)d3;	g_stSys.IP4 = (u8)d4;
		}
		g_stSys.nModify = 1; 				// 必须先设置参数修改标志，然后保存参数
		PCLINK_Echo_SetIP(stCmd);			// 回应设置消息
		m_bForceStore = true;				// main()函数中执行保存
		return 1;
	}
#endif
	return 0;
}

int PCLINK_Decode_SetId(COMM_COMMAND stCmd)
{
	// 设置设备ID
	g_stSys.nID = (int)stCmd.wParam;
	PCLINK_EchoASC(Device_Cmd_Set_Id, g_stSys.nAddr, g_stSys.nID); 
	SysParam_Save(); return 1;
}

int PCLINK_Decode_SetSn(COMM_COMMAND stCmd)
{
	// 设置设备传感器序号
	g_stSys.nSn = (int)stCmd.wParam;
	PCLINK_EchoASC(Device_Cmd_Set_Sn, g_stSys.nAddr, g_stSys.nSn); 
	SysParam_Save(); return 1;
}

int PCLINK_Decode_SetRTC(char* cmd)
{
	// 设置系统实时时钟
	COMM_COMMAND stCmd;
	int result = 0, nRtcYMD, nRtcHMS, nRtcWeek;
	assert_param(cmd);

	// $X,X,150415,2,100526*
	result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%d%*[ ,]%d%*[ ,]%d",
		&stCmd.nAddr, &stCmd.nCmd, &nRtcYMD, &nRtcWeek, &nRtcHMS);

	if (result == 4) nRtcHMS = nRtcWeek;					// 当只有4个参数时，第四个参数对应时分秒
	if (result >= 4)
	{
#if DEVICE_TYPE == tDeviceITimeRef	|| DEVICE_TYPE == tDeviceITimeRefModel || \
	DEVICE_TYPE == tDeviceIPowerCtrl
		Device_Decode_SetRtc(nRtcYMD, nRtcHMS);
#else
		RTC_UpdateTime2(nRtcYMD, nRtcHMS);					// 更新RTC值
		m_uAtomicCnt = RTC_YMDHMS2Cnt(nRtcYMD, nRtcHMS);	// 更新原子钟计数值
	#if USE_DS1302
		Ds1302Update(nRtcYMD, nRtcHMS);						// 更新DS1302的时间值
	#elif USE_DS3231
		Ds3231Update(nRtcYMD, nRtcHMS);						// 更新DS3231的时间值
	#endif
	#if USE_STM8_PWCTRL
		iListen_Send_SetRtc(nRtcYMD, nRtcHMS);				// 更新 STM8 控制器的RTC时钟
	#endif
#endif
		stCmd.wParam = nRtcYMD;	stCmd.lParam = nRtcHMS;		// 更新结构体中的参数，用于响应消息返回
		PCLINK_Echo(stCmd);	return 1;
	}
	return 0;
}

int PCLINK_Decode_SetPowerEn(COMM_COMMAND stCmd)
{
	// 设置设备供电后保持上电状态时间参数，用于某些允许指令激活的设备
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS	 || DEVICE_TYPE == tDeviceIListenM	   	|| DEVICE_TYPE == tDeviceILogger || \
	DEVICE_TYPE == tDeviceITimeRef	 || DEVICE_TYPE == tDeviceITimeRefModel || \
	DEVICE_TYPE == tDeviceIPowerAmp  || DEVICE_TYPE == tDeviceIPowerCtrl		
	result = Device_Decode_SetPowerEn(&stCmd);
#endif
	if (result) { PCLINK_Echo(stCmd ); SysParam_Save();}
	return result;
}

int PCLINK_Decode_SetPowerDelay(COMM_COMMAND stCmd)
{
	// 设置设备供电后延迟上电时间参数
int result = 0;
#if DEVICE_TYPE == tDeviceIListenS	 || DEVICE_TYPE == tDeviceIListenM
	result = Device_Decode_SetPowerDelay(&stCmd);
#endif
	if (result) { PCLINK_Echo(stCmd ); SysParam_Save();}
	return result;
}

int PCLINK_Decode_SetPowerTime(char* cmd)
{
	// 设置设备上电模式、时间参数
	// uSampleTime， uSampleGap 单位均为分钟
	// 2026.03.24 修改：参数保存通过 main() 处理
	COMM_COMMAND stCmd;
	int nPwYMD, nPwHMS;
	u32 uSampleTime, uSampleGap;
	if ( cmd == NULL ) return 0;

	// $X,X,X,150415,100526*
	int result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%d%*[ ,]%d%*[ ,]%u%*[ ,]%u",
		&stCmd.nAddr, &stCmd.nCmd, &stCmd.wParam, &nPwYMD, &nPwHMS,	&uSampleTime, &uSampleGap);

	if (result >= 7)
	{
		g_stSys.nPwMode = (u8)stCmd.wParam;		// 保存上电模式
		g_stSys.nPwYMD  = nPwYMD;
		g_stSys.nPwHMS  = nPwHMS;
		SetPwTimeCnt(nPwYMD, nPwHMS, uSampleTime, uSampleGap);
		PCLINK_Echo_SetPowerTime(); m_bForceStore = true;		// main()函数中执行保存
		return 1;
	}
	else return 0;
}

int PCLINK_Decode_SetPwOffTime(char* cmd)
{
	// 设置设备下电模式、时间参数
	COMM_COMMAND stCmd;
	int nPwYMD, nPwHMS;
	if ( cmd == NULL ) return 0;

	// $X,X,X,150415,100526*
	int result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%d%*[ ,]%d",
		&stCmd.nAddr, &stCmd.nCmd, &stCmd.wParam, &nPwYMD, &nPwHMS);

	if (result >= 5)
	{
		g_stSys.nPwOffMode = (u8)stCmd.wParam;	// 保存下电模式
		g_stSys.nPwOffYMD  = nPwYMD;
		g_stSys.nPwOffHMS  = nPwHMS;
		PCLINK_Echo_SetPwOffTime(); m_bForceStore = true;		// main()函数中执行保存
		return 1;
	}
	else return 0;
}

int PCLINK_Decode_SetSampleCh(COMM_COMMAND stCmd)
{
	// 设置采样通道
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceIListenN	|| DEVICE_TYPE == tDeviceIPowerAmp
	g_stSys.nSampleCh    = (u8)stCmd.wParam;
	g_stSys.uSampleChSel = (u32)stCmd.lParam;
	if ( !m_bStartStatus ) result = 1;			// 如果正在采集，则不能返回消息
	SysParam_Save();
#endif
	if (result) PCLINK_Echo(stCmd); 
	return result;
}

int PCLINK_Decode_SetSampleRate(COMM_COMMAND stCmd)
{
	// 设置采样率
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS 	|| DEVICE_TYPE == tDeviceIListenM	|| DEVICE_TYPE == tDeviceIListenN 		|| \
	DEVICE_TYPE == tDeviceIUTM 		|| DEVICE_TYPE == tDeviceIUDM		|| DEVICE_TYPE == tDeviceIPowerProbe 	|| \
	DEVICE_TYPE == tDeviceIPowerAmp	|| DEVICE_TYPE == tDeviceISensor	|| DEVICE_TYPE == tDeviceShortBaseLine
	result = Device_Decode_SetSampleRate(&stCmd);
#else
	g_stSys.nSampleRate = (u8)stCmd.wParam;	result = 1; 
#endif
	if (result) { PCLINK_Echo(stCmd); SysParam_Save();}
	return result;
}

int PCLINK_Decode_SetSampleGain(COMM_COMMAND stCmd, char* cmd)
{
	// 设置增益
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS 	  	|| DEVICE_TYPE == tDeviceIListenM 	|| DEVICE_TYPE == tDeviceIListenN 		|| \
	DEVICE_TYPE == tDeviceIPowerProbe 	|| DEVICE_TYPE == tDeviceISigCond		
	Device_Decode_SetSampleGain(&stCmd, cmd);		// 函数内部判断并进行参数存储，不要引用返回值
#elif DEVICE_TYPE == tDeviceIUTM	  	|| DEVICE_TYPE == tDeviceIUDM		|| \
	  DEVICE_TYPE == tDeviceIPowerAmp	|| DEVICE_TYPE == tDeviceIPowerCtrl || DEVICE_TYPE == tDeviceShortBaseLine
	result = Device_Decode_SetSampleGain(stCmd.wParam, stCmd.lParam);
#elif DEVICE_TYPE == tDeviceITimeRef 	|| DEVICE_TYPE == tDeviceITimeRefModel 
	g_stSys.nSampleGain = stCmd.wParam&0xFF;
	result = Device_SendSetSampleGain(&stCmd);
#else
	result = 1; g_stSys.nSampleGain = (u8)stCmd.wParam;	
#endif
	if (result) { PCLINK_Echo(stCmd); SysParam_Save();}
	return result;
}

int PCLINK_Decode_SetSampleFilter(COMM_COMMAND stCmd, char* cmd)
{
	// 设置滤波器参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS 		|| DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceIListenN || \
	DEVICE_TYPE == tDeviceIPowerProbe	|| DEVICE_TYPE == tDeviceISigCond
	result = Device_Decode_SetSampleFilter(&stCmd, cmd);
#endif
	if (result) { PCLINK_Echo(stCmd); SysParam_Save();}
	return result;
}

int PCLINK_Decode_SetSampleUnit(COMM_COMMAND stCmd)
{
	// 设置输出结果单位
	int result = 0;
	if (stCmd.wParam <= Unit_Volt)
	{
		g_stSys.nSampleUnit = (u8)stCmd.wParam;
		PCLINK_Echo(stCmd); SysParam_Save(); result = 1;
	}
	return result;
}

int PCLINK_Decode_SetAcqLogTime(COMM_COMMAND stCmd)
{
	// 设置采样时长参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS	|| DEVICE_TYPE == tDeviceIListenM	|| DEVICE_TYPE == tDeviceILogger
	result = Device_Decode_SetAcqLogTime(&stCmd);
#elif DEVICE_TYPE == tDeviceITimeRef || DEVICE_TYPE == tDeviceITimeRefModel
	result = Send_CmdFromPclink2(&stCmd);
#elif DEVICE_TYPE == tDeviceIPowerAmp 
	g_stSys.uSaveParam = (u16)stCmd.wParam;
	PCLINK_Echo(stCmd); SysParam_Save(); result = 1;
#elif DEVICE_TYPE == tDeviceIPowerCtrl
	g_stSys.nSampleFileLength = (u16)stCmd.wParam;
	PCLINK_Echo(stCmd); SysParam_Save(); result = 1;
#endif
	return result;
}

int PCLINK_Decode_SetAcqStartTime(COMM_COMMAND stCmd)
{
	// 设置同步采集起始时间
	int nResult = 0;
#if DEVICE_TYPE == tDeviceIListenS	|| DEVICE_TYPE == tDeviceIListenM 		|| \
	DEVICE_TYPE == tDeviceITimeRef	|| DEVICE_TYPE == tDeviceITimeRefModel
	u32 uT1, uT2;
	int nYMD = (int)stCmd.wParam;
	int nHMS = (int)stCmd.lParam;
	
	// 由于上位机软件可能发送非法时间信息，此处需要进行过滤
	// 年月日时间值必须如示例所示：20240101
	if (nYMD < RTC_DEFAULT_YMD) return 0;
	uT1 = RTC_YMDHMS2Cnt(stCmd.wParam, stCmd.lParam);	
	#if DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM	|| DEVICE_TYPE == tDeviceIListenN
	uT2 = GetSyncModeStatus() ? m_uAtomicCnt : RTC_YMDHMS2Cnt(m_nRtcYMD, m_nRtcHMS);
	#else
	uT2 = USE_ATOMIC ? m_uAtomicCnt : RTC_YMDHMS2Cnt(m_nRtcYMD, m_nRtcHMS);
	#endif
	
	if (uT1 > uT2 + POWER_TIME_LIMIT)
	{
		// 至少要求 POWER_TIME_LIMIT 秒后再开始采集
		g_stSys.nPwYMD = nYMD;
		g_stSys.nPwHMS = nHMS;
		SetPwTimeCnt(g_stSys.nPwYMD, g_stSys.nPwHMS, g_stSys.uSampleTime, g_stSys.uSampleGap);		// 更新上电计数参数
		nResult = 1;
	}
#endif
	if(nResult) { PCLINK_Echo(stCmd); SysParam_Save(); };	
	return nResult;
}

int PCLINK_Decode_SetCommBaud(COMM_COMMAND stCmd)
{
	// 设置PCLINK串口波特率
	int result= 0;
	
#if DEVICE_TYPE == tDeviceIListenS	|| \
	DEVICE_TYPE == tDeviceILogger	|| DEVICE_TYPE == tDeviceISensor
	result = Device_SetBaudRate(&stCmd);
#else
	if (stCmd.wParam >= tBaud150 && stCmd.wParam <= tBaud115200)
	{
		g_stSys.nExCommBaud = stCmd.wParam;
		delay(10); SysParam_Save();	result = 1;		// 延迟一点时间，避免指令冲突
	}
#endif
	if (result) PCLINK_Echo(stCmd);
	return result;
}

int PCLINK_Decode_SetCalibrate(COMM_COMMAND stCmd)
{
	// 设置设备校准
	int result = 0;
#if DEVICE_TYPE == tDeviceShortBaseLine
	result = Device_Decode_SetCalibrate(&stCmd);
#elif DEVICE_TYPE == tDeviceILogger
	result = Device_Decode_SetSensorCalibrate(stCmd.wParam, stCmd.lParam);
#endif
	if (result) PCLINK_Echo(stCmd);
	return result;	
}
 
int PCLINK_Decode_SetPwRelay(COMM_COMMAND stCmd, char* cmd)
{
	// 设置开启继电器
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenM
	Device_Decode_SetPwRelay(cmd);		// 函数内自带回执处理
#elif DEVICE_TYPE == tDeviceShortBaseLine	|| DEVICE_TYPE == tDeviceIPowerCtrl	|| DEVICE_TYPE == tDeviceIBeacon
	result = Device_Decode_SetRelay(stCmd.wParam, stCmd.lParam);
#elif DEVICE_TYPE == tDeviceILogger
	result = Device_Decode_SetRelay(stCmd.wParam, stCmd.lParam, cmd);
#endif
	if (result) PCLINK_Echo(stCmd);
	return result;	
}

int PCLINK_Decode_SetEmitEn(COMM_COMMAND stCmd)
{
	// 设置发射
	int result = 0;
#if DEVICE_TYPE == tDeviceShortBaseLine
	result = Device_Decode_SetEmitEn(stCmd.wParam);
#endif
	return result;
}

int PCLINK_Decode_SetSensor(COMM_COMMAND stCmd, char* cmd)
{
	// 设置传感器参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS		|| DEVICE_TYPE == tDeviceIListenM	|| \
	DEVICE_TYPE == tDeviceIPowerProbe	|| DEVICE_TYPE == tDeviceITimeRef
	result = Device_Decode_SetSensor(cmd);
#endif
	if (result) PCLINK_Echo_SetSensor(stCmd);
	return 0;
}

int PCLINK_Decode_SetErrCoef(char* cmd)
{
	// 设置传感器测量误差修正参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS		|| DEVICE_TYPE == tDeviceIListenM	|| \
	DEVICE_TYPE == tDeviceIPowerProbe 	|| DEVICE_TYPE == tDeviceISensor
	result = Device_Decode_SetErrCoef(cmd);
#endif
	return result;
}

int PCLINK_Decode_SetCompassParam(char* cmd)
{
	// 设置设备集成的电子罗盘附件参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenM
	result = Device_Decode_SetCompassParam(cmd);
#endif
	return result;
}

int PCLINK_Decode_SetTdSensorParam(char* cmd)
{
	// 设置设备集成的TD传感器附件参数
	int result = 0;
	return result;
}

int PCLINK_Decode_SetTrigGate(char* cmd)
{
	// 设置设备工作触发门限参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIPowerCtrl	|| DEVICE_TYPE == tDeviceIPowerAmp
	COMM_COMMAND stCmd;
	float fData1, fData2;

	// $X,X,0,0*
	result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[ ,]%f%*[ ,]%f",
		&stCmd.nAddr,  &stCmd.nCmd, &stCmd.wParam, &stCmd.lParam, &fData1, &fData2);

	if (result >= 6)
	{
		g_stSys.m_fDepthGate    = fData1;
		g_stSys.m_fDepthErrCoef = fData2;
		SysParam_Save(); result = 1;
	}
	else result = 0;
	if (result) PCLINK_Echo_SetTrigGate();
#endif	
	return result;
}

int PCLINK_Decode_SetParam(COMM_COMMAND stCmd, char* cmd)
{
	// 设置定位检测的相关参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIPowerAmp	|| DEVICE_TYPE == tDeviceIUTM 		|| DEVICE_TYPE == tDeviceIUDM			||\
	DEVICE_TYPE == tDeviceIBeacon	|| DEVICE_TYPE == tDeviceIDeckUnit	|| DEVICE_TYPE == tDeviceShortBaseLine
	result = Device_Decode_SetParam(&stCmd, cmd);
#endif
	return result;
}

int PCLINK_Decode_GetParam(COMM_COMMAND stCmd, char* cmd)
{
	// 响应设置相关参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIPowerAmp || DEVICE_TYPE == tDeviceIUTM || DEVICE_TYPE == tDeviceIUDM
	result = Device_Decode_GetParam(&stCmd);
#endif
	return result;
}

int PCLINK_Decode_SaveParam(COMM_COMMAND stCmd)
{
	// 响应保存参数回执
	int result = 1;		// 默认直接响应命令保存参数
#if DEVICE_TYPE == tDeviceIPowerAmp	|| DEVICE_TYPE == tDeviceIUTM || DEVICE_TYPE == tDeviceIUDM
	result = Device_Decode_SaveParam(&stCmd);
#endif
	if (result) { PCLINK_Echo(stCmd); SysParam_Save(); }
	return result;
}

int PCLINK_Decode_InitParam(COMM_COMMAND stCmd)
{
	// 初始化设备配置
	PCLINK_Echo(stCmd);
	printf( "PC command initparam, then system reset.\r\n" );
	g_stSys.bInit = 0x00;	// 复位初始化校验值
	SysParam_Save();		// 复位并保存默认参数
	SysParam_Load();		// 再重新加载系统参数
	NVIC_SystemReset();		// 设置完后系统复位
	return 0;
}

int PCLINK_Decode_GetValue(COMM_COMMAND stCmd, char* cmd)
{
	// 获取传感器节点参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS
	result = Device_Decode_GetTestData(&stCmd, cmd);
#elif DEVICE_TYPE == tDeviceILogger
	result = Device_OnGetValue(&stCmd, cmd);
#elif DEVICE_TYPE == tDeviceISensor
	result = Device_Echo_GetValue(&stCmd);
#endif
	return result;
}

int PCLINK_Decode_Response(COMM_COMMAND stCmd)
{
	// 响应消息回执
	int result = 0;
#if DEVICE_TYPE == tDeviceILogger || DEVICE_TYPE == tDeviceIBuoy
	Device_OnResponse();
	result = 1;
#endif
	return result;
}

int PCLINK_Decode_SetCommTalkMode(COMM_COMMAND stCmd)
{
	// 响应设置声通模式
	int result = 0;
#if DEVICE_TYPE == tDeviceITimeRefModel
	result = Device_Decode_SetCommTalkMode(&stCmd);
#endif
	return result;
}

int PCLINK_Decode_SetLimitParam(char* cmd)
{
	// 设置限制参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIPowerAmp
	result = Device_Decode_SetLimitParam(cmd);
#endif
	return result;	
}

int PCLINK_Decode_GetMasterStatus(COMM_COMMAND stCmd, char* cmd)
{
	// 响应获取主机状态参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIBeacon || DEVICE_TYPE == tDeviceIPowerCtrl
	result = Device_Decode_GetMasterStatus(&stCmd, cmd);
#endif
	return result;	
}

int PCLINK_Decode_RecvData(COMM_COMMAND stCmd, char* cmd)
{
	// 接收数据
	int result = 0;
#if DEVICE_TYPE == tDeviceShortBaseLine
	result = Device_Decode_RecvData(&stCmd, cmd);
#endif
	return result;
}

int PCLINK_Decode_SetArrayUnit(COMM_COMMAND stCmd)
{
	// 设置阵元参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenM
	g_stSys.nArrayUnit = stCmd.wParam;		
	g_stSys.nSampleCh  = stCmd.wParam*4;	// 更新通道数
	result = 1;								// 保存设置参数
#elif DEVICE_TYPE == tDeviceILogger
	result = Device_Decode_SetArrayUnit(stCmd.wParam, stCmd.lParam);
#endif
	if (result) { PCLINK_Echo(stCmd); SysParam_Save(); }
	return result;
}

int PCLINK_Decode_SetArrayUnitAddr(COMM_COMMAND stCmd, char* cmd)
{
	// 设置阵元地址参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenM
	iListen_Send_CmdToUnit(&stCmd, cmd);
#elif DEVICE_TYPE == tDeviceILogger
	result = Device_Decode_SetArrayUnitAddr(stCmd.wParam, stCmd.lParam);
#endif
	if (result) { PCLINK_Echo(stCmd); }
	return result;	
}

int PCLINK_Decode_SetWorkBand(COMM_COMMAND stCmd)
{
	// 设置设备工作带宽
	int result = 0;
#if DEVICE_TYPE == tDeviceIPowerAmp
	if ( stCmd.wParam < stCmd.lParam )
	{
		g_stSys.uDeviceWorkBandL = stCmd.wParam;
		g_stSys.uDeviceWorkBandH = stCmd.lParam;
		result  = 1;
	}
#endif
	if (result) 
	{ 
		PCLINK_Echo(stCmd);
		SysParam_Save();
	}
	return result;	
}

int PCLINK_Decode_TimeRef(char* cmd, COMM_COMMAND* pStCmd)
{
	// 响应时钟模块消息
	int result = 0;
#if (DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceIPowerAmp) && USE_ATOMIC
	result = TimeRef_Decode_operate(cmd, pStCmd);
#elif DEVICE_TYPE == tDeviceITimeRef || DEVICE_TYPE == tDeviceITimeRefModel
	result = Device_Decode_TimeRef(cmd, pStCmd);
#endif
	return result;
}

int PCLINK_Decode_SyncRecvSet(COMM_COMMAND stCmd)
{
	// 设置同步采集设备的时钟基准参数
	int result = 0;
#if DEVICE_TYPE == tDeviceIListenS || DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceIPowerAmp
	if ( stCmd.wParam == ATOMIC_SA45 || stCmd.wParam/100 == OCXO_MODEL || stCmd.wParam/100 == TCXO_MODEL )
	{
		g_stSys.nTimeRefModelType	= stCmd.wParam;
		g_stSys.bTimeRefEnable		= stCmd.lParam&1;
		SysParam_Save(); PCLINK_Echo_SyncRecvSet();		// 保存设置参数，回应设置消息
		result = 1;
	}
#endif
	return result;
}

int PCLINK_Decode_SyncRecvMsg(COMM_COMMAND* pStCmd)
{
	// 响应同步采集消息指令
	int result = 0;
#if DEVICE_TYPE == tDeviceITimeRef
	result = Device_Decode_SyncRecvMsg(pStCmd);
#endif
	return result;
}

/////////////////////////////////////////////////
// 以下函数当有文件操作时使用
int PCLINK_Decode_SD(char* cmd, COMM_COMMAND* pCmd)
{
	// 解码SD卡命令
#if USE_FATFS
	// 如果使用USB-HUB读卡器接口，则不响应文件操作
	if ( !m_bFatfsStatus ) return 0;
	switch (pCmd->nCmd)
	{
		case Device_Cmd_SD_Format		:	PCLINK_Decode_SD_Format(pCmd);       break;
		case Device_Cmd_SD_OpenDir		:	PCLINK_Decode_SD_OpenDir(cmd, pCmd); break;
		case Device_Cmd_SD_DelFile		:	PCLINK_Decode_SD_DelFile(cmd, pCmd); break;
		case Device_Cmd_SD_DelDir		:	PCLINK_Decode_SD_DelDir(cmd, pCmd);  break;
		case Device_Cmd_SD_NewFile		:	PCLINK_Decode_SD_NewFile(cmd, pCmd); break;
		case Device_Cmd_SD_OpenFile		:	PCLINK_Decode_SD_OpenFile(cmd, pCmd);break;
		case Device_Cmd_SD_CloseFile	:	PCLINK_Decode_SD_CloseFile(pCmd);    break;
		case Device_Cmd_SD_LoadData		:	PCLINK_Decode_SD_ReadFile(pCmd);     break;
		case Device_Cmd_SD_WriteFile	:	PCLINK_Decode_SD_WriteFile(pCmd);    break;
		default : break;
	}
#endif
	return pCmd->nCmd;
}

int PCLINK_Decode_SD_SetCardType(COMM_COMMAND stCmd)
{
	// 设置设备内存参数
	if (stCmd.lParam >= SD_128M && stCmd.lParam  <= SD_4T)
	{
		g_stSys.uCardType = stCmd.lParam;
		PCLINK_Echo(stCmd); SysParam_Save(); return 1;
	}	
	return 0;
}

#if USE_FATFS
void PCLINK_Echo_SD_DelDir(int nStatus, FRESULT lParam)
{
	// 格式化响应消息
	// nStatus = 0 删除失败， 1 删除成功， 2 正在删除
	// lParam 表示FR_??文件操作返回值
	assert_param(m_pCommObject);
	if ( m_bPcStatus )
	{
		if ( nStatus == 0)
		{
			sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d, %s ,error*\r\n",
				g_stSys.nAddr, Device_Cmd_SD_DelDir, 
				nStatus, lParam, m_chDirBuf);
		}
		else if (nStatus == 1)
		{
			sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d, %s ,ok*\r\n",
				g_stSys.nAddr, Device_Cmd_SD_DelDir, 
				nStatus, lParam, m_chDirBuf);
		}
		else if (nStatus == 2)
		{
			RTC_FreshTime();	// 由于 Timer2 中断优先级高，无法再响应RTC中断，因此也无法刷新时间，此处强制刷新
			sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d, %s ,%02d:%02d:%02d...*\r\n",
				g_stSys.nAddr, Device_Cmd_SD_DelDir, 
				nStatus, lParam, m_chDirBuf, m_nRtcHMS/10000, (m_nRtcHMS/100)%100, m_nRtcHMS%100);
		}
		PCLINK_SendMsg(m_pCommObject);
	}
	else
	{
	#if DEVICE_TYPE == tDeviceIListenM && DEVICE_SUBTYPE/100 == iListenM_Unit
		// 采集单元从设备不能直接发送删除文件夹或者格式化的状态信息
		// 要去掉地址数据前的所有无关字符
		if ( nStatus == 0)
		{
			sprintf(m_chSlaveMsg, "%d,%d,%d,%d, %s ,error",
				g_stSys.nAddr, Device_Cmd_SD_DelDir, nStatus, lParam, m_chDirBuf);
		}
		else if (nStatus == 1)
		{
			sprintf(m_chSlaveMsg, "%d,%d,%d,%d, %s ,ok",
				g_stSys.nAddr, Device_Cmd_SD_DelDir, nStatus, lParam, m_chDirBuf);
		}
		else if (nStatus == 2)
		{
			sprintf(m_chSlaveMsg, "%d,%d,%d,%d, %s ,continue...",
				g_stSys.nAddr, Device_Cmd_SD_DelDir, nStatus, lParam, m_chDirBuf);
		}
	#endif
	}
}

void PCLINK_Echo_SD_Format(int nStatus, FRESULT lParam)
{
	// 格式化响应消息，0 格式化失败， 1 格式化完成， 2 正在格式化，lParam = FR_OK
	if ( m_bPcStatus )
	{
		assert_param(m_pCommObject);
		if (nStatus == 2)
			sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d, start formatting...*\r\n",
				g_stSys.nAddr, Device_Cmd_SD_Format, nStatus, lParam);
		else if (nStatus == 1)
			sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d, format over.*\r\n",
				g_stSys.nAddr, Device_Cmd_SD_Format, nStatus, lParam);
		else
			sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d, format error*\r\n",
				g_stSys.nAddr, Device_Cmd_SD_Format, nStatus, lParam);
		PCLINK_SendMsg(m_pCommObject);
	}
	else
	{
	#if DEVICE_TYPE == tDeviceIListenM && DEVICE_SUBTYPE/100 == iListenM_Unit
		// 采集单元从设备不能直接发送删除文件夹或者格式化的状态信息
		// 要去掉地址数据前的所有无关字符
		if (nStatus == 2)
			sprintf(m_chSlaveMsg, "%d,%d,%d,%d, start formatting...*",
				g_stSys.nAddr, Device_Cmd_SD_Format, nStatus, lParam);
		else if (nStatus == 1)
		{
			sprintf(m_chSlaveMsg, "%d,%d,%d,%d, format over.*",
				g_stSys.nAddr, Device_Cmd_SD_Format, nStatus, lParam);
		#if USE_FILE
			// 由于格式化会删除配置文件，必须初始化以重新创建配置文件并保存参数
			Init_SD_Fatfs(true);		// PCLINK_Echo_SD_Format()
		#endif
		}
		else
			sprintf(m_chSlaveMsg, "%d,%d,%d,%d, format error*",
				g_stSys.nAddr, Device_Cmd_SD_Format, nStatus, lParam);
	#endif
	}
}

void PCLINK_Echo_SD_Upload_Fname(FILINFO* pFi, COMM_COMMAND* pCmd)
{
	// 发送当前检索到的文件或文件夹名
	// fdate  文件最后一次修改 / 目录创建的date
	// bit15 : 9	Year origin from 1980 (0..127)
	// bit8 : 5		Month(1..12)
	// bit4 : 0		Day(1..31)
	// ftime 文件最后一次修改 / 目录创建的time
	// bit15 : 11	Hour(0..23)
	// bit10 : 5	Minute(0..59)
	// bit4  : 0	Second / 2 (0..29)
	unsigned long fDate, fTime;
	int nYear, nMon, nDay, nH, nM, nS;
	assert_param(m_pCommObject);

	// 将日期转换为 例如 20160831
	// 将时间转换为 例如 082034
	fDate = pFi->fdate;
	fTime = pFi->ftime;
	nYear = (fDate>>9)+1980;
	nMon  = (fDate>>5)&0x0f;
	nDay  = fDate&0x1f;
	nH    = (fTime>>11);
	nM    = (fTime>>5)&0x3f;
	nS    = fTime&0x1f;
	fDate = nYear*10000 + nMon*100 + nDay;
	fTime = nH*10000 + nM*100 + nS;
	if (pFi != NULL && strcmp(m_chFileBuf, "System Volume Information"))
	{
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%lu,%lu,%u,%lu,%lu,%lu, %s %s ,ok*\n",
			g_stSys.nAddr, Device_Cmd_SD_FName, pCmd->wParam, pCmd->lParam, 
			pFi->fattrib, fDate, fTime, (unsigned long)pFi->fsize, m_chDirBuf, m_chFileBuf);
		PCLINK_SendMsg(m_pCommObject);
	}
}

int PCLINK_Decode_SD_OpenDir(char* cmd, COMM_COMMAND* pCmd)
{
	// 打开指定文件夹
	int nFile= 0;			// 标志是否上传了文件
	FRESULT fres;
	FILINFO hFileInfo;

	if (pCmd->wParam == 0)
	{
		if ( SD_ToRootDir(&fres) )
		{
			strcpy(m_chDirBuf, "0:");
			while (SD_EnumCurDir(&hFileInfo, &fres))
			{
				pCmd->lParam = OpenDir_OK;
				PCLINK_Echo_SD_Upload_Fname(&hFileInfo, pCmd);
				// 每上传32个文件信息，停一下，避免文件信息上传过快造成网口堵塞
				if (++nFile >= 32) break;
			}
			
			if (nFile < 32) 
			{
				// 单次上传不超过32个文件，则认为文件夹已经枚举到结尾处
				pCmd->nCmd = Device_Cmd_SD_FName;
				pCmd->lParam = OpenDir_Over;
				PCLINK_Echo(*pCmd);
				return fres;
			}
		}
	}
	else if (pCmd->wParam == 1 || pCmd->wParam == 2)
	{
		// stCmd->wParam == 1 打开非根目录文件夹获取文件列表
		// stCmd->wParam == 2 打开指定下载文件夹获取下载文件列表
		int result;
		result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[,]%s",
			&pCmd->nAddr, &pCmd->nCmd, &pCmd->wParam, &pCmd->lParam, m_chDirBuf);
		if (result >= 5)
		{
			if (SD_OpenDir(m_chDirBuf, &fres))
			{
				while (SD_EnumCurDir(&hFileInfo, &fres))
				{
					nFile++;
					if (nFile%32 == 0) pCmd->lParam = OpenDir_KeepOn;
					PCLINK_Echo_SD_Upload_Fname(&hFileInfo, pCmd);
					// 每上传32个文件信息，停一下，避免文件信息上传过快造成网口堵塞
					if (nFile%32 == 0) break;
				}
				
				if (nFile < 32) 
				{
					// 单次上传不超过32个文件，则认为文件夹已经枚举到结尾处
					pCmd->nCmd = Device_Cmd_SD_FName;
					pCmd->lParam = OpenDir_Over;
					PCLINK_Echo(*pCmd);
					return fres;
				}
			}
			else
			{
				printf("-> can not open indicate folder.");
			}
		}
	}
	else if (pCmd->wParam == 3 || pCmd->wParam == 4)
	{
		// 由于网口文件数据上传的问题，接收到文件信息后，将该文件之后的文件名枚举上传
		// pCmd->wParam == 3 在当前文件夹下，枚举指定文件之后的文件
		// pCmd->wParam == 4 在当前需要下载的文件夹下，枚举指定文件之后的文件
		int result;
		char chBuf[64];
		result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[,]%s",
			&pCmd->nAddr, &pCmd->nCmd, &pCmd->wParam, &pCmd->lParam, chBuf);
		if (result >= 5)
		{
			if (SD_OpenDir(m_chDirBuf, &fres))
			{
				bool bFind = false;
				while (SD_EnumCurDir(&hFileInfo, &fres))
				{
					if (bFind)
					{
						nFile++;
						if (nFile%32 == 0) pCmd->lParam = OpenDir_KeepOn;
						PCLINK_Echo_SD_Upload_Fname(&hFileInfo, pCmd);
						// 每上传32个文件信息，停一下，避免文件信息上传过快造成网口堵塞
						if (nFile%32 == 0) break;
					}
					if (strcmp(chBuf, m_chFileBuf) == 0)
					{
						bFind = true;
					}
				}
				
				if (nFile < 32) 
				{
					// 单次上传不超过32个文件，则认为文件夹已经枚举到结尾处
					pCmd->nCmd = Device_Cmd_SD_FName;
					pCmd->lParam = OpenDir_Over;
					PCLINK_Echo(*pCmd);
					return fres;
				}
			}
			else
			{
				printf("-> can not open indicate folder.");
			}
		}
	}
	
	if (nFile == 0)
	{
		// 没有文件也要回应
		pCmd->lParam = OpenDir_Over;
		PCLINK_Echo(*pCmd);
	}
	return fres;
}


int PCLINK_Decode_SD_DelFile(char* cmd, COMM_COMMAND* pCmd)
{
	// 删除指定文件，指定全路径文件名
	FRESULT fres = FR_OK;
	int result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[,]%s",
		&pCmd->nAddr, &pCmd->nCmd, &pCmd->wParam, &pCmd->lParam, m_chFileBuf);
	
	if (result >= 5)
	{
		assert_param(m_pCommObject);
		if (SD_DelFile(m_chFileBuf, &fres))
		{
			// 找到并删除文件
			pCmd->wParam = true;
		}
		else pCmd->wParam = false;
		
		pCmd->lParam = fres;
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%lu,%lu, %s ,ok*\n",
			g_stSys.nAddr, Device_Cmd_SD_DelFile, pCmd->wParam, pCmd->lParam, m_chFileBuf);
		PCLINK_SendMsg(m_pCommObject);
		return 1;
	}
	return 0;
}

int PCLINK_Decode_SD_DelDir(char* cmd, COMM_COMMAND* pCmd)
{
	// 删除指定文件，指定全路径文件夹名
	int result;
	result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[,]%s",
		&pCmd->nAddr, &pCmd->nCmd, &pCmd->wParam, &pCmd->lParam,
		m_chDirBuf);

	if (result >= 5)
	{
		m_bSdDelDir = true;			// 在定时器2中断中删除文件夹
		TIMER2_Config(2, true);		// 使能文件操作定时器2
		return 1;
	}
	return 0;
}

int PCLINK_Decode_SD_NewFile(char* cmd, COMM_COMMAND* pCmd)
{
	// 新建指定文件，指定全路径文件名（但是是在当前路径下建立文件）
	int result;
	FRESULT fres = FR_OK;
	result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[,]%s",
		&pCmd->nAddr, &pCmd->nCmd, &pCmd->wParam, &pCmd->lParam, m_chFileBuf);
	
	if (result >= 5)
	{
		assert_param(m_pCommObject);
		if (SD_NewFile(m_chFileBuf, &fres))
		{
			// 新创建文件
			pCmd->wParam = true;
			fres = f_stat(m_chFileBuf, &m_hFileInfo);
			
			sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,%u,%u,%u,%lu, %s %s ,ok*\n",
			g_stSys.nAddr, Device_Cmd_SD_NewFile, 1, fres, 
			m_hFileInfo.fattrib, m_nRtcYMD%1000000, m_nRtcHMS, (unsigned long)m_hFileInfo.fsize, m_chDirBuf, m_chFileBuf);
		}
		else
		{
			pCmd->wParam = false;
			m_pCommObject->uCommTx = sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%d,%d,%d,%d,%d,%d, %s %s ,ok*\n",
			g_stSys.nAddr, Device_Cmd_SD_NewFile, 0, fres, 
			0, 0, 0, 0, m_chDirBuf, m_chFileBuf);
		}
		PCLINK_SendMsg(m_pCommObject);
		
		if ( pCmd->wParam )
		{
			// 开数据接收双缓冲，后续要改为DMA接收
			if (m_pFileLoadBuf[0] == NULL)
			{
				// 申请预读取数据缓存
				m_pFileLoadBuf[0] = mymalloc(SRAMIN, sizeof(DATA_PACK_STRUCT));
				if (m_pFileLoadBuf[0] == NULL)
				{
					PCLINK_EchoASC(Device_Cmd_SD_WriteFile, Upload_Err_CreateBuf, 0);	// 返回出错状态
					printf(" Error: There is no free space in SRAMIN to create the buffer for loading.\n");
					return -1;
				}
			}
			if (m_pFileLoadBuf[1] == NULL)
			{
				// 申请预读取数据缓存
				m_pFileLoadBuf[1] = mymalloc(SRAMIN, sizeof(DATA_PACK_STRUCT));
				if (m_pFileLoadBuf[1] == NULL)
				{
					PCLINK_EchoASC(Device_Cmd_SD_WriteFile, Upload_Err_CreateBuf, 0);	// 返回出错状态
					printf(" Error: There is no free space in SRAMIN to create the buffer for loading.\n");
					return -1;
				}
			}		
			return 1;	// 返回正确状态
		}
	}
	return -1;
}

int PCLINK_Decode_SD_NewDir(char* cmd, COMM_COMMAND* pCmd)
{
	// 新建指定文件夹，指定全路径文件夹名（但是是在当前路径下建立文件夹）
	int result;
	FRESULT fres = FR_OK;
	result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[,]%s",
		&pCmd->nAddr, &pCmd->nCmd, &pCmd->wParam, &pCmd->lParam, m_chDirBuf);
	pCmd->wParam = false;
	if (result >= 5)
	{
		assert_param(m_pCommObject);
		if (SD_NewDir(m_chDirBuf, &fres))
		{
			// 新创建文件夹
			pCmd->wParam = true;
		}
		
		pCmd->lParam = fres;
		sprintf(m_pCommObject->chTxBuf, "-> $%d,%d,%lu,%lu, %s ,ok*\n",
			g_stSys.nAddr, pCmd->nCmd, pCmd->wParam, pCmd->lParam, m_chDirBuf);
		PCLINK_SendMsg(m_pCommObject);
	}
	return fres;
}

int PCLINK_Decode_SD_Format(COMM_COMMAND* pCmd)
{
	// 格式化磁盘
	// 为了防止设备假死，需要跳出消息响应函数进行格式化处理
	m_nFormatCmd = pCmd->wParam == 1 ? SDOP_FMT : SDOP_DEL;
	
	// 格式化响应消息，0 格式化失败， 1 格式化完成，2 正在格式化
	PCLINK_Echo_SD_Format(2, FR_OK);	// 返回格式化响应消息
	
	m_bSdFormat = true;					// 触发软中断，在主程序中格式化磁盘
	TIMER2_Config(2, true);				// 使能删除文件夹定时器
	return 1;
}

int PCLINK_Decode_SD_OpenFile(char* cmd, COMM_COMMAND* pCmd)
{
	// 打开指定文件
	int result;
	u32 fSectors;
	FRESULT fres  = FR_OK;
	m_bFileUpload = false;		// 确保文件未打开前处于非上传数据状态
	m_bFileDownload = false;	// 确保文件未打开前处于非下传数据状态

	result = sscanf(cmd, "%d%*[ ,]%d%*[ ,]%lu%*[ ,]%lu%*[,]%s",
		&pCmd->nAddr, &pCmd->nCmd, &pCmd->wParam, &pCmd->lParam, m_chFileBuf);
	if (result >= 5)
	{
		m_uFileLoadSectors = pCmd->wParam;		// 得到当前打开的文件数据指针应转到的指定位置
		// 文件路径已打开的前提下，打开文件
		// 必须确保要寻址的扇区号小于文件最大扇区号
		fres = f_stat(m_chFileBuf, &m_hFileInfo);
		fSectors = m_hFileInfo.fsize%512 > 0 ? m_hFileInfo.fsize/512 + 1 : m_hFileInfo.fsize/512;	
		if (fres == FR_OK && m_uFileLoadSectors < fSectors)
		{
			// 文件找到并获取到正确的文件信息
			fres = f_open(&m_hFile, m_chFileBuf, FA_READ);
			if (fres != FR_OK)
			{
				pCmd->wParam = Upload_Err_OpenFile;
				pCmd->lParam = fres;
				printf(" Error: The load file can't be open.\n");
				PCLINK_Echo(*pCmd);					// 返回出错状态
				return fres;
			}
			else
			{
				fres = f_lseek(&m_hFile, m_uFileLoadSectors * 512);
				if (fres != FR_OK)
				{
					pCmd->wParam = Upload_Err_OpenFile;
					pCmd->lParam = fres;
					printf(" Error: The load file seek operate error.\n");
					PCLINK_Echo(*pCmd);				// 返回出错状态
					return fres;
				}
			}

			if (m_pDataPack == NULL)
			{
				// 申请发送数据帧缓存
				m_pDataPack = mymalloc(SRAMIN, sizeof(DATA_PACK_STRUCT));	
				if (m_pDataPack == NULL)
				{
					pCmd->wParam = Upload_Err_CreateBuf;
					pCmd->lParam = 0;
					printf(" Error: There is no free space in SRAMIN for loading.\n");
					PCLINK_Echo(*pCmd);				// 返回出错状态
					return fres;
				}
				
				// 初始化需要发送的数据包头
				m_pDataPack->uHeader  = DATA_HEADER_KEYWORD;
				m_pDataPack->uPackLen = sizeof(DATA_PACK_STRUCT);
				m_pDataPack->uDataBytes = 0;
				m_pDataPack->uCrcType = DATA_CRC_MODE;
				m_pDataPack->nAddr    = g_stSys.nAddr;
				m_pDataPack->nCmd     = -1;
				m_pDataPack->lParam   = FR_OK;
				m_pDataPack->nBlock   = 0;
				m_pDataPack->iBlock   = 0;
			}
			if (m_pFileLoadBuf[0] == NULL)
			{
				// 申请预读取数据缓存
				m_pFileLoadBuf[0] = mymalloc(SRAMIN, MAX_PRELOAD_BUF);
				if (m_pFileLoadBuf[0] == NULL)
				{
					pCmd->wParam = Upload_Err_CreateBuf;
					pCmd->lParam = 0;
					printf(" Error: There is no free space in SRAMIN to create the buffer for loading.\n");
					PCLINK_Echo(*pCmd);				// 返回出错状态
					return fres;
				}
			}
			if (m_pFileLoadBuf[1] == NULL)
			{
				// 申请预读取数据缓存
				m_pFileLoadBuf[1] = mymalloc(SRAMIN, MAX_PRELOAD_BUF);
				if (m_pFileLoadBuf[1] == NULL)
				{
					pCmd->wParam = Upload_Err_CreateBuf;
					pCmd->lParam = 0;
					printf(" Error: There is no free space in SRAMIN to create the buffer for loading.\n");
					PCLINK_Echo(*pCmd);				// 返回出错状态
					return fres;
				}
			}

			// 复位数据缓冲区相关参数
			m_uFileLoadBufSel   = 0;
			m_uFileLoadBytes[0] = 0;
			m_uFileLoadBytes[1] = 0;
			m_uFileLoadFres[0]  = FR_OK;
			m_uFileLoadFres[1]  = FR_OK;
			m_bFileLoadRdy[0]   = false;
			m_bFileLoadRdy[1]   = false;
			m_nPreLoadBufSel    = 0;
			m_stHexCmdHold.bCmdHold = false;

			// printf(" Info: The file has been opened.\n");
			pCmd->wParam = Upload_OK;
			pCmd->lParam = fres;
			m_bFileUpload = true;		// 设置工作在发送数据状态
			PCLINK_Echo(*pCmd);			// 返回正确状态
#if USE_STM32F1XX
			TIMER2_Config(100, true);	// 使能文件读取定时器
#else
			TIMER2_Config(500, true);	// 使能文件读取定时器
#endif
		}
		else if (fres != FR_OK)
		{
			pCmd->wParam = Upload_Err_File;
			pCmd->lParam = fres;
			printf(" Error: Can't get the indicate load file information.\n");
			PCLINK_Echo(*pCmd);		// 返回出错状态
		}
		else if (m_uFileLoadSectors >= fSectors)
		{
			pCmd->wParam = Upload_Err_OpenFile;
			pCmd->lParam = FR_INVALID_PARAMETER;
			printf(" Error: Can't get the indicate load file information.\n");
			PCLINK_Echo(*pCmd);		// 返回出错状态
		}
	}
	return fres;
}

int PCLINK_Decode_SD_CloseFile(COMM_COMMAND* pCmd)
{
	// 关闭当前打开的文件
	FRESULT fres = FR_OK;
	if (m_bFileUpload)
	{
		m_bFileUpload = false;			// 复位数据发送状态
		m_nPreLoadBufSel = -1;			// 复位加载缓冲区参数
		fres = f_close(&m_hFile);		// 关闭文件	
	}
	
	PCLINK_ReleaseFileBuf();			// 释放文件数据缓冲区
	
	if ( pCmd )
	{
		pCmd->wParam = Upload_OK;
		pCmd->lParam = FR_OK;
		PCLINK_Echo(*pCmd);				// 返回状态信息
	}
		
	m_stHexCmdHold.bCmdHold = false;
	return fres;
}

int PCLINK_Decode_SD_ReadFile(COMM_COMMAND* pCmd)
{
	// 根据指令参数，发送文件缓冲区的数据块
	// 根据指令参数，将未正确接收的当前缓冲区的数据重新发送
	// 此条指令采用缓冲机制
	int i, iBlock;
	u32 uFileLoadBytes;
	char* pFileLoadBuf;
	FRESULT fres = FR_OK;
	bool bLoadRdy;

	// 为了避免数据加载和数据上传两个函数由于中断相互影响，在上传过程中，禁止加载数据
	// 以免对参数的判断错误造成数据加载失败
	if (pCmd != NULL)
	{
		m_stHexCmdHold = *pCmd;			// 拷贝当前命令
		m_stHexCmdHold.bCmdHold = false;
	}

	iBlock = (int)m_stHexCmdHold.lParam;	// 当前区块号
	
	if (!m_bFileUpload)
	{
		// 当前文件未打开，返回错误状态
		m_stHexCmdHold.bCmdHold = false;
		PCLINK_SendHexMsg(m_pDataPack, Device_Cmd_SD_SendData, Upload_Err_LoadData);
		return -1;
	}

	SetArmLedG(true);
	if (m_stHexCmdHold.wParam == Upload_End)
	{
		// 上位机发送的回应，表示当前缓冲区数据接收完毕，重新开始新的数据发送
		m_stHexCmdHold.wParam = Upload_Newload;
		m_stHexCmdHold.lParam = 0;
		
		if ( (m_uFileLoadBufSel&0x01) == 0 )
		{
			// 通知加载缓冲区1
			m_bFileLoadRdy[0]   = false;
			m_uFileLoadBytes[0] = 0;
			m_uFileLoadBufSel   = 1;
			m_nPreLoadBufSel    = 0;	
		}
		else
		{
			// 通知加载缓冲区2
			m_bFileLoadRdy[1]   = false;
			m_uFileLoadBytes[1] = 0;
			m_uFileLoadBufSel   = 0;
			m_nPreLoadBufSel    = 1;	
		}	
	}
	
	//***** 2025.08.02 更新 ***********************
	// 优化为快速文件数据传输模式
	iBlock = (int)m_stHexCmdHold.lParam;	// 当前区块号
	if ( iBlock < 0 && iBlock > m_pDataPack->nBlock)
	{
		// printf(" Error: Device_Cmd_SD_SendData, %d.\n", Upload_Err_iBlock);
		PCLINK_SendHexMsg(m_pDataPack, Device_Cmd_SD_SendData, Upload_Err_iBlock);	// 返回错误状态
		return fres;
	}
	//*********************************************
	
	// 从缓冲区读取数据，并发送指定缓冲区中的数据块
	if ((m_uFileLoadBufSel&0x01) == 0)
	{
		// 缓冲区1操作
		fres = (FRESULT)m_uFileLoadFres[0];
		uFileLoadBytes = m_uFileLoadBytes[0];
		pFileLoadBuf   = m_pFileLoadBuf[0];
		bLoadRdy = m_bFileLoadRdy[0];
	}
	else
	{
		// 缓冲区2操作
		fres = (FRESULT)m_uFileLoadFres[1];
		uFileLoadBytes = m_uFileLoadBytes[1];
		pFileLoadBuf   = m_pFileLoadBuf[1];
		bLoadRdy = m_bFileLoadRdy[1];
	}

	if (m_stHexCmdHold.wParam == Upload_Newload)
	{
		//************************************
		// 为了测试网速，不读取数据
		//fres = FR_OK;
		//uFileLoadBytes = MAX_PRELOAD_BUF;
		//************************************
		
		if (fres == FR_OK && bLoadRdy)
		{
			m_stHexCmdHold.bCmdHold = false;	// 当前缓冲区数据可操作，则清零加载数据命令
			m_pDataPack->lParam = fres;		// 文件操作状态信息
			if (uFileLoadBytes == 0)
			{
				// 没有从文件中读取到数据，不进行数据上传，仅返回信息
				m_pDataPack->uDataBytes = 0;
				m_pDataPack->nBlock = 0;
				m_pDataPack->iBlock = 0;
				PCLINK_SendHexMsg(m_pDataPack, Device_Cmd_SD_SendData, Upload_End);				// 返回结束状态
				return fres;
			}
			else
			{
				// 预加载缓冲区有数据，将数据先拷贝至当前数据缓冲区，然后直接发送
				char* pBuf;
				// 必须标明本次上传总块数，以便上位机程序通讯错误时，对接收数据状态能够进行检验
				m_pDataPack->nBlock = GetCurLoadBufBlocks(uFileLoadBytes);
			#if USE_FAST_UPLOAD
				//***** 2025.08.02 更新 ***********************
				// 优化为快速文件数据传输模式
				for ( i=iBlock; i<m_pDataPack->nBlock; i++ )
				{
					m_pDataPack->iBlock = iBlock++;
					if (m_pDataPack->iBlock >= 0 && m_pDataPack->iBlock <m_pDataPack->nBlock)
					{
						// 发送缓存中的数据, m_pDataPack->iBlock 为当前发送的数据块号
						m_pDataPack->uDataBytes = GetCurLoadBufBlockBytes(uFileLoadBytes, m_pDataPack->iBlock);

						pBuf = pFileLoadBuf + m_pDataPack->iBlock*MAX_LOAD_BUF;
						GetCurLoadBufBlockCrc(m_pDataPack, pBuf, m_pDataPack->uDataBytes);					// 拷贝数据并计算数据的校验结果
						if ( !PCLINK_SendHexData(m_pDataPack, Device_Cmd_SD_SendData, Upload_Fast_OK) )		// 发送数据，如果发送错误，则退出
						{
						//	printf(" Error: Device_Cmd_SD_SendData failed, block %d.\r\n", m_pDataPack->iBlock);
							return -1;
						}
						else
						{
						//	printf(" Info: Device_Cmd_SD_SendData, block %d.\r\n", m_pDataPack->iBlock);
							m_hRtcCnt.uPCLinkIdle = 0;
						}
					}
					else
					{
						// printf(" Inf: Device_Cmd_SD_SendData over.\r\n");
						// 缓冲区未加载，等待
						m_stHexCmdHold.bCmdHold = true;
						m_nPreLoadBufSel = m_uFileLoadBufSel&0x01;	// 更新缓冲区序号
						return fres;
					}
				}
				//*******************************************
			#else
				// 非快速上传模式时，暂时保留原来的文件传输方法和机制
				i = iBlock;
				m_pDataPack->iBlock = iBlock;
				if (i >= 0 && i < m_pDataPack->nBlock)
				{
					// 发送缓存中的数据, m_pDataPack->iBlock 为当前发送的数据块号
					m_pDataPack->uDataBytes = GetCurLoadBufBlockBytes(uFileLoadBytes, i);

					// printf(" Info: Device_Cmd_SD_SendData, beging upload block %d.\n", iBlock);
					pBuf = pFileLoadBuf + i*MAX_LOAD_BUF;
					GetCurLoadBufBlockCrc(m_pDataPack, pBuf, m_pDataPack->uDataBytes);			// 拷贝数据并计算数据的校验结果
					PCLINK_SendHexData(m_pDataPack, Device_Cmd_SD_SendData, Upload_OK);			// 发送数据
				}
				else
				{
					// printf(" Error: Device_Cmd_SD_SendData, %d.\n", Upload_Err_iBlock);
					PCLINK_SendHexMsg(m_pDataPack, Device_Cmd_SD_SendData, Upload_Err_iBlock);	// 返回错误状态
					return fres;
				}
			#endif
			}
		}
		else if (fres != FR_OK)
		{
			// 不进行数据上传，仅返回信息
			// printf(" Error: Device_Cmd_SD_SendData, %d.\n", Upload_Err_LoadData);
			PCLINK_SendHexMsg(m_pDataPack, Device_Cmd_SD_SendData, Upload_Err_LoadData);	// 返回出错状态
			return fres;
		}
		else
		{
			// 缓冲区未加载，等待
			m_stHexCmdHold.bCmdHold = true;
			m_nPreLoadBufSel = m_uFileLoadBufSel&0x01;	// 更新缓冲区序号
		}
	}
	else if (m_stHexCmdHold.wParam == Upload_Reload && iBlock < MAX_LOAD_BUF_BLOCK)
	{
		// 重新发送指定数据块
		char* pBuf;

		m_pDataPack->lParam = fres;
		m_pDataPack->nBlock = GetCurLoadBufBlocks(uFileLoadBytes);					// 本次上传总块数
		m_pDataPack->uDataBytes = GetCurLoadBufBlockBytes(uFileLoadBytes, iBlock);	// 本次需要上传的字节数

		pBuf = pFileLoadBuf + iBlock*MAX_LOAD_BUF;
		GetCurLoadBufBlockCrc(m_pDataPack, pBuf, m_pDataPack->uDataBytes);			// 拷贝数据并计算数据的校验结果
		PCLINK_SendHexData(m_pDataPack, Device_Cmd_SD_SendData, Upload_OK);			// 发送当前请求重发的数据帧
	}
	SetArmLedG(false);

	return fres;
}

int PCLINK_Decode_SD_WriteFile(COMM_COMMAND* pCmd)
{
	// 根据指令参数，对新建文件进行数据写入
	int result = 0;
	FRESULT fres = FR_OK;
	if ( pCmd && pCmd->nCmd == Device_Cmd_SD_WriteFile)
	{
		if ( m_bFileDownload && pCmd->wParam == Upload_Newload )
		{
			// 保存下传的数据
			// 文件路径已打开的前提下，打开文件
			fres = f_open(&m_hFile, m_chFileBuf, FA_WRITE);
			if (fres == FR_OK)
			{
				fres = f_lseek(&m_hFile, m_uFileLoadSectors); 
				if ( fres == FR_OK )
				{
					if ( pCmd->lParam == 0 && m_pCommObject->bRdy )
					{
						UINT bt;
						DATA_PACK_STRUCT* pPack = (DATA_PACK_STRUCT*)m_pFileLoadBuf[0];
						fres = f_write(&m_hFile, pPack->chCurLoadBuf, pPack->uDataBytes, &bt);
						if ( fres == FR_OK )
						{
							m_uFileLoadSectors += pPack->uDataBytes;
							result = 1;
						}
					}
					else if ( pCmd->lParam == 1 && m_pCommObject->bRdy )
					{
						UINT bt;
						DATA_PACK_STRUCT* pPack = (DATA_PACK_STRUCT*)m_pFileLoadBuf[1];
						fres = f_write(&m_hFile, pPack->chCurLoadBuf, pPack->uDataBytes, &bt);
						if ( fres == FR_OK )
						{
							m_uFileLoadSectors += pPack->uDataBytes;
							result = 1;
						}
					}
				}
				fres = f_close(&m_hFile);
			
				if ( result )
				{
					PCLINK_EchoASC(Device_Cmd_SD_WriteFile, Upload_Newload, m_uFileLoadSectors);	
				}
				else if ( fres != FR_OK )
				{
					PCLINK_EchoASC(Device_Cmd_SD_WriteFile, Upload_Err_File, fres);		// 返回出错状态
					printf(" Error: Write file error.\n");
				}
			}
			else
			{
				PCLINK_EchoASC(Device_Cmd_SD_WriteFile, Upload_Err_OpenFile, fres);		// 返回出错状态
				printf(" Error: Open file error.\n");
			}
			if ( result != 1 ) m_bFileDownload = false;		// 写入数据出错，退出数据下传
		}
		else if ( m_bFileDownload && pCmd->wParam == Upload_Reload )
		{
			PCLINK_EchoASC(Device_Cmd_SD_WriteFile, pCmd->wParam, m_uFileLoadSectors);	
		}
		else if ( m_bFileDownload && pCmd->wParam == Upload_End )
		{
			m_bFileDownload = false;
			PCLINK_EchoASC(Device_Cmd_SD_WriteFile, pCmd->wParam, m_uFileLoadSectors);	
		}
		else if ( !m_bFileDownload && pCmd->wParam == Upload_Newload )
		{
			// 复位数据缓冲区相关参数
			m_uFileLoadBufSel   = 0;

			// printf(" Info: The file has been opened.\n");
			m_bFileDownload = true;		// 设置工作在接收数据状态
			m_uFileLoadSectors = 0;		// 文件数据指针应转到的指定初始位置（下传时以字节为单位）
			TIMER2_Config(100, true);	// 使能文件读写定时器，由于写入速度较慢，因此定时器的速率设定为 100Hz
			
			// 为了提高利用率，数据接收仍然使用串口对应的控制变量
			// 每次请求写入数据前，清空接收缓冲区和相关控制变量
			PCLINK_EchoASC(Device_Cmd_SD_WriteFile, Upload_Newload, 0);	
		}
	}
	
	m_pCommObject->uCommRx = 0;
	m_pCommObject->bRdy = false;
	return result;
}

void PCLINK_PreLoadFileData(void)
{
	// 通过 TIMER2 定时器周期调用，在下载文件过程中填充数据缓冲区
	if (m_bFileUpload && m_nPreLoadBufSel >=0 )
	{
		FRESULT fres;
		m_nPreLoadBufSel &= 1;
		// 判断并填充缓冲区
		m_uFileLoadBytes[m_nPreLoadBufSel] = SD_PreLoadData(m_pFileLoadBuf[m_nPreLoadBufSel], &fres);
		m_uFileLoadFres[m_nPreLoadBufSel]  = fres;
		m_bFileLoadRdy[m_nPreLoadBufSel]   = true;
		// printf(" Load buf%d.\n", m_nPreLoadBufSel);
		m_nPreLoadBufSel = -1;		// 复位加载参数

		if (m_stHexCmdHold.bCmdHold)
		{
			// 当前缓冲了数据读取命令，发送数据
			PCLINK_Decode_SD_ReadFile(0);
		}
	}
}

void PCLINK_ReleaseFileBuf(void)
{
	// 释放上传文件建立的缓冲区
	if (m_pDataPack)
		myfree(SRAMIN, m_pDataPack);		// 释放发送数据帧缓存
	if (m_pFileLoadBuf[0])
		myfree(SRAMIN, m_pFileLoadBuf[0]);	// 释放数据缓存
	if (m_pFileLoadBuf[1])
		myfree(SRAMIN, m_pFileLoadBuf[1]);	// 释放数据缓存

	m_pDataPack = 0;
	m_pFileLoadBuf[0] = 0;
	m_pFileLoadBuf[1] = 0;
}

u32 DataPackHeaderCrcSum(DATA_PACK_STRUCT* pPack)
{
	// 计算数据帧包头校验值
	int i, n;
	u8* pBuf;
	u32 uSum = 0;
	assert_param(pPack != NULL);
	pBuf = (u8*)pPack;
	n = sizeof(DATA_PACK_STRUCT) - MAX_LOAD_BUF - 4;	// 校验位不能算在内
	for (i = 0; i<n; i++)
	{
		uSum += *pBuf++;
	}
	return uSum;
}

int GetCurLoadBufBlocks(u32 uPreLoadBytes)
{
	// 计算当前缓冲区数据分块数
	int nBlock = 0;
	assert_param(uPreLoadBytes <= MAX_PRELOAD_BUF);
	nBlock = uPreLoadBytes / MAX_LOAD_BUF;
	nBlock = uPreLoadBytes > nBlock*MAX_LOAD_BUF ? nBlock + 1 : nBlock;
	return nBlock;
}

int GetCurLoadBufBlockBytes(u32 uPreloadBytes, int iBlock)
{
	// 计算当前缓冲区数据块的数据字节数
	int nBytes = 0;
	assert_param(uPreLoadBytes < MAX_PRELOAD_BUF && iBlock < MAX_LOAD_BUF_BLOCK);

	if (uPreloadBytes >= (iBlock + 1)*MAX_LOAD_BUF)
		nBytes = MAX_LOAD_BUF;
	else if (uPreloadBytes > iBlock*MAX_LOAD_BUF)
		nBytes = uPreloadBytes - iBlock*MAX_LOAD_BUF;
	return nBytes;
}

u32 GetCurLoadBufBlockCrc(DATA_PACK_STRUCT* pPack, void* pBuf, int nLen)
{
	// 计算当前缓冲区数据块的校验结果，并将数据搬移至发送缓冲中
	int i;
	u8* p;
	u32 uCrc = 0;
	assert_param(pPack && pBuf && nLen <= MAX_LOAD_BUF);
	p = pBuf;
	if (DATA_CRC_MODE == CRC_SUM16)
	{
		for (i = 0; i < nLen; i++)
		{
			pPack->chCurLoadBuf[i] = *(p++);
			uCrc += (u8)pPack->chCurLoadBuf[i];
		}
		pPack->uPackLen = DATA_PACK_HEADER_SIZE + nLen;
		pPack->uCrc = uCrc;
	}
	else 
	{
		memcpy(pPack->chCurLoadBuf, pBuf, nLen);
		pPack->uPackLen = DATA_PACK_HEADER_SIZE + nLen;
		pPack->uCrc = 0;
	}
	return uCrc;
}
#endif // USE_FATFS

void SetPwTimeCnt(int nYMD, int nHMS, u32 uTime, u32 uGap)
{
	// 根据参数设置上电参数计数器
	// uTime 和 uGap 均以分钟为单位
	u8 nYear, nMonth, nDay, nHour, nMinute, nSecond;
	
	nSecond = GetU8Second(nHMS);			// 时间格式是BCD码
	nMinute = GetU8Minute(nHMS);
	nHour   = GetU8Hour(nHMS);
	nDay    = GetU8Day(nYMD);
	nMonth  = GetU8Month(nYMD);
	nYear   = GetU8Year(nYMD);

	if (g_stSys.nPwMode == PwMode_Relative)
	{
		// 相对时间要求延迟不超过3天
		nDay = nDay > 3 ? 3 : nDay;
		m_uPwTimeSetYMD = 0;  
		m_uPwTimeSetHMS = nDay * 3600 * 24 + nHour * 3600 + nMinute * 60 + nSecond;
		m_uPwTimeSetHMS = m_uPwTimeSetHMS > 259200 ? 259200 : m_uPwTimeSetHMS;		
		g_stSys.uSampleTime = 0;
		g_stSys.uSampleGap  = 0;
	}
	else if (g_stSys.nPwMode == PwMode_Absolute)
	{
		m_uPwTimeSetYMD = (nYear + 2000) * 10000 + nMonth * 100 +nDay;
		m_uPwTimeSetHMS = nHour * 10000 + nMinute * 100 + nSecond;
		g_stSys.uSampleTime = 0;
		g_stSys.uSampleGap  = 0;
	}
	else if (g_stSys.nPwMode == PwMode_RealTimeInterval)
	{
		m_uPwTimeSetYMD = 0;
		m_uPwTimeSetHMS = 0;
		if (uTime == 0 || uGap == 0)
		{
			// 采集时长为0 或者 间隔时长为0 则认为是连续采集模式
			g_stSys.nPwMode = PwMode_RealTime;
			g_stSys.uSampleTime = 0;
			g_stSys.uSampleGap  = 0;
		}
		else
		{
			g_stSys.uSampleTime = uTime;
			g_stSys.uSampleGap  = uGap;
		}
	}
	else if (g_stSys.nPwMode == PwMode_RelativeInterval)
	{
		nDay = nDay > 2 ? 2 : nDay;
		m_uPwTimeSetYMD = 0;
		m_uPwTimeSetHMS = nDay * 3600 * 24 + nHour * 3600 + nMinute * 60 + nSecond;
		if (uTime == 0 || uGap == 0)
		{
			// 采集时长为0 或者 间隔时长为0 则认为是连续采集模式
			g_stSys.nPwMode = PwMode_Relative;
			g_stSys.uSampleTime = 0;
			g_stSys.uSampleGap  = 0;
		}
		else
		{
			g_stSys.uSampleTime = uTime;
			g_stSys.uSampleGap  = uGap;
		}
	}
	else if (g_stSys.nPwMode == PwMode_AbsoluteInterval)
	{
		m_uPwTimeSetYMD = (nYear + 2000) * 10000 + nMonth * 100 + nDay;
		m_uPwTimeSetHMS = nHour * 10000 + nMinute * 100 + nSecond;
		if (uTime == 0 || uGap == 0)
		{
			// 采集时长为0 或者 间隔时长为0 则认为是连续采集模式
			g_stSys.nPwMode = PwMode_Absolute;
			g_stSys.uSampleTime = 0;
			g_stSys.uSampleGap  = 0;
		}
		else
		{
			g_stSys.uSampleTime = uTime;
			g_stSys.uSampleGap  = uGap;
		}
	}
	else if (g_stSys.nPwMode == PwMode_RealTimeSchedule)
	{
		m_uPwTimeSetYMD = 0;
		m_uPwTimeSetHMS = 0;
		if (uGap == 0)
		{
			// 间隔时长为0 则认为是连续采集模式
			g_stSys.nPwMode = PwMode_RealTime;
			g_stSys.uSampleTime = 0;
			g_stSys.uSampleGap  = 0;
		}
		else
		{
			g_stSys.uSampleTime = 0;
			g_stSys.uSampleGap  = uGap;
		}
	}
	else if (g_stSys.nPwMode == PwMode_RelativeSchedule)
	{
		nDay = nDay > 2 ? 2 : nDay;
		m_uPwTimeSetYMD = 0;
		m_uPwTimeSetHMS = nDay * 3600 * 24 + nHour * 3600 + nMinute * 60 + nSecond;
		if (uGap == 0)
		{
			// 间隔时长为0 则认为是连续采集模式
			g_stSys.nPwMode = PwMode_Relative;
			g_stSys.uSampleTime = 0;
			g_stSys.uSampleGap  = 0;
		}
		else
		{
			g_stSys.uSampleTime = 0;
			g_stSys.uSampleGap  = uGap;
		}
	}
	else if (g_stSys.nPwMode == PwMode_AbsoluteSchedule)
	{
		m_uPwTimeSetYMD = (nYear + 2000) * 10000 + nMonth * 100 + nDay;
		m_uPwTimeSetHMS = nHour * 10000 + nMinute * 100 + nSecond;
		if (uGap == 0)
		{
			// 间隔时长为0 则认为是连续采集模式
			g_stSys.nPwMode = PwMode_Absolute;
			g_stSys.uSampleTime = 0;
			g_stSys.uSampleGap  = 0;
		}
		else
		{
			g_stSys.uSampleTime = 0;
			g_stSys.uSampleGap  = uGap;
		}
	}
	else
	{
		g_stSys.nPwMode = PwMode_RealTime;
		m_uPwTimeSetYMD = nYMD;  
		m_uPwTimeSetHMS = nHMS;
		g_stSys.uSampleTime = 0;
		g_stSys.uSampleGap  = 0;
	}
}

bool IsPwModeInterval(void)
{
	// 判断是否是间歇上电采集模式
	if( g_stSys.nPwMode == PwMode_RealTimeInterval || \
		g_stSys.nPwMode == PwMode_RelativeInterval || \
		g_stSys.nPwMode == PwMode_AbsoluteInterval ) 
		return true;
	return false;
}

void PCLINK_InitCmdHeader(char* buf, int nBuf, int nCmd, u32 wParam, u32 lParam)
{
	// 初始化2进制传输的指令包头
#if DEVICE_TYPE == tDeviceIListenS    || DEVICE_TYPE == tDeviceIListenM || DEVICE_TYPE == tDeviceIListenN || \
	DEVICE_TYPE == tDeviceIPowerProbe || DEVICE_TYPE == tDeviceIUTM 	|| DEVICE_TYPE == tDeviceIUDM
	int nPackLen;	// nLen
	assert_param(buf && nBuf >= MAX_RT_DATA_PACK_HEADER);
	
	if (nCmd == Device_Cmd_SendData)
		nPackLen = CalcRtDataBufSize();						// 更新 m_uRtDataBufSize;
	else
		nPackLen = MAX_RT_DATA_PACK_HEADER;
	
	// 将指令包信息写入缓冲区中
	m_hRtDataPackHead.uHeader	= DATA_HEADER_KEYWORD;		// 包头
	m_hRtDataPackHead.uPackLen 	= nPackLen;					// 数据包数据长度，包含包头
	m_hRtDataPackHead.nAddr    	= g_stSys.nAddr;			// 设备地址
	m_hRtDataPackHead.nCmd     	= nCmd;						// 设备指令
	m_hRtDataPackHead.wParam   	= wParam;					// 参数1
	m_hRtDataPackHead.lParam   	= lParam;					// 参数2
	m_hRtDataPackHead.nDeviceType 	 = (int)DEVICE_TYPE;	// 设备类型
	m_hRtDataPackHead.nDevictSubtype = (int)DEVICE_SUBTYPE;	// 设备型号
	m_hRtDataPackHead.nID	   	= g_stSys.nID;				// 设备出厂序号

	m_hRtDataPackHead.uParam1	= 0;
	m_hRtDataPackHead.uParam2	= 0;
	m_hRtDataPackHead.uParam3	= 0;
	m_hRtDataPackHead.uParam4	= 0;
	m_hRtDataPackHead.uParam5	= 0;
	m_hRtDataPackHead.uParam6	= 0;

	memcpy(buf, (u8*)(&m_hRtDataPackHead), sizeof(m_hRtDataPackHead));	// 直接赋值提高效率
	
	U32ToChar(buf + MAX_RT_DATA_PACK_HEADER - 4, nBuf, DATA_END_KEYWORD);
#endif
}

void PCLINK_UpdateCmdHeader(char* buf, int nBuf, int nCmd, u32 wParam, u32 lParam)
{
	// 更新2进制传输的指令包头
	int nLen = 12;													// 包头12字节
	assert_param(buf && nBuf >= MAX_RT_DATA_PACK_HEADER);
	
	// 将指令包信息写入缓冲区中
	nLen += IntToChar(buf + nLen, nBuf, nCmd);						// 设备指令
	nLen += U32ToChar(buf + nLen, nBuf, wParam);					// 参数1
	nLen += U32ToChar(buf + nLen, nBuf, lParam);					// 参数2
	nLen += 12;														// int x3 设备类型，子类型，ID号
	nLen += U32ToChar(buf + nLen, nBuf, m_hRtDataPackHead.uParam1);
	nLen += U32ToChar(buf + nLen, nBuf, m_hRtDataPackHead.uParam2);
	nLen += U32ToChar(buf + nLen, nBuf, m_hRtDataPackHead.uParam3);
	nLen += U32ToChar(buf + nLen, nBuf, m_hRtDataPackHead.uParam4);
	nLen += U32ToChar(buf + nLen, nBuf, m_hRtDataPackHead.uParam5);
	nLen += U32ToChar(buf + nLen, nBuf, m_hRtDataPackHead.uParam6);
}

void PCLINK_UpdateCmdHeader2(char* buf, int nBuf, float fParam1, float fParam2)
{
	// 更新2进制传输的指令包头	
	int nLen = 36;													// 包头 + 指令，参数1，参数2 + 设备类型，子类型，ID号 共计36字节
	assert_param(buf && nBuf >= MAX_RT_DATA_PACK_HEADER);
	
	// 将指令包信息写入缓冲区中
	nLen += FloatToChar(buf + nLen, nBuf, fParam1);					// 参数1
	nLen += FloatToChar(buf + nLen, nBuf, fParam2);					// 参数2
}

float CharToFloat(char* buf, int nBuf)
{
	// 将数据转换成浮点数，BigEnd
	float data;
	assert_param(buf && nBuf>=sizeof(float));
	data = *((float*)buf);
	return data;
}

float U16ToFloat(char* buf, int nBuf)
{
	// 将数据转换成浮点数，BigEnd
	short data;
	assert_param(buf && nBuf>=2);
	data = *((short*)buf);
	return (float)data;
}

float U24ToFloat(char* buf, int nBuf)
{
	// 将数据转换成浮点数，BigEnd
	int data;
	assert_param(buf && nBuf>=3);
	data = *((int*)(buf-1));
	data = data/256;
	return (float)data;
}

unsigned short CharToUShort(char* buf, int nBuf)
{
	unsigned short data;
	assert_param(buf && nBuf>=2);
	data = *((unsigned short*)buf);
	return (u16)data;
}

int IntToChar(char* buf, int nBuf, int nData)
{
	// BigEnd 模式
	int i, n = sizeof(int);
	BYTE* pByte = (BYTE*)&nData;
	assert_param(buf && nBuf>=n);
	for(i=0; i<n; i++) *buf++ = *pByte++;
	return n;
}

int U16ToChar(char* buf, int nBuf, u16 uData)
{
	// BigEnd 模式
	int i, n = sizeof(u16);
	BYTE* pByte = (BYTE*)&uData;
	assert_param(buf && nBuf>=n);
	for(i=0; i<n; i++) *buf++ = *pByte++;
	return n;
}

int U32ToChar(char* buf, int nBuf, u32 uData)
{
	// BigEnd 模式
	int i, n = sizeof(u32);
	BYTE* pByte = (BYTE*)&uData;
	assert_param(buf && nBuf>=n);
	for(i=0; i<n; i++) *buf++ = *pByte++;
	return n;
}

int FloatToChar(char* buf, int nBuf, float fData)
{
	// BigEnd 模式
	int i, n = sizeof(float);
	BYTE* pByte = (BYTE*)&fData;
	assert_param(buf && nBuf>=n);
	for(i=0; i<n; i++) *buf++ = *pByte++;
	return n;
}

int ShortToChar(char* buf, int nBuf, short nData)
{
	// BigEnd 模式
	int i, n = sizeof(short);
	BYTE* pByte = (BYTE*)&nData;
	assert_param(buf && nBuf>=n);
	for(i=0; i<n; i++) *buf++ = *pByte++;
	return n;
}

int U16ToChar2(char* buf, int nBuf, u16 uData)
{
	int i, n = sizeof(u16);
	BYTE* pByte = (BYTE*)&uData;
	assert_param(buf && nBuf>=n);
	for(i=0; i<n; i++) *(buf+n-i-1) = *pByte++;
	return n;
}

int U32ToChar2(char* buf, int nBuf, u32 uData)
{
	int i, n = sizeof(u32);
	BYTE* pByte = (BYTE*)&uData;
	assert_param(buf && nBuf>=n);
	for(i=0; i<n; i++) *(buf+n-i-1) = *pByte++;
	return n;
}

bool Is_CMD_GPS(char* cmd, COMM_COMMAND* pCmd)
{
	// 判断是否接收的是NEMA格式的GPS数据
	// 根据是否有字符头，自适应进行判断处理
#if USE_GPS
	char header[6] = {0};
	if (cmd[0] == '$')
		memcpy(header, cmd+3, 3);
	else
		memcpy(header, cmd+2, 3);
	if (strcmp(header, "GGA") == 0)
	{
		pCmd->nCmd = GPGGA;
		return true;
	}
	else if (strcmp(header, "VTG") == 0)
	{
		pCmd->nCmd = GPVTG;
		return true;
	}
	else if (strcmp(header, "HDT") == 0)
	{
		pCmd->nCmd = GPHDT;
		return true;
	}
	else if (strcmp(header, "RMC") == 0)
	{
		pCmd->nCmd = GPRMC;
		return true;
	}
	else if (strcmp(header, "GLL") == 0)
	{
		pCmd->nCmd = GPGLL;
		return true;
	}
#endif // USE_GPS
	return false;
}

void IWDG_Init(int nSec)
{
	// I看门狗主频 32.768K，使用LSI
	// 重载计数器值必须在 0 ~ 0x0FFF 范围内
	// 分频数为256, 重载值为128*nSec,溢出时间单位为1s，最大时间不能超过32秒
	// 看门狗一旦开始运行，便无法停止。
	int nWdgLoad = 128*nSec >= 4096 ? 4095 : 128*nSec;	
#if USE_IWDG && USE_STM32F1XX
	// STM32F1xx 独立看门狗初始化（标准库版本）
	// F1的IWDG使用LSI时钟，无需开启外设时钟，直接解锁访问
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable); 	// 解锁IWDG寄存器（默认锁定）
	IWDG_SetPrescaler(IWDG_Prescaler_256); 			// 设置分频系数为256（F1支持的最大分频）
	IWDG_SetReload(nWdgLoad);						// 设置重载值（0~0xFFF）
	IWDG_ReloadCounter();							// 重载计数器（喂狗）
	IWDG_Enable(); 									// 使能看门狗
#elif USE_IWDG && USE_STM32F4XX	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_WWDG, ENABLE);
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable); 	// 使能对IWDG->PR IWDG->RLR的写
	IWDG_SetPrescaler(IWDG_Prescaler_256); 			// 设置IWDG分频系数
	IWDG_SetReload(nWdgLoad);						// 设置IWDG装载值
	IWDG_ReloadCounter();							// 重载计数器（喂狗）
	IWDG_Enable(); 									// 使能看门狗
#elif USE_IWDG && USE_STM32H7XX
	IWDG_HandleTypeDef hiwdg1;
	hiwdg1.Instance 	  = IWDG1;
	hiwdg1.Init.Prescaler = IWDG_PRESCALER_256;		// 设置IWDG分频系数
	hiwdg1.Init.Window 	  = nWdgLoad;				// 设置IWDG装载值
	hiwdg1.Init.Reload    = nWdgLoad;				// 重载计数器（喂狗）
	if (HAL_IWDG_Init(&hiwdg1) != HAL_OK)			// 使能看门狗
		Error_Handler();
#endif
}

void IWDG_Feed(void)
{
	// 喂狗函数
#if USE_IWDG && USE_STM32F1XX
	IWDG_ReloadCounter();	// reload
#elif USE_IWDG && USE_STM32F4XX
	IWDG_ReloadCounter();	// reload
#elif USE_IWDG && USE_STM32H7XX
	// HAL库推荐使用HAL_IWDG_Refresh
	// HAL_IWDG_Refresh(&hiwdg1);
	IWDG1->KR = 0XAAAA;		// reload
#endif
}

char* FmtFloat(char* buf, int nFraction, float fData)
{
	// 格式化浮点数
	if ( buf && nFraction >= 1 )
	{
		int nFrac = pow(10.0f, nFraction);
		int nData = fData*nFrac;
		sprintf(buf, "%d.%0*d", nData/nFrac, nFraction, abs(nData%nFrac));
	}
	return buf;
}

bool CheckFlashUpdateStatus(void)
{
	// 在线升级时，用于判断并处理FLASH更新状态
	bool bCheck = RTC_BackRegRead(BkReg_FlashUpdateCheck);
	if ( bCheck ) 
	{
		// 不管是否进行了程序更新，需要复位更新标志参数寄存器
		// 避免启动后不停的进行程序更新
		RTC_BackRegWrite(BkReg_FlashUpdateCheck, 0);	
	}
	return bCheck;
}

void iap_load_app(u32 appxaddr)
{
	// 重新加载应用程序
#if USE_IAP_UPDATE
    /* Initialize the webserver module */
	// 当设备上电且网络接口联通后，再检查后备寄存器是否设置为更新状态
	// 如果是，则启动初始化，并允许更新
//    if ( m_bPcConnect && CheckFlashUpdateStatus() ) 
	if ( !m_bUpdateInit )
	{
		m_bUpdateInit = true;
		if (((*(__IO uint32_t*)USER_FLASH_FIRST_PAGE_ADDRESS) & 0x2FFE0000 ) == 0x20000000)
		{
			uint32_t JumpAddress;
			pFunction Jump_To_Application;
			/* Jump to user application */
			JumpAddress = *(__IO uint32_t*) (USER_FLASH_FIRST_PAGE_ADDRESS + 4);	// 用户代码区第二个字为程序开始地址(复位地址) 
			Jump_To_Application = (pFunction) JumpAddress;			
			/* Initialize user application's Stack Pointer */
			__set_MSP(*(__IO uint32_t*) USER_FLASH_FIRST_PAGE_ADDRESS);				// 初始化APP堆栈指针(用户代码区的第一个字用于存放栈顶地址)
			Jump_To_Application();
		}
	}
#endif
}

