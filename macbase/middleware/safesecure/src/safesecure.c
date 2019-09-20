#include "safesecure.h"

#define CRCSIZE 2

uint8_t key[] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
struct AES_ctx ctx;
ptrFnc_Transmit pTransmit;
ptrFnc_Receive pReceive;
uint8_t u8MaxSizePkg;

void SafeSecure_Init(ptrFnc_Transmit p_Callback, ptrFnc_Receive p_RecCallback, uint8_t maxSizePkg)
{
	pTransmit = p_Callback;
	pReceive = p_RecCallback;
	u8MaxSizePkg = maxSizePkg;
}

SafeSecureTransmitMsg_t SafeSecure_Transmit(uint16_t dest_address, uint8_t* data, uint8_t data_len)
{
	if (data_len + CRCSIZE <= u8MaxSizePkg)
	{
		// Create buffer
		uint8_t * buffer = (uint8_t *)malloc(data_len + CRCSIZE);
		memcpy(buffer, data, data_len);
		memset(buffer, 0, data_len);

		// Encrypt AES
		AES_init_ctx(&ctx, key);
		AES_ECB_encrypt(&ctx, buffer);

		// CRC header at the end (2 bytes)
		*((uint16_t*)&(buffer[data_len])) = crc_16((unsigned char *)data, data_len);

		// Transmit
		if (0 != pTransmit)
		{
			pTransmit(dest_address, data, data_len);
		}

		free(buffer);
	}
	else return SafeSecureTransmitMsg_MaxSizeExceeded;

	return SafeSecureTransmitMsg_Success;
}

SafeSecureReceiveMsg_t SafeSecure_Decrypt(uint8_t* data, uint8_t dataLen)
{
	if (dataLen >= CRCSIZE)
	{
		uint16_t currentCrc = crc_16((unsigned char *)data, dataLen);

		// Extract CRC from data
		uint16_t crc = *((uint16_t *)&(data[dataLen - CRCSIZE]));

		if (crc == currentCrc)
		{
			uint8_t * buffer = (uint8_t *)malloc(dataLen - CRCSIZE);
			memcpy(buffer, data, dataLen - CRCSIZE);

			AES_init_ctx(&ctx, key);
			AES_ECB_decrypt(&ctx, buffer);

			if (0 != pReceive)
			{
				pReceive(buffer, dataLen);
			}

			free(buffer);
		}
		else return SafeSecureReceiveMsg_CurruptData;
	}
	else return SafeSecureReceiveMsg_CurruptData;

	return SafeSecureReceiveMsg_Success;
}

