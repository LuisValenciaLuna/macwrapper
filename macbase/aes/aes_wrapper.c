#include "aes_wrapper.h"
#include "board.h"
#include "aes.h"

void AES_init(ptrFnc_Call p_Callbck)
{
	pFCllbck = p_Callbck;
}

void AES_Encrypt(uint16_t dest_address, uint8_t* data, uint8_t data_len)
{
	AES_init_ctx(&ctx, key);
	AES_ECB_encrypt(&ctx, data);
	pFCllbck(dest_address,data,data_len);
	//pTransmit(dest_address,data,data_len);
}

void AES_Decrypt(uint8_t* data)
{
	AES_init_ctx(&ctx, key);
	AES_ECB_decrypt(&ctx, data);
}

