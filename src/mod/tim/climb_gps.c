/*
 * climb_gps.c
 *
 *  Created on: 27.02.2022
 *      Author: Robert
 */
#include "climb_gps.h"

#include <ado_uart.h>

// prototypes
void gpsUartIRQ(LPC_USART_T *pUART);
void gpsProcessRxByte(uint8_t rxByte);

// local/module variables
static gps_initdata_t *gpsInitData;

// ************************** TX Circular byte Buffer Begin ********************
#define GPS_TX_BUFFERSIZE	200
static uint8_t 		   gpsTxBuffer[GPS_TX_BUFFERSIZE];
static uint8_t         gpsTxWriteIdx = 0;
static uint8_t         gpsTxReadIdx  = 0;
static bool		  	   gpsTxBufferFull = false;

static bool inline gpsTxBufferEmpty() {
	if (gpsTxBufferFull) {
		return false;
	} else {
		return (gpsTxReadIdx == gpsTxWriteIdx);
	}
}

static bool inline gpsTxAddByte(uint8_t b) {
	if (!gpsTxBufferFull) {
		gpsTxBuffer[gpsTxWriteIdx++] = b;
		if (gpsTxWriteIdx >= GPS_TX_BUFFERSIZE) {
			gpsTxWriteIdx = 0;
		}
		if (gpsTxWriteIdx == gpsTxReadIdx) {
			gpsTxBufferFull = true;
		}
		return true;
	} else {
		return false;
	}
}

static uint8_t inline gpsTxGetByte(void) {
	uint8_t retVal = gpsTxBuffer[gpsTxReadIdx++];
	if (gpsTxReadIdx >= GPS_TX_BUFFERSIZE) {
		gpsTxReadIdx = 0;
	}
	gpsTxBufferFull = false;
	return retVal;
}

// ************************** TX Circular byte Buffer End ********************


void gpsInit (void *initData) {
	gpsInitData = (gps_initdata_t*) initData;

	// Switch the 'enable' pin to low (no internal Volage regulator needed
	Chip_GPIO_SetPinOutLow(LPC_GPIO, gpsInitData->pEnablePin->pingrp, gpsInitData->pEnablePin->pinnum);

	// Init UART
	InitUart(gpsInitData->pUart, 9600, gpsUartIRQ);
}

void gpsMain (void) {
	// Uart Rx
	int32_t stat = Chip_UART_ReadLineStatus(gpsInitData->pUart);
	if (stat & UART_LSR_RDR) {
		// there is a byte available. Lets read and process it.
		uint8_t b = Chip_UART_ReadByte(gpsInitData->pUart);
		gpsProcessRxByte(b);
	}
}

void gpsUartIRQ(LPC_USART_T *pUART) {
	if (gpsInitData->pUart->IER & UART_IER_THREINT) {
		// Transmit register is empty now (byte was sent out)
		if (gpsTxBufferEmpty() == false) {
			uint8_t nextByte = gpsTxGetByte();
			Chip_UART_SendByte(gpsInitData->pUart, nextByte);
		} else {
			// No more bytes available -> stop the THRE IRQ.
			Chip_UART_IntDisable(gpsInitData->pUart, UART_IER_THREINT);
		}
	}
}

void gpsSendByte(uint8_t b) {
	// block irq while handling tx buffer
	Chip_UART_IntDisable(gpsInitData->pUart, UART_IER_THREINT);

	if (gpsTxBufferEmpty()) {
		// First Byte: Store in Buffer and initiate TX
		gpsTxAddByte(b);
		// Put this byte on the UART Line.
		Chip_UART_SendByte(gpsInitData->pUart, b);
	} else {
		// add byte to Tx buffer. Tx should be running already...
		if (gpsTxAddByte(b) == false) {
			// Buffer Full Error. Byte is skipped -> count errors or signal event ?????
			// .....
		}
	}

	// enable irq after handling tx buffer
	Chip_UART_IntEnable(gpsInitData->pUart, UART_IER_THREINT);
}

void gpsSendBytes(uint8_t *data, uint8_t len) {
	for (int i=0;i<len;i++) {
		gpsSendByte(data[i]);
	}
}


void gpsProcessRxByte(uint8_t rxByte) {
	// do your processing of RX here....

}
