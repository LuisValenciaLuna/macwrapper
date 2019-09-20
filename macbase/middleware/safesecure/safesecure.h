#include "board.h"
#include "src/aes.h"

uint8_t key[] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
struct AES_ctx ctx;

typedef void (* ptrFnc_Transmit)(uint16_t dest_address, uint8_t* data, uint8_t data_len);
ptrFnc_Transmit pTransmit;

typedef void (* ptrFnc_Receive)(uint8_t* data);
ptrFnc_Transmit pReceive;

extern void SafeSecure_Init(ptrFnc_Transmit p_Callbck);
extern void SafeSecure_Encrypt(uint16_t dest_address, uint8_t* data, uint8_t data_len);
void SafeSecure_Decrypt(uint8_t* data);
