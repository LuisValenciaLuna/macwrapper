#include "board.h"
#include "checksum.h"

typedef void (* ptrFnc_Transmit)(uint16_t dest_address, uint8_t* data, uint8_t data_len);
ptrFnc_Transmit pTransmit;

typedef void (* ptrFnc_Receive)(uint8_t* data);
ptrFnc_Receive pReceive;

extern void CRC_Init(ptrFnc_Transmit p_Callbck, ptrFnc_Receive p_Callbck_Receive);

extern void CRC_calculate(uint16_t dest_address,uint8_t* data, uint8_t data_length);

extern bool CRC_validate(uint8_t* data, uint8_t data_length);
