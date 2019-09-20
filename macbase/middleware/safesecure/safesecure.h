#include "board.h"
#include "aes.h"
#include "checksum.h"

/*****************************
/*  Type definition
/*****************************/

typedef enum
{
	/* Transaction success */
	SafeSecureTransmitMsg_Success,

	/*  */
	SafeSecureTransmitMsg_MaxSizeExceeded,

} SafeSecureTransmitMsg_t;


typedef enum
{
	/* Transaction OK */
	SafeSecureReceiveMsg_Success,

	/*  */
	SafeSecureReceiveMsg_CurruptData,

} SafeSecureReceiveMsg_t;


typedef SafeSecureTransmitMsg_t (* ptrFnc_Transmit)(uint16_t dest_address, uint8_t* data, uint8_t data_len);

typedef SafeSecureReceiveMsg_t (* ptrFnc_Receive)(uint8_t* data, uint8_t datalen);


/*****************************
/*  Functions
/*****************************/

extern void SafeSecure_Init(ptrFnc_Transmit p_Callback, ptrFnc_Receive p_RecCallback, uint8_t maxSizePkg);
extern SafeSecureTransmitMsg_t SafeSecure_Transmit(uint16_t dest_address, uint8_t* data, uint8_t data_len);
extern SafeSecureReceiveMsg_t  SafeSecure_Decrypt(uint8_t* data, uint8_t dataLen);
