#include <nds.h>
#include <fat.h>
#include <nds/arm9/dldi.h>
#include <nds/fifocommon.h>
#include <nds/fifomessages.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

#include <string.h>
#include <unistd.h>

#include "bootsplash.h"
#include "nrio_dldi.h"
#include "my_sd.h"
#include "tonccpy.h"
#include "read_card.h"
#include "nds_card.h"
#include "launch_engine.h"

#define NDS_HEADER 0x027FFE00
#define DSI_HEADER 0x027FE000

#define INITBUFFER  0x02000000
#define STAGE2_HEADER 0x02000200
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

ALIGN(4) u8 ReadBuffer[SECTOR_SIZE];
// ALIGN(4) u32 CartReadBuffer[512];

static bool SCFGUnlocked = false;
static bool ErrorState = false;
static bool fatMounted = false;
static bool nrioDLDIMounted = false;
static bool sdMounted = false;
static int MenuID = 0;
static int cartSize = 0;
static bool wasDSi = false;
static bool ntrMode = false;
static bool dldiWarned = false;
static bool uDiskFileFound = false;

extern bool __dsimode;
// static bool forceNTRMode = false;

char gameTitle[13] = {0};

static const char* textBuffer = "X------------------------------X\nX------------------------------X";
static const char* textProgressBuffer = "X------------------------------X\nX------------------------------X";
static int ProgressTracker = 0;
static bool UpdateProgressText = false;

static tNDSHeader* loadHeader(tDSiHeader* twlHeaderTemp) {
	tNDSHeader* ntrHeader = (tNDSHeader*)NDS_HEADER;
	*ntrHeader = twlHeaderTemp->ndshdr;
	return ntrHeader;
}

/*DISC_INTERFACE io_nrio_ = {
    0x4E52494F, // "NRIO"
    FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_SLOT_NDS,
    (FN_MEDIUM_STARTUP)&_nrio_startUp,
    (FN_MEDIUM_ISINSERTED)&_nrio_isInserted,
    (FN_MEDIUM_READSECTORS)&_nrio_readSectors,
    (FN_MEDIUM_WRITESECTORS)&_nrio_writeSectors,
    (FN_MEDIUM_CLEARSTATUS)&_nrio_clearStatus,
    (FN_MEDIUM_SHUTDOWN)&_nrio_shutdown
};*/

void DoWait(int waitTime = 30) {
	if (waitTime > 0)for (int i = 0; i < waitTime; i++) { swiWaitForVBlank(); }
}

void DoFATerror(bool isFatel, bool isSDError) {
	consoleClear();
	if (isSDError) { printf("SD Init Failed!\n"); } else { printf("FAT Init Failed!\n"); }
	printf("\nPress A to exit...\n");
	ErrorState = isFatel;
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
	}
}

void DoError(const char* Message) {
	consoleClear();
	printf(Message);
	printf("\n\nPress A to exit...\n");
	ErrorState = true;
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
	}
}

void InitCartNandMode(int mode = 0) {
	switch (mode) {
		case (0) : {
			u8 Init[8] = { 0, 0, 0, 0, 0, 0, 0, 0x66 };
			u8 Init2[8] = { 0, 0, 0, 0, 0, 0x21, 0x0D, 0xC1 };
			u8 Init3[8] = { 0, 0, 0, 0, 0, 0xB0, 0x0F, 0xC1 };
			u8 Init4[8] = { 0, 0, 0, 0, 0, 0x83, 0x10, 0xC1 };
			cardPolledTransfer((u32)0xA7586000, (u32*)INITBUFFER, 128, Init);
			DoWait(8);
			cardPolledTransfer((u32)0xA7020000, (u32*)(INITBUFFER + 0x10), 128, Init2);
			DoWait(8);
			cardPolledTransfer((u32)0xA7020000, (u32*)(INITBUFFER + 0x20), 128, Init3);
			DoWait(8);
			cardPolledTransfer((u32)0xA7020000, (u32*)(INITBUFFER + 0x30), 128, Init4);
			DoWait(8);
		} break;
		case (1) : {
			u8 InitB8[8] = { 0, 0, 0, 0, 0, 0, 0, 0xB8 };
			u8 Init[8] = { 0, 0, 0, 0, 0, 0, 0x09, 0xC1 };
			cardPolledTransfer((u32)0xA7586000, (u32*)(INITBUFFER + 0x40), 128, InitB8);
			DoWait(8);
			cardPolledTransfer((u32)0xAF000000, (u32*)(INITBUFFER + 0x50), 128, Init);
			DoWait(8);
			/*u8 InitB8[8] = { 0, 0, 0, 0, 0, 0, 0, 0xB8 };
			u8 Init1[8] = { 0, 0, 0, 0, 0, 0xCF, 0x10, 0xC1 };
			u8 Init2[8] = { 0, 0, 0, 0, 0, 0, 0, 0xD0 };
			cardPolledTransfer((u32)0xA7586000, (u32*)INITBUFFER, 128, InitB8);
			DoWait(8);
			cardPolledTransfer((u32)0xAF000000, (u32*)INITBUFFER, 128, Init1);
			DoWait(8);
			cardPolledTransfer((u32)0xAF180000, (u32*)INITBUFFER, 128, Init2);
			DoWait(8);*/
		} break;
	}
}

void CardInit(bool Silent = true, bool SkipSlotReset = false) {
	if (!Silent || !wasDSi)consoleClear();
	DoWait(30);
	if (!wasDSi) {
		ALIGN(4) u32 ntrHeader[0x80];
		printf("Cart reset required!\nPlease eject Cart...\n");
		do { swiWaitForVBlank(); getHeader (ntrHeader); } while (ntrHeader[0] != 0xffffffff);
		consoleClear();
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
	InitCartNandMode(/*ndsHeader, cardGetId()*/);
	DoWait();
	if (!Silent) {
		if (wasDSi && SCFGUnlocked) { iprintf("SCFG_MC Status:  %2x \n\n", REG_SCFG_MC); }
		iprintf("Detected Cart Name: %12s \n\n", gameTitle);
		iprintf("Detected Cart Game Id: %6s \n\n", gameCode);
	}
}

void MountFATDevices(bool mountSD = true) {
	if (wasDSi && mountSD && !sdMounted) {
		// Important to set this else SD init will hang/fail!
		fifoSetValue32Handler(FIFO_USER_04, sdStatusHandler, nullptr);
		DoWait();
		sdMounted = fatMountSimple("sd", __my_io_dsisd());
	} else if (!wasDSi && !fatMounted) {
		// if (!fatMounted)fatMounted = fatMountSimple("nrio", &io_nrio_);
		if (!fatMounted)fatMounted = fatInitDefault();
	}
}

void DoCartBoot() {
	consoleClear();
	if(access("sd:/nrioFiles/xBootStrap.nds", F_OK) == 0) {
		struct stat st;
		char filePath[PATH_MAX];
		int pathLen;
		const char* filename = "sd:/nrioFiles/xBootStrap.nds";
		if (stat (filename, &st) < 0) { ErrorState = true; return; }
		if (!getcwd (filePath, PATH_MAX)) { ErrorState = true; return; }
		pathLen = strlen(filename);
		strcpy (filePath + pathLen, filename);
		runLaunchEngine(-1, st.st_ino);
		ErrorState = true;
		return;
	} else {
		while(1) {
			printf("ERROR: xBootStrap.nds not found!");
			printf("\n\nPress A to abort.\n");
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;
		}
	}
	return;
}

void DoImageRestore() {
	consoleClear();
	DoWait(60);
	printf("About to write default fat image\n");
	iprintf("Target Cart Capacity: %d\n", cartSize);
	if (cartSize < 1100000) {
		while(1) {
			printf("ERROR: 8g(1GB) Cart capacity\nnot yet supported!\n");
			printf("\nPress A to abort.\n");
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;
		}
	}
	printf("\nPress A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if(access("sd:/nrioFiles/nrio_basicimage_16g.img", F_OK) != 0) { DoError("ERROR: Failed to locate\nnrio_basicimage_16g!\n"); return; }
	FILE *src = fopen("sd:/nrioFiles/nrio_basicimage_16g.img", "rb");
	if (!src) { DoError("ERROR: Failed to open\nnrio_basicimage_16g.img!\n"); return; }
	fseek(src, 0, SEEK_END);
	u32 fSectorSize = (ftell(src) / 0x200) + 0x200;
	u32 fSize = ftell(src);
	fseek(src, 0, SEEK_SET);
	ProgressTracker = (int)fSectorSize;
	textBuffer = "Writing FAT image to cart.\nPlease Wait...\n\n\n";
	textProgressBuffer = "Sectors Remaining: ";
	for (u64 i = 0; i < fSectorSize; i++){
		fseek(src, (i * 0x200), SEEK_SET);
		fread(ReadBuffer, 1, 0x200, src);
		io_dldi_data->ioInterface.writeSectors(i, 1, ReadBuffer);
		ProgressTracker--;
		UpdateProgressText = true;
		if (fSize < (i * 0x200))break;
	}
	while(UpdateProgressText)swiWaitForVBlank();
	fclose(src);
	consoleClear();
	printf("FAT image write finished!\n\n");
	printf("Press A to return to main menu!\n\n");
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
	// if ((u64)fsize > driveSizeFree("fat:/")) { }
}

void DoSector0Dump(){
	consoleClear();
	DoWait(60);
	printf("About to dump FAT sector 0.\n\n");
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	FILE *dest = fopen("sd:/nrioFiles/nrio_fatsector0.bin", "wb");
	if (!dest) { DoError("ERROR: Failed to create file!"); return; }
	io_dldi_data->ioInterface.readSectors(0, 1, ReadBuffer);
	fwrite(ReadBuffer, 0x200, 1, dest);
	fclose(dest);
	consoleClear();
	printf("Sector 0 dump finished!\n\n");
	printf("Press A to return to main menu!\n\n");
	printf("Press B to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)return;
		if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
	// fseek(dest, 0x200, SEEK_SET);
	// if ((u64)fsize > driveSizeFree("fat:/")) { }
}

void DoSector0Restore(){
	consoleClear();
	DoWait(60);
	printf("About to write FAT sector 0.\n\n");
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if (access("sd:/nrioFiles/nrio_fatsector0.bin", F_OK) != 0) {
		DoError("ERROR: Failed to find\nnrio_fatsector0.bin!"); 
		return;
	}
	FILE *src = fopen("sd:/nrioFiles/nrio_fatsector0.bin", "rb");
	if (!src) { DoError("ERROR: Failed to open\nnrio_fatsector0.bin!"); return; }
	fseek(src, 0, SEEK_END);
	u32 fSectorSize = (ftell(src) / 0x200);
	fseek(src, 0, SEEK_SET);
	if (fSectorSize != 1) { DoError("ERROR: nrio_fatsector0.bin has incorrect size!"); return; }
	fread(ReadBuffer, 1, 0x200, src);
	io_dldi_data->ioInterface.readSectors(0, 1, ReadBuffer);
	fclose(src);
	consoleClear();
	printf("Sector 0 write finished!\n");
	printf("Press A to return to main menu!\n\n");
	printf("Press B to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)return;
		if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
	// fseek(dest, 0x200, SEEK_SET);
	// if ((u64)fsize > driveSizeFree("fat:/")) { }
}

void DoTestDump() {
	consoleClear();
	DoWait(60);
	iprintf("About to dump %d sectors.\n\n", NUM_SECTORS);
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) {
		MountFATDevices(wasDSi);
		if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) { DoFATerror(true, wasDSi); return; }
	}
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/nrioFiles/nrio_data.bin", "wb"); } else { dest = fopen("fat:/nrioFiles/nrio_data.bin", "wb"); }
	ProgressTracker = NUM_SECTORS;
	textBuffer = "Dumping sectors to nrio_data.bin\n\nPlease Wait...\n\n\n";
	textProgressBuffer = "Sectors Remaining: ";
	for (int i = 0; i < NUM_SECTORS; i++){ 
		// _nrio_readSectors(i, 1, ReadBuffer);
		// _nrio_readSectorsTest(i, 1, ReadBuffer);
		readSectorB7Mode(ReadBuffer, (u32)(i * 0x200));
		fwrite(ReadBuffer, 0x200, 1, dest); // Used Region
		ProgressTracker--;
		UpdateProgressText = true;
	}
	fclose(dest);
	while(UpdateProgressText)swiWaitForVBlank();
	consoleClear();
	printf("Sector dump finished!\n\n");
	printf("Press A to return to main menu!\n\n");
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
	consoleClear();
	DoWait(60);
	iprintf("About to dump %d sectors.\n\n", NUM_SECTORSALT);
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) {
		MountFATDevices(wasDSi);
		if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) { DoFATerror(true, wasDSi); return; }
	}
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/nrioFiles/nrio_altdata.bin", "wb"); } else { dest = fopen("fat:/nrioFiles/nrio_altdata.bin", "wb"); }
	if (!dest) { DoError("ERROR: Failed to create file!"); return; }
	
	InitCartNandMode(1);
		
	ProgressTracker = NUM_SECTORSALT;
	textBuffer = "Dumping sectors to\nnrio_altdata.bin\n\nPlease Wait...\n\n\n";
	textProgressBuffer = "Sectors Remaining: ";
	
	for (int i = 0; i < NUM_SECTORSALT; i++){ 
		// _nrio_readSectors(i, 1, ReadBuffer);
		// _nrio_readSectorsTest(i, 1, ReadBuffer);
		readSectorB7Mode(ReadBuffer, (u32)(i * 0x200));
		fwrite(ReadBuffer, 0x200, 1, dest); // Used Region
		ProgressTracker--;
		UpdateProgressText = true;
	}
	fclose(dest);
	while (UpdateProgressText)swiWaitForVBlank();
	consoleClear();
	printf("Sector dump finished!\n\n");
	printf("Press A to return to main menu!\n\n");
	printf("Press B to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) {
			CardInit(); // Card Init required to return to normal.
			InitCartNandMode();
			return; 
		} else if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
}

void DoStage2Dump() {
	consoleClear();
	DoWait(60);
	printf("About to dump stage2 SRL...\n\n");
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) {
		MountFATDevices(wasDSi);
		if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) { DoFATerror(true, wasDSi); return; }
	}
	int Sectors;
	readSectorB7Mode((void*)STAGE2_HEADER, 0);
	DoWait(2);
	if ((stage2Header->romSize > 0) && (stage2Header->romSize < 0x00FFFFF)) {
		Sectors = (int)(stage2Header->romSize / 0x200) + 1; // Add one sector to avoid underdump if size is not a multiple of 128 words
	} else {
		Sectors = 20000;
	}
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/nrioFiles/nrio_stage2.nds", "wb"); } else { dest = fopen("fat:/nrioFiles/nrio_stage2.nds", "wb"); }
	if (!dest) { DoError("ERROR: Failed to create file!"); return; }
	ProgressTracker = Sectors;
	textBuffer = "Dumping to nrio_stage2.nds...\nPlease Wait...\n\n\n";
	textProgressBuffer = "Sectors Remaining: ";
	for (int i = 0; i < Sectors; i++){
		readSectorB7Mode(ReadBuffer, (u32)(i * 0x200));
		fwrite(ReadBuffer, 0x200, 1, dest);
		ProgressTracker--;
		UpdateProgressText = true;
	}
	fclose(dest);
	consoleClear();
	iprintf("Dump finished!\n\n");
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
	consoleClear();
	DoWait(60);
	printf("About to dump uDisk...\n\n");
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) {
		MountFATDevices(wasDSi);
		if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) { DoFATerror(true, wasDSi); return; }
	}
	int Sectors;
	readSectorB7Mode((void*)UDISK_HEADER, UDISKROMOFFSET);
	DoWait(2);
	if ((uDiskHeader->romSize > 0) && (uDiskHeader->romSize < 0x00FFFFF)) {
		Sectors = (int)(uDiskHeader->romSize / 0x200) + 1; // Add one sector to avoid underdump if size is not a multiple of 128 words
	} else {
		Sectors = 20000;
	}
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/nrioFiles/udisk.nds", "wb"); } else { dest = fopen("fat:/nrioFiles/udisk.nds", "wb"); }
	if (!dest) { DoError("ERROR: Failed to create file!"); return; }
	ProgressTracker = Sectors;
	textBuffer = "Dumping to udisk.nds...\nPlease Wait...\n\n\n";
	textProgressBuffer = "Sectors Remaining: ";
	for (int i = 0; i < Sectors; i++){
		readSectorB7Mode(ReadBuffer, (u32)(i * 0x200) + UDISKROMOFFSET);
		fwrite(ReadBuffer, 0x200, 1, dest);
		ProgressTracker--;
		UpdateProgressText = true;
	}
	fclose(dest);
	consoleClear();
	iprintf("Dump finished!\n\n");
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


void vblankHandler (void) {
	if (UpdateProgressText) {
		consoleClear();
		printf(textBuffer);
		printf(textProgressBuffer);
		iprintf("%d \n", ProgressTracker);
		UpdateProgressText = false;
	}
}

int DLDIMenu() {
	int value = -1;
	consoleClear();
	if (!dldiWarned) {
		printf("WARNING! Leaving DLDI menu will\nrequire restart\n");
		printf("Do you wish to continue?\n\n");
		printf("\nPress A to continue.\n");
		printf("\nPress B to return to main menu.\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)break;
			if(keysDown() & KEY_B) { value = 5; break; }
		}
		consoleClear();
		if (value == 5) { return value; } else { dldiWarned = true; }
	}
	swiWaitForVBlank();
	if (!ntrMode) {
		ntrMode = true;
		// __dsimode = false;
		REG_SCFG_EXT &= ~(1UL << 14);
		REG_SCFG_EXT &= ~(1UL << 15);
		for (int i = 0; i < 30; i++) { while(REG_VCOUNT!=191); while(REG_VCOUNT==191); }
	}
	if (!nrioDLDIMounted) {
		nrioDLDIMounted = fatInitDefault();
		if (!nrioDLDIMounted) { DoError("DLDI Init Failed!\n"); return 4; }
		if (!uDiskFileFound)uDiskFileFound = (access("sd:/nrioFiles/xBootStrap.nds", F_OK) == 0);
	}
	consoleClear();
	struct statvfs st;
	if ((cartSize == 0) && statvfs("fat:/", &st) == 0)cartSize = (st.f_bsize * st.f_blocks) / 1024;
	printf("Press [A] to boot cart menu\n");
	printf("\nPress [X] to dump sector 0\n");
	printf("\nPress [Y] to restore sector 0\n");
	printf("\nPress [START] to write default\nFAT image\n");
	printf("\n\n\n\n\n\n\n\nPress [B] to exit...\n");
	while(value == -1) {
		swiWaitForVBlank();
		scanKeys();
		switch (keysDown()){
			case KEY_A: 	{ value = 0; } break;
			case KEY_X: 	{ value = 1; } break;
			case KEY_Y: 	{ value = 2; } break;
			case KEY_START:	{ value = 3; } break;
			case KEY_B:		{ ErrorState = true; value = 4; } break;
		}
	}
	return value;
}

int MainMenu() {
	int value = -1;
	consoleClear();
	printf("Press [A] to dump Stage2 SRL\n");
	printf("\nPress [Y] to dump UDISK SRL\n");
	if (wasDSi)printf("\nPress [X] go to recovery menu\n");
	printf("\nPress [DPAD UP] to do test dump\n");
	printf("\nPress [DPAD DOWN] to do test\ndump in alt mode\n");
	if (!wasDSi)printf("\n");
	printf("\n\n\n\n\n\nPress [B] to exit\n");
	// printf("START to write new banner\n");
	// printf("SELECT to write new Arm binaries\n\n\n");
	while(value == -1) {
		swiWaitForVBlank();
		scanKeys();
		switch (keysDown()){
			case KEY_A: 	{ value = 0; } break;
			case KEY_Y: 	{ value = 1; } break;
			case KEY_X: { if (wasDSi)value = 2; } break;
			case KEY_UP: 	{ value = 3; } break;
			case KEY_DOWN: 	{ value = 4; } break;
			case KEY_B:		{ value = 5; } break;
			/*case KEY_X: 	{ value = 5; } break;
			case KEY_SELECT:{ value = 7; } break;*/
		}
	}
	return value;
}


int main() {
	wasDSi = isDSiMode();
	if (wasDSi && (REG_SCFG_EXT & BIT(31)))SCFGUnlocked = true;
	defaultExceptionHandler();
	BootSplashInit(wasDSi);
	printf("\n\n\n\n\n\n Checking cart. Please Wait...");
	sysSetCardOwner(BUS_OWNER_ARM9);
	if (!wasDSi)sysSetCartOwner(BUS_OWNER_ARM7);
	MountFATDevices();
	toncset((void*)0x02000000, 0, 0x800);
	CardInit();
	if ((!sdMounted && wasDSi) || (!fatMounted && !wasDSi)) {
		DoFATerror(true, wasDSi);
		consoleClear();
		fifoSendValue32(FIFO_USER_03, 1);
		return 0;
	}
	if (memcmp(cartHeader->ndshdr.gameCode, "DSGB", 4)) {
		consoleClear();
		printf("WARNING! The cart in slot 1\ndoesn't appear to be an N-Card\nor one of it's clones!\n\n");
		printf("Press A to continue...\n\n");
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
	if (*(u32*)(INITBUFFER) != 0x2991AE1D) {
		consoleClear();
		InitCartNandMode(1);
		FILE *initFile = fopen("sd:/nrioFiles/nrio_InitLog.bin", "wb");
		if (initFile) {
			fwrite((u32*)INITBUFFER, 0x200, 1, initFile); // Used Region
			fclose(initFile);
		}
		toncset((void*)0x02000000, 0, 0x800);
		printf("WARNING! Cart returned\nunexpected response from init\ncommand!\n\n");
		printf("Press A to continue...\n\n");
		printf("Press B to abort...\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A) {
				consoleClear();
				CardInit();
				DoWait();
				break;
			}
			if(keysDown() & KEY_B) {
				consoleClear();
				fifoSendValue32(FIFO_USER_03, 1);
				return 0;
			}
		}
	}
	// Enable vblank handler
	irqSet(IRQ_VBLANK, vblankHandler);
	
	if (wasDSi) {
		if(access("sd:/nrioFiles", F_OK) != 0)mkdir("sd:/nrioFiles", 0777);
	} else {
		if(access("fat:/nrioFiles", F_OK) != 0)mkdir("fat:/nrioFiles", 0777);
	}
	
	while(1) {
		if (ErrorState) {
			consoleClear();
			fifoSendValue32(FIFO_USER_03, 1);
			return 0;
		}
		switch (MenuID) {
			case 0: {
				switch (MainMenu()) {
					case 0: { DoStage2Dump(); } break;
					case 1: { DoUdiskDump(); } break;
					case 2: { MenuID = 1; } break;
					case 3: { DoTestDump(); } break;
					case 4: { DoAltTestDump(); } break;
					case 5: { ErrorState = true; } break;
				}
			} break;
			case 1: {
				switch (DLDIMenu()) {
					case 0: { DoCartBoot(); } break;
					case 1: { DoSector0Dump(); } break;
					case 2: { DoSector0Restore(); } break;
					case 3: { DoImageRestore(); } break;
					case 4: { ErrorState = true; } break;
					case 5: { MenuID = 0; } break;
				}
			} break;
		}
		swiWaitForVBlank();
    }
	return 0;
}

