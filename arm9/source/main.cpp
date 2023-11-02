#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <nds/arm9/dldi.h>
#include <nds/fifocommon.h>
#include <nds/fifomessages.h>

#include "bootsplash.h"
#include "nrio_dldi.h"
#include "my_sd.h"
#include "tonccpy.h"
#include "read_card.h"
#include "nds_card.h"

#define NDS_HEADER 0x02FFFE00
#define DSI_HEADER 0x02FFE000

#define INITBUFFER  0x02000000
#define STAGE2_HEADER 0x02002000
#define UDISK_HEADER 0x02000400

#define CONSOLE_SCREEN_WIDTH 32
#define CONSOLE_SCREEN_HEIGHT 24
#define StatRefreshRate 41

#define NUM_SECTORS 10000
#define NUM_SECTORSALT 162
#define SECTOR_SIZE 512

#define UDISKROMOFFSET (u32)0x60000
#define FALLBACKSIZE 20000

tNDSHeader* ndsHeader = (tNDSHeader*)NDS_HEADER;
tDSiHeader* cartHeader = (tDSiHeader*)DSI_HEADER;

tNDSHeader* stage2Header = (tNDSHeader*)STAGE2_HEADER;
tNDSHeader* uDiskHeader = (tNDSHeader*)UDISK_HEADER;

// ALIGN(4) u8 CopyBuffer[SECTOR_SIZE*NUM_SECTORS];
ALIGN(4) u8 ReadBuffer[SECTOR_SIZE];
ALIGN(4) u32 CartReadBuffer[512];

static bool SCFGUnlocked = false;
static bool ErrorState = false;
static bool fatMounted = false;
static bool sdMounted = false;

// extern bool __dsimode;
// static bool forceNTRMode = false;

char gameTitle[13] = {0};

static tNDSHeader* loadHeader(tDSiHeader* twlHeaderTemp) {
	tNDSHeader* ntrHeader = (tNDSHeader*)NDS_HEADER;
	*ntrHeader = twlHeaderTemp->ndshdr;
	return ntrHeader;
}

DISC_INTERFACE io_nrio_ = {
    0x4E52494F, // "NRIO"
    FEATURE_MEDIUM_CANREAD | /*FEATURE_MEDIUM_CANWRITE |*/ FEATURE_SLOT_NDS,
    (FN_MEDIUM_STARTUP)&_nrio_startUp,
    (FN_MEDIUM_ISINSERTED)&_nrio_isInserted,
    (FN_MEDIUM_READSECTORS)&_nrio_readSectors,
    (FN_MEDIUM_WRITESECTORS)&_nrio_writeSectors,
    (FN_MEDIUM_CLEARSTATUS)&_nrio_clearStatus,
    (FN_MEDIUM_SHUTDOWN)&_nrio_shutdown
};

void DoWait(int waitTime = 30) {
	if (waitTime > 0)for (int i = 0; i < waitTime; i++) { swiWaitForVBlank(); }
}

void PrintProgramName() {
	consoleClear();
	printf("\n");
	printf("X------------------------------X\n");
	printf("|     NRIO Special NAND Tool   |\n");
	printf("X------------------------------X\n\n");
}

void DoFATerror(bool isFatel, bool isSDError) {
	PrintProgramName();
	if (isSDError) { printf("SD Init Failed!\n"); } else { printf("FAT Init Failed!\n"); }
	printf("\nPress A to exit...\n");
	ErrorState = isFatel;
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
	}
}

void InitCartData(/*tNDSHeader* ndsHeader, u32 CHIPID, bool isReInit = false*/) {
	/*if (!isReInit) {
		// Set memory values expected by loaded NDS
		// from NitroHax, thanks to Chism
		*((u32*)0x02FFF800) = CHIPID;					// CurrentCardID
		*((u32*)0x02FFF804) = CHIPID;					// Command10CardID
		*((u16*)0x02FFF808) = ndsHeader->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
		*((u16*)0x02FFF80A) = ndsHeader->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]
		*((u16*)0x02FFF850) = 0x5835;
		// Copies of above
		*((u32*)0x02FFFC00) = CHIPID;					// CurrentCardID
		*((u32*)0x02FFFC04) = CHIPID;					// Command10CardID
		*((u16*)0x02FFFC08) = ndsHeader->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
		*((u16*)0x02FFFC0A) = ndsHeader->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]
		*((u16*)0x02FFFC10) = 0x5835;
		*((u16*)0x02FFFC40) = 0x01;						// Boot Indicator
	}*/
	u8 Init[8] = { 0, 0, 0, 0, 0, 0, 0, 0x66 };
	u8 Init2[8] = { 0, 0, 0, 0, 0, 0x21, 0x0D, 0xC1 };
	u8 Init3[8] = { 0, 0, 0, 0, 0, 0xB0, 0x0F, 0xC1 };
	u8 Init4[8] = { 0, 0, 0, 0, 0, 0x83, 0x10, 0xC1 };
	// uint8_t InitHeader[8] = { 0, 0, 0, 0, 0, 0, 0, 0xB7 };
	
	cardPolledTransfer((u32)0xA7586000, (u32*)INITBUFFER, 0, Init);
	DoWait(5);
	cardPolledTransfer((u32)0xA7020000, (u32*)INITBUFFER, 128, Init2);
	DoWait(5);
	cardPolledTransfer((u32)0xA7020000, (u32*)INITBUFFER, 128, Init3);
	DoWait(5);
	cardPolledTransfer((u32)0xA7020000, (u32*)INITBUFFER, 128, Init4);
	DoWait(5);
	// if (!isReInit)readSectorB7Mode((u32*)NDS_HEADER, UDISKROMOFFSET);
	// readSectorB7Mode((u32*)NDS_HEADER, 0);
	// cardPolledTransfer((u32)0xB918027E, (u32*)NDS_HEADER, 0x200, InitHeader);
}

void CardInit(bool Silent = true, bool SkipSlotReset = false) {
	if (!Silent || !isDSiMode())PrintProgramName();
	DoWait(30);
	if (!isDSiMode()) {
		ALIGN(4) u32 ntrHeader[0x80];
		printf("Cart reset required!\nPlease eject Cart...\n");
		do { swiWaitForVBlank(); getHeader (ntrHeader); } while (ntrHeader[0] != 0xffffffff);
		PrintProgramName();
		printf("Reinsert Cart...");
		do { swiWaitForVBlank(); getHeader (ntrHeader); } while (ntrHeader[0] == 0xffffffff);
		// Delay half a second for the DS card to stabilise
		DoWait();
	}
	// Do cart init stuff to wake cart up. DLDI init may fail otherwise!
	cardInit((sNDSHeaderExt*)cartHeader, SkipSlotReset);
	char gameCode[7] = {0};
	tonccpy(gameTitle, cartHeader->ndshdr.gameTitle, 12);
	tonccpy(gameCode, cartHeader->ndshdr.gameCode, 6);
	ndsHeader = loadHeader(cartHeader); // copy twlHeaderTemp to ndsHeader location
	InitCartData(/*ndsHeader, cardGetId()*/);
	DoWait();
	if (!Silent) {
		if (isDSiMode() && SCFGUnlocked) { iprintf("SCFG_MC Status:  %2x \n\n", REG_SCFG_MC); }
		iprintf("Detected Cart Name: %12s \n\n", gameTitle);
		iprintf("Detected Cart Game Id: %6s \n\n", gameCode);
	}
}

void MountFATDevices(bool mountSD = true) {
	if (isDSiMode() && mountSD && !sdMounted) {
		// Important to set this else SD init will hang/fail!
		fifoSetValue32Handler(FIFO_USER_04, sdStatusHandler, nullptr);
		DoWait();
		sdMounted = fatMountSimple("sd", __my_io_dsisd());
		PrintProgramName();
	} else if (!isDSiMode() && !fatMounted) {
		// if (!fatMounted)fatMounted = fatMountSimple("nrio", &io_nrio_);
		if (!fatMounted)fatMounted = fatInitDefault();
	}
	// fatMounted = true;
}

void DoTestDump() {
	PrintProgramName();
	DoWait(60);
	iprintf("About to dump %d sectors.\n", NUM_SECTORS);
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if ((isDSiMode() && !sdMounted) || (!isDSiMode() && !fatMounted)) {
		MountFATDevices(isDSiMode());
		if ((isDSiMode() && !sdMounted) || (!isDSiMode() && !fatMounted)) { DoFATerror(true, isDSiMode()); return; }
	}
	/*
	
	u8 InitB8[8] = { 0, 0, 0, 0, 0, 0, 0, 0xB8 };
	u8 Init1[8] = { 0, 0, 0, 0, 0, 0xCF, 0x10, 0xC1 };
	u8 Init2[8] = { 0, 0, 0, 0, 0, 0, 0, 0xD0 };
	
	cardPolledTransfer((u32)0xA7586000, (u32*)INITBUFFER, 128, InitB8);
	DoWait(5);
	cardPolledTransfer((u32)0xAF000000, (u32*)INITBUFFER, 128, Init1);
	DoWait(5);
	cardPolledTransfer((u32)0xAF180000, (u32*)INITBUFFER, 128, Init2);
	DoWait(5);
	
	*/
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/nrio_data.bin", "wb"); } else { dest = fopen("nrio:/nrio_data.bin", "wb"); }
	int SectorsRemaining = NUM_SECTORS;
	int RefreshTimer = 0;
		
	for (int i = 0; i < NUM_SECTORS; i++){ 
		if (RefreshTimer == 0) {
			RefreshTimer = StatRefreshRate;
			swiWaitForVBlank();
			PrintProgramName();
			printf("Dumping sectors to nrio_data.bin.\nPlease Wait...\n\n\n");
			iprintf("Sectors Remaining: %d \n", SectorsRemaining);
		}
		// _nrio_readSectors(i, 1, ReadBuffer);
		// _nrio_readSectorsTest(i, 1, ReadBuffer);
		readSectorB7Mode(ReadBuffer, (u32)(i * 0x200));
		fwrite(ReadBuffer, 0x200, 1, dest); // Used Region
		SectorsRemaining--;
		RefreshTimer--;
	}
	fclose(dest);
	PrintProgramName();
	printf("Sector dump finished!\n");
	printf("Press A to return to main menu!\n");
	printf("Press B to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
		if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
}

void DoAltTestDump() {
	PrintProgramName();
	DoWait(60);
	iprintf("About to dump %d sectors.\n", NUM_SECTORSALT);
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if ((isDSiMode() && !sdMounted) || (!isDSiMode() && !fatMounted)) {
		MountFATDevices(isDSiMode());
		if ((isDSiMode() && !sdMounted) || (!isDSiMode() && !fatMounted)) { DoFATerror(true, isDSiMode()); return; }
	}
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/nrio_altdata.bin", "wb"); } else { dest = fopen("nrio:/nrio_altdata.bin", "wb"); }
	int SectorsRemaining = NUM_SECTORSALT;
	int RefreshTimer = 0;
		
	u8 InitB8[8] = { 0, 0, 0, 0, 0, 0, 0, 0xB8 };
	u8 Init[8] = { 0, 0, 0, 0, 0, 0, 0x09, 0xC1 };
	
	cardPolledTransfer((u32)0xA7586000, (u32*)INITBUFFER, 128, InitB8);
	DoWait(8);
	cardPolledTransfer((u32)0xAF000000, (u32*)INITBUFFER, 128, Init);
	DoWait(8);
	
	for (int i = 0; i < NUM_SECTORSALT; i++){ 
		if (RefreshTimer == 0) {
			RefreshTimer = StatRefreshRate;
			swiWaitForVBlank();
			PrintProgramName();
			printf("Dumping sectors to nrio_altdata.bin.\nPlease Wait...\n\n\n");
			iprintf("Sectors Remaining: %d \n", SectorsRemaining);
		}
		// _nrio_readSectors(i, 1, ReadBuffer);
		// _nrio_readSectorsTest(i, 1, ReadBuffer);
		readSectorB7Mode(ReadBuffer, (u32)(i * 0x200));
		fwrite(ReadBuffer, 0x200, 1, dest); // Used Region
		SectorsRemaining--;
		RefreshTimer--;
	}
	fclose(dest);
	PrintProgramName();
	printf("Sector dump finished!\n");
	printf("Press A to return to main menu!\n");
	printf("Press B to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) {
			CardInit(); // Card Init required to return to normal.
			InitCartData();
			return; 
		} else if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
}

void DoStage2Dump() {
	PrintProgramName();
	DoWait(60);
	printf("About to dump stage2 SRL...\n");
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if ((isDSiMode() && !sdMounted) || (!isDSiMode() && !fatMounted)) {
		MountFATDevices(isDSiMode());
		if ((isDSiMode() && !sdMounted) || (!isDSiMode() && !fatMounted)) { DoFATerror(true, isDSiMode()); return; }
	}
	int Sectors, SectorsRemaining;
	readSectorB7Mode((void*)STAGE2_HEADER, 0);
	DoWait(2);
	if ((stage2Header->romSize > 0) && (stage2Header->romSize < 0x00FFFFF)) {
		Sectors = (int)(stage2Header->romSize / 0x200) + 1; // Add one sector to avoid underdump if size is not a multiple of 128 words
	} else {
		Sectors = 20000;
	}
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/nrio_stage2.nds", "wb"); } else { dest = fopen("nrio:/nrio_stage2.nds", "wb"); }
	int RefreshTimer = 0;
	SectorsRemaining = Sectors;
	for (int i = 0; i < Sectors; i++){ 
		if (RefreshTimer == 0) {
			RefreshTimer = StatRefreshRate;
			swiWaitForVBlank();
			PrintProgramName();
			printf("Dumping to nrio_stage2.nds...\nPlease Wait...\n\n\n");
			iprintf("Sectors remaining: %d \n", SectorsRemaining);
		}
		readSectorB7Mode(ReadBuffer, (u32)(i * 0x200));
		fwrite(ReadBuffer, 0x200, 1, dest);
		SectorsRemaining--;
		RefreshTimer--;
	}
	fclose(dest);
	PrintProgramName();
	iprintf("Dump finished!\n");
	iprintf("Press A to return to main menu!\n");
	iprintf("Press B to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
		if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
}

void DoUdiskDump() {
	PrintProgramName();
	DoWait(60);
	printf("About to dump uDisk...\n");
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if ((isDSiMode() && !sdMounted) || (!isDSiMode() && !fatMounted)) {
		MountFATDevices(isDSiMode());
		if ((isDSiMode() && !sdMounted) || (!isDSiMode() && !fatMounted)) { DoFATerror(true, isDSiMode()); return; }
	}
	int Sectors, SectorsRemaining;
	readSectorB7Mode((void*)UDISK_HEADER, UDISKROMOFFSET);
	DoWait(2);
	if ((uDiskHeader->romSize > 0) && (uDiskHeader->romSize < 0x00FFFFF)) {
		Sectors = (int)(uDiskHeader->romSize / 0x200) + 1; // Add one sector to avoid underdump if size is not a multiple of 128 words
	} else {
		Sectors = 20000;
	}
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/udisk.nds", "wb"); } else { dest = fopen("nrio:/udisk.nds", "wb"); }
	int RefreshTimer = 0;
	SectorsRemaining = Sectors;
	for (int i = 0; i < Sectors; i++){ 
		if (RefreshTimer == 0) {
			RefreshTimer = StatRefreshRate;
			swiWaitForVBlank();
			PrintProgramName();
			printf("Dumping to udisk.nds...\nPlease Wait...\n\n\n");
			iprintf("Sectors remaining: %d \n", SectorsRemaining);
		}
		readSectorB7Mode(ReadBuffer, (u32)(i * 0x200) + UDISKROMOFFSET);
		fwrite(ReadBuffer, 0x200, 1, dest);
		SectorsRemaining--;
		RefreshTimer--;
	}
	fclose(dest);
	PrintProgramName();
	iprintf("Dump finished!\n");
	iprintf("Press A to return to main menu!\n");
	iprintf("Press B to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
		if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
}


int MainMenu() {
	PrintProgramName();
	printf("Press [A] to dump Stage2 SRL\n");
	printf("Press [Y] to dump UDISK SRL\n");
	printf("Press [DPAD UP] to do test dump\n");
	printf("Press [DPAD DOWN] to do test\ndump in alt mode\n");
	printf("\nPress [B] to exit\n");
	// printf("START to write new banner\n");
	// printf("SELECT to write new Arm binaries\n\n\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		switch (keysDown()){
			case KEY_A: 	{ return 0; } break;
			case KEY_Y: 	{ return 1; } break;
			case KEY_UP: 	{ return 2; } break;
			case KEY_DOWN: 	{ return 3; } break;
			case KEY_B:		{ return 4; } break;
			/*case KEY_X: 	{ return 5; } break;
			case KEY_START: { return 6; } break;
			case KEY_SELECT:{ return 7; } break;*/
		}
	}
	return 0;
}


int main() {
	// Wait till Arm7 is ready
	// Some SCFG values may need updating by arm7. Wait till that's done.
	if (REG_SCFG_EXT & BIT(31)) {
		// REG_SCFG_CLK = 0x80;
		// REG_SCFG_EXT &= ~(1UL << 13);
		/*if (forceNTRMode) { 
			REG_SCFG_EXT &= ~(1UL << 14);
			REG_SCFG_EXT &= ~(1UL << 15);
			__dsimode = false;
		}*/
		SCFGUnlocked = true;
		// DoWait(60);
	}
	defaultExceptionHandler();
	BootSplashInit(isDSiMode());
	sysSetCardOwner(BUS_OWNER_ARM9);
	if (!isDSiMode())sysSetCartOwner(BUS_OWNER_ARM7);
	MountFATDevices();
	CardInit();
	if ((!sdMounted && isDSiMode()) || (!fatMounted && !isDSiMode())) {
		DoFATerror(true, isDSiMode());
		consoleClear();
		fifoSendValue32(FIFO_USER_03, 1);
		return 0;
	}
	if (memcmp(cartHeader->ndshdr.gameCode, "DSGB", 4)) {
		PrintProgramName();
		printf("WARNING! The cart in slot 1\ndoesn't appear to be an N-Card\nor one of it's clones!\n\n");
		printf("Press A to continue...\n");
		printf("Press B to abort...\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)break;
			if(keysDown() & KEY_B) {
				consoleClear();
				fifoSendValue32(FIFO_USER_03, 1);
				return 0;
			}
		}
	}
	while(1) {
		switch (MainMenu()) {
			case 0: { DoStage2Dump(); } break;
			case 1: { DoUdiskDump(); } break;
			case 2: { DoTestDump(); } break;
			case 3: { DoAltTestDump(); } break;
			case 4: { ErrorState = true; } break;
		}
		if (ErrorState) {
			consoleClear();
			fifoSendValue32(FIFO_USER_03, 1);
			break;
		}
		swiWaitForVBlank();
    }
	return 0;
}

