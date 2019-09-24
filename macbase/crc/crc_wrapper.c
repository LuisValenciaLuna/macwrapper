#include "crc_wrapper.h"


void CRC_Init(ptrFnc_Transmit p_Callbck_Transmit, ptrFnc_Receive p_Callbck_Receive)
{
	pTransmit = p_Callbck_Transmit;
	pReceive = p_Callbck_Receive;
}

void CRC_calculate(uint16_t dest_address,uint8_t* data, uint8_t data_length)
{
	uint16_t CRC;
	uint16_t* ptr_CRC;
	CRC = crc_16(data,data_length);
	ptr_CRC = &data[data_length];
	*ptr_CRC = CRC;
	pTransmit(dest_address,data,(data_length + sizeof(CRC)));
}

bool CRC_validate(uint8_t* data, uint8_t data_length)
{
	uint16_t CRC;
	uint16_t* ptr_CRC;
	bool bool_CRC_valid = false;
	ptr_CRC = &data[data_length];
	CRC = * ptr_CRC;
	if(CRC == crc_16(data,data_length))
	{
		pReceive(data);
		bool_CRC_valid = true;
	}else
	{
		bool_CRC_valid = false;
	}
	return bool_CRC_valid;
}
