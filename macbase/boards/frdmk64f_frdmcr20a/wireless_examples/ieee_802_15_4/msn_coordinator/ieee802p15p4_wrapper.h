/*
 * ieee802p15p4_wrapper.h
 *
 *  Created on: Sep 4, 2019
 *      Author: Nico
 */

#ifndef IEEE802P15P4_WRAPPER_IEEE802P15P4_WRAPPER_H_
#define IEEE802P15P4_WRAPPER_IEEE802P15P4_WRAPPER_H_

#include "MacInterface.h"
/************************************************************************************
*************************************************************************************
* Public macros
*************************************************************************************
************************************************************************************/

/************************************************************************************
*************************************************************************************
* Public type definitions
*************************************************************************************
************************************************************************************/

/* Error codes */
enum
{
  mwErrorNoError,
  mwErrorWrongConfirm,
  mwErrorNotSuccessful,
  mwErrorNoMessage,
  mwErrorAllocFailed,
  mwErrorInvalidParameter,
  mwErrorNoScanResults,
  mwErrorAlreadyInitialized,
  mwErrorAlreadyConnected,
  mwErrorPanAccessDenied,
  mwErrorTransmissionInprogress,
};

typedef enum {
	mac_data_event_c,
	mac_management_event_c,
	mac_max_event_c
}mac_wrapper_event_id_t;

typedef struct _mac_event_data{
	mac_wrapper_event_id_t mac_event_type;
	union{
		/*Check MacInterface.h and MacMessages.h to see the contents of these
		 * MAC structures */
		mcpsToNwkMessage_t* 	data_event_data;
		nwkMessage_t*		management_event_data;
	}evt_data;
}mac_event_data_t;

/******************************************************************************
*******************************************************************************
* Public Prototypes
*******************************************************************************
******************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif
/*FUNCTION**********************************************************************
 *
 * Function Name : mac_init
 * Description   : Initializes the ieee802.15.4 data link layer custom wrapper.
 *                 This is a blocking function.
 *
 * Params: mac_addr - Pointer to a 8 bytes array that contains the board's MAC
 *                    address.
 *
 * Return: int: 0 - success.
 *
 *END**************************************************************************/
extern int mac_init(uint8_t* mac_addr);

/*FUNCTION**********************************************************************
 *
 * Function Name : mac_connect
 * Description   : Commands the node to connect to a ieee802.15.4 network, if
 *                 the network does not exists then the node starts it.
 *                 mac_init() has to be called first.
 *                 This is a non-blocking function. The result of the connection
 *                 request will be recibed in a call to the event handler
 *                 callback (evt_hdlr).
 *
 * Params: channel  - Channel to start the network (11-26).
 *         pan_id   - Id of the network
 *         evt_hdlr - Pointer to a callback function that will be called by the
 *                    mac wrapper to signal events on the MAC layer to the
 *                    calling layer. The events received by this callback are:
 *                    * Data reception.
 *                    * Data Transmission confirm.
 *                    * Network Connection.
 *                    * Network Disconnections
 *
 * Return: int: 0 - success.
 *
 *END**************************************************************************/
extern int mac_connect(uint8_t channel, uint16_t pan_id, void (*evt_hdlr)(void*));

/*FUNCTION**********************************************************************
 *
 * Function Name : mac_transmit
 * Description   : Commands the node to send data to another node over a
 *                 ieee802.15.4 network.
 *                 mac_connect() has to be called before hand.
 *                 This is a non-blocking function. The result of the
 *                 transmission request will be recibed in a call to the event
 *                 handler callback (evt_hdlr).
 *
 *
 * Params: dest_address  - Address of the data's destination node.
 *         data   - Pointer to the data array to transmit
 *         data_len - Size in bytes of the data array.
 *
 * Return: int: 0 - success.
 *
 *END**************************************************************************/
extern int mac_transmit(uint16_t dest_address, uint8_t* data, uint8_t data_len);

#ifdef __cplusplus
}
#endif

/**********************************************************************************/

#endif /* IEEE802P15P4_WRAPPER_IEEE802P15P4_WRAPPER_H_ */
