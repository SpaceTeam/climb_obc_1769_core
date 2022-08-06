/*
 * based on climb_gps.c
 *
 *  Created on: 27.02.2022
 *      Copy paste by: Jevgeni
 */
#include "thr.h"
#include <ado_uart.h>
#include "../l7_climb_app.h"
#include "../l2_debug_com.h"
#include "../l4_thruster.h" // with that we include variable  l4_thr_ExpectedReceiveBuffer  which defines expected RX buffer length


// prototypes
void thrUartIRQ(LPC_USART_T *pUART);
void thrProcessRxByte(uint8_t rxByte);
void thr_debugPrintBuffer(uint8_t *buffer,int bufferlen);

// local/module variables
static thr_initdata_t *thrInitData;

// ************************** TX Circular byte Buffer Begin ********************
#define THR_TX_BUFFERSIZE	200
static uint8_t 		   thrTxBuffer[THR_TX_BUFFERSIZE];
static uint8_t         thrTxWriteIdx = 0;
static uint8_t         thrTxReadIdx  = 0;
static bool		  	   thrTxBufferFull = false;
static bool				thrFirstByteAfterReset=true;


/////////////////// MODULE LOCAL VARIABLES////////////

#define THR_BUFFERLEN 700 // maximum buffer length
//int thr_counter = 0; would be defined in l4_thruster
char thr_receiveBuffer[THR_BUFFERLEN] ="";

//l4_thr_counter = 0; // set received bytes counter to 0 initialy // defined in l4_thruster.c


static bool inline thrTxBufferEmpty() {
	if (thrTxBufferFull) {
		return false;
	} else {
		return (thrTxReadIdx == thrTxWriteIdx);
	}
}

static bool inline thrTxAddByte(uint8_t b) {
	if (!thrTxBufferFull) {
		thrTxBuffer[thrTxWriteIdx++] = b;
		if (thrTxWriteIdx >= THR_TX_BUFFERSIZE) {
			thrTxWriteIdx = 0;
		}
		if (thrTxWriteIdx == thrTxReadIdx) {
			thrTxBufferFull = true;
		}
		return true;
	} else {
		return false;
	}
}

static uint8_t inline thrTxGetByte(void) {
	uint8_t retVal = thrTxBuffer[thrTxReadIdx++];
	if (thrTxReadIdx >= THR_TX_BUFFERSIZE) {
		thrTxReadIdx = 0;
	}
	thrTxBufferFull = false;
	return retVal;
}

// ************************** TX Circular byte Buffer End ********************


void thrInit (void *initData) {
	thrInitData = (thr_initdata_t*) initData;

	// Switch the 'enable' pin to low (no internal Volage regulator needed
	//Chip_GPIO_SetPinOutLow(LPC_GPIO, thrInitData->pEnablePin->pingrp, thrInitData->pEnablePin->pinnum);

	//// IMPORTANT PINIDX_RS485_TX_RX   HIGH - TRANSMIT              LOW - RECEIVE

	// SET TO RECEIVE UPON INITIALIZATION
	//Chip_GPIO_SetPinOutLow(LPC_GPIO, 2, 5); //  This is PINIDX_RS485_TX_RX RECEIVE
	//Chip_GPIO_SetPinOutHigh(LPC_GPIO, 2, 5); //  This is PINIDX_RS485_TX_RX TRANSMIT



	// Init UART
	InitUart(thrInitData->pUart, 115200, thrUartIRQ);
	thrFirstByteAfterReset = true;

	//Enable Auto direction controll fo UART1
	//uint32_t uart_bit_mask = 1 << 4; // DCRTL at 4th bit position
	//LPC_UART1->RS485CTRL = ( ( LPC_UART1->RS485CTRL &~ uart_bit_mask)  |   (1<<4)        );
	// NOTE - THIS HAD NO EFFECT ON RS485 Dirrection controll perfomance

	// Enable dirrection controll
	LPC_UART1->RS485CTRL |= UART_RS485CTRL_DCTRL_EN; // Enable Auto Direction Control

	// If direction control is enabled (bit DCTRL = 1), pin DTR is used for direction control
	LPC_UART1->RS485CTRL |= UART_RS485CTRL_SEL_DTR;

	//This bit reverses the polarity of the direction control signal on the RTS (or DTR) pin. The direction control pin
	 //will be driven to logic "1" when the transmitter has data to be sent
	LPC_UART1->RS485CTRL |= UART_RS485CTRL_OINV_1;



}

void thrMain (void) {
	// Uart Rx
	int32_t stat = Chip_UART_ReadLineStatus(thrInitData->pUart);
	if (stat & UART_LSR_RDR) {
		// there is a byte available. Lets read and process it.


		uint8_t b = Chip_UART_ReadByte(thrInitData->pUart);
		thrProcessRxByte(b);
	}
}

void thrUartIRQ(LPC_USART_T *pUART) {
	if (thrInitData->pUart->IER & UART_IER_THREINT) {
		// Transmit register is empty now (byte was sent out)
		if (thrTxBufferEmpty() == false) {
			// Send next byte
			uint8_t nextByte = thrTxGetByte();
			Chip_UART_SendByte(thrInitData->pUart, nextByte);
		} else {
			// No more bytes available -> stop the THRE IRQ.
			Chip_UART_IntDisable(thrInitData->pUart, UART_IER_THREINT);

			// switch back to receive when all bytes are transmited
			Chip_GPIO_SetPinOutLow(LPC_GPIO, 2, 5); //  This is PINIDX_RS485_TX_RX RECEIVE
		}
	}
}

void thrSendByte(uint8_t b) {

	// block irq while handling tx buffer
	Chip_UART_IntDisable(thrInitData->pUart, UART_IER_THREINT);



	//if (thrTxBufferEmpty()) {
		// First Byte: Store in Buffer and initiate TX
		//thrTxAddByte(b);
		// Put this byte on the UART Line.
	if(thrTxBufferEmpty()){
				// first time after reset the THRE IRQ will not be triggered by just enabling it here
				// So we have to really send the byte here and do not put this into buffer. From next byte on
				// we always put bytes to the TX buffer and enabling the IRQ will trigger it when THR (transmit hold register)
				// gets empty (or also if it was and is still empty!)
				// see UM10360 Datasheet rev 4.1  page 315 first paragraph for details of this behavior!
				thrFirstByteAfterReset = false;
				Chip_UART_SendByte(thrInitData->pUart, b);
	} else {
		// add byte to buffer if there is room left
			// and wait for IRQ to fetch it
		if (thrTxAddByte(b) == false) {
			// Buffer Full Error. Byte is skipped -> count errors or signal event ?????
			// .....
		}
	}

	// enable irq after handling tx buffer
	Chip_UART_IntEnable(thrInitData->pUart, UART_IER_THREINT);

}

void thrSendBytes(uint8_t *data, uint8_t len) {
	//set to transmit when sending data package
	Chip_GPIO_SetPinOutHigh(LPC_GPIO, 2, 5); //  This is PINIDX_RS485_TX_RX TRANSMIT
	/*
	for (int i=0;i<1000000;i++) {
			// DELAY
		}
	*/
	for (int i=0;i<len;i++) {
		thrSendByte(data[i]);
	}
	/*
	for (int i=0;i<1000000;i++) {
				// DELAY
			}

	*/
	// switch back to receive when data transmission end
	//Chip_GPIO_SetPinOutLow(LPC_GPIO, 2, 5); //  This is PINIDX_RS485_TX_RX RECEIVE

	// SWITCHING BACK TO RX IS MOVED INTO void thrUartIRQ(LPC_USART_T *pUART)
	// No delays

}


void thrProcessRxByte(uint8_t rxByte) {
	// do your processing of RX here....
	// Simply print RX into debug UART
	//char* Byte;

	//Byte = (char*) rxByte;
	//SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_STRING, Byte, strlen(Byte));

	//if (thr_counter<= THR_BUFFERLEN){
	if (l4_thr_counter<= l4_thr_ExpectedReceiveBuffer){ // change it to expected buffer length SET by REQUEST functions

		thr_receiveBuffer[l4_thr_counter]=(char) rxByte;
		Chip_UART_SendByte(LPC_UART2, rxByte); // print received byte


		l4_thr_counter++;

	}
	else {
		l4_thr_counter =0;
		//print whole receive buffer
		//SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_STRING, thr_receiveBuffer, strlen(thr_receiveBuffer));

		//thr_debugPrintBuffer( (uint8_t*)thr_receiveBuffer,strlen(thr_receiveBuffer));
		ParseReadRequest((uint8_t*)thr_receiveBuffer,l4_thr_ExpectedReceiveBuffer);

	}



}





void thr_debugPrintBuffer(uint8_t *buffer,int bufferlen){

	//LPC_UART2 is debug UART

	for (int i=0;i<bufferlen;i++){
		Chip_UART_SendByte(LPC_UART2, buffer[i]);

	}

}
