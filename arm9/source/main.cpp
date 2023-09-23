#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <nds/arm9/dldi.h>
#include <nds/fifocommon.h>
#include <nds/fifomessages.h>

#include "bootsplash.h"
#include "dsx_dldi.h"
#include "my_sd.h"
#include "tonccpy.h"
#include "read_card.h"

#define CONSOLE_SCREEN_WIDTH 32
#define CONSOLE_SCREEN_HEIGHT 24

#define SECTOR_SIZE 512
#define SECTOR_START 0
// Full Hidden Region
// int NUM_SECTORS = 24577; // Most stuff after 5264 sector count is built in skin/theme files and other stuff
#define USED_NUMSECTORS 5264 // Reduced dump size to the original region not including extra sectors for TWL banner as that's unofficial.

// Arm9 Binary Region
#define ARM9SECTORSTART 8
#define ARM9SECTORCOUNT 4710
#define ARM9BUFFERSIZE 0x24CC00
// Arm7 Binary Region
#define ARM7SECTORSTART 5128
#define ARM7SECTORCOUNT 128
#define ARM7BUFFERSIZE 0x10000

// Banner Region 
// (All data after this in the rom region is unused dummy data so a larger TWL banner can be used!
#define BANNERSECTORSTART 5256
#define BANNERSECTORCOUNT 18
#define BANNERBUFFERSIZE 0x2400

// Incase I ever find it...it appears to either be encrypted or not present on nand
// #define HEADERSECTORSTART 0
// #define HEADERSIZE 8
// #define HEADERBufSize 0x1000

u8 CopyBuffer[SECTOR_SIZE*USED_NUMSECTORS];
u8 BannerBuffer[BANNERBUFFERSIZE];
u8 ReadBuffer[SECTOR_SIZE];

extern int tempSectorTracker;

bool ErrorState = false;

bool dsxMounted = false;
bool sdMounted = false;

char gameTitle[13] = {0};

DISC_INTERFACE io_dsx_ = {
    0x44535820, // "DSX "
    FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_SLOT_NDS,
    (FN_MEDIUM_STARTUP)&dsxStartup,
    (FN_MEDIUM_ISINSERTED)&dsxIsInserted,
    (FN_MEDIUM_READSECTORS)&dsxReadSectors,
    (FN_MEDIUM_WRITESECTORS)&dsxWriteSectors,
    (FN_MEDIUM_CLEARSTATUS)&dsxClearStatus,
    (FN_MEDIUM_SHUTDOWN)&dsxShutdown
};

void DoWait(int waitTime = 30) {
	if (waitTime > 0)for (int i = 0; i < waitTime; i++) { swiWaitForVBlank(); }
}

void PrintProgramName() {
	consoleClear();
	printf("\n");
	printf("<------------------------------>\n");
	printf("      DSX Special NAND Tool     \n");
	printf("<------------------------------>\n\n");
}

void DoFATError(bool isFatel) {
	PrintProgramName();
	printf("FAT Init Failed!\n");
	ErrorState = isFatel;
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
	}
}

void CardInit(bool Silent, bool SCFGUnlocked, bool SkipSlotReset = false) {
	if (!Silent) { PrintProgramName(); }
	DoWait(30);
	// Do cart init stuff to wake cart up. DLDI init may fail otherwise!
	sNDSHeaderExt cartHeader;
	cardInit(&cartHeader, SkipSlotReset);
	char gameCode[7] = {0};
	tonccpy(gameTitle, cartHeader.gameTitle, 12);
	tonccpy(gameCode, cartHeader.gameCode, 6);
	DoWait(60);
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
	if (!dsxMounted)dsxMounted = fatMountSimple("dsx", &io_dsx_);
}

bool DumpSectors(u32 sectorStart, u32 sectorCount, void* buffer, bool allowHiddenRegion) {
	PrintProgramName();
	if (!dsxMounted) {
		printf("ERROR! DS-Xtreme DLDI Init failed!\n");
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
		dsx2ReadSectors(sectorStart, sectorCount, buffer);
	} else {
		dsxReadSectors(sectorStart, sectorCount, buffer);
	}
	DoWait(80);
	return true;
}

void MenuDoNormalDump(bool SCFGUnlocked) {
	PrintProgramName();
	DoWait(60);
	iprintf("About to dump %d sectors.\n", USED_NUMSECTORS);
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
			DoFATError(true);
			return;
		}
	} 
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/dsx_rom.bin", "wb"); } else { dest = fopen("dsx:/dsx_rom.bin", "wb"); }
	int SectorsRemaining = USED_NUMSECTORS;
	for (int i = 0; i < USED_NUMSECTORS; i++){ 
		PrintProgramName();
		printf("Dumping sectors to dsx_rom.bin.\nPlease Wait...\n\n\n");
		iprintf("Sectors Remaining: %d \n", SectorsRemaining);
		dsx2ReadSectors(i, 1, ReadBuffer);
		fwrite(ReadBuffer, 0x200, 1, dest); // Used Region
		SectorsRemaining--;
		swiWaitForVBlank();
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

void MenuDoCartSwap() {
	PrintProgramName();
	DoWait(80);
	if (!DumpSectors(0, USED_NUMSECTORS, CopyBuffer, true))return;
	if (dsxMounted) {
		fatUnmount("dsx");
		dsxMounted = false;
	}
	PrintProgramName();
	printf("\n--------[Swap carts now]--------\n\n");
	printf("Press A once done...");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) break;
	}
	PrintProgramName();
	printf("Please wait...");
	DoWait(60);
	CardInit(false, false, true);
	printf("Press A to continue...");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) break;
	}
	PrintProgramName();
	DoWait(30);
	if (!fatInitDefault()) {
		DoFATError(true);
		return;
	}
	FILE* dest = fopen("/dsx_rom.bin", "wb");
	PrintProgramName();
	iprintf("Writing to dsx_rom.bin.\n\nPlease wait...\n");
	fwrite(CopyBuffer, 0x292000, 1, dest); // Used Region
	fclose(dest);
	fflush(dest);
	PrintProgramName();
	iprintf("Sector dump finished!\n");
	iprintf("Press A to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) {
			ErrorState = true; 
			return;
		}
	}
}

void MenuDoBannerWrite(bool SCFGUnlocked) {
	PrintProgramName();
	printf("About to write custom banner.\n");
	DoWait(60);
	printf("Press A to begin!\nPress B to abort!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) break;
		if(keysDown() & KEY_B) return;
	}
	DoWait();
	if (!sdMounted && !dsxMounted) {		
		MountFATDevices(SCFGUnlocked);
		if (!sdMounted && !dsxMounted) {
			DoFATError(true);
			return;
		}
	}
	PrintProgramName();
	printf("Reading dsx_banner.bin...\n");
	FILE *bannerFile;
	if (sdMounted) { bannerFile = fopen("sd:/dsx_banner.bin", "rb"); } else { bannerFile = fopen("dsx:/dsx_banner.bin", "rb"); }
	if (bannerFile) {
		fread(BannerBuffer, 1, BANNERBUFFERSIZE, bannerFile);
		PrintProgramName();
		printf("Do not power off!\n");
		printf("Writing new banner to cart...\n");
		DoWait(60);
		fclose(bannerFile);
		tempSectorTracker = BANNERSECTORCOUNT;
		dsx2WriteSectors(BANNERSECTORSTART, BANNERSECTORCOUNT, BannerBuffer);
		PrintProgramName();
		printf("Finished!\n\nPress A to return to main menu!\n");
	} else {
		PrintProgramName();
		printf("Banner file not found!\n\nPress A to return to main menu!\n");
	}
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
	}
}

void MenuDoArmBinaryWrites(bool SCFGUnlocked) {
	PrintProgramName();
	printf("About to write custom arm\nbinaries!\n\n");
	DoWait(60);
	printf("Press A to begin!\nPress B to abort!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) break;
		if(keysDown() & KEY_B) return;
	}
	DoWait();
	if (!sdMounted && !dsxMounted) {		
		MountFATDevices(SCFGUnlocked);
		if (!sdMounted && !dsxMounted) {
			DoFATError(true);
			return;
		}
	}
	PrintProgramName();
	printf("Reading dsx_firmware.nds...\n");
	FILE *ndsFile;
	if (sdMounted) {
		ndsFile = fopen("sd:/dsx_firmware.nds", "rb");
	} else { 
		ndsFile = fopen("dsx:/dsx_firmware.nds", "rb"); 
	}
	if (!ndsFile) {
		PrintProgramName();
		printf("Error! dsx_firmware.nds\nis missing!\n");
		printf("Press A to return to Main Menu!\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;			
		}
	}
	printf("Reading dsx_firmware.nds header...\n");
	ALIGN(4) tNDSHeader* srcNdsHeader = (tNDSHeader*)malloc(sizeof(tNDSHeader));
	fread(srcNdsHeader, sizeof(tNDSHeader), 1, ndsFile);
	// sanity check the binary sizes. We do have limits, after all
	if(srcNdsHeader->arm9binarySize > ARM9BUFFERSIZE || srcNdsHeader->arm7binarySize > ARM7BUFFERSIZE)
	{
		PrintProgramName();
		printf("Error! The ARM9 or ARM7 binary is\ntoo large!\n");
		if(srcNdsHeader->arm9binarySize > ARM9BUFFERSIZE)
			printf("ARM9 size must be under\n%d bytes!", ARM9BUFFERSIZE);
		if(srcNdsHeader->arm7binarySize > ARM7BUFFERSIZE)
			printf("ARM7 size must be under\n%d bytes!", ARM7BUFFERSIZE);
		printf("Press A to return to Main Menu!\n");
		fclose(ndsFile);
		free(srcNdsHeader);
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;			
		}
	}
	/*
		sanity check the load/execute addresses.
		These must match the original DS-Xtreme header, which, as of 1.1.3, are the following:
		ARM9 = 0x02000000, ARM7 = 0x03800000
	*/
	if(
		(u32)srcNdsHeader->arm9executeAddress != 0x02000000 ||
		(u32)srcNdsHeader->arm9destination != 0x02000000 ||
		(u32)srcNdsHeader->arm7executeAddress != 0x03800000 ||
		(u32)srcNdsHeader->arm7destination != 0x03800000
	)
	{
		PrintProgramName();
		printf("Error! The ARM9 or ARM7 binary cannot\nboot on this flashcart!\n");
		if((u32)srcNdsHeader->arm9executeAddress != 0x02000000 || (u32)srcNdsHeader->arm9destination != 0x02000000)
			printf("ARM9 must be located at\naddress 0x02000000!");
		if((u32)srcNdsHeader->arm7executeAddress != 0x03800000 || (u32)srcNdsHeader->arm7destination != 0x03800000)
			printf("ARM7 must be located at\naddress 0x03800000!");
		printf("Press A to return to Main Menu!\n");
		fclose(ndsFile);
		free(srcNdsHeader);
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;			
		}
	}
	
	printf("Reading ARM9...\n");
	fseek(ndsFile, srcNdsHeader->arm9romOffset, SEEK_SET);
	fread(CopyBuffer, 1, srcNdsHeader->arm9binarySize, ndsFile);
	PrintProgramName();
	printf("Writing new arm9 binary to cart...\n");
	printf("Do not power off!\n");
	DoWait(60);
	tempSectorTracker = ARM9SECTORCOUNT;
	dsx2WriteSectors(ARM9SECTORSTART, ARM9SECTORCOUNT, CopyBuffer);
	DoWait(60);
	PrintProgramName();
	printf("Reading ARM7...\n");
	fseek(ndsFile, srcNdsHeader->arm7romOffset, SEEK_SET);
	fread(CopyBuffer, 1, srcNdsHeader->arm7binarySize, ndsFile);
	PrintProgramName();
	printf("Writing new arm7 binary to cart...\n");
	printf("Do not power off!\n");
	fclose(ndsFile);
	free(srcNdsHeader);
	DoWait(60);
	PrintProgramName();
	tempSectorTracker = ARM7SECTORCOUNT;
	dsx2WriteSectors(ARM7SECTORSTART, ARM7SECTORCOUNT, CopyBuffer);
	PrintProgramName();
	printf("Finished!\n\nPress A to return to main menu!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
	}
}


int MainMenu(bool SCFGUnlocked) {
	int Value = 0;
	PrintProgramName();
	printf("A to dump hidden region to file.\n");
	if (!SCFGUnlocked)printf("(X to switch Slot1 DLDI target)\n\n");
	printf("START to write new banner\n");
	printf("SELECT to write new Arm binaries\n");
	printf("B to aboart and exit\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) { 
			Value = 0;
			break;
		}
		if (!SCFGUnlocked) {
			if(keysDown() & KEY_X) {
				Value = 1;
				break;
			}
		}
		if(keysDown() & KEY_START) {
			Value = 2;
			break;
		}
		if(keysDown() & KEY_SELECT){
			Value = 3;
			break;
		}
		if(keysDown() & KEY_B){
			Value = 4;
			break;
		}
	}
	return Value;
}

int main() {
	// Wait till Arm7 is ready
	// Some SCFG values may need updating by arm7. Wait till that's done.
	bool SCFGUnlocked = false;
	if ((REG_SCFG_EXT & BIT(31))) { 
		// Set NTR clocks. (DSx Does not play nice at the higher clock speeds. You've been warned!
		REG_SCFG_CLK = 0x00;
		REG_SCFG_EXT &= ~(1UL << 13);
		SCFGUnlocked = true;
		DoWait(10);
	}
	defaultExceptionHandler();
	BootSplashInit();
	sysSetCardOwner (BUS_OWNER_ARM9);
	sysSetCartOwner (BUS_OWNER_ARM9);
	MountFATDevices(SCFGUnlocked);
	if (!dsxMounted) {
		DoFATError(true);
		consoleClear();
		fifoSendValue32(FIFO_USER_03, 1);
		return 0;
	}
	while(1) {
		int Result = MainMenu(SCFGUnlocked);
		switch (Result) {
			case 0: { MenuDoNormalDump(SCFGUnlocked); } break;
			case 1: { if (!SCFGUnlocked)MenuDoCartSwap(); } break;
			case 2: { MenuDoBannerWrite(SCFGUnlocked); } break;
			case 3: { MenuDoArmBinaryWrites(SCFGUnlocked); } break;
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

