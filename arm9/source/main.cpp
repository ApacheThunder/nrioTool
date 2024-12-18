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

#include "ui.h"
#include "nrio_card.h"
#include "my_sd.h"
#include "nitrofs.h"
#include "tonccpy.h"
#include "read_card.h"
#include "nds_card.h"
#include "launch_engine.h"

#define NDS_HEADER 0x02FFFE00
#define DSI_HEADER 0x02FFE000
#define FILECOPYBUFFER 0x02100000
#define MAXFILESIZE (u32)0x00200000

#define INITBUFFER  0x02000000
#define STAGE2_HEADER 0x02000200
#define UDISK_HEADER 0x02000400

#define CONSOLE_SCREEN_WIDTH 32
#define CONSOLE_SCREEN_HEIGHT 24

// #define NUM_SECTORS 4023552
// #define NUM_SECTORS 3991808
// #define NUM_SECTORS 524288 // 2GBIT (256MB)
// #define NUM_SECTORS 32769
// #define NUM_SECTORS 16896
// #define NUM_SECTORS 10000
// #define NUM_SECTORS 5056
#define NUM_SECTORS 3300 // Default

#define SECTOR_SIZE 512

#define STAGE1OFFSET (u32)0x400
#define STAGE1SECTORCOUNT 22

// #define STAGE2OFFSET (u32)0x00020000
// #define UDISKROMOFFSET (u32)0x00080000

#define FALLBACKSIZE 20000

#define UPDATEBUFFER  0x02700000
// #define MAXUPDATERESULTSIZE 1545
#define MAXUPDATERESULTSIZE 4096
// #define UPDATE_SIZE 1170


tNDSHeader* ndsHeader = (tNDSHeader*)NDS_HEADER;
tDSiHeader* cartHeader = (tDSiHeader*)DSI_HEADER;

tNDSHeader* stage2Header = (tNDSHeader*)STAGE2_HEADER;
tNDSHeader* uDiskHeader = (tNDSHeader*)UDISK_HEADER;

DTCM_DATA ALIGN(4) u8 ReadBuffer[SECTOR_SIZE];
DTCM_DATA ALIGN(4) u32 ReadBuffer32[128];
DTCM_DATA ALIGN(4) u8 FATBuffer[2048];

ALIGN(4) u32 BlockTableBuffer[0x4000];
ALIGN(4) u32 NandBlockBuffer[0x1000]; // Each block is eqilivent to 32 sectors (when a sector is 512 bytes in size)

// ALIGN(4) u32 CartReadBuffer[512];

const u32 STAGE2BLOCKTABLE = 0x8000;

DTCM_DATA volatile u32 UDISKBLOCKTABLE = 0x40000;
DTCM_DATA volatile u32 STAGE2OFFSET = 0x00020000;
DTCM_DATA volatile u32 UDISKROMOFFSET = 0x00080000;
DTCM_DATA volatile int BlockTableSize = 0;
DTCM_DATA volatile int MenuID = 0;
DTCM_DATA volatile int cartSize = 0;
DTCM_DATA volatile int ProgressTracker = 0;
DTCM_DATA volatile bool autoBootCart = false;
DTCM_DATA volatile bool SCFGUnlocked = false;
DTCM_DATA volatile bool ErrorState = false;
DTCM_DATA volatile bool fatMounted = false;
DTCM_DATA volatile bool nitroFSMounted = false;
DTCM_DATA volatile bool nrioDLDIMounted = false;
DTCM_DATA volatile bool sdMounted = false;
DTCM_DATA volatile bool wasDSi = false;
DTCM_DATA volatile bool ntrMode = false;
DTCM_DATA volatile bool dldiWarned = false;
DTCM_DATA volatile bool uDiskFileFound = false;
DTCM_DATA volatile bool WarningPosted = false;
DTCM_DATA volatile bool isBootleg = false;
DTCM_DATA volatile bool isCustom = false;
DTCM_DATA volatile bool UpdateProgressText = false;
DTCM_DATA volatile bool UpdateDebugText = false;

DTCM_DATA char gameTitle[13] = {0};

const char* textBufferTop = "X------------------------------X\nX------------------------------X";
const char* textProgressTopBuffer = "X------------------------------X\nX------------------------------X";
const char* textBuffer = "X------------------------------X\nX------------------------------X";
const char* textProgressBuffer = "X------------------------------X\nX------------------------------X";

bool dldiDebugMode = false;
extern bool TopSelected;

ITCM_CODE void SetSCFG() {
	if (REG_SCFG_EXT & BIT(31)) {
		REG_SCFG_EXT &= ~(1UL << 14);
		REG_SCFG_EXT &= ~(1UL << 15);
		for (int i = 0; i < 10; i++) { while(REG_VCOUNT!=191); while(REG_VCOUNT==191); }
	}
}

u64 getBytesFree(const char* drivePath) {
    struct statvfs st;
    statvfs(drivePath, &st);
    return (u64)st.f_bsize * (u64)st.f_bavail;
}

static tNDSHeader* loadHeader(tDSiHeader* twlHeaderTemp) {
	tNDSHeader* ntrHeader = (tNDSHeader*)NDS_HEADER;
	*ntrHeader = twlHeaderTemp->ndshdr;
	return ntrHeader;
}

void DoWait(int waitTime = 30) {
	if (waitTime > 0)for (int i = 0; i < waitTime; i++) { swiWaitForVBlank(); }
}

void DoFATerror(bool isFatel, bool isSDError) {
	consoleClear();
	if (isSDError) { printf("SD Init Failed!\n"); } else { printf("FAT Init Failed!\n"); }
	printf("\nPress [A] to exit...\n");
	ErrorState = isFatel;
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
	}
}

void DoError(const char* Message, bool isFatal = true) {
	consoleClear();
	printf(Message);
	if (isFatal) { printf("\n\nPress [A] to exit...\n"); } else { printf("\n\nPress [A] to abort...\n"); }
	ErrorState = isFatal;
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
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
		printf("Cart reset required!\nPlease eject and reinsert Cart.\n\n");
		iprintf("Press A when finished....\n");
		DoWait();
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)break;
		}
	}
	// Do cart init stuff to wake cart up. DLDI init may fail otherwise!
	cardInit((sNDSHeaderExt*)cartHeader, SkipSlotReset);
	char gameCode[7] = {0};
	tonccpy(gameTitle, cartHeader->ndshdr.gameTitle, 12);
	tonccpy(gameCode, cartHeader->ndshdr.gameCode, 6);
	ndsHeader = loadHeader(cartHeader); // copy twlHeaderTemp to ndsHeader location
	u32 CardType = *(u32*)(DSI_HEADER + 0x08);
	InitCartNandReadMode(CardType);
	DoWait();
	if (!Silent) {
		if (wasDSi && SCFGUnlocked) { iprintf("SCFG_MC Status:  %2x \n\n", REG_SCFG_MC); }
		iprintf("Detected Cart Name: %12s \n\n", gameTitle);
		iprintf("Detected Cart Game Id: %6s \n\n", gameCode);
		iprintf("Detected Cart Type: %8x \n\n", (unsigned int)CardType);
		printf("Press any button to continue...");
		do { swiWaitForVBlank(); scanKeys(); } while (!keysDown());
	}
}

void MountFATDevices(bool mountSD = true) {
	if (wasDSi) {
		if (mountSD && !sdMounted) {
			// Important to set this else SD init will hang/fail!
			fifoSetValue32Handler(FIFO_USER_04, sdStatusHandler, nullptr);
			DoWait();
			sdMounted = fatMountSimple("sd", __my_io_dsisd());
		}
		if (sdMounted && !nitroFSMounted) {
			if (!nitroFSMounted)nitroFSMounted = nitroFSInit("sd:/title/00030004/44534e52/content/00000000.app");
			if (!nitroFSMounted)nitroFSMounted = nitroFSInit("sd:/nrioTool.nds");
			if (!nitroFSMounted)nitroFSMounted = nitroFSInit("sd:/nds/nrioTool.nds");
			if (!nitroFSMounted)nitroFSMounted = nitroFSInit("sd:/_nds/nrioTool.nds");
			if (!nitroFSMounted)nitroFSMounted = nitroFSInit("sd:/homebrew/nrioTool.nds");
		}
	} else {
		// if (!fatMounted)fatMounted = fatMountSimple("nrio", &io_nrio_);
		if (!fatMounted)fatMounted = fatInitDefault();
		if (fatMounted && !nitroFSMounted) {
			nitroFSMounted = nitroFSInit("fat:/nrioTool.nds");
			if (!nitroFSMounted)nitroFSMounted = nitroFSInit("fat:/nrioTool.nds");
			if (!nitroFSMounted)nitroFSMounted = nitroFSInit("fat:/nds/nrioTool.nds");
			if (!nitroFSMounted)nitroFSMounted = nitroFSInit("fat:/_nds/nrioTool.nds");
			if (!nitroFSMounted)nitroFSMounted = nitroFSInit("fat:/homebrew/nrioTool.nds");
		}
	}
	
}

void DoUpdateConvert() {
	consoleClear();
	DoWait(60);
	printf("About to convert update file...\n\n");
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
	
	FILE *src;
	
	if (sdMounted) { src = fopen("sd:/nrioFiles/update.bin", "rb"); } else { src = fopen("fat:/nrioFiles/update.bin", "rb"); }
	
	if (!src) { DoError("ERROR: Failed to find/open\n update file!", false); return; }
	
	fseek(src, 0, SEEK_END);
	int fileSize = (int)(ftell(src) / 0x200);
	fseek(src, 0, SEEK_SET);
	
	int UpdateResultSize = (((fileSize * 512) + (fileSize * 3)) / 512) + 1;
	
	if (UpdateResultSize > MAXUPDATERESULTSIZE) { DoError("ERROR: Update result will exceed\nmemory limits!", false); return; }
	
	// if (fileSize != UPDATE_SIZE) { fclose(src); DoError("ERROR: Unexpected file size for\n target file!", false); return; }

	consoleClear();
	ProgressTracker = (fileSize + 1);
	textBuffer = "Reading and converting\nupdate.bin...\n\n\n";
	textProgressBuffer = "Blocks Remaining: ";
	u32 MarkerOffset = 0;
	
	for (int i = 0; i < (fileSize + 1); i++) {
		fseek(src, (u32)(i * 512), SEEK_SET);
		u32 ReadOffset = ((u32)UPDATEBUFFER + ((u32)(i * 512) + MarkerOffset));
		fread((void*)ReadOffset, 1, 0x200, src);
		/* *(vu8*)((u32)(ReadOffset + 0x200)) = 0x3C;
		*(vu8*)((u32)(ReadOffset + 0x201)) = 0xC0;
		*(vu8*)((u32)(ReadOffset + 0x202)) = 0x3C;*/
		*(vu8*)((u32)(ReadOffset + 0x200)) = 0x4D;
		*(vu8*)((u32)(ReadOffset + 0x201)) = 0x4B;
		*(vu8*)((u32)(ReadOffset + 0x202)) = 0x52;
		MarkerOffset += 0x03;
		ProgressTracker--;
		UpdateProgressText = true;
	}
	fclose(src);
	while(UpdateProgressText)swiWaitForVBlank();
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/nrioFiles/update_converted.bin", "wb"); } else { dest = fopen("fat:/nrioFiles/update_converted.bin", "wb"); }
	if (!dest) { DoError("ERROR: Failed to create\nupdate_converted.bin!", false); return; }
	consoleClear();
	printf("Writing final result to\nupdate_converted.bin...\n");
	fwrite((void*)UPDATEBUFFER, (u32)(UpdateResultSize * 512), 1, dest);
	fclose(dest);
	while (UpdateProgressText)swiWaitForVBlank();
	consoleClear();
	iprintf("Conversion finished!\n\n");
	iprintf("Press A to return to menu\n");
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

void DoCartBoot() {
	consoleClear();
	if(access("sd:/nrioFiles/xBootStrap.nds", F_OK) != 0 && nitroFSMounted) {
		FILE *src = fopen("nitro:/xBootStrap.nds", "rb");
		if (src) {
			FILE *dest = fopen("sd:/nrioFiles/xBootStrap.nds", "wb");
			if (dest) {
				fseek(src, 0, SEEK_END);
				u32 fSize = ftell(src);
				fseek(src, 0, SEEK_SET);
				if (fSize < MAXFILESIZE && fSize > 0) {
					printf("WARNING: xBootStrap not found!\n");
					printf("Trying to use NitroFS copy.\n\n");
					printf("Please wait...\n");
					fread((void*)FILECOPYBUFFER, 1, fSize, src);
					fwrite((void*)FILECOPYBUFFER, fSize, 1, dest);
					fclose(src);
					fclose(dest);
				}
			}
		}
	}
	if (access("sd:/nrioFiles/xBootStrap.nds", F_OK) == 0) {
		struct stat st;
		char filePath[PATH_MAX];
		int pathLen;
		const char* filename = "sd:/nrioFiles/xBootStrap.nds";
		if (stat (filename, &st) < 0) { ErrorState = true; return; }
		if (!getcwd (filePath, PATH_MAX)) { ErrorState = true; return; }
		pathLen = strlen(filename);
		strcpy (filePath + pathLen, filename);
		runLaunchEngine(-1, st.st_ino, false);
		ErrorState = true;
		return;
	} else {
		printf("ERROR: xBootStrap.nds not found!");
		printf("\n\n\nPress [A] to abort.\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;
		}
	}
	return;
}

void DoGM9iBoot() {
	consoleClear();
	if(access("sd:/nrioFiles/GM9Nrio.nds", F_OK) != 0 && nitroFSMounted) {
		FILE *src = fopen("nitro:/GM9Nrio.nds", "rb");
		if (src) {
			FILE *dest = fopen("sd:/nrioFiles/GM9Nrio.nds", "wb");
			if (dest) {
				fseek(src, 0, SEEK_END);
				u32 fSize = ftell(src);
				fseek(src, 0, SEEK_SET);
				if (fSize < MAXFILESIZE && fSize > 0) {
					printf("WARNING: GM9Nrio not found!\n");
					printf("Trying to use NitroFS copy.\n\n");
					printf("Please wait...\n");
					fread((void*)FILECOPYBUFFER, 1, fSize, src);
					fwrite((void*)FILECOPYBUFFER, fSize, 1, dest);
					fclose(src);
					fclose(dest);
				}
			}
		}
	}
	if (access("sd:/nrioFiles/GM9Nrio.nds", F_OK) == 0) {
		struct stat st;
		char filePath[PATH_MAX];
		int pathLen;
		const char* filename = "sd:/nrioFiles/GM9Nrio.nds";
		if (stat (filename, &st) < 0) { ErrorState = true; return; }
		if (!getcwd (filePath, PATH_MAX)) { ErrorState = true; return; }
		pathLen = strlen(filename);
		strcpy (filePath + pathLen, filename);
		runLaunchEngine(-1, st.st_ino, true);
		ErrorState = true;
		return;
	} else {
		printf("ERROR: GM9Nrio.nds not found!");
		printf("\n\n\nPress [A] to abort.\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;
		}
	}
	return;
}

void DoSaveTestBoot() {
	consoleClear();
	if(access("sd:/nrioFiles/nrioSaveTester.nds", F_OK) != 0 && nitroFSMounted) {
		FILE *src = fopen("nitro:/nrioSaveTester.nds", "rb");
		if (src) {
			FILE *dest = fopen("sd:/nrioFiles/nrioSaveTester.nds", "wb");
			if (dest) {
				fseek(src, 0, SEEK_END);
				u32 fSize = ftell(src);
				fseek(src, 0, SEEK_SET);
				if (fSize < MAXFILESIZE && fSize > 0) {
					printf("nrioSaveTester not found!\n");
					printf("Trying to use NitroFS copy.\n\n");
					printf("Please wait...\n");
					fread((void*)FILECOPYBUFFER, 1, fSize, src);
					fwrite((void*)FILECOPYBUFFER, fSize, 1, dest);
					fclose(src);
					fclose(dest);
				}
			}
		}
	}
	if (access("sd:/nrioFiles/nrioSaveTester.nds", F_OK) == 0) {
		struct stat st;
		char filePath[PATH_MAX];
		int pathLen;
		const char* filename = "sd:/nrioFiles/nrioSaveTester.nds";
		if (stat (filename, &st) < 0) { ErrorState = true; return; }
		if (!getcwd (filePath, PATH_MAX)) { ErrorState = true; return; }
		pathLen = strlen(filename);
		strcpy (filePath + pathLen, filename);
		runLaunchEngine(-1, st.st_ino, true);
		ErrorState = true;
		return;
	} else {
		printf("ERROR:\nnrioSaveTester.nds not found!");
		printf("\n\n\nPress [A] to abort.\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;
		}
	}
	return;
}


void DoImageDump() {
	consoleClear();
	DoWait(60);
	printf("About to dump FAT image.\n");
	printf("This may take while!.\n\n");
	printf("Press [A] to continue.\n\n");
	printf("Press [B] to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	if (wasDSi && !sdMounted) {
		MountFATDevices(wasDSi);
		if (!sdMounted) { DoFATerror(true, true); return; }
	}
	u64 CartCapacity = 0;
	struct statvfs st;
	if (statvfs("fat:/", &st) == 0)CartCapacity = (st.f_bsize * st.f_blocks);
	if (CartCapacity > getBytesFree("sd:") || CartCapacity == 0 || getBytesFree("sd:") == 0) {
		consoleClear();
		printf("ERROR: Insufficient free space!\n\n");
		printf("Press [A] to abort.\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;
		}
	}
	FILE *dest = fopen("sd:/nrioFiles/nrio_fatimage_dump.bin", "wb");
	if (!dest) { DoError("ERROR: Failed to create file!"); return; }
	ProgressTracker = (int)(CartCapacity / 0x800) + 4;
	int ReadSize = ProgressTracker;
	textBuffer = "Dumping sectors to\nnrio_fatimage_dump.bin\n\nPress [B] to abort...\n\n\n";
	textProgressBuffer = "Blocks Remaining: ";
	bool readEndedEarly = false;
	UpdateDebugText = dldiDebugMode;
	for (int i = 0; i < ReadSize; i++) {
		if (io_dldi_data->ioInterface.readSectors((i * 4), 4, FATBuffer)) {
			fwrite(FATBuffer, 0x800, 1, dest); // Used Region
		} else {
			readEndedEarly = true;
			break;
		}
		ProgressTracker--;
		if (ProgressTracker < 4)ProgressTracker = 0;
		UpdateProgressText = true;
		scanKeys();
		if(keysDown() & KEY_B)break;
	}
	fclose(dest);
	while (UpdateProgressText)swiWaitForVBlank();
	if(UpdateDebugText)UpdateDebugText = false;
	consoleClear();
	if (!readEndedEarly) {
		printf("Image dump finished!\n\n");
	} else {
		printf("Warning: Image dump finished\n early!\n\n");
	}
	printf("Press [A] to return to DLDI menu\n\n");
	printf("Press [B] to exit\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) {
			// CardInit();
			// InitCartNandReadMode();
			return; 
		} else if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
}

void DoImageRestore() {
	consoleClear();
	DoWait(60);
	printf("About to write default fat image\n\n");
	iprintf("Target Cart Capacity: %d\n\n", cartSize);
	const char* FilePath = "sd:/NOTSUPPORTED";
	if (cartSize == 2011584)FilePath = "sd:/nrioFiles/nrio_basicimage_2011584_16g.img";
	if (cartSize == 1995712)FilePath = "sd:/nrioFiles/nrio_basicimage_1995712_16g.img";
	if (access("sd:/nrioFiles/nrio_customimage.img", F_OK) == 0)FilePath = "nrio_customimage.img"; // if present, will write this file over any other.
	if (access(FilePath, F_OK) != 0 && nitroFSMounted) {
		if (cartSize == 2011584)FilePath = "nitro:/nrio_basicimage_2011584_16g.img";
		if (cartSize == 1995712)FilePath = "nitro:/nrio_basicimage_1995712_16g.img";
	}
	if (!memcmp(FilePath, "sd:/NOTSUPPORTED", 16)) {
		printf("\nERROR: Cart capacity not\nsupported!\n");
		printf("\nPress [A] to abort.\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;
		}
	}
	printf("\nPress [A] to continue.\n\n");
	printf("Press [B] to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	FILE *src = fopen(FilePath, "rb");
	if (!src) { DoError("ERROR: Failed to find or open\nnrio_basicimage!\n"); return; }
	fseek(src, 0, SEEK_END);
	int fileSize = (int)(ftell(src) / 0x800) + 4;
	u32 fSize = ftell(src);
	fseek(src, 0, SEEK_SET);
	ProgressTracker = (int)fileSize;
	textBuffer = "Writing FAT image to cart.\nPlease Wait...\n\n\n";
	textProgressBuffer = "Blocks Remaining: ";
	for (int i = 0; i < fileSize; i++){
		fseek(src, (i * 0x800), SEEK_SET);
		fread(FATBuffer, 1, 0x800, src);
		io_dldi_data->ioInterface.writeSectors((i * 4), 4, FATBuffer);
		if (ProgressTracker < 4)ProgressTracker = 0;
		ProgressTracker--;
		UpdateProgressText = true;
		if (fSize < (u32)(i * 0x800))break;
	}
	while(UpdateProgressText)swiWaitForVBlank();
	fclose(src);
	consoleClear();
	printf("FAT image write finished!\n\n");
	printf("Press [A] to return to DLDI menu\n\n");
	printf("Press [B] to exit\n");
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

void DoSector0Dump() {
	consoleClear();
	DoWait(60);
	printf("About to dump FAT sector 0.\n\n");
	printf("Press [A] to continue.\n\n");
	printf("Press [B] to abort.\n");
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
	printf("Press [A] to return to DLDI menu\n");
	printf("Press [B] to exit\n");
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

void DoSector0Restore() {
	consoleClear();
	DoWait(60);
	printf("About to write FAT sector 0.\n\n");
	printf("Press [A] to continue.\n\n");
	printf("Press [B] to abort.\n");
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
	printf("Press [A] to return to DLDI menu\n\n");
	printf("Press [B] to exit\n");
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
	printf("Press [A] to continue.\n\n");
	printf("Press [B] to abort.\n");
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
	textBuffer = "Dumping sectors to nrio_data.bin\n\nPress [B] to abort...\n\n\n";
	textProgressBuffer = "Sectors Remaining: ";
	// int Sector0Location = 44800;
	// for (int i = Sector0Location; i < NUM_SECTORS + Sector0Location; i++) {
	for (int i = 0; i < NUM_SECTORS; i++) {
		nrio_readSector(ReadBuffer, (u32)(i * 0x200));
		fwrite(ReadBuffer, 0x200, 1, dest); // Used Region
		ProgressTracker--;
		UpdateProgressText = true;
		scanKeys();
		if(keysDown() & KEY_B)break;
	}
	fclose(dest);
	while(UpdateProgressText)swiWaitForVBlank();
	consoleClear();
	printf("Sector dump finished!\n\n");
	printf("Press [A] to return to main menu\n\n");
	printf("Press [B] to exit\n");
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

void DoStage1Dump() {
	consoleClear();
	DoWait(60);
	printf("About to dump stage1 data...\n\n");
	printf("Press [A] to continue.\n\n");
	printf("Press [B] to abort.\n");
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
	if (sdMounted) { dest = fopen("sd:/nrioFiles/stage1.bin", "wb"); } else { dest = fopen("fat:/nrioFiles/stage1.bin", "wb"); }
	ProgressTracker = STAGE1SECTORCOUNT;
	textBuffer = "Dumping stage1 rom to file.\n\nPlease Wait...\n\n\n";
	textProgressBuffer = "Sectors Remaining: ";
	UpdateProgressText = true;
	for (int i = 0; i < STAGE1SECTORCOUNT; i++){
		nrio_readSector(ReadBuffer, (u32)(i * 0x200) + STAGE1OFFSET);
		fwrite(ReadBuffer, 0x200, 1, dest); // Used Region
		ProgressTracker--;
		UpdateProgressText = true;
	}
	fclose(dest);
	while(UpdateProgressText)swiWaitForVBlank();
	consoleClear();
	printf("Stage1 dump finished!\n\n");
	printf("Press [A] to return to main menu\n\n");
	printf("Press [B] to exit\n");
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

void DoStage2Dump() {
	consoleClear();
	DoWait(60);
	if (isBootleg) {
		printf("About to dump bootleg SRL...\n\n");
	} else {
		printf("About to dump stage2 SRL...\n\n");
	}
	printf("Press [A] to continue.\n\n");
	printf("Press [B] to abort.\n");
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
	toncset32((void*)BlockTableBuffer, 0xFFFFFFFF, 0xFA0);
	toncset((void*)NandBlockBuffer, 0xFF, 0x4000);
	
	// int Sectors;
	int Blocks = 1;
	int BadBlocks = 0;
	bool hasValidRomsize = false;
	
	nrio_readSector((void*)STAGE2_HEADER, STAGE2OFFSET);
	
	// 0x10000000 is the max possible rom size block table can setup
	if ((stage2Header->romSize > 0) && (stage2Header->romSize <= 0x10000000))hasValidRomsize = true;
	
	for (int i = 0; i < 128; i++) {
		nrio_readSector(((u32*)BlockTableBuffer + (i * 128)), (STAGE2BLOCKTABLE + (i * 0x200)));
	}
	u32 FirstBlock = BlockTableBuffer[0];
	u32 CurrentBlock = FirstBlock;
		
	for (int i = 1; i < 4000; i++) {
		if ((BlockTableBuffer[i] < 0x20000) || (BlockTableBuffer[i] == 0xFFFFFFFF) || (BlockTableBuffer[i] < CurrentBlock) || (BlockTableBuffer[i] > 0x80000000))break;
		if ((BlockTableBuffer[i - 1] + 0x4000) != BlockTableBuffer[i])BadBlocks++;
		if (hasValidRomsize && ((BlockTableBuffer[i] - BlockTableBuffer[0]) > stage2Header->romSize))break;
		CurrentBlock = BlockTableBuffer[i];
		Blocks++;
	}
	/*DoWait(2);
	if ((stage2Header->romSize > 0) && (stage2Header->romSize <= 0x10000000)) { // 0x10000000 is the max possible rom size block table can setup
		Sectors = (int)(stage2Header->romSize / 0x200) + 2; // Add one sector to avoid underdump if size is not a multiple of 128 words
	} else {
		Sectors = 20000;
	}*/
	FILE *dest;
	if (isBootleg) {
		if (sdMounted) { dest = fopen("sd:/nrioFiles/bootleg.nds", "wb"); } else { dest = fopen("fat:/nrioFiles/bootleg.nds", "wb"); }
	} else {
		if (sdMounted) { dest = fopen("sd:/nrioFiles/stage2.nds", "wb"); } else { dest = fopen("fat:/nrioFiles/stage2.nds", "wb"); }
	}
	
	if (!dest) { DoError("ERROR: Failed to create file!"); return; }
	ProgressTracker = Blocks;
	if (isBootleg) { 
		textBuffer = "Dumping to bootleg.nds...\nPlease Wait...\n\n\n";
	} else {
		textBuffer = "Dumping to stage2.nds...\nPlease Wait...\n\n\n";
	}
	// textProgressBuffer = "Sectors Remaining: ";
	textProgressBuffer = "Blocks Remaining: ";
	for (int i = 0; i < Blocks; i++) {
		for (int I = 0; I < 32; I++) {
			nrio_readSector(((u32*)NandBlockBuffer + (I * 128)), (BlockTableBuffer[i] + (I * 0x200)));
		}
		fwrite((u8*)NandBlockBuffer, 0x4000, 1, dest);
		ProgressTracker--;
		UpdateProgressText = true;
	}

	/* for (int i = 0; i < Sectors; i++) {
		nrio_readSector(ReadBuffer, (u32)(i * 0x200) + STAGE2OFFSET);
		fwrite((u8*)ReadBuffer, 0x200, 1, dest);
		ProgressTracker--;
		UpdateProgressText = true;
	}*/
	fclose(dest);
	consoleClear();
	printf("Dump finished!\n");
	if (BadBlocks > 0) {
		iprintf("%d blocks were reassigned\nin this section's block table.\n", BadBlocks);
	}
	printf("\nPress [A] to return to main menu\n\n");
	printf("Press [B] to exit\n");
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
	printf("About to dump uDisk SRL...\n\n");
	printf("Press [A] to continue.\n\n");
	printf("Press [B] to abort.\n");
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
	toncset32((void*)BlockTableBuffer, 0xFFFFFFFF, 0xFA0);
	toncset((void*)NandBlockBuffer, 0xFF, 0x4000);
	
	int Blocks = 1;
	int BadBlocks = 0;
	bool hasValidRomsize = false;
	
	nrio_readSector((void*)UDISK_HEADER, UDISKROMOFFSET);
	
	// 0x10000000 is the max possible rom size block table can setup
	if ((uDiskHeader->romSize > 0) && (uDiskHeader->romSize <= 0x10000000))hasValidRomsize = true;
	
	for (int i = 0; i < 128; i++) {
		nrio_readSector(((u32*)BlockTableBuffer + (i * 128)), (UDISKBLOCKTABLE + (i * 0x200)));
	}
	
	u32 FirstBlock = BlockTableBuffer[0];
	u32 CurrentBlock = FirstBlock;
		
	for (int i = 1; i < 4000; i++) {
		if ((BlockTableBuffer[i] < 0x20000) || (BlockTableBuffer[i] == 0xFFFFFFFF) || (BlockTableBuffer[i] < CurrentBlock) || (BlockTableBuffer[i] > 0x80000000))break;
		if ((BlockTableBuffer[i - 1] + 0x4000) != BlockTableBuffer[i])BadBlocks++;
		if (hasValidRomsize && ((BlockTableBuffer[i] - BlockTableBuffer[0]) > uDiskHeader->romSize))break;
		CurrentBlock = BlockTableBuffer[i];
		Blocks++;
	}
	FILE *dest;
	if (sdMounted) { dest = fopen("sd:/nrioFiles/udisk.nds", "wb"); } else { dest = fopen("fat:/nrioFiles/udisk.nds", "wb"); }
	if (!dest) { DoError("ERROR: Failed to create file!"); return; }
	ProgressTracker = Blocks;
	textBuffer = "Dumping to udisk.nds...\nPlease Wait...\n\n\n";
	textProgressBuffer = "Blocks Remaining: ";
	for (int i = 0; i < Blocks; i++) {
		for (int I = 0; I < 32; I++) {
			nrio_readSector(((u32*)NandBlockBuffer + (I * 128)), (BlockTableBuffer[i] + (I * 0x200)));
		}
		fwrite((u8*)NandBlockBuffer, 0x4000, 1, dest);
		ProgressTracker--;
		UpdateProgressText = true;
	}
	fclose(dest);
	consoleClear();
	printf("Dump finished!\n");
	if (BadBlocks > 0) {
		iprintf("%d blocks were reassigned\nin this section's block table.\n", BadBlocks);
	}
	printf("\nPress [A] to return to main menu\n\n");
	printf("Press [B] to exit\n");
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


/*void DoUdiskDump() {
	consoleClear();
	DoWait(60);
	printf("About to dump uDisk...\n\n");
	printf("Press [A] to continue.\n\n");
	printf("Press [B] to abort.\n");
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
	nrio_readSector((void*)UDISK_HEADER, UDISKROMOFFSET);
	DoWait(2);
	if ((uDiskHeader->romSize > 0) && (uDiskHeader->romSize <= 0x10000000)) {
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
		nrio_readSector(ReadBuffer, (u32)(i * 0x200) + UDISKROMOFFSET);
		fwrite(ReadBuffer, 0x200, 1, dest);
		ProgressTracker--;
		UpdateProgressText = true;
	}
	fclose(dest);
	consoleClear();
	iprintf("Dump finished!\n\n");
	iprintf("Press [A] to return to main menu\n\n");
	iprintf("Press [B] to exit\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
		if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
}*/

// Not working yet
/*void DoBannerWrite() {
	consoleClear();
	DoWait(60);
	printf("About to write banner!\n\n");
	printf("Press A to continue.\n");
	printf("Press B to abort.\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A)break;
		if(keysDown() & KEY_B)return;
	}
	toncset((void*)0x02000000, 0, 0x800);
	consoleClear();
	printf("Reading banner please wait...\n\n");
	// CardInit();
	if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) {
		MountFATDevices(wasDSi);
		if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) { DoFATerror(true, wasDSi); return; }
	}
	// nrio_readSector((void*)STAGE2_HEADER, 0);
	DoWait(2);
	consoleClear();
	FILE *bannerFile;
	if (sdMounted) { bannerFile = fopen("sd:/nrioFiles/banner.bin", "rb"); } else { bannerFile = fopen("fat:/nrioFiles/banner.bin", "rb"); }
	if (!bannerFile) { DoError("ERROR: Failed to read banner\nfile!\n\n"); return; }
	fread((void*)UPDATEBUFFER, 1, 0xA00, bannerFile);
	printf("Writing banner.bin to cart...\nPlease Wait...\n\n\n");
	// int Sectors = 5;
	// ProgressTracker = Sectors;
	// textBuffer = "Writing banner.bin to cart...\nPlease Wait...\n\n\n";
	// textProgressBuffer = "Sectors Remaining: ";
	// UpdateProgressText = true;
	// for (int i = 0; i < Sectors; i++){
		// nrio_writeSector((stage2Header->bannerOffset + (u32)(i * 0x200)), (void*)((u32)UPDATEBUFFER + (u32)(i * 0x200)));
	nrio_writeSectors(0x2000, UPDATEBUFFER, 0x800);
	nrio_writeSectors(0x2800, UPDATEBUFFER, 0x800);
		// ProgressTracker--;
		// UpdateProgressText = true;
	// }
	// UpdateProgressText = true;
	fclose(bannerFile);
	CardInit();
	consoleClear();
	printf("Banner write finished!\n\n");
	printf("Press A to return to main menu!\n");
	printf("Press B to exit!\n");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if(keysDown() & KEY_A) return;
		if(keysDown() & KEY_B) { ErrorState = true; return; }
	}
}*/

int UtilityMenu() {
	if(!WarningPosted)LoadTopScreenUtilitySplash();
	int value = -1;
	consoleClear();
	printf("Press [A] to boot GodMode9Nrio\n");
	printf("\n");
	printf("Press [X] to build update file\n");
	printf("\n"); 
	printf("\n"); 
	printf("\n");
	printf("\n"); // printf("Press [Y] write banner to cart\n");
	printf("\n");
	printf("\n\n\n\n\n\n\n\nPress [B] to go to main menu...\n");
	while(value == -1) {
		swiWaitForVBlank();
		scanKeys();
		switch (keysDown()){
			case KEY_A: 	{ value = 0; } break;
			case KEY_X:		{ value = 1; } break;
			// case KEY_LEFT:		{ value = 2; } break;
			case KEY_B:		{ value = 4; } break;
		}
	}
	return value;
}


int DLDIMenu() {
	int value = -1;
	consoleClear();
	if (!dldiWarned) {
		printf("WARNING! Leaving DLDI menu will\nrequire restart!\n");
		printf("Do you wish to continue?\n\n");
		printf("\nPress [A] to continue\n");
		printf("\nPress [B] to return to main menu\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)break;
			if(keysDown() & KEY_B) { value = 6; break; }
		}
		consoleClear();
		if (value == 6) { return value; } else { dldiWarned = true; }
	}
	if (WarningPosted)consoleClearTop(false);
	LoadTopScreenDLDISplash();
	swiWaitForVBlank();
	if (!ntrMode) {
		ntrMode = true;
		SetSCFG();
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
	printf("\nPress [DPAD UP] to dump FAT\nimage\n");
	printf("\nPress [START] to write default\nFAT image\n");
	printf("\n\n\n\n\n\n\n\nPress [B] to exit...\n");
	while(value == -1) {
		swiWaitForVBlank();
		scanKeys();
		switch (keysDown()){
			case KEY_A: 	{ value = 0; } break;
			case KEY_X: 	{ value = 1; } break;
			case KEY_Y: 	{ value = 2; } break;
			case KEY_UP:	{ value = 3; } break;
			case KEY_START:	{ value = 4; } break;
			case KEY_B:		{ ErrorState = true; value = 5; } break;
		}
	}
	return value;
}

int MainMenu() {
	int value = -1;
	consoleClear();
	if (isBootleg) {
		printf("Press [A] to dump Bootleg SRL\n");
		printf("\n \n");
	} else {
		if (isCustom) {
			printf(" \n");
		} else {
			printf("Press [A] to dump Stage2 SRL\n");
		}
		printf("\nPress [Y] to dump UDISK SRL\n");
	}
	if (wasDSi)printf("\nPress [X] go to DLDI menu\n");
	printf("\nPress [DPAD LEFT] dump main SRL\n");
	printf("\nPress [DPAD RIGHT] to do test\ndump\n");
	printf("\nPress [L] go to utility menu\n");
	
	if (!wasDSi)printf("\n");
	printf("\n\n\n\nPress [B] to exit\n");
	// printf("START to write new banner\n");
	// printf("SELECT to write new Arm binaries\n\n\n");
	while(value == -1) {
		swiWaitForVBlank();
		scanKeys();
		switch (keysDown()) {
			case KEY_A: 		{ value = 0; } break;
			case KEY_Y: 		{ value = 1; } break;
			case KEY_L: 		{ value = 2; } break;
			case KEY_X: 		{ if (wasDSi)value = 3; } break;
			case KEY_LEFT: 		{ value = 4; } break;
			case KEY_RIGHT:		{ value = 5; } break;
			case KEY_B:			{ value = 6; } break;
			case KEY_START:		{ value = 7; } break;
			case KEY_SELECT:	{ value = 8; } break;
		}
	}
	return value;
}


void vblankHandler (void) {
	if (UpdateProgressText) {
		if (TopSelected) {
			SelectBotConsole();
			TopSelected = false;
		}
		consoleClear();
		printf(textBuffer);
		printf(textProgressBuffer);
		iprintf("%d \n", ProgressTracker);
		UpdateProgressText = false;
	}
	if (UpdateDebugText) {
		PrintToTop("%02x ", *(u8*)0x40001A1, false);
		PrintToTop("%08x", *(u32*)0x40001A8, false);
		PrintToTop("%08x ", *(u32*)0x40001AC, false);
		PrintToTop("%08x\n", *(u32*)0x40001A4, false);
		// UpdateDebugText = false;
	}
}

int main() {
	scanKeys();
	if(keysDown() & KEY_B)autoBootCart = true;
	wasDSi = isDSiMode();
	if (wasDSi && (REG_SCFG_EXT & BIT(31)))SCFGUnlocked = true;
	defaultExceptionHandler();
	BootSplashInit();
	
	if (autoBootCart) {
		printf("\n\n\n\n\n\n       Launching XuluMenu.\n          Please Wait...");
	} else {
		printf("\n\n\n\n\n\n Checking cart. Please Wait...");
	}
	
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
		if(!WarningPosted)LoadTopScreenDebugSplash();
		WarningPosted = true;
		consoleClear();
		PrintToTop("WARNING! The cart in slot 1\ndoesn't appear to be an\nN-Card or one of it's clones!\n\n", -1, false);
		
	}
	if (*(u32*)(INITBUFFER) != 0x2991AE1D) {
		if(!WarningPosted)LoadTopScreenDebugSplash();
		consoleClear();
		/*FILE *initFile = fopen("sd:/nrioFiles/nrio_InitLog.bin", "wb");
		if (initFile) {
			fwrite((u32*)INITBUFFER, 0x200, 1, initFile); // Used Region
			fclose(initFile);
		}
		toncset((void*)0x02000000, 0, 0x800);*/
		PrintToTop("WARNING! Cart returned\nunexpected response from init\ncommand!\n\n", -1, false);
		PrintToTop("Command response debug:\n\n", -1, false);
		PrintToTop("%08x\n", *(u32*)0x02000010, false);
		PrintToTop("%08x\n", *(u32*)0x02000020, false);
		PrintToTop("%08x\n", *(u32*)0x02000030, false);
		PrintToTop("%08x\n", *(u32*)0x02000040, false);
		WarningPosted = true;
	}
	if (autoBootCart) { SetSCFG(); DoCartBoot(); }
	// Enable vblank handler
	irqSet(IRQ_VBLANK, vblankHandler);
	
	if (wasDSi) { if(access("sd:/nrioFiles", F_OK) != 0)mkdir("sd:/nrioFiles", 0777); } else { if(access("fat:/nrioFiles", F_OK) != 0)mkdir("fat:/nrioFiles", 0777); }
	
	// Determin location of stage2 rom and set offset based on first block table entry for it in 0x8000 block table used by stage1.
	// "Bootleg" style N-Cards typically have block table setup to load stage2 from 0x40000 instead of 0x80000. (stage2 being the actual game rom in this instance)
	// This is not a gurantee so I've added code to handle reading first block table address and determining stage2 location from that.
	nrio_readSector(ReadBuffer32, STAGE2BLOCKTABLE);
	// Detect if bootleg style N-Card if the value is the expected value bootleg style N-Cards use. (mostly just for UI purposes)
	if (ReadBuffer32[0] > 0x7FFFF) { isCustom = true; } else if (ReadBuffer32[0] > 0x3FFFF) { isBootleg = true;	}
	STAGE2OFFSET = ReadBuffer32[0];
	
	// Determin location of udisk rom and set offset based on first block table entry.
	// Will use stage2 block table if stage2 block table was modified to use udisk section.
	if (isCustom) {
		nrio_readSector(ReadBuffer32, STAGE2BLOCKTABLE);
		UDISKBLOCKTABLE = STAGE2BLOCKTABLE;
	} else {
		nrio_readSector(ReadBuffer32, UDISKBLOCKTABLE);
	}
	UDISKROMOFFSET = ReadBuffer32[0];

	while(1) {
		if (ErrorState) {
			consoleClear();
			fifoSendValue32(FIFO_USER_03, 1);
			return 0;
		}
		switch (MenuID) {
			case 0: {
				switch (MainMenu()) {
					case 0: { if (!isCustom)DoStage2Dump(); } break;
					case 1: { if(!isBootleg)DoUdiskDump(); } break;
					case 2: { MenuID = 1; } break;
					case 3: { MenuID = 2; } break;
					case 4: { DoStage1Dump(); } break;
					case 5: { DoTestDump(); } break;
					case 6: { ErrorState = true; } break;
					case 7: {
						SetSCFG();
						DoCartBoot();
					} break;
					case 8: {
						SetSCFG();
						DoGM9iBoot();
					} break;
				}
			} break;
			case 1: {
				switch (UtilityMenu()) {
					case 0: { 
						SetSCFG();
						DoGM9iBoot();
					} break;
					case 1: { DoUpdateConvert(); } break;
					// case 3: { DoBannerWrite(); } break;
					case 4: { 
						MenuID = 0; 
						if (!WarningPosted)LoadTopScreenSplash();
					} break;
				}
			} break;
			case 2: {
				switch (DLDIMenu()) {
					case 0: { DoCartBoot(); } break;
					case 1: { DoSector0Dump(); } break;
					case 2: { DoSector0Restore(); } break;
					case 3: { DoImageDump(); } break;
					case 4: { DoImageRestore(); } break;
					case 5: { ErrorState = true; } break;
					case 6: { MenuID = 0; } break;
				}
			} break;
		}
		swiWaitForVBlank();
    }
	return 0;
}

