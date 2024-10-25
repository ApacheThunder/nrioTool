/*
 main.arm7.c

 By Michael Chisholm (Chishm)

 All resetMemory and startBinary functions are based
 on the MultiNDS loader by Darkain.
 Original source available at:
 http://cvs.sourceforge.net/viewcvs.py/ndslib/ndslib/examples/loader/boot/main.cpp

 License:
    NitroHax -- Cheat tool for the Nintendo DS
    Copyright (C) 2008  Michael "Chishm" Chisholm

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef ARM7
# define ARM7
#endif
#include <nds/ndstypes.h>
#include <nds/system.h>
#include <nds/interrupts.h>
#include <nds/timers.h>
#include <nds/dma.h>
#include <nds/arm7/audio.h>
#include <nds/arm7/codec.h>
#include <nds/memory.h>
#include <nds/ipc.h>
#include <string.h>

#ifndef NULL
#define NULL 0
#endif

#include "common.h"
#include "tonccpy.h"
#include "my_fat.h"
/*-------------------------------------------------------------------------
External functions
--------------------------------------------------------------------------*/
extern void arm7_clearmem (void* loc, size_t len);
extern void arm7_reset (void);

static int language = -1;

static bool useTwlCfg = false;
static int twlCfgLang = 0;

static unsigned long storedFileCluster;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Important things
#define NDS_HEADER			0x02FFFE00
#define TMP_HEADER			0x02FFC000

#define REG_GPIO_WIFI		*(vu16*)0x4004C04

tNDSHeader* ndsHeader;

static u32 chipID;

const char* getRomTid(const tNDSHeader* ndsHeader) {
	static char romTid[5];
	strncpy(romTid, ndsHeader->gameCode, 4);
	romTid[4] = '\0';
	return romTid;
}

static void errorOutput (u32 code, bool isError) {
	arm9_errorCode = code;
	if (isError) { 
		ipcSendState(ARM7_ERR);
		while(1); // Stop
	}
}

static void NDSTouchscreenMode(void) {
	u8 volLevel = 0xA7;

	// Touchscreen
	cdcReadReg (0x63, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x3A, 0x00);
	cdcReadReg (CDC_CONTROL, 0x51);
	cdcReadReg (CDC_TOUCHCNT, 0x02);
	cdcReadReg (CDC_CONTROL, 0x3F);
	cdcReadReg (CDC_SOUND, 0x28);
	cdcReadReg (CDC_SOUND, 0x2A);
	cdcReadReg (CDC_SOUND, 0x2E);
	cdcWriteReg(CDC_CONTROL, 0x52, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x40, 0x0C);
	cdcWriteReg(CDC_SOUND, 0x24, 0xFF);
	cdcWriteReg(CDC_SOUND, 0x25, 0xFF);
	cdcWriteReg(CDC_SOUND, 0x26, 0x7F);
	cdcWriteReg(CDC_SOUND, 0x27, 0x7F);
	cdcWriteReg(CDC_SOUND, 0x28, 0x4A);
	cdcWriteReg(CDC_SOUND, 0x29, 0x4A);
	cdcWriteReg(CDC_SOUND, 0x2A, 0x10);
	cdcWriteReg(CDC_SOUND, 0x2B, 0x10);
	cdcWriteReg(CDC_CONTROL, 0x51, 0x00);
	cdcReadReg (CDC_TOUCHCNT, 0x02);
	cdcWriteReg(CDC_TOUCHCNT, 0x02, 0x98);
	cdcWriteReg(CDC_SOUND, 0x23, 0x00);
	cdcWriteReg(CDC_SOUND, 0x1F, 0x14);
	cdcWriteReg(CDC_SOUND, 0x20, 0x14);
	cdcWriteReg(CDC_CONTROL, 0x3F, 0x00);
	cdcReadReg (CDC_CONTROL, 0x0B);
	cdcWriteReg(CDC_CONTROL, 0x05, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x0B, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x0C, 0x02);
	cdcWriteReg(CDC_CONTROL, 0x12, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x13, 0x02);
	cdcWriteReg(CDC_SOUND, 0x2E, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x3A, 0x60);
	cdcWriteReg(CDC_CONTROL, 0x01, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x39, 0x66);
	cdcReadReg (CDC_SOUND, 0x20);
	cdcWriteReg(CDC_SOUND, 0x20, 0x10);
	cdcWriteReg(CDC_CONTROL, 0x04, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x12, 0x81);
	cdcWriteReg(CDC_CONTROL, 0x13, 0x82);
	cdcWriteReg(CDC_CONTROL, 0x51, 0x82);
	cdcWriteReg(CDC_CONTROL, 0x51, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x04, 0x03);
	cdcWriteReg(CDC_CONTROL, 0x05, 0xA1);
	cdcWriteReg(CDC_CONTROL, 0x06, 0x15);
	cdcWriteReg(CDC_CONTROL, 0x0B, 0x87);
	cdcWriteReg(CDC_CONTROL, 0x0C, 0x83);
	cdcWriteReg(CDC_CONTROL, 0x12, 0x87);
	cdcWriteReg(CDC_CONTROL, 0x13, 0x83);
	cdcReadReg (CDC_TOUCHCNT, 0x10);
	cdcWriteReg(CDC_TOUCHCNT, 0x10, 0x08);
	cdcWriteReg(0x04, 0x08, 0x7F);
	cdcWriteReg(0x04, 0x09, 0xE1);
	cdcWriteReg(0x04, 0x0A, 0x80);
	cdcWriteReg(0x04, 0x0B, 0x1F);
	cdcWriteReg(0x04, 0x0C, 0x7F);
	cdcWriteReg(0x04, 0x0D, 0xC1);
	cdcWriteReg(CDC_CONTROL, 0x41, 0x08);
	cdcWriteReg(CDC_CONTROL, 0x42, 0x08);
	cdcWriteReg(CDC_CONTROL, 0x3A, 0x00);
	cdcWriteReg(0x04, 0x08, 0x7F);
	cdcWriteReg(0x04, 0x09, 0xE1);
	cdcWriteReg(0x04, 0x0A, 0x80);
	cdcWriteReg(0x04, 0x0B, 0x1F);
	cdcWriteReg(0x04, 0x0C, 0x7F);
	cdcWriteReg(0x04, 0x0D, 0xC1);
	cdcWriteReg(CDC_SOUND, 0x2F, 0x2B);
	cdcWriteReg(CDC_SOUND, 0x30, 0x40);
	cdcWriteReg(CDC_SOUND, 0x31, 0x40);
	cdcWriteReg(CDC_SOUND, 0x32, 0x60);
	cdcReadReg (CDC_CONTROL, 0x74);
	cdcWriteReg(CDC_CONTROL, 0x74, 0x02);
	cdcReadReg (CDC_CONTROL, 0x74);
	cdcWriteReg(CDC_CONTROL, 0x74, 0x10);
	cdcReadReg (CDC_CONTROL, 0x74);
	cdcWriteReg(CDC_CONTROL, 0x74, 0x40);
	cdcWriteReg(CDC_SOUND, 0x21, 0x20);
	cdcWriteReg(CDC_SOUND, 0x22, 0xF0);
	cdcReadReg (CDC_CONTROL, 0x51);
	cdcReadReg (CDC_CONTROL, 0x3F);
	cdcWriteReg(CDC_CONTROL, 0x3F, 0xD4);
	cdcWriteReg(CDC_SOUND, 0x23, 0x44);
	cdcWriteReg(CDC_SOUND, 0x1F, 0xD4);
	cdcWriteReg(CDC_SOUND, 0x28, 0x4E);
	cdcWriteReg(CDC_SOUND, 0x29, 0x4E);
	cdcWriteReg(CDC_SOUND, 0x24, 0x9E);
	cdcWriteReg(CDC_SOUND, 0x25, 0x9E);
	cdcWriteReg(CDC_SOUND, 0x20, 0xD4);
	cdcWriteReg(CDC_SOUND, 0x2A, 0x14);
	cdcWriteReg(CDC_SOUND, 0x2B, 0x14);
	cdcWriteReg(CDC_SOUND, 0x26, 0xA7);
	cdcWriteReg(CDC_SOUND, 0x27, 0xA7);
	cdcWriteReg(CDC_CONTROL, 0x40, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x3A, 0x60);
	cdcWriteReg(CDC_SOUND, 0x26, volLevel);
	cdcWriteReg(CDC_SOUND, 0x27, volLevel);
	cdcWriteReg(CDC_SOUND, 0x2E, 0x03);
	cdcWriteReg(CDC_TOUCHCNT, 0x03, 0x00);
	cdcWriteReg(CDC_SOUND, 0x21, 0x20);
	cdcWriteReg(CDC_SOUND, 0x22, 0xF0);
	cdcReadReg (CDC_SOUND, 0x22);
	cdcWriteReg(CDC_SOUND, 0x22, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x52, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x51, 0x00);
	
	// Set remaining values
	cdcWriteReg(CDC_CONTROL, 0x03, 0x44);
	cdcWriteReg(CDC_CONTROL, 0x0D, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x0E, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x0F, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x10, 0x08);
	cdcWriteReg(CDC_CONTROL, 0x14, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x15, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x16, 0x04);
	cdcWriteReg(CDC_CONTROL, 0x1A, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x1E, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x24, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x33, 0x34);
	cdcWriteReg(CDC_CONTROL, 0x34, 0x32);
	cdcWriteReg(CDC_CONTROL, 0x35, 0x12);
	cdcWriteReg(CDC_CONTROL, 0x36, 0x03);
	cdcWriteReg(CDC_CONTROL, 0x37, 0x02);
	cdcWriteReg(CDC_CONTROL, 0x38, 0x03);
	cdcWriteReg(CDC_CONTROL, 0x3C, 0x19);
	cdcWriteReg(CDC_CONTROL, 0x3D, 0x05);
	cdcWriteReg(CDC_CONTROL, 0x44, 0x0F);
	cdcWriteReg(CDC_CONTROL, 0x45, 0x38);
	cdcWriteReg(CDC_CONTROL, 0x49, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x4A, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x4B, 0xEE);
	cdcWriteReg(CDC_CONTROL, 0x4C, 0x10);
	cdcWriteReg(CDC_CONTROL, 0x4D, 0xD8);
	cdcWriteReg(CDC_CONTROL, 0x4E, 0x7E);
	cdcWriteReg(CDC_CONTROL, 0x4F, 0xE3);
	cdcWriteReg(CDC_CONTROL, 0x58, 0x7F);
	cdcWriteReg(CDC_CONTROL, 0x74, 0xD2);
	cdcWriteReg(CDC_CONTROL, 0x75, 0x2C);
	cdcWriteReg(CDC_SOUND, 0x22, 0x70);
	cdcWriteReg(CDC_SOUND, 0x2C, 0x20);

	// Finish up!
	cdcReadReg (CDC_TOUCHCNT, 0x02);
	cdcWriteReg(CDC_TOUCHCNT, 0x02, 0x98);
	cdcWriteReg(0xFF, 0x05, 0x00); //writeTSC(0x00, 0xFF);

	// Power management
	writePowerManagement(PM_READ_REGISTER, 0x00); //*(unsigned char*)0x40001C2 = 0x80, 0x00; // read PWR[0]   ;<-- also part of TSC !
	writePowerManagement(PM_CONTROL_REG, 0x0D); //*(unsigned char*)0x40001C2 = 0x00, 0x0D; // PWR[0]=0Dh    ;<-- also part of TSC !
}

static void arm7_readFirmware(tNDSHeader* ndsHeader) {
	PERSONAL_DATA slot1;
	PERSONAL_DATA slot2;

	short slot1count, slot2count; //u8
	short slot1CRC, slot2CRC;

	u32 userSettingsBase;

	// Get settings location
	readFirmware(0x20, &userSettingsBase, 2);

	u32 slot1Address = userSettingsBase * 8;
	u32 slot2Address = userSettingsBase * 8 + 0x100;

	// Reload DS Firmware settings
	readFirmware(slot1Address, &slot1, sizeof(PERSONAL_DATA)); //readFirmware(slot1Address, personalData, 0x70);
	readFirmware(slot2Address, &slot2, sizeof(PERSONAL_DATA)); //readFirmware(slot2Address, personalData, 0x70);
	readFirmware(slot1Address + 0x70, &slot1count, 2); //readFirmware(slot1Address + 0x70, &slot1count, 1);
	readFirmware(slot2Address + 0x70, &slot2count, 2); //readFirmware(slot1Address + 0x70, &slot2count, 1);
	readFirmware(slot1Address + 0x72, &slot1CRC, 2);
	readFirmware(slot2Address + 0x72, &slot2CRC, 2);

	// Default to slot 1 user settings
	void *currentSettings = &slot1;

	short calc1CRC = swiCRC16(0xFFFF, &slot1, sizeof(PERSONAL_DATA));
	short calc2CRC = swiCRC16(0xFFFF, &slot2, sizeof(PERSONAL_DATA));

	// Bail out if neither slot is valid
	if (calc1CRC != slot1CRC && calc2CRC != slot2CRC) { return; }

	// If both slots are valid pick the most recent
	if (calc1CRC == slot1CRC && calc2CRC == slot2CRC) { 
		currentSettings = (slot2count == ((slot1count + 1) & 0x7f) ? &slot2 : &slot1); //if ((slot1count & 0x7F) == ((slot2count + 1) & 0x7F)) {
	} else {
		if (calc2CRC == slot2CRC) { currentSettings = &slot2; }
	}

	PERSONAL_DATA* personalData = (PERSONAL_DATA*)((u32)__NDSHeader - (u32)ndsHeader + (u32)PersonalData); //(u8*)((u32)ndsHeader - 0x180)

	tonccpy(PersonalData, currentSettings, sizeof(PERSONAL_DATA));

	if (useTwlCfg && (language == 0xFF || language == -1)) { language = twlCfgLang; }

	if (language >= 0 && language <= 7) {
		// Change language
		personalData->language = language; //*(u8*)((u32)ndsHeader - 0x11C) = language;
	}

	if (personalData->language != 6 && ndsHeader->reserved1[8] == 0x80) {
		ndsHeader->reserved1[8] = 0;	// Patch iQue game to be region-free
		ndsHeader->headerCRC16 = swiCRC16(0xFFFF, ndsHeader, 0x15E);	// Fix CRC
	}
}

static void arm7_resetMemory (void) {
	int i, reg;

	REG_IME = 0;

	for (i=0; i<16; i++) {
		SCHANNEL_CR(i) = 0;
		SCHANNEL_TIMER(i) = 0;
		SCHANNEL_SOURCE(i) = 0;
		SCHANNEL_LENGTH(i) = 0;
	}
	REG_SOUNDCNT = 0;

	// Clear out ARM7 DMA channels and timers
	for (i=0; i<4; i++) {
		DMA_CR(i) = 0;
		DMA_SRC(i) = 0;
		DMA_DEST(i) = 0;
		TIMER_CR(i) = 0;
		TIMER_DATA(i) = 0;
		for(reg=0; reg<0x1c; reg+=4)*((u32*)(0x04004104 + ((i*0x1c)+reg))) = 0; //Reset NDMA.
	}

	// Clear out FIFO
	REG_IPC_SYNC = 0;
	REG_IPC_FIFO_CR = IPC_FIFO_ENABLE | IPC_FIFO_SEND_CLEAR;
	REG_IPC_FIFO_CR = 0;

	// clear IWRAM - 037F:8000 to 0380:FFFF, total 96KiB
	arm7_clearmem ((void*)0x037F8000, 96*1024);

	// clear most of EXRAM - except after 0x022FD800, which has the ARM9 code
	arm7_clearmem ((void*)0x02000000, 0x002FD800);

	// clear last part of EXRAM, skipping the ARM9's section
	arm7_clearmem ((void*)0x023FE000, 0x2000);
	
	// Clear tmp header region previously used by custom struct
	arm7_clearmem ((void*)TMP_HEADER, 0x160);

	REG_IE = 0;
	REG_IF = ~0;
	REG_AUXIE = 0;
	REG_AUXIF = ~0;
	
	(*(vu32*)(0x04000000-4)) = 0;  //IRQ_HANDLER ARM7 version
	(*(vu32*)(0x04000000-8)) = ~0; //VBLANK_INTR_WAIT_FLAGS, ARM7 version
	REG_POWERCNT = 1;
}

static void setMemoryAddress(const tNDSHeader* ndsHeader) {
	if (ndsHeader->unitCode > 0) {
		copyLoop((u32*)0x02FFFA80, (u32*)ndsHeader, 0x160);	// Make a duplicate of DS header

		*(u32*)(0x02FFA680) = 0x02FD4D80;
		*(u32*)(0x02FFA684) = 0x00000000;
		*(u32*)(0x02FFA688) = 0x00001980;

		*(u32*)(0x02FFF00C) = 0x0000007F;
		*(u32*)(0x02FFF010) = 0x550E25B8;
		*(u32*)(0x02FFF014) = 0x02FF4000;

		// Set region flag
		if (strncmp(getRomTid(ndsHeader)+3, "J", 1) == 0) {
			*(u8*)(0x02FFFD70) = 0;
		} else if (strncmp(getRomTid(ndsHeader)+3, "E", 1) == 0) {
			*(u8*)(0x02FFFD70) = 1;
		} else if (strncmp(getRomTid(ndsHeader)+3, "P", 1) == 0) {
			*(u8*)(0x02FFFD70) = 2;
		} else if (strncmp(getRomTid(ndsHeader)+3, "U", 1) == 0) {
			*(u8*)(0x02FFFD70) = 3;
		} else if (strncmp(getRomTid(ndsHeader)+3, "C", 1) == 0) {
			*(u8*)(0x02FFFD70) = 4;
		} else if (strncmp(getRomTid(ndsHeader)+3, "K", 1) == 0) {
			*(u8*)(0x02FFFD70) = 5;
		}
	}
	
    // Set memory values expected by loaded NDS
    // from NitroHax, thanks to Chism
	*((u32*)0x02FFF800) = chipID;					// CurrentCardID
	*((u32*)0x02FFF804) = chipID;					// Command10CardID
	*((u16*)0x02FFF808) = ndsHeader->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
	*((u16*)0x02FFF80A) = ndsHeader->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]
	*((u16*)0x02FFF850) = 0x5835;
	// Copies of above
	*((u32*)0x02FFFC00) = chipID;					// CurrentCardID
	*((u32*)0x02FFFC04) = chipID;					// Command10CardID
	*((u16*)0x02FFFC08) = ndsHeader->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
	*((u16*)0x02FFFC0A) = ndsHeader->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]
	*((u16*)0x02FFFC10) = 0x5835;
	*((u16*)0x02FFFC40) = 0x01;						// Boot Indicator -- EXTREMELY IMPORTANT!!! Thanks to cReDiAr
}


static void arm7_loadBinary (aFile File) {
	fileRead((void*)NDS_HEADER, &File, 0, 0x170);
	ndsHeader = (tNDSHeader*)NDS_HEADER;
	fileRead(ndsHeader->arm9destination, &File, ndsHeader->arm9romOffset, ndsHeader->arm9binarySize);
	fileRead(ndsHeader->arm7destination, &File, ndsHeader->arm7romOffset, ndsHeader->arm7binarySize);
	setMemoryAddress(ndsHeader);
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Main function
void arm7_main (void) {
	
	if (REG_SCFG_EXT & BIT(31)) {
		REG_MBK9=0xFCFFFF0F;
		REG_MBK6=0x09403900;
		REG_MBK7=0x09803940;
		REG_MBK8=0x09C03980;
	}
		
	if (language != 0xFF)language = (int)tmpData->language;
	storedFileCluster = tmpData->fileCluster;
	if ((tmpData->cachedChipID != 0) && (tmpData->cachedChipID != 0xFFFFFFFF))chipID = tmpData->cachedChipID;
	
	// Init card
	if(!FAT_InitFiles(true))errorOutput(ERR_SDINIT, true);
	
	aFile file;

	getFileFromCluster(&file, storedFileCluster);
	
	if ((file.firstCluster < CLUSTER_FIRST) || (file.firstCluster >= CLUSTER_EOF))errorOutput(ERR_FILELOAD, true);

	if (file.firstCluster == CLUSTER_FREE)errorOutput(ERR_FILELOAD, true);
	
	// Synchronise start
	while (ipcRecvState() != ARM9_START);
	ipcSendState(ARM7_START);

	// Wait until ARM9 is ready
	while (ipcRecvState() != ARM9_READY);

	errorOutput(ERR_STS_CLR_MEM, false);

	ipcSendState(ARM7_MEMCLR);

	// Get ARM7 to clear RAM
	arm7_resetMemory();
	
	errorOutput(ERR_STS_LOAD_BIN, false);

	ipcSendState(ARM7_LOADBIN);

	// Load the NDS file
	arm7_loadBinary(file);
	
	errorOutput(ERR_STS_STARTBIN, false);
	
	arm7_readFirmware(ndsHeader); // Header has to be loaded first

	if (REG_SCFG_EXT & BIT(31)) {
		if (cdcReadReg(CDC_SOUND, 0x22) == 0xF0) {
			// Switch touch mode to NTR
			*(u16*)0x4004700 = 0x800F;
			NDSTouchscreenMode();
			*(u16*)0x4000500 = 0x807F;
		}
		REG_GPIO_WIFI |= BIT(8);	// Old NDS-Wifi mode
		REG_SCFG_ROM = 0x703;
		REG_SCFG_CLK = 0x100;
		REG_SCFG_EXT = 0x12A00000;
		while(REG_VCOUNT!=191);
		while(REG_VCOUNT==191);
	}
	
	ipcSendState(ARM7_BOOTBIN);
	
	arm7_reset();
}

