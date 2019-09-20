#include "safesecure.h"

void SafeSecure_Init(ptrFnc_Transmit p_Callbck)
{
	pTransmit = p_Callbck;
}

void SafeSecure_Encrypt(uint16_t dest_address, uint8_t* data, uint8_t data_len)
{
	AES_init_ctx(&ctx, key);
	AES_ECB_encrypt(&ctx, data);
	pTransmit(dest_address,data,data_len);
}

void SafeSecure_Decrypt(uint8_t* data)
{
	AES_init_ctx(&ctx, key);
	AES_ECB_decrypt(&ctx, data);
}

