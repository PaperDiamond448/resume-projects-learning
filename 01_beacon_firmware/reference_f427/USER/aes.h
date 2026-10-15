#ifndef _AES_H_
#define _AES_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h> 	// CBC mode, for memset

// #define the macros below to 1/0 to enable/disable the mode of operation.
//
// CBC enables AES encryption in CBC-mode of operation.
// CTR enables encryption in counter-mode.
// ECB enables the basic ECB 16-byte block algorithm. All can be enabled simultaneously.

#define CBC 	0
#define ECB 	0
#define CTR 	1

#define AES128 	1
#define AES192 	0
#define AES256 	0

#define AES_BLOCKLEN 	16 		// Block length in bytes - AES is 128b block only

#if defined(AES256) && (AES256 == 1)
    #define AES_KEYLEN 	32
    #define AES_keyExpSize 240
#elif defined(AES192) && (AES192 == 1)
    #define AES_KEYLEN 	24
    #define AES_keyExpSize 208
#else
    #define AES_KEYLEN 	16   	// Key length in bytes
    #define AES_keyExpSize 176
#endif

#define Nb 4					// The number of columns comprising a state in AES. This is a constant in AES. Value=4
#if defined(AES256) && (AES256 == 1)
    #define Nk 8
    #define Nr 14
#elif defined(AES192) && (AES192 == 1)
    #define Nk 6
    #define Nr 12
#else
    #define Nk 4        		// The number of 32 bit words in a key.
    #define Nr 10       		// The number of rounds in AES Cipher.
#endif

#if defined(CTR) && (CTR == 1)
// 修改时间：2025.12.13
// AES128-CTR 查表法优化

extern int 		m_nTables_generated;
extern uint8_t	m_roundKey[AES_keyExpSize]; 	// 扩展密钥
extern uint8_t  m_nonce[AES_KEYLEN];			// 初始化向量

// public function
void AES_KeyExpansion(const uint8_t* Key);		// 生成轮秘钥
void AES_InitEncryptionTables(void);			// 生成所有预计算表

void AES_CTR_Encrypt_buffer(const uint8_t* key, const uint8_t* nonce, 
							const uint8_t* input, uint8_t* output, size_t data_len);	// 查找表法加密函数

int aes_key_to_base64(const unsigned char *in_buf, int in_len, unsigned char *out_buf);	// 基于base64秘钥转换，避免明文存储
int base64_to_aes_key(const unsigned char *b64_buf,int in_len, unsigned char *out_buf);	// 基于base64秘钥转换，转换为明文
#endif // #if defined(CTR) && (CTR == 1)

#endif // _AES_H_
