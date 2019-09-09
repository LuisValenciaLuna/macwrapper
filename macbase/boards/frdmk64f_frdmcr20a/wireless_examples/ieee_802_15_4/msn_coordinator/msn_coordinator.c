/*!

 */

/*! *********************************************************************************
 *************************************************************************************
 * Include
 *************************************************************************************
 ********************************************************************************** */
#include "ieee802p15p4_wrapper/ieee802p15p4_wrapper.h"

/* Drv */
#include "LED.h"
#include "Keyboard.h"

/* Fwk */
#include "SerialManager.h"
#include "TimersManager.h"
#include "FunctionLib.h"

/* KSDK */
#include "board.h"
#include "fsl_os_abstraction.h"

/************************************************************************************
 *************************************************************************************
 * Private macros
 *************************************************************************************
 ************************************************************************************/
enum
{
	stateInit,
	waitConnectionResponse,
	stateConnected
};

#define mDefaultValueOfDataLen_c               20
#define gMessageMarkCR_c   0x0D

/* Events */
#define gAppEvtDummyEvent_c             (1 << 0)
#define gAppEvtRxFromComm_c             (1 << 1)
#define gAppEvtMacManagement_c			(1 << 2)
#define gAppEvtMacData_c				(1 << 2)

enum
{
  errorNoError,
  errorWrongConfirm,
  errorNotSuccessful,
  errorNoMessage,
  errorAllocFailed,
  errorInvalidParameter,
  errorNoScanResults
};

/************************************************************************************
 *************************************************************************************
 * Private prototypes
 *************************************************************************************
 ************************************************************************************/
static void    App_CommRxCallBack(void*);
static void    App_CommSendDeviceInfo(void);
static void    App_UpdateLEDs(void);
static void    App_HandleKeys(key_event_t events);

void App_init( void );
void AppThread (uint32_t argument);

/************************************************************************************
 *************************************************************************************
 * Private memory declarations
 *************************************************************************************
 ************************************************************************************/

/************************************************************** 
 * The following initializations can be modified by the user to
 * better fit the needs, without affecting the way that the app
 * is working. 
 **************************************************************/

static const uint16_t mShortAddress = mDefaultValueOfShortAddress_c;
static const uint16_t mPanId = mDefaultValueOfPanId_c;

/* Current code to be displayed on the end devices LED. Also
   used as internal time for the application - to track how much
   time a packet remains in the indirect queue. */
static uint8_t mCounterLEDs = 0;

osaEventId_t mAppEvent;

/************************************************************************************
 *************************************************************************************
 * Public memory declarations
 *************************************************************************************
 ************************************************************************************/

/* The current state of the applications state machine */
uint8_t gState;

/************************************************************************************
 *************************************************************************************
 * Public functions
 *************************************************************************************
 ************************************************************************************/

/*! *********************************************************************************
 * \brief  This is the first task created by the OS. This task will initialize
 *         the system
 *
 * \param[in]  argument
 *
 * \return  None.
 *
 * \pre
 *
 * \post
 *
 * \remarks
 *
 ********************************************************************************** */
void main_task(uint32_t param)
{
	static uint8_t initialized = FALSE;
	uint8_t mac_address[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01};

	if( !initialized )
	{
		initialized = TRUE;
		hardware_init();

		MEM_Init();
		TMR_Init();
		LED_Init();
		SecLib_Init();
		SerialManager_Init();
		mac_init(mac_address);
		App_init();
	}

	/* Call application task */
	AppThread( param );
}

/*****************************************************************************
 * Initialization function for the App Task. This is called during
 * initialization and should contain any application specific initialization
 * (ie. hardware initialization/setup, table initialization, power up
 * notification.
 *
 * Interface assumptions: None
 *
 * Return value: None
 *
 *****************************************************************************/
void App_init(void)
{
	mAppEvent = OSA_EventCreate(TRUE);
	/* The initial application state */
	gState = stateInit;

	mSoftTimerId_c = TMR_AllocateTimer();

	/* Register keyboard callback function */
	KBD_Init(App_HandleKeys);

	/* Initialize the serial terminal interface so that we can print out status messages */
	Serial_InitInterface(&mInterfaceId, APP_SERIAL_INTERFACE_TYPE, APP_SERIAL_INTERFACE_INSTANCE);
	Serial_SetBaudRate(mInterfaceId, gUARTBaudRate57600_c);
	Serial_SetRxCallBack(mInterfaceId, App_CommRxCallBack, NULL);

	/*signal app ready*/
	LED_StartSerialFlash(LED1);

	Serial_Print(mInterfaceId,"\n\rPress any switch on board to start running the application.\n\r", gAllowToBlock_d);
}

void mac_events_handler(void* evt_data)
{
	mac_event_data_t* mac_evt_data = (mac_event_data_t* ) evt_data;

	switch(mac_evt_data->mac_event_type) {
	case mac_management_event_c:

		break;
	case mac_data_event_c:

		break;

	default:
		Serial_Print(mInterfaceId,"Invalid MAC event.\n\r", gAllowToBlock_d);
	}
}

/*****************************************************************************
 * The AppTask(event_t events) function is the applicantion main loop and
 * will process any incoming event. Events include timers, messages and any
 * other user defined events.
 *
 * Interface assumptions:
 *     None
 *
 * Return value:
 *     None
 *****************************************************************************/
void AppThread(uint32_t argument)
{ 
	osaEventFlags_t ev;
	/* Stores the error/success code returned by some functions. */
	uint8_t ret;
    static uint8_t maCommDataBuffer[mDefaultValueOfDataLen_c] = {0};
    static uint8_t mCounter = 0;

	while(1)
	{
		OSA_EventWait(mAppEvent, osaEventFlagsAll_c, FALSE, osaWaitForever_c, &ev);

		switch(gState)
		{
		case stateInit:
			/* Print a welcome message to the serial terminal interface */
			Serial_Print(mInterfaceId,"\n\rMyStarNetwork node application is initialized and ready.\n\r\n\r", gAllowToBlock_d);
			/* Goto Energy Detection state. */
			mac_connect(0x0B, 0xC0C0, mac_events_handler);
			gState = waitConnectionResponse;
			OSA_EventSet(mAppEvent, gAppEvtDummyEvent_c);
			break;

		case waitConnectionResponse:
			/* Handle connection response */
			gState = stateConnected;
			OSA_EventSet(mAppEvent, gAppEvtDummyEvent_c);
			break;

		case stateConnected:
			if (ev & gAppEvtRxFromComm_c)
			{
				uint16_t count;

				(void)Serial_GetByteFromRxBuffer(mInterfaceId, &maCommDataBuffer[mCounter], &count);

				if((mCounter == (mDefaultValueOfDataLen_c-1))||(maCommDataBuffer[mCounter] == gMessageMarkCR_c))
				{
					mac_transmit(0x0001, maCommDataBuffer, mCounter);
					mCounter = 0;
				}
				else
				{
					mCounter++;
				}
			}
			break;
		} /* end switch*/

	}
}

/************************************************************************************
 *************************************************************************************
 * Private functions
 *************************************************************************************
 ************************************************************************************/

/*****************************************************************************
 * App_CommRxCallBack
 *
 * This callback is triggered when a new byte is received over the serial terminal interface
 *
 *****************************************************************************/
static void App_CommRxCallBack(void *pData) 
{
	uint8_t pressedKey;
	uint16_t count;

	(void)pData;
	if(stateConnected == gState)
	{
		OSA_EventSet(mAppEvent, gAppEvtRxFromComm_c);
	}
	else
	{
		do{
			(void)Serial_GetByteFromRxBuffer(mInterfaceId, &pressedKey, &count);
		}while(count);
	}
}


/*****************************************************************************
 * Function to handle a generic key press. Called for all keys.
 *****************************************************************************/
static void App_HandleGenericKey(void)
{
	if(gState == stateInit)
	{
		LED_StopFlashingAllLeds();

		OSA_EventSet(mAppEvent, gAppEvtDummyEvent_c);
	}
}

/*****************************************************************************
 * The App_HandleKeys(key_event_t events) function can handle different
 * key events. It waits for user to push a button in order to start
 * the application.
 *
 * Interface assumptions:
 *     None
 *
 * Return value:
 *     None
 *****************************************************************************/
static void App_HandleKeys( key_event_t events )
{
#if gKBD_KeysCount_c > 0 
	switch ( events )
	{
	case gKBD_EventSW1_c:
	case gKBD_EventSW2_c:
	case gKBD_EventSW3_c:
	case gKBD_EventSW4_c:
#if gTsiSupported_d
	case gKBD_EventSW5_c:
	case gKBD_EventSW6_c:
#endif
	case gKBD_EventLongSW1_c:
	case gKBD_EventLongSW2_c:
	case gKBD_EventLongSW3_c:
	case gKBD_EventLongSW4_c:
#if gTsiSupported_d
	case gKBD_EventLongSW5_c:
	case gKBD_EventLongSW6_c:
#endif
		App_HandleGenericKey();
		break;
	default:
		break;
	}
#endif
}
