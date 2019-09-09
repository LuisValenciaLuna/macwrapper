/*
 * ieee802p15p4_wrapper.c
 *
 *  Created on: Sep 4, 2019
 *      Author: Nico
 */

/*! *********************************************************************************
 *************************************************************************************
 * Include
 *************************************************************************************
 ********************************************************************************** */
/* 802.15.4 */
#include "EmbeddedTypes.h"
#include "RNG_Interface.h"
#include "TimersManager.h"
#include "FunctionLib.h"

#include "ieee802p15p4_wrapper.h"
#include "ieee802p15p4_wrapper_cfg.h"

#include "PhyInterface.h"
#include "fsl_os_abstraction.h"

/************************************************************************************
 *************************************************************************************
 * Private macros
 *************************************************************************************
 ************************************************************************************/
/* The various states of the mac state machine. */
enum
{
	macStateInit,
	macStateScanActiveStart,
	macStateScanActiveWaitConfirm,
	macStateAssociate,
	macStateAssociateWaitConfirm,
	macStateWaitInterval,
	macStateScanEdStart,
	macStateScanEdWaitConfirm,
	macStateStartCoordinator,
	macStateStartCoordinatorWaitConfirm,
	macStateListen
};

/* Events */
#define gAppEvtDummyEvent_c            (1 << 0)
#define gAppEvtStartCoordinator_c      (1 << 1)
#define gAppEvtMessageFromMLME_c       (1 << 2)
#define gAppEvtMessageFromMCPS_c       (1 << 3)
#define mw_event_connect_request_c     (1 << 4)
#define mw_event_transmit_request_c    (1 << 5)
#define gAppEvtStartWait_c             (1 << 6)

/************************************************************************************
 *************************************************************************************
 * Private data types
 *************************************************************************************
 ************************************************************************************/
typedef struct _mw_connect_reqest_data{
	uint8_t channel;
	uint16_t pan_id;
	void (*evt_hdlr)(void*);
}mw_connect_request_data_t;

typedef struct _mw_transmit_reqest_data{
	uint16_t dest_address;
	uint16_t data_len;
	uint8_t  data[0];
}mw_transmit_request_data_t;

/************************************************************************************
 *************************************************************************************
 * Private prototypes
 *************************************************************************************
 ************************************************************************************/
static void mac_task(void* argument);
static uint8_t WaitMsg(nwkMessage_t *pMsg, uint8_t msgType);
static uint8_t StartScan(macScanType_t scanType, uint8_t channel);
static uint8_t HandleScanActiveConfirm( nwkMessage_t *pMsg, uint16_t pan_id );
static void WaitIntervalTimeoutHandler(void *pData);
static uint8_t SendAssociateRequest( void );
static uint8_t HandleAssociateConfirm( nwkMessage_t *pMsg );
static void HandleScanEdConfirm(nwkMessage_t *pMsg);
static uint8_t StartCoordinator(void);
static uint8_t TransmitData(uint16_t dest_address, uint8_t * pData, uint8_t length);
static uint8_t HandleMlmeInput(nwkMessage_t *pMsg);
static uint8_t SendAssociateResponse(nwkMessage_t *pMsgIn);
static void HandleMcpsInput(mcpsToNwkMessage_t *pMsgIn);
static resultType_t MLME_NWK_SapHandler (nwkMessage_t* pMsg, instanceId_t instanceId);
static resultType_t MCPS_NWK_SapHandler (mcpsToNwkMessage_t* pMsg, instanceId_t instanceId);
extern void Mac_SetExtendedAddress(uint8_t *pAddr, instanceId_t instanceId);
static uint8_t HandleAssociateConfirm( nwkMessage_t *pMsg );

/************************************************************************************
 *************************************************************************************
 * Private memory declarations
 *************************************************************************************
 ************************************************************************************/
static uint8_t mac_initialized = FALSE;
static uint8_t mac_connected = FALSE;
static instanceId_t mMacInstance;
static osaEventId_t mac_event;
static uint8_t mac_state;
static mw_connect_request_data_t* connect_request_data_p = NULL;
static mw_transmit_request_data_t* transmit_request_data_p = NULL;

/* Application input queues */
static anchor_t mMlmeNwkInputQueue;
static anchor_t mMcpsNwkInputQueue;

OSA_TASK_DEFINE(mac_task, gMainThreadPriority_c-1, 1, gMainThreadStackSize_c, 0);

static uint16_t mShortAddress = 0xFFFF;
static uint16_t mPanId = 0xFFFF;
static tmrTimerID_t mac_timer = gTmrInvalidTimerID_c;
static void (*mwevt_hdlr)(void*);
/* Data request packet for sending data to the coordinator */
static nwkToMcpsMessage_t *mpPacket = NULL;
/* The MSDU handle is a unique data packet identifier */
static uint8_t mMsduHandle = 0;
/* Information about the PAN we are part of */
static panDescriptor_t mCoordInfo;
/* This is a bit map having on the last 4 bits the OR-ed short addresses
   assigned to end devices associated to the coordinator.
   The addresses generated are: 1, 2, 4 and 8. When this value is 0x0F,
   no more devices can associate. */
static uint8_t mAddressesMap = 0x00;

/* Reserved address for the device that is currently associating */
static uint8_t mReservedAddress = 0x00;
/************************************************************************************
 *************************************************************************************
 * Public memory declarations
 *************************************************************************************
 ************************************************************************************/

/************************************************************************************
 *************************************************************************************
 * Public functions
 *************************************************************************************
 ************************************************************************************/
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
int mac_init(uint8_t* pAddr)
{
	/*****************************************/
	/* Initialize the ieee802.15.4 MAC stack */
	/*****************************************/
	if(mac_initialized) {
		return mwErrorAlreadyInitialized;
	}

	Phy_Init();
	RNG_Init(); /* RNG must be initialized after the PHY is Initialized */
	MAC_Init();

	/* Bind to MAC layer */
	mMacInstance = BindToMAC( (instanceId_t)0 );

	Mac_RegisterSapHandlers( MCPS_NWK_SapHandler, MLME_NWK_SapHandler, mMacInstance );

	/* Prepare input queues.*/
	MSG_InitQueue(&mMlmeNwkInputQueue);
	MSG_InitQueue(&mMcpsNwkInputQueue);
	/* Initialize the MAC 802.15.4 extended address */
	Mac_SetExtendedAddress( pAddr, mMacInstance );

	/******************************/
	/* Initialize the mac wrapper */
	/******************************/
	mac_event = OSA_EventCreate(TRUE);
	mac_state = macStateInit;
	/* Create the mac task */
	OSA_TaskCreate(OSA_TASK(mac_task), NULL);
	mac_timer = TMR_AllocateTimer();

	/****************************/
	/* Initialization completed */
	/****************************/
	mac_initialized = TRUE;
	return 0;
}

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
int mac_connect(uint8_t channel, uint16_t pan_id, void (*evt_hdlr)(void*))
{
	/* The MAC shall not be connected yet and there should not be a connection
	 * in progress */
	if(mac_connected || (connect_request_data_p != NULL)) {
		return mwErrorAlreadyConnected;
	}

	/* Allocate memory for the request */
	connect_request_data_p = MSG_Alloc(sizeof(mw_connect_request_data_t));

	/* Fill the message */
	connect_request_data_p->channel = channel;
	connect_request_data_p->pan_id = pan_id;
	connect_request_data_p->evt_hdlr = evt_hdlr;

	/* Signal the mac wrapper task */
	OSA_EventSet(mac_event, mw_event_connect_request_c);

	return mwErrorNoError;
}

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
int mac_transmit(uint16_t dest_address, uint8_t* data, uint8_t data_len)
{
	/* The MAC shall be connected yet and there should not be a transmission
	 * in progress */
	if(!mac_connected || (transmit_request_data_p != NULL)) {
		return mwErrorAlreadyConnected;
	}

	/* Allocate memory for the request */
	transmit_request_data_p = MSG_Alloc(sizeof(mw_connect_request_data_t) + data_len);

	/* Fill the message */
	transmit_request_data_p->dest_address = dest_address;
	transmit_request_data_p->data_len = data_len;
	FLib_MemCpy(transmit_request_data_p->data, data, data_len);

	/* Signal the mac wrapper task */
	OSA_EventSet(mac_event, mw_event_transmit_request_c);
	return mwErrorNoError;
}

/************************************************************************************
 *************************************************************************************
 * Private functions
 *************************************************************************************
 ************************************************************************************/
/*FUNCTION**********************************************************************
 *
 * Function Name : mac_task
 * Description   : Wrapper over ieee802.15.4 data link layer.
 *
 *END**************************************************************************/
static void mac_task(void* argument)
{
	osaEventFlags_t ev;
	/* Pointer for storing the messages from MLME */
	void *pMsgIn;
	mac_event_data_t event_data;
	/* Stores the status code returned by some functions. */
	uint8_t rc;

	while(1) {
		/* Wait for events */
		OSA_EventWait(mac_event, osaEventFlagsAll_c, FALSE, osaWaitForever_c, &ev);
		pMsgIn = NULL;

		/* Dequeue the MLME message */
		if (ev & gAppEvtMessageFromMLME_c)
		{
			/* Get the message from MLME */
			pMsgIn = MSG_DeQueue(&mMlmeNwkInputQueue);

			/* Any time a beacon might arrive. Always handle the beacon frame first */
			if (pMsgIn)
			{
				rc = WaitMsg(pMsgIn, gMlmeBeaconNotifyInd_c);
				if(rc == mwErrorNoError)
				{
					/* ALWAYS free the beacon frame contained in the beacon notify indication.*/
					/* ALSO the application can use the beacon payload.*/
					MSG_Free(((nwkMessage_t *)pMsgIn)->msgData.beaconNotifyInd.pBufferRoot);
				}
			}
		}

		switch(mac_state)
		{
		/* State machine */
		case macStateInit:
			if((ev & mw_event_connect_request_c) && (NULL != connect_request_data_p)) {
				/* Goto Energy Detection state. */
				mac_state = macStateScanActiveStart;
				OSA_EventSet(mac_event, gAppEvtDummyEvent_c );
			}
			break;

		case macStateScanActiveStart:
			/* Start the Active scan, and goto wait for confirm state. */

			rc = StartScan( gScanModeActive_c, connect_request_data_p->channel );
			if( rc == mwErrorNoError )
			{
				mac_state = macStateScanActiveWaitConfirm;
			}
			break;

		case macStateScanActiveWaitConfirm:
			/* Stay in this state until the Scan confirm message arrives, and then goto
			 the associate state or do a rescan in case of invalid short address.     */
			if( ev & gAppEvtMessageFromMLME_c )
			{
				if( pMsgIn )
				{
					/* Handle the Scan Confirm message. */
					rc = WaitMsg( pMsgIn, gMlmeScanCnf_c );
					if( rc == mwErrorNoError )
					{
						rc = HandleScanActiveConfirm( pMsgIn, connect_request_data_p->pan_id );
						if( rc == mwErrorNoError )
						{
							mac_state = macStateAssociate;
							OSA_EventSet(mac_event, gAppEvtDummyEvent_c );
						}
						else
						{
							/* Restart scanning */
							TMR_StartSingleShotTimer( mac_timer, 7000, WaitIntervalTimeoutHandler, NULL );
							mac_state = macStateWaitInterval;
						}
					}
				}
			}
			break;

		case macStateWaitInterval:
			if( ev & gAppEvtStartWait_c )
			{
				/* Since the network does not exist on this channel then we can start it */
				mac_state = macStateScanEdStart;
				OSA_EventSet(mac_event, gAppEvtDummyEvent_c );
			}
			break;

		case macStateAssociate:
			/* Associate to the PAN coordinator */
			rc = SendAssociateRequest();
			if( rc == mwErrorNoError )
			{
				mac_state = macStateAssociateWaitConfirm;
			}
			break;

		case macStateAssociateWaitConfirm:
			/* Stay in this state until the Associate confirm message
			 arrives, and then goto the Listen state. */
			if( ev & gAppEvtMessageFromMLME_c )
			{
				if( pMsgIn )
				{
					rc = WaitMsg( pMsgIn, gMlmeAssociateCnf_c );
					if( rc == mwErrorNoError )
					{
						/* Check for coordinator at full capacity error */
						if( HandleAssociateConfirm(pMsgIn) == mwErrorNoError )
						{
							/*Call the upper layer callback */
							mwevt_hdlr = connect_request_data_p->evt_hdlr;
							if(mwevt_hdlr != NULL) {
								event_data.mac_event_type = mac_management_event_c;
								event_data.evt_data.management_event_data = (nwkMessage_t*)pMsgIn;
								mwevt_hdlr((void*)&event_data);
							}
							/* Release the connection request memory */
							MSG_Free(connect_request_data_p);
							connect_request_data_p = NULL;
							mac_connected = TRUE;
							/* Go to listen state */
							mac_state = macStateListen;
							OSA_EventSet(mac_event, gAppEvtDummyEvent_c);
						}
						else
						{
							/* Restart scanning */
							TMR_StartSingleShotTimer( mac_timer, 7000, WaitIntervalTimeoutHandler, NULL );
							mac_state = macStateWaitInterval;
						}
					}
				}
			}
			break;

		case macStateScanEdStart:
			/* Start the Energy Detection scan, and goto wait for confirm state. */
			rc = StartScan(gScanModeED_c, connect_request_data_p->channel);
			if(rc == mwErrorNoError)
			{
				mac_state = macStateScanEdWaitConfirm;
			}
			break;

		case macStateScanEdWaitConfirm:
			/* Stay in this state until the MLME Scan confirm message arrives,
			 and has been processed. Then goto Start Coordinator state. */
			if (ev & gAppEvtMessageFromMLME_c)
			{
				if (pMsgIn)
				{
					rc = WaitMsg(pMsgIn, gMlmeScanCnf_c);
					if(rc == mwErrorNoError)
					{
						/* Process the ED scan confirm. The logical
						 channel is selected by this function. */
						HandleScanEdConfirm(pMsgIn);
						mac_state =macStateStartCoordinator;
						OSA_EventSet(mac_event, gAppEvtStartCoordinator_c);
					}
				}
			}
			break;

		case macStateStartCoordinator:
			/* Start up as a PAN Coordinator on the selected channel. */
			if (ev & gAppEvtStartCoordinator_c)
			{
				rc = StartCoordinator();
				if(rc == mwErrorNoError)
				{
					/* If the Start request was sent successfully to
					 the MLME, then goto Wait for confirm state. */
					mac_state = macStateStartCoordinatorWaitConfirm;
				}
			}
			break;

		case macStateStartCoordinatorWaitConfirm:
			/* Stay in this state until the Start confirm message
			 arrives, and then goto the Listen state. */
			if (ev & gAppEvtMessageFromMLME_c)
			{
				if (pMsgIn)
				{
					rc = WaitMsg(pMsgIn, gMlmeStartCnf_c);
					if(rc == mwErrorNoError)
					{
						mPanId = connect_request_data_p->pan_id;
						/*Call the upper layer callback */
						mwevt_hdlr = connect_request_data_p->evt_hdlr;
						if(mwevt_hdlr != NULL) {
							event_data.mac_event_type = mac_management_event_c;
							event_data.evt_data.management_event_data = (nwkMessage_t*)pMsgIn;
							mwevt_hdlr((void*)&event_data);
						}
						/* Release the connection request memory */
						MSG_Free(connect_request_data_p);
						connect_request_data_p = NULL;
						mac_state = macStateListen;
						mac_connected = TRUE;
						OSA_EventSet(mac_event, gAppEvtDummyEvent_c);
					}
				}
			}
			break;
		case macStateListen:
			/* Stay in this state forever.
			 Transmit to end device data from serial terminal interface */
			if (ev & gAppEvtMessageFromMLME_c)
			{
				/* Get the message from MLME */
				if (pMsgIn)
				{
					/* Process it */
					HandleMlmeInput(pMsgIn);
					/*Call the upper layer callback */
					if(mwevt_hdlr != NULL) {
						event_data.mac_event_type = mac_management_event_c;
						event_data.evt_data.management_event_data = (nwkMessage_t*)pMsgIn;
						mwevt_hdlr((void*)&event_data);
					}
					/* Messages from the MLME must always be freed. */
				}
			}

			if (ev & mw_event_transmit_request_c)
			{
				TransmitData(transmit_request_data_p->dest_address,
						transmit_request_data_p->data,
						transmit_request_data_p->data_len);
			}

			break;
		} /* end switch*/

		/* Free Mlme messages*/
		if (pMsgIn)
		{
			/* Messages must always be freed. */
			MSG_Free(pMsgIn);
			pMsgIn = NULL;
		}

		/* Handle MCPS confirms and transmit data from serial terminal interface */
		if (ev & gAppEvtMessageFromMCPS_c)
		{
			/* Get the message from MCPS */
			pMsgIn = MSG_DeQueue(&mMcpsNwkInputQueue);
			if (pMsgIn)
			{
				/* Process it */
				HandleMcpsInput(pMsgIn);
				/*Call the upper layer callback */
				if(mwevt_hdlr != NULL) {
					event_data.mac_event_type = mac_data_event_c;
					event_data.evt_data.data_event_data = (mcpsToNwkMessage_t*)pMsgIn;
					mwevt_hdlr((void*)&event_data);
				}
				/* Messages from the MCPS must always be freed. */
				MSG_Free(pMsgIn);
				pMsgIn = NULL;
			}
		}

		/* Check for pending messages in the Queue */
		if(MSG_Pending(&mMcpsNwkInputQueue))
			OSA_EventSet(mac_event, gAppEvtMessageFromMCPS_c);
		if(MSG_Pending(&mMlmeNwkInputQueue))
			OSA_EventSet(mac_event, gAppEvtMessageFromMLME_c);




	}
}

/******************************************************************************
 * The WaitMsg(nwkMessage_t *pMsg, uint8_t msgType) function does not, as
 * the name implies, wait for a message, thus blocking the execution of the
 * state machine. Instead the function analyzes the supplied message to determine
 * whether or not the message is of the expected type.
 * The function may return either of the following values:
 *   mwErrorNoError: The message was of the expected type.
 *   mwErrorNoMessage: The message pointer is NULL.
 *   mwErrorWrongConfirm: The message is not of the expected type.
 *
 ******************************************************************************/
static uint8_t WaitMsg(nwkMessage_t *pMsg, uint8_t msgType)
{
	/* Do we have a message? If not, the exit with error code */
	if(pMsg == NULL)
	{
		return mwErrorNoMessage;
	}
	/* Is it the expected message type? If not then exit with error code */
	if(pMsg->msgType != msgType)
	{
		return mwErrorWrongConfirm;
	}

	/* Found the expected message. Return with success code */
	return mwErrorNoError;
}

/******************************************************************************
 * The StartScan(scanType) function will start the scan process of the
 * specified type in the MAC. This is accomplished by allocating a MAC message,
 * which is then assigned the desired scan parameters and sent to the MLME
 * service access point.
 * The function may return either of the following values:
 *   mwErrorNoError:          The Scan message was sent successfully.
 *   mwErrorInvalidParameter: The MLME service access point rejected the
 *                          message due to an invalid parameter.
 *   mwErrorAllocFailed:      A message buffer could not be allocated.
 *
 ******************************************************************************/
static uint8_t StartScan(macScanType_t scanType, uint8_t channel)
{
	mlmeMessage_t *pMsg;
	mlmeScanReq_t *pScanReq;

	//Serial_Print(mInterfaceId,"Sending the MLME-Scan Request message to the MAC... ", gAllowToBlock_d);

	/* Allocate a message for the MLME (We should check for NULL). */
	pMsg = MSG_AllocType(mlmeMessage_t);
	if(pMsg != NULL)
	{
		/* This is a MLME-SCAN.req command */
		pMsg->msgType = gMlmeScanReq_c;
		/* Create the Scan request message data. */
		pScanReq = &pMsg->msgData.scanReq;
		/* gScanModeED_c, gScanModeActive_c, gScanModePassive_c, or gScanModeOrphan_c */
		pScanReq->scanType = scanType;
		/* ChannelsToScan */
		pScanReq->scanChannels = 0x01<<channel;
		/* Duration per channel 0-14 (dc). T[sec] = (16*960*((2^dc)+1))/1000000.
    A scan duration of 5 on 16 channels approximately takes 8 secs. */
		pScanReq->scanDuration = 5;
		pScanReq->securityLevel = gMacSecurityNone_c;

		/* Send the Scan request to the MLME. */
		if(NWK_MLME_SapHandler( pMsg, mMacInstance ) == gSuccess_c)
		{
			//Serial_Print(mInterfaceId,"Done.\n\r", gAllowToBlock_d);
			return mwErrorNoError;
		}
		else
		{
			//Serial_Print(mInterfaceId,"Invalid parameter.\n\r", gAllowToBlock_d);
			return mwErrorInvalidParameter;
		}
	}
	else
	{
		/* Allocation of a message buffer failed. */
		//Serial_Print(mInterfaceId,"Message allocation failed.\n\r", gAllowToBlock_d);
		return mwErrorAllocFailed;
	}
}

/******************************************************************************
 * The HandleScanActiveConfirm(nwkMessage_t *pMsg) function will handle the
 * Active Scan confirm message received from the MLME when the Active scan has
 * completed. The message contains a list of PAN descriptors. the requested
 * coordinator is chosen. The corresponding pan descriptor is stored in the
 * global variable mCoordInfo.
 *
 * The function may return either of the following values:
 *   mwErrorNoError:       A suitable pan descriptor was found.
 *   mwErrorNoScanResults: No scan results were present in the confirm message.
 *
 ******************************************************************************/
static uint8_t HandleScanActiveConfirm( nwkMessage_t *pMsg, uint16_t pan_id )
{
	void    *pBlock;
	uint8_t panDescListSize = pMsg->msgData.scanCnf.resultListSize;
	uint8_t rc = mwErrorNoScanResults;
	uint8_t j;
	panDescriptorBlock_t *pDescBlock = pMsg->msgData.scanCnf.resList.pPanDescriptorBlockList;
	panDescriptor_t      *pPanDesc;

	/* Check if the scan resulted in any coordinator responses. */
	if( panDescListSize > 0 )
	{
		/* Check all PAN descriptors. */
		while( NULL != pDescBlock )
		{
			for( j = 0; j < pDescBlock->panDescriptorCount; j++ )
			{
				pPanDesc = &pDescBlock->panDescriptorList[j];

				/* Only attempt to associate if the coordinator
                accepts associations and is non-beacon. */
				if( ( pPanDesc->superframeSpec.associationPermit ) &&
						( pPanDesc->superframeSpec.beaconOrder == 0x0F) )
				{
					/* Find the requested coordinator. */
					if( pPanDesc->coordPanId == pan_id )
					{
						/* Save the information of the coordinator  */
						FLib_MemCpy( &mCoordInfo, pPanDesc, sizeof(panDescriptor_t) );
						rc = mwErrorNoError;
					}
				}
			}

			/* Free current block */
			pBlock = pDescBlock;
			pDescBlock = pDescBlock->pNext;
			MSG_Free( pBlock );
		}
	}

	if( pDescBlock )
		MSG_Free( pDescBlock );

	return rc;
}

/******************************************************************************
 * The IntervalTimeoutHandler(uint8_t timerID) function will send an event
 * to the application task after the programmed interval of time expires
 *
 ******************************************************************************/
static void WaitIntervalTimeoutHandler(void *pData)
{
	(void)pData;
	OSA_EventSet(mac_event, gAppEvtStartWait_c);
}

/******************************************************************************
 * The SendAssociateRequest(void) will create an Associate Request message
 * and send it to the coordinator it wishes to associate to. The function uses
 * information gained about the coordinator during the scan procedure.
 *
 * The function may return either of the following values:
 *   mwErrorNoError:          The Associate Request message was sent successfully.
 *   mwErrorInvalidParameter: The MLME service access point rejected the
 *                          message due to an invalid parameter.
 *   mwErrorAllocFailed:      A message buffer could not be allocated.
 *
 ******************************************************************************/
static uint8_t SendAssociateRequest( void )
{
	mlmeMessage_t *pMsg;
	mlmeAssociateReq_t *pAssocReq;
	uint8_t boolFlag;

	/* Allocate a message for the MLME message. */
	pMsg = MSG_AllocType( mlmeMessage_t );
	if( pMsg != NULL )
	{
		/* Set the gMPibRxOnWhenIdle_c flag to true, this will allow direct data messages */
		pMsg->msgType = gMlmeSetReq_c;
		pMsg->msgData.setReq.pibAttribute = gMPibRxOnWhenIdle_c;
		boolFlag = TRUE;
		pMsg->msgData.setReq.pibAttributeValue = &boolFlag;
		NWK_MLME_SapHandler( pMsg, mMacInstance );

		/* We must set the Association Permit flag to TRUE
        in order to allow devices to associate to us. */
		/* We can re-use the pMsg data because gMlmeSetReq_c is a synchronous request */
		/* This is a MLME-ASSOCIATE.req command. */
		pMsg->msgType = gMlmeAssociateReq_c;
		/* Create the Associate request message data. */
		pAssocReq = &pMsg->msgData.associateReq;

		/* Use the coordinator info we got from the Active Scan. */
		FLib_MemCpy(&pAssocReq->coordAddress, &mCoordInfo.coordAddress, 8);
		FLib_MemCpy(&pAssocReq->coordPanId,   &mCoordInfo.coordPanId, 2);
		pAssocReq->coordAddrMode  = mCoordInfo.coordAddrMode;
		pAssocReq->logicalChannel = mCoordInfo.logicalChannel;
		pAssocReq->securityLevel  = gMacSecurityNone_c;
		pAssocReq->channelPage = gDefaultChannelPageId_c;

		/* We want the coordinator to assign a short address to us. */
		pAssocReq->capabilityInfo     = gCapInfoAllocAddr_c;

		/* Send the Associate Request to the MLME. */
		if( NWK_MLME_SapHandler( pMsg, mMacInstance ) == gSuccess_c )
		{
			return mwErrorNoError;
		}
		else
		{
			/* One or more parameters in the message were invalid. */
			return mwErrorInvalidParameter;
		}
	}
	else
	{
		/* Allocation of a message buffer failed. */
		return mwErrorAllocFailed;
	}
}


/******************************************************************************
 * The HandleAssociateConfirm(nwkMessage_t *pMsg) function will handle the
 * Associate confirm message received from the MLME when the Association
 * procedure has completed. The message contains the short address that the
 * coordinator has assigned to us. The address and address mode are saved in
 * global variables. They will be used in the next demo application when sending
 * data .
 *
 * The function may return either of the following values:
 *   gSuccess_c:          Correct short address
 *   gPanAccessDenied_c:  Invalid short address
 *
 ******************************************************************************/
static uint8_t HandleAssociateConfirm( nwkMessage_t *pMsg )
{
	if( pMsg->msgData.associateCnf.status == gSuccess_c )
	{
		FLib_MemCpy( &mShortAddress, &pMsg->msgData.associateCnf.assocShortAddress, 2 );
		mPanId = connect_request_data_p->pan_id;
		return mwErrorNoError;
	}
	else
	{
		/* No valid short address. */
		return mwErrorPanAccessDenied;
	}
}

/******************************************************************************
 * The HandleScanEdConfirm(nwkMessage_t *pMsg) function will handle the
 * ED scan confirm message received from the MLME when the ED scan has completed.
 * The message contains the ED scan result list. This function will search the
 * list in order to select the logical channel with the least energy. The
 * selected channel is stored in the global variable called 'mLogicalChannel'.
 *
 ******************************************************************************/
static void HandleScanEdConfirm(nwkMessage_t *pMsg)
{
	uint8_t *pEdList;

	/* Get a pointer to the energy detect results */
	pEdList = pMsg->msgData.scanCnf.resList.pEnergyDetectList;

	/* The list of detected energies must be freed. */
	MSG_Free(pEdList);
}

/******************************************************************************
 * The App_StartScan(scanType) function will start the scan process of the
 * specified type in the MAC. This is accomplished by allocating a MAC message,
 * which is then assigned the desired scan parameters and sent to the MLME
 * service access point. The MAC PIB attributes "macShortAddress", and
 * "macAssociatePermit" are modified.
 *
 * The function may return either of the following values:
 *   mwErrorNoError:          The Scan message was sent successfully.
 *   mwErrorInvalidParameter: The MLME service access point rejected the
 *                          message due to an invalid parameter.
 *   mwErrorAllocFailed:      A message buffer could not be allocated.
 *
 ******************************************************************************/
static uint8_t StartCoordinator(void)
{
	/* Message for the MLME will be allocated and attached to this pointer */
	mlmeMessage_t *pMsg;

	/* Allocate a message for the MLME (We should check for NULL). */
	pMsg = MSG_AllocType(mlmeMessage_t);
	if(pMsg != NULL)
	{
		/* Pointer which is used for easy access inside the allocated message */
		mlmeStartReq_t *pStartReq;
		/* Return value from MSG_send - used for avoiding compiler warnings */
		uint8_t ret = 0;
		/* Boolean value that will be written to the MAC PIB */
		uint8_t boolFlag;

		(void) ret;     // remove compiler warning
		/* Set-up MAC PIB attributes. Please note that Set, Get, and Reset
       messages are not freed by the MLME. We must always set the short
       address to something else than 0xFFFF before starting a PAN.
		 */
		mShortAddress = 0x0000;
		pMsg->msgType = gMlmeSetReq_c;
		pMsg->msgData.setReq.pibAttribute = gMPibShortAddress_c;
		pMsg->msgData.setReq.pibAttributeValue = (uint8_t *)&mShortAddress;
		ret = NWK_MLME_SapHandler( pMsg, mMacInstance );

		/* We must set the Association Permit flag to TRUE
    in order to allow devices to associate to us. */
		pMsg->msgType = gMlmeSetReq_c;
		pMsg->msgData.setReq.pibAttribute = gMPibAssociationPermit_c;
		boolFlag = TRUE;
		pMsg->msgData.setReq.pibAttributeValue = &boolFlag;
		ret = NWK_MLME_SapHandler( pMsg, mMacInstance );

		/* This is a MLME-START.req command */
		pMsg->msgType = gMlmeStartReq_c;
		/* Create the Start request message data. */
		pStartReq = &pMsg->msgData.startReq;
		/* PAN ID - LSB, MSB. The demo shows a PAN ID of 0xAAAA. */
		FLib_MemCpy(&pStartReq->panId, (void*)&connect_request_data_p->pan_id, 2);
		/* Logical Channel - the default of 11 will be overridden */
		pStartReq->logicalChannel = connect_request_data_p->channel;

		/* Beacon Order - 0xF = turn off beacons */
		pStartReq->beaconOrder = 0x0F;
		/* Superframe Order - 0xF = turn off beacons */
		pStartReq->superframeOrder = 0x0F;
		/* Be a PAN coordinator */
		pStartReq->panCoordinator = TRUE;
		/* Dont use battery life extension */
		pStartReq->batteryLifeExtension = FALSE;
		/* This is not a Realignment command */
		pStartReq->coordRealignment = FALSE;
		/* Dont use security */
		pStartReq->coordRealignSecurityLevel = gMacSecurityNone_c;
		pStartReq->beaconSecurityLevel = gMacSecurityNone_c;

		/* Send the Start request to the MLME. */
		if(NWK_MLME_SapHandler( pMsg, mMacInstance ) == gSuccess_c)
		{
			return mwErrorNoError;
		}
		else
		{
			/* One or more parameters in the Start Request message were invalid. */
			return mwErrorInvalidParameter;
		}
	}
	else
	{
		/* Allocation of a message buffer failed. */
		return mwErrorAllocFailed;
	}
}

/******************************************************************************
 * The TransmitData() function will perform data transmissions of data.
 *
 * The function uses the address map, that was updated when a device associates,
 * to build MCPS-Data Request messages, one for every associated device. The
 * messages are sent to the MCPS service access point in the MAC.
 ******************************************************************************/
static uint8_t TransmitData(uint16_t dest_address, uint8_t * pData, uint8_t length)
{
	uint8_t ret = 0;

	(void) ret;       // remove compiler warning

	/* For every device associated, if there is still room in the queue
  allocate and send a packet */
	if((length != 0) && (pData != NULL))
	{
		/* Transmit packet to the device */
		if (NULL == mpPacket)
		{
			mpPacket = MSG_Alloc(sizeof(nwkToMcpsMessage_t) + gMaxPHYPacketSize_c);

			if (mpPacket != NULL)
			{
				/* Create an MCPS-Data Request message containing the data. */
				mpPacket->msgType = gMcpsDataReq_c;
				/* Copy data to be sent to packet */
				mpPacket->msgData.dataReq.pMsdu = (uint8_t*)(&(mpPacket->msgData.dataReq.pMsdu)) + sizeof(uint8_t*);
				FLib_MemCpy(mpPacket->msgData.dataReq.pMsdu, (void *)pData, length);
				/* Create the header using device information stored when creating
          the association response. In this simple example the use of short
          addresses is hardcoded. In a real world application we must be
          flexible, and use the address mode required by the given situation. */
				mpPacket->msgData.dataReq.dstAddr = dest_address;

				FLib_MemCpy(&mpPacket->msgData.dataReq.srcAddr,  (void*)&mShortAddress, 2);
				FLib_MemCpy(&mpPacket->msgData.dataReq.dstPanId, (void*)&mPanId, 2);
				FLib_MemCpy(&mpPacket->msgData.dataReq.srcPanId, (void*)&mPanId, 2);
				mpPacket->msgData.dataReq.dstAddrMode = gAddrModeShortAddress_c;
				mpPacket->msgData.dataReq.srcAddrMode = gAddrModeShortAddress_c;
				mpPacket->msgData.dataReq.msduLength = length;
				/* Request MAC level acknowledgement, and
          indirect transmission of the data packet */
				mpPacket->msgData.dataReq.txOptions = gMacTxOptionsAck_c;
				/* Give the data packet a handle. The handle is
          returned in the MCPS-Data Confirm message. */
				mpPacket->msgData.dataReq.msduHandle = mMsduHandle++;
				mpPacket->msgData.dataReq.securityLevel = gMacSecurityNone_c;

				/* Send the Data Request to the MCPS */
				(void)NWK_MCPS_SapHandler(mpPacket, mMacInstance);

			}
			else
			{
				return mwErrorAllocFailed;
			}
		}
		else
		{
			return mwErrorTransmissionInprogress;
		}
	}
	else {
		return mwErrorInvalidParameter;
	}
	return mwErrorNoError;
}

/******************************************************************************
 * The HandleMlmeInput(nwkMessage_t *pMsg) function will handle various
 * messages from the MLME, e.g. (Dis)Associate Indication.
 *
 * The function may return either of the following values:
 *   errorNoError:   The message was processed.
 *   errorNoMessage: The message pointer is NULL.
 ******************************************************************************/
static uint8_t HandleMlmeInput(nwkMessage_t *pMsg)
{
	if(pMsg == NULL)
	{
		return mwErrorNoMessage;
	}

	/* Handle the incoming message. The type determines the sort of processing.*/
	switch(pMsg->msgType)
	{
	case gMlmeAssociateInd_c:
		/* A device sent us an Associate Request. We must send back a response.  */
		return SendAssociateResponse(pMsg);
		break;

	case gMlmeCommStatusInd_c:
		/* Sent by the MLME after the Association Response has been transmitted. */
		switch(pMsg->msgData.commStatusInd.status)
		{
		case gSuccess_c:
			/*Device successfully associated. Storing reserved address.*/
			mAddressesMap |= mReservedAddress;
			mReservedAddress = 0x00;
			break;

		case gTransactionExpired_c:
			/*Association response expired. Device not associated. Dropping reserved address.*/
			mReservedAddress = 0x00;
			break;

		case  gNoAck_c:
			/*ACK not received for Association Response. Device not associated. Dropping reserved address.*/
			mReservedAddress = 0x00;
			break;

		default:
			mReservedAddress = 0x00;
		}
		break;
		default:
			break;
	}
	return mwErrorNoError;
}

/******************************************************************************
 * The SendAssociateResponse(nwkMessage_t *pMsgIn) will create the response
 * message to an Associate Indication (device sends an Associate Request to its
 * MAC. The request is transmitted to the coordinator where it is converted into
 * an Associate Indication). This function will extract the devices long address,
 * and various other flags from the incoming indication message for building the
 * response message.
 *
 * The function may return either of the following values:
 *   errorNoError:          The Associate Response message was sent successfully.
 *   errorInvalidParameter: The MLME service access point rejected the
 *                          message due to an invalid parameter.
 *   errorAllocFailed:      A message buffer could not be allocated.
 *
 ******************************************************************************/
static uint8_t SendAssociateResponse(nwkMessage_t *pMsgIn)
{
	mlmeMessage_t *pMsg;
	mlmeAssociateRes_t *pAssocRes;
	uint8_t selectedAddress = 0x01;
	resultType_t requestResolution = gSuccess_c;

	if(mReservedAddress)
	{
		return mwErrorNoError;
	}

	/* Allocate a message for the MLME */
	pMsg = MSG_AllocType(mlmeMessage_t);
	if(pMsg != NULL)
	{
		/* This is a MLME-ASSOCIATE.res command */
		pMsg->msgType = gMlmeAssociateRes_c;

		/* Create the Associate response message data. */
		pAssocRes = &pMsg->msgData.associateRes;

		/* See if we still have space. */
		if(0x0F > mAddressesMap)
		{
			/* We can assign 1, 2, 4 and 8 as a short address. Check the map and determine
      the first free address. */
			while((selectedAddress & mAddressesMap) != 0)
			{
				selectedAddress = selectedAddress << 1;
			}

			pAssocRes->assocShortAddress = selectedAddress;

			/* Association granted.*/
			requestResolution = gSuccess_c;
		}
		else
		{
			/* Signal that we do not have a valid short address. */
			pAssocRes->assocShortAddress = 0xFFFE;
			requestResolution = gPanAtCapacity_c;
		}

		/* Association  resolution. */
		pAssocRes->status = requestResolution;

		/* Do not use security */
		pAssocRes->securityLevel = gMacSecurityNone_c;

		/* Get the 64 bit address of the device requesting association. */
		FLib_MemCpy(&pAssocRes->deviceAddress, &pMsgIn->msgData.associateInd.deviceAddress, 8);

		/* Send the Associate Response to the MLME. */
		if(NWK_MLME_SapHandler( pMsg, mMacInstance ) == gSuccess_c)
		{
			if(gSuccess_c == requestResolution)
			{
				mReservedAddress |= selectedAddress;
			}
			return mwErrorNoError;
		}
		else
		{
			/* One or more parameters in the message were invalid. */
			return mwErrorInvalidParameter;
		}
	}
	else
	{
		/* Allocation of a message buffer failed. */
		return mwErrorAllocFailed;
	}
}

/******************************************************************************
 * The HandleMcpsInput(mcpsToNwkMessage_t *pMsgIn) function will handle
 * messages from the MCPS, e.g. Data Confirm, and Data Indication.
 *
 ******************************************************************************/
static void HandleMcpsInput(mcpsToNwkMessage_t *pMsgIn)
{
	uint8_t ret = 0;

	(void) ret;       // remove compiler warning
	switch(pMsgIn->msgType)
	{
	/* The MCPS-Data confirm is sent by the MAC to the network
    or application layer when data has been sent. */
	case gMcpsDataCnf_c:
		if(mpPacket){
			MSG_Free(mpPacket);
			mpPacket = NULL;
		}

		if(transmit_request_data_p){
			MSG_Free(transmit_request_data_p);
			transmit_request_data_p = NULL;
		}

		break;

	case gMcpsDataInd_c:
		break;

	case gMcpsPurgeCnf_c:
		break;

	default:
		break;
	}
}


/******************************************************************************
 * The following functions are called by the MAC to put messages into the
 * Application's queue. They need to be defined even if they are not used
 * in order to avoid linker errors.
 ******************************************************************************/

static resultType_t MLME_NWK_SapHandler (nwkMessage_t* pMsg, instanceId_t instanceId)
{
	/* Put the incoming MLME message in the applications input queue. */
	MSG_Queue(&mMlmeNwkInputQueue, pMsg);
	OSA_EventSet(mac_event, gAppEvtMessageFromMLME_c);
	return gSuccess_c;
}

static resultType_t MCPS_NWK_SapHandler (mcpsToNwkMessage_t* pMsg, instanceId_t instanceId)
{
	/* Put the incoming MCPS message in the applications input queue. */
	MSG_Queue(&mMcpsNwkInputQueue, pMsg);
	OSA_EventSet(mac_event, gAppEvtMessageFromMCPS_c);
	return gSuccess_c;
}




