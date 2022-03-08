/*
===============================================================================
 Name        : l7_climb_app.c
 Author      : Robert
 Created on	 : 07.09.2021
===============================================================================
*/
#include "l7_climb_app.h"

#include <string.h>
#include <stdlib.h>
#include "l2_debug_com.h"
#include <mod/ado_mram.h>
#include <mod/ado_sdcard.h>

#include "mem/obc_memory.h"
#include "tim/obc_time.h"
#include "l3_sensors.h"
#include "hw_check.h"
#include "tim/climb_gps.h"
#include "thr/thr.h"


#include "ai2c/obc_i2c.h"
#include "ai2c/obc_i2c_rb.h"
#include "ai2c/obc_i2c_int.h"


typedef struct {
	uint8_t	cmdId;
	void 	(*command_function)(int argc, char *argv[]);
} app_command_t;


typedef struct {
	uint32_t 				SerialShort;
	char					InstanceName[16];
	char					CardName[16];
//	tim_synced_systime_t 	CurrentTime;
	mem_status_t			MemoryStatus;
	uint32_t				SdCardBlock0Number;
	uint32_t				SdCardSize;
	uint32_t				SdCardUsed;
	uint32_t				SystemCommandCounter;
	uint32_t				SystemErrorCounter;
	char					SwRelease[16];
} app_systeminfo_t;

static uint32_t climbCmdCounter = 0;
static uint32_t climbErrorCounter = 0;


// Prototypes
void app_processCmd(int argc, char *argv[]);
void ReadMramCmd(int argc, char *argv[]);
void ReadMramFinished (uint8_t chipIdx,mram_res_t result, uint32_t adr, uint8_t *data, uint32_t len);
void WriteMramCmd(int argc, char *argv[]);
void WriteMramFinished (uint8_t chipIdx,mram_res_t result, uint32_t adr, uint8_t *data, uint32_t len);
void ReadSdcardCmd(int argc, char *argv[]);
void ReadSdcardFinished (sdc_res_t result, uint32_t blockNr, uint8_t *data, uint32_t len);
void WriteSdcardCmd(int argc, char *argv[]);
//void WriteSdcardFinished (mram_res_t result, uint32_t adr, uint8_t *data, uint32_t len);
void HwcSetOutputCmd(int argc, char *argv[]);
void HwcMirrorInputCmd(int argc, char *argv[]);
void ReadAllSensorsCmd(int argc, char *argv[]);
void SpPowerCmd(int argc, char *argv[]);
void CardPowerOnCmd(int argc, char *argv[]);
void CardPowerOffCmd(int argc, char *argv[]);
void SetObcNameCmd(int argc, char *argv[]);
void ReadStatusMramCmd(int argc, char *argv[]);
void GetSystemInfoCmd(int argc, char *argv[]);
void SetSdCardNameCmd(int argc, char *argv[]);
void TriggerWatchdogCmd(int argc, char *argv[]);
void SetUtcDateTimeCmd(int argc, char *argv[]);
void GetFullTimeCmd(int argc, char *argv[]);
void SendToGpsUartCmd(int argc, char *argv[]);

void JevgeniDebugCmd(int argc, char *argv[]); // STP JEVGENI
void ThrSendVersionRequestCmd(int argc, char *argv[]); // STP JEVGENI : VERSION REQUEST SEND TO THRUSTER SIMULATOR HARDWARE

void I2cSendCmd(int argc, char *argv[]); // Michael

//extern void *sdCard;

static const app_command_t Commands[] = {
		{ 'j' , JevgeniDebugCmd }, //IT DOES NOT WORK WHEN NEW COMMAND ADDED ???
		{ 'J' , ThrSendVersionRequestCmd }, //IT DOES NOT WORK WHEN NEW COMMAND ADDED ???
		{ 'k' , I2cSendCmd }, // Michael
		{ 'h' , HwcSetOutputCmd },
		{ 'm' , HwcMirrorInputCmd },
		{ 'r' , ReadMramCmd },
		{ 'w' , WriteMramCmd },
		{ 'R' , ReadSdcardCmd },
		{ 'C' , CardPowerOnCmd },
		{ 'c' , CardPowerOffCmd },
		{ 's' , ReadAllSensorsCmd },
		{ 'p' , SpPowerCmd },
		{ 'O' , SetObcNameCmd },
		{ 'N' , SetSdCardNameCmd },
		{ 'i' , GetSystemInfoCmd },
		{ 'd' , TriggerWatchdogCmd },
		{ 't' , SetUtcDateTimeCmd },
		{ 'T' , GetFullTimeCmd },
		{ 'g' , SendToGpsUartCmd }

};


#define APP_CMD_CNT	(sizeof(Commands)/sizeof(app_command_t))

#define SysEventString(str) { \
	SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_STRING, str, strlen(str)); \
}

void app_init (void *dummy) {
	//SdcCardinitialize(0);
	char ver[32] = "SW-Version: ";
	ver[31] = 0;
	strncpy(&ver[12], BUILD_SWVERSION, 18);
	SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_STRING, ver, 12 + strlen(BUILD_SWVERSION));



	// SEND SOME BYTES TO THRUSTER DURING INITIALIZATION
	uint8_t request[5];
		request[0]= 0xFF;
		request[1]= 0x31;
		request[2]= 0x32;
		request[3]= 0x33;
		request[4]= 0xFF;
		thrSendBytes(request, sizeof(request));


		// INITIALIZE I2C
		init_i2c(LPC_I2C0, 400); // I2C0  , there are also I2C1 and I2C2 - what are those ? How to locate those on physical OBC pins ?
}

void app_main (void) {
	// Debug Command Polling (direct from L2 CLI Module)
	DEB_L2_CMD_T cmd;
	if ( deb_getCommandIfAvailable(&cmd) ) {


		app_processCmd(cmd.parCnt, cmd.pars);



	}
	// handle event - queue ....

}


void _SysEvent(event_t event) {
	deb_sendEventFrame(event.id, event.data, event.byteCnt);

	if ( (event.id.severity == EVENT_ERROR) || (event.id.severity == EVENT_FATAL)) {
		climbErrorCounter++;
	}
}

uint8_t  tempData[MRAM_MAX_WRITE_SIZE];

void app_processCmd(int argc, char *argv[]) {
	char* cmd = argv[0];

	for (int i=0; i<APP_CMD_CNT; i++) {
		if (cmd[0] == Commands[i].cmdId) {
			Commands[i].command_function(argc, argv);
			climbCmdCounter++;
			break;
		}
	}
}

static bool spOn[4]={false,false,false,false};
void SpPowerSwitch(char sp) {
	uint8_t pinIdx = PINIDX_SP3_VCC_EN;
	bool *flag  = 0;
	if ((sp=='a')||(sp=='A')) {
		pinIdx = PINIDX_SP1_VCC_EN;
		flag = &spOn[0];
	} else if ((sp=='b')||(sp=='B')) {
		pinIdx = PINIDX_SP2_VCC_EN;
		flag = &spOn[1];
	} else if ((sp=='c')||(sp=='C')) {
		pinIdx = PINIDX_SP3_VCC_EN;
		flag = &spOn[2];
	} else if ((sp=='d')||(sp=='D')) {
		pinIdx = PINIDX_SP4_VCC_EN;
		flag = &spOn[3];
	}
	hwc_OutStatus pinStat = HWC_Low;
	*flag = true;
	if ((sp=='a')||(sp=='b')||(sp=='c')||(sp=='d')) {
		pinStat = HWC_High;
		*flag = false;
	}
	HwcSetOutput(pinIdx, pinStat);
	if (spOn[0]||spOn[1]||spOn[2]||spOn[3]) {
		HwcMirrorInput(PINIDX_SP_VCC_FAULT, PINIDX_LED);		// this uses idx in pinmuxing2 s5tructure. Mirror VPP_FAULT (55) -> LED (44)
	} else {
		HwcMirrorInput(200, 200);	// Mirror off
		HwcSetOutput(PINIDX_LED, HWC_Low);	// Led Off
	}
}


void SendToGpsUartCmd(int argc, char *argv[]) {
	gpsSendBytes((uint8_t *)"Hello GPS!", 11);

	////////////////////////////////////////
	/*
	uint8_t request[8];
		request[0]= 0x00;
		request[1]= 0xFF;
		request[2]= 0x03;
		request[3]= 0x14;
		request[4]= 0x02;
		request[5]= 0x00;
		request[6]= 0x00;
		request[7]= 0x01;


		int len = sizeof(request);
		thrSendBytes(request, len);
		*/
		//////////////////////////////////////////


}


void SpPowerCmd(int argc, char *argv[]) {
	if (argc != 2) {
		SysEventString("uasge: p <a|A/b|B/c|C/d|D>");
	} else {
		int i = strlen(argv[1]);
		if (i > 4) {
			i = 4;
		}
		for (int x= 0; x<i;x++) {
			char sp = argv[1][x];
			SpPowerSwitch(sp);
		}
	}
}

void CardPowerOnCmd(int argc, char *argv[]) {
	memCardOn();
//	HwcSetOutput(PINIDX_SD_VCC_EN, HWC_Low);	// Sd Card Power On
//	SdcCardinitialize(0);		// initialize Card[0]
}

void CardPowerOffCmd(int argc, char *argv[]) {
	memCardOff();
//	HwcSetOutput(PINIDX_SD_VCC_EN, HWC_High);
}

void TriggerWatchdogCmd(int argc, char *argv[]) {
	while(true);
}


void ReadAllSensorsCmd(int argc, char *argv[]) {
	SenReadAllValues();
	//SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_SENSORVALUES, &values, sizeof(sensor_values_t));

	////////////////////////////////////////
	uint8_t request[8];
		request[0]= 0x00;
		request[1]= 0xFF;
		request[2]= 0x03;
		request[3]= 0x14;
		request[4]= 0x02;
		request[5]= 0x00;
		request[6]= 0x00;
		request[7]= 0x01;


		int len = sizeof(request);
		thrSendBytes(request, len);
		//////////////////////////////////////////
}

uint8_t tempBlockData[2000];
void ReadSdcardCmd(int argc, char *argv[]) {
	if (argc != 2) {
		SysEventString("uasge: R <blockNr>")
	} else {
		// CLI params to binary params
		uint32_t  block = atoi(argv[1]);
		// memReadObcBlockAsync(block, tempBlockData, ReadSdcardFinished );
		SdcReadBlockAsync(0, block, tempBlockData, ReadSdcardFinished);
	 }
}

void ReadSdcardFinished (sdc_res_t result, uint32_t blockNr, uint8_t *data, uint32_t len) {
    if (result == SDC_RES_SUCCESS) {
    	SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_RAWDATA, data, len);
    	//deb_sendFrame((uint8_t*)data, len);
    } else {
    	SysEventString("ERROR  !!!");
    }
}

void WriteSdcardCmd(int argc, char *argv[]) {

}


void ReadMramCmd(int argc, char *argv[]) {
	if (argc != 4) {
		SysEventString("uasge: r <chipIdx> <adr> <len>");
	} else {
		// CLI params to binary params
		uint8_t  idx = atoi(argv[1]);
		uint32_t adr = atoi(argv[2]);
		uint32_t len = atoi(argv[3]);
		if (len > MRAM_MAX_READ_SIZE) {
			len = MRAM_MAX_READ_SIZE;
		}
		if (idx >= MRAM_CHIP_CNT) {
		   idx = 0;
		}
		MramReadAsync(idx, adr, tempData, len, ReadMramFinished);
	 }
}

void ReadMramFinished (uint8_t chipIdx, mram_res_t result, uint32_t adr, uint8_t *data, uint32_t len) {
    if (result == MRAM_RES_SUCCESS) {
    	SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_RAWDATA, data, len);
    	//deb_sendFrame((uint8_t*)data, len);
    } else {
    	char *str="ERROR  !!!";
    	SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_STRING, str,strlen(str));
    }
}



void WriteMramCmd(int argc, char *argv[]) {
	if (argc != 5) {
		char *str="uasge: w <chipidx> <adr> <databyte> <len>";
		SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_STRING, str,strlen(str));
	} else {
		// CLI params to binary params
		uint8_t  idx = atoi(argv[1]);
		uint32_t adr = atoi(argv[2]);
		uint8_t byte = atoi(argv[3]);
		uint32_t len = atoi(argv[4]);
		if (len > MRAM_MAX_WRITE_SIZE) {
			len = MRAM_MAX_WRITE_SIZE;
		}
		if (idx > MRAM_CHIP_CNT) {
			idx = 0;
		}

		for (int i=0;i<len;i++){
			tempData[i] = byte;
		}

		// Binary Command
		MramWriteAsync(idx, adr, tempData, len,  WriteMramFinished);
	}
}


void WriteMramFinished (uint8_t chipIdx, mram_res_t result, uint32_t adr, uint8_t *data, uint32_t len) {
	if (result == MRAM_RES_SUCCESS) {
		SysEventString("SUCCESS");
	} else {
		SysEventString("Write ERROR!!!");
	}
}


void HwcSetOutputCmd(int argc, char *argv[]) {
	hwc_OutStatus stat = HWC_Signal_Slow;
	uint8_t idx = 0;
	if (argc > 1) {
		idx = atoi(argv[1]);
	}
	if (argc > 2) {
		stat = (hwc_OutStatus)atoi(argv[2]);
	}

	HwcSetOutput(idx, stat);
}

void HwcMirrorInputCmd(int argc, char *argv[]) {
	uint8_t idxIn  = PINIDX_RBF;
	uint8_t idxOut = PINIDX_LED;	//LED
	if (argc > 1) {
		idxIn = atoi(argv[1]);
	}
	if (argc > 2) {
		idxOut = atoi(argv[2]);
	}

	HwcMirrorInput(idxIn, idxOut);
}

void SetObcNameCmd(int argc, char *argv[]) {
	if (argc != 2) {
		SysEventString("uasge: O <instanceName>");
	} else {
		memChangeInstanceName(argv[1]);
	 }
}


void SetSdCardNameCmd(int argc, char *argv[]) {
	if (argc != 2) {
		SysEventString("uasge: N <cardName>");
	} else {
		memChangeCardName(argv[1]);
	 }
}


void GetSystemInfoCmd(int argc, char *argv[]) {
	app_systeminfo_t info;
	//info.CurrentTime = tim_getSystemTime();
	memGetInstanceName(info.InstanceName,16);
	memGetCardName(info.CardName, 20);

	memset(info.SwRelease, 0, 16);
	strncpy(info.SwRelease, BUILD_SWVERSION , 16);

	info.MemoryStatus = memGetStatus();
	mem_sdcobcdataarea_t *obcarea = memGetObcArea();
	info.SdCardBlock0Number = obcarea->basisBlockNumber;
	info.SdCardSize = obcarea->blocksAvailable;
	info.SdCardUsed = obcarea->blocksUsed;
	info.SystemCommandCounter = climbCmdCounter;
	info.SystemErrorCounter = climbErrorCounter;
	info.SerialShort = memGetSerialNumber(1);

	SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_SYSTEMSTATUS, &info, sizeof(info));
}

void SetUtcDateTimeCmd(int argc, char *argv[]) {
	// setTime <year><Month><day><hours><minutes><seconds> as single uint32
	uint16_t year = 0;
	uint8_t month = 0;
	uint8_t dayOfMonth = 0;
	uint8_t sec = 0;
	uint8_t min = 0;
	uint8_t hrs = 0;

	uint32_t date;
	uint32_t time;

	if (argc != 3) {
		SysEventString("uasge: t <date> <time>");
	} else {
		date = atol(argv[1]);
		time = atol(argv[2]);

		year = date / 10000;
		date %= 10000;

		month = date / 100;
		dayOfMonth = date % 100;

		hrs = time / 10000;
		time %= 10000;

		min = time / 100;
		sec = time % 100;

		// binary cmd
		TimSetUtc1(year, month, dayOfMonth, hrs, min, sec, true);
	}
}


void GetFullTimeCmd(int argc, char *argv[]) {
	obc_utc_fulltime_t ft = timGetUTCTime();
	SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_FULLTIMEINFO, &ft, sizeof(ft));
}

// THIS DOES NOT WORK
void JevgeniDebugCmd(int argc, char *argv[]){
	//SysEventString("executed");
	char* test_hex = "\x04\x05\x06"; // bytes mean that chars being processed
	SysEvent(MODULE_ID_CLIMBAPP, EVENT_INFO, EID_APP_STRING, test_hex, strlen(test_hex));//SEND HEX ARRAY

}







void ThrSendVersionRequestCmd(int argc, char *argv[]){


	uint8_t request[8];
	request[0]= 0x00;
	request[1]= 0xFF;
	request[2]= 0x03;
	request[3]= 0x14;
	request[4]= 0x02;
	request[5]= 0x00;
	request[6]= 0x00;
	request[7]= 0x01;


	int len = sizeof(request);
	thrSendBytes(request, len);




}




void I2cSendCmd(int argc, char *argv[]){

		uint8_t request[8];
		request[0]= 0x00;
		request[1]= 0xFF;
		request[2]= 0x03;
		request[3]= 0x14;
		request[4]= 0x02;
		request[5]= 0x00;
		request[6]= 0x00;
		request[7]= 0x01;
		uint8_t len = sizeof(request);

		I2C_Data i2c_message; // create structure that will contain i2c message
		i2c_message.tx_data=request; // assign "request" bytes into transmit data buffer
		i2c_message.tx_size = len; // length of transmit message ?

		uint8_t add_job_return =  i2c_add_job(&i2c_message); // add job ??? and message is transmitted ? // unused wariable warning

}

		// How to send bytes over I2C ??
		//How to assemble I2C_Data structure ????

		/*
		 typedef struct
{
	uint8_t tx_count;
	uint8_t rx_count;
	uint8_t dir;
	uint8_t status;
	uint8_t tx_size;
	uint8_t rx_size;
	uint8_t* tx_data;
	uint8_t* rx_data;
	uint8_t job_done;
	uint8_t adress;
	enum i2c_errors_e error;
	LPC_I2C_T *device;
} volatile I2C_Data;
		 */


