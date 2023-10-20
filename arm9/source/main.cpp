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

#define NDS_HEADER 0x02FFFE00
#define DSI_HEADER 0x02FFE000

#define STAGE2_HEADER 0x02000000
#define UDISK_HEADER 0x02000200

#define CONSOLE_SCREEN_WIDTH 32
#define CONSOLE_SCREEN_HEIGHT 24
#define StatRefreshRate 41

#define NUM_SECTORS 50000
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

bool ErrorState = false;
bool nrioMounted = false;
bool sdMounted = false;

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

void DoFATerror(bool isFatel) {
	PrintProgramName();
	printf("FAT Init Failed!\n");
	ErrorState = isFatel;
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
	}
}

void InitCartData(tNDSHeader* ndsHeader, u32 CHIPID) {
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
	
	uint8_t Init[8] = { 0, 0, 0, 0, 0, 0, 0, 0x66 };
	uint8_t Init2[8] = { 0, 0, 0, 0, 0, 0x21, 0x0D, 0xC1 };
	uint8_t Init3[8] = { 0, 0, 0, 0, 0, 0xB0, 0x0F, 0xC1 };
	uint8_t Init4[8] = { 0, 0, 0, 0, 0, 0x83, 0x10, 0xC1 };
	// uint8_t InitHeader[8] = { 0, 0, 0, 0, 0, 0, 0, 0xB7 };
	
	cardPolledTransfer((u32)0xA7586000, (u32*)0x02000000, 0, Init);
	DoWait(5);
	cardPolledTransfer((u32)0xA7020000, (u32*)0x02000000, 128, Init2);
	DoWait(5);
	cardPolledTransfer((u32)0xA7020000, (u32*)0x02000000, 128, Init3);
	DoWait(5);
	cardPolledTransfer((u32)0xA7020000, (u32*)0x02000000, 128, Init4);
	DoWait(5);
	readSectorB7Mode((u32*)NDS_HEADER, UDISKROMOFFSET);
	// readSectorB7Mode((u32*)NDS_HEADER, 0);
	// cardPolledTransfer((u32)0xB918027E, (u32*)NDS_HEADER, 0x200, InitHeader);
}

void CardInit(bool Silent, bool SCFGUnlocked, bool SkipSlotReset = false) {
	if (!Silent) { PrintProgramName(); }
	DoWait(30);
	// Do cart init stuff to wake cart up. DLDI init may fail otherwise!
	cardInit((sNDSHeaderExt*)cartHeader, SkipSlotReset);
	char gameCode[7] = {0};
	tonccpy(gameTitle, cartHeader->ndshdr.gameTitle, 12);
	tonccpy(gameCode, cartHeader->ndshdr.gameCode, 6);
	
	ndsHeader = loadHeader(cartHeader); // copy twlHeaderTemp to ndsHeader location
	
	InitCartData(ndsHeader, cardGetId());
	DoWait();
	if (!Silent) {
		if (SCFGUnlocked) { iprintf("SCFG_MC Status:  %2x \n\n", REG_SCFG_MC); }
		iprintf("Detected Cart Name: %12s \n\n", gameTitle);
		iprintf("Detected Cart Game Id: %6s \n\n", gameCode);
	}
}

void MountFATDevices(bool mountSD) {
	if (mountSD) {
		if (!sdMounted) {
			// Important to set this else SD init will hang/fail!
			fifoSetValue32Handler(FIFO_USER_04, sdStatusHandler, nullptr);
			DoWait();
			sdMounted = fatMountSimple("sd", __my_io_dsisd());
			PrintProgramName();
		}
	}
	
	CardInit(false, false);
		
	if (!nrioMounted)nrioMounted = fatMountSimple("nrio", &io_nrio_);
	// nrioMounted = true;
}

/*bool DumpSectors(u32 sectorStart, u32 sectorCount, void* buffer, bool allowHiddenRegion) {
	PrintProgramName();
	if (!nrioMounted) {
		printf("ERROR! NRIO DLDI Init failed!\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)break;
		}
		ErrorState = true;
		return false;
	}
	DoWait(80);
	iprintf("About to dump %d sectors.\n\nPress A to begin!\n", USED_NUMSECTORS);
	printf("Press B to abort!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) break;
		if(keysDown() & KEY_B) return false;
	}
	PrintProgramName();
	printf("Reading sectors to ram...\n");
	if (allowHiddenRegion) {
		_nrio_readSectorsTest(sectorStart, sectorCount, buffer);
		// _nrio_readSectors(sectorStart, sectorCount, buffer);
	} else {
		// _nrio_readSectors(sectorStart, sectorCount, buffer);
		_nrio_readSectorsTest(sectorStart, sectorCount, buffer);
	}
	DoWait(80);
	return true;
}*/


void DoNormalDump(bool SCFGUnlocked) {
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
	if (SCFGUnlocked & !sdMounted) {
		MountFATDevices(SCFGUnlocked);
		if (!sdMounted) {
			DoFATerror(true);
			return;
		}
	} 
	FILE *dest;
	// if (sdMounted) { dest = fopen("sd:/nrio_rom.bin", "wb"); } else { dest = fopen("nrio:/nrio_rom.bin", "wb"); }
	if (sdMounted) { dest = fopen("sd:/nrio_test.bin", "wb"); } else { dest = fopen("nrio:/nrio_test.bin", "wb"); }
	int SectorsRemaining = NUM_SECTORS;
	int RefreshTimer = 0;
	for (int i = 0; i < NUM_SECTORS; i++){ 
		if (RefreshTimer == 0) {
			RefreshTimer = StatRefreshRate;
			swiWaitForVBlank();
			PrintProgramName();
			printf("Dumping sectors to nrio_rom.bin.\nPlease Wait...\n\n\n");
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
	iprintf("Sector dump finished!\n");
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

void DoStage2Dump(bool SCFGUnlocked) {
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
	if (SCFGUnlocked & !sdMounted) {
		MountFATDevices(SCFGUnlocked);
		if (!sdMounted) {
			DoFATerror(true);
			return;
		}
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

void DoUdiskDump(bool SCFGUnlocked) {
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
	if (SCFGUnlocked & !sdMounted) {
		MountFATDevices(SCFGUnlocked);
		if (!sdMounted) {
			DoFATerror(true);
			return;
		}
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


int MainMenu(bool SCFGUnlocked) {
	PrintProgramName();
	printf("Press [A] to dump Stage2.\n");
	printf("Press [X] to dump UDISK.\n");
	printf("Press [DPAD UP] to do test dump.\n");
	printf("\nPress [B] to abort and exit.\n");
	// printf("DPAD DOWN to write custom header\n");
	// printf("START to write new banner\n");
	// printf("SELECT to write new Arm binaries\n\n\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		switch (keysDown()){
			case KEY_A: 	{ return 0; } break;
			case KEY_X: 	{ return 1; } break;
			case KEY_UP: 	{ return 2; } break;
			case KEY_B: 	{ return 3; } break;
			/*case KEY_START: { return 4; } break;
			case KEY_SELECT:{ return 5; } break;
			case KEY_DOWN:	{ return 6; } break;*/
		}
	}
	return 0;
}

extern bool __dsimode;
bool forceNTRMode = false;

int main() {
	// Wait till Arm7 is ready
	// Some SCFG values may need updating by arm7. Wait till that's done.
	bool SCFGUnlocked = false;
	if ((REG_SCFG_EXT & BIT(31))) {
		// REG_SCFG_CLK = 0x80;
		// REG_SCFG_EXT &= ~(1UL << 13);
		/*if (forceNTRMode) { 
			REG_SCFG_EXT &= ~(1UL << 14);
			REG_SCFG_EXT &= ~(1UL << 15);
			__dsimode = false;
		}*/
		SCFGUnlocked = true;
		DoWait(60);
	}
	defaultExceptionHandler();
	BootSplashInit(false);
	sysSetCardOwner(BUS_OWNER_ARM9);
	MountFATDevices(SCFGUnlocked);
	if (!nrioMounted) {
		DoFATerror(true);
		consoleClear();
		fifoSendValue32(FIFO_USER_03, 1);
		return 0;
	}
	while(1) {
		switch (MainMenu(SCFGUnlocked)) {
			case 0: { DoStage2Dump(SCFGUnlocked); } break;
			case 1: { DoUdiskDump(SCFGUnlocked); } break;
			case 2: { DoNormalDump(SCFGUnlocked); } break;
			case 3: { ErrorState = true; } break;
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

