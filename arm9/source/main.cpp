#include <nds.h>
// #include <nds/arm9/dldi.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>

#include "bootsplash.h"
#include "dsx_dldi.h"
// #include "my_sd.h"
#include "tonccpy.h"
#include "read_card.h"

#define CONSOLE_SCREEN_WIDTH 32
#define CONSOLE_SCREEN_HEIGHT 24

// Full Hidden Region
#define SECTOR_SIZE 512
// #define SECTOR_START 0
int SECTOR_START = 0;
#define NUM_SECTORS 6000

// Trimmed Region
// Data after this is different everytime the card boots up so this region is the only one that matters
#define USED_NUMSECTORS 5376

// Arm9 Binary Region
#define ARM9SECTORSTART 8
#define ARM9SECTORCOUNT 4710
#define ARM9BYTECOUNT 2411520
#define ARM9BUFFERSIZE 0x24CC00
// Arm7 Binary Region
#define ARM7SECTORSTART 5128
#define ARM7SECTORCOUNT 128
#define ARM7BYTECOUNT 65536
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

#define COPYBUFFERSIZE 0x2A0000 // Used Region
// #define COPYBUFFERSIZE 0x2EE000 // Full Region

// u8 CopyBuffer[SECTOR_SIZE*NUM_SECTORS];
u8 CopyBuffer[SECTOR_SIZE*USED_NUMSECTORS];
u8 BannerBuffer[SECTOR_SIZE*BANNERSECTORCOUNT];
// char* CopyBuffer[SECTOR_SIZE*USED_NUMSECTORS];
// char* CopyBuffer = (char*)0x02FC0000;
// u8* CopyBuffer = (u8*)0x02040000;

extern int tempSectorTracker;
extern bool enableReadWriteConsoleMessages;
extern void PrintProgramName(void);

bool ErrorState = false;

bool SwapMode = false;
bool BannerMode = false;

// extern void bannerWrite(int sectorStart, int writeSize);

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

 bool FoundWriteDevice(bool unlockedSCFG) {
	/*if (unlockedSCFG & fatMountSimple("dsx", __my_io_dsisd())) {
		PrintProgramName();
		return true;
	} else*/
	if (fatMountSimple("dsx", &io_dsx_)) { return true; }
	return false;
}


void DoWait(int waitTime) {
	for (int i = 0; i < waitTime; i++) { swiWaitForVBlank(); }
}

void DoNormalDump(bool SCFGUnlocked) {
	PrintProgramName();
	if (!FoundWriteDevice(SCFGUnlocked)) {
		printf("FAT Init Failed!\n");
		ErrorState = true;
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_START) break;
		}
		return;
	}
	iprintf("About to dump %d sectors.\n\nPress start to begin!\n", USED_NUMSECTORS);
	printf("Press select to abort!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_START) break;
		if(keysDown() & KEY_SELECT) return;
	}
	PrintProgramName();
	printf("Reading sectors to ram...\n");
	dsx2ReadSectors(SECTOR_START, USED_NUMSECTORS, CopyBuffer);
	DoWait(80);
	PrintProgramName();
	iprintf("Writing to dsx_rom.bin.\n\nPlease wait...\n");
	FILE* dest = fopen("dsx:/dsx_rom.bin", "wb");
	fwrite(CopyBuffer, 0x2A0000, 1, dest); // Used Region
	// fwrite(CopyBuffer, 0x2EE000, 1, dest); // Full Region
	// fwrite(CopyBuffer, sizeof(CopyBuffer), 1, dest);
	fclose(dest);
	fflush(dest);
	PrintProgramName();
	iprintf("Sector dump finished!\n");
	iprintf("Press A to return to main menu!\n");
	iprintf("Press B to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
		if(keysDown() & KEY_B) ErrorState = true;
	}
}

void DoCartSwap(bool scfgUnlocked) {
	if (scfgUnlocked) {
		// printf("\n---------[Then press X]---------\n\n");
		// printf("Press A once SCFG_MC = 0x18.\n");
		// printf("Repeat X button press if it\ndoesn't match.\n");
		while(1) {
			PrintProgramName();
			printf("\n--------[Swap carts now]--------\n\n");
			printf("Press A once done...\n");
			iprintf("SCFG_MC Status:  %08x \n\n", REG_SCFG_MC);
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A) break;
		}
		fifoSendValue32(FIFO_USER_01, 1);
		DoWait(60);
		while (REG_SCFG_MC != 0x18) {
			PrintProgramName();
			iprintf("SCFG_MC Status:  %08x \n\n", REG_SCFG_MC);
			printf("Please wait.\nPress B abort and exit...\n");
			if(keysDown() & KEY_B) {
				ErrorState = true;
				return;
			}
			swiWaitForVBlank();
		}
	} else {
		PrintProgramName();
		printf("\n--------[Swap carts now]--------\n\n");
		printf("Press A once done...");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A) break;
		}
	}
	PrintProgramName();
	printf("Please wait...");
	fifoSendValue32(FIFO_USER_02, 1);
	DoWait(60);
	// Do cart init stuff to wake cart up. DLDI init may fail otherwise!
	sNDSHeaderExt cartHeader;
	cardInit(&cartHeader);
	char gameTitle[13] = {0};
	char gameCode[7] = {0};
	tonccpy(gameTitle, cartHeader.gameTitle, 12);
	tonccpy(gameCode, cartHeader.gameCode, 6);
	DoWait(60);
	PrintProgramName();
	if (scfgUnlocked) { iprintf("SCFG_MC Status:  %8x \n\n", REG_SCFG_MC); }
	iprintf("Detected Cart Name: %12s \n\n", gameTitle);
	iprintf("Detected Cart Game Id: %6s \n\n", gameCode);
	printf("Press A to continue...");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) break;
	}
	PrintProgramName();
	DoWait(30);
	if (!fatInitDefault()) {			
		printf("FAT Init Failed!\n");
		ErrorState = true;
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A) return;
		}
	}
	FILE* dest = fopen("/dsx_rom.bin", "wb");
	PrintProgramName();
	iprintf("Writing to dsx_rom.bin.\n\nPlease wait...\n");
	fwrite(CopyBuffer, 0x2A0000, 1, dest); // Used Region
	// fwrite(CopyBuffer, 0x2EE000, 1, dest); // Full Region
	// fwrite(CopyBuffer, sizeof(CopyBuffer), 1, dest);
	fclose(dest);
	fflush(dest);
	PrintProgramName();
	iprintf("Sector dump finished!\n");
	iprintf("Press A to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		// if(keysDown() & KEY_A) return;
		if(keysDown() & KEY_A) {
			ErrorState = true; 
			return;
		}
	}
}

void DoBannerWrite(bool SCFGUnlocked) {
	PrintProgramName();
	printf("About to write custom banner.\n");
	DoWait(120);
	PrintProgramName();
	printf("About to write custom banner.\n");
	printf("Press A to begin!\nPress B to abort!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) break;
		if(keysDown() & KEY_B) return;
	}
	if (!FoundWriteDevice(SCFGUnlocked)) {
		PrintProgramName();
		printf("FAT Init Failed!\n");
		ErrorState = true;
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_START) return;
		}
	}
	// PrintProgramName();
	// printf("Reading dsx_banner.bin...\n");
	FILE* sourceBanner = fopen("dsx:/dsx_banner.bin", "wb");
	if (sourceBanner) {
		fseek(sourceBanner, 0, SEEK_SET);
		fread(BannerBuffer, 1, BANNERBUFFERSIZE, sourceBanner);
		DoWait(60);
		PrintProgramName();
		printf("Do not power off or press X!\n");
		printf("Writing banner.bin to cart...\n");
		// bannerWrite(BANNERSECTORSTART, BANNERSECTORCOUNT);
		dsx2WriteSectors(BANNERSECTORSTART, BANNERSECTORCOUNT, BannerBuffer);
		fclose(sourceBanner);
		fflush(sourceBanner);
		PrintProgramName();
		iprintf("Write finished!\n\nPress A to return to main menu!\n");
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

int MainMenu(bool SCFGUnlocked) {
	int Value = 0;
	PrintProgramName();
	if (SCFGUnlocked) { printf("---------[SCFG Unlocked]--------\n\n"); }
	printf("A for normal mode.\n");
	printf("B for cart swap mode.\n");
	printf("START for banner write mode.\n");
	printf("SELECT to enable read/write\ntracking.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_SELECT){
			PrintProgramName();
			if (SCFGUnlocked) { printf("---------[SCFG Unlocked]--------\n\n"); }
				printf("A for normal mode.\n");
				printf("B for cart swap mode.\n");
				printf("START for banner write mode.\n");
			if (enableReadWriteConsoleMessages) { 
				printf("SELECT to enable read/write\ntracking.\n");
				enableReadWriteConsoleMessages = false;
			} else {
				printf("SELECT to disable read/write\ntracking.\n");
				enableReadWriteConsoleMessages = true;
			}
			DoWait(60);
		}
		if(keysDown() & KEY_A) { 
			Value = 0;
			break;
		}
		if(keysDown() & KEY_B) {
			Value = 1;
			break;
		}
		if(keysDown() & KEY_START) {
			Value = 2;
			break;
		}
	}
	return Value;
}

//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------
	defaultExceptionHandler();
	bool SCFGUnlocked = false;
	if (REG_SCFG_EXT != 0x00000000) {
		// REG_SCFG_EXT = 0x830F0191;
		// REG_SCFG_CLK = 0x187;
		SCFGUnlocked = true;
	}
	// Wait till Arm7 is ready
	// fifoWaitValue32(FIFO_USER_03);
	BootSplashInit();
	sysSetCardOwner (BUS_OWNER_ARM9);
	sysSetCartOwner (BUS_OWNER_ARM9);
	enableReadWriteConsoleMessages = false;
    while(1) {
        swiWaitForVBlank();
		int Result = MainMenu(SCFGUnlocked);
		switch (Result) {
			case 0: { DoNormalDump(SCFGUnlocked); } break;
			case 1: { DoCartSwap(SCFGUnlocked); } break;
			case 2: { DoBannerWrite(SCFGUnlocked); } break;
		}
		if (ErrorState) { break; }
    }
}

