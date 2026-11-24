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
#include "srl-dump.h"
#include "slot2-usb.h"
extern "C" const u8 usb_mount_bin[] __attribute__((weak));
extern "C" const u8 usb_mount_bin_end[] __attribute__((weak));

#define NDS_HEADER 0x02FFFE00
#define DSI_HEADER 0x02FFE000
#define FILECOPYBUFFER 0x02100000
#define MAXFILESIZE (u32)0x00200000

#define INITBUFFER  0x02000000

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


#define FALLBACKSIZE 20000

#define UPDATEBUFFER  0x02700000
// #define MAXUPDATERESULTSIZE 1545
#define MAXUPDATERESULTSIZE 4096
// #define UPDATE_SIZE 1170


tNDSHeader* ndsHeader = (tNDSHeader*)NDS_HEADER;
tDSiHeader* cartHeader = (tDSiHeader*)DSI_HEADER;


DTCM_DATA ALIGN(4) u8 ReadBuffer[SECTOR_SIZE];
DTCM_DATA ALIGN(4) u8 FATBuffer[2048];


// ALIGN(4) u32 CartReadBuffer[512];


DTCM_DATA volatile int BlockTableSize = 0;
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
DTCM_DATA volatile bool UpdateProgressText = false;
DTCM_DATA volatile bool UpdateDebugText = false;

DTCM_DATA char gameTitle[13] = {0};

static nrio_srl_layout srlLayout;
static bool rawNandAvailable;

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

u32 CardInit(bool Silent = true, bool SkipSlotReset = false) {
	if (!Silent || !wasDSi)consoleClear();
	DoWait(30);
	if (!wasDSi) {
		/*ALIGN(4) u32 ntrHeader[0x80];
		printf("Checking state of slot1...\n");
		do { swiWaitForVBlank(); getHeader(ntrHeader); } while (ntrHeader[0] != 0xffffffff);
		consoleClear();
		printf("Eject Cart...\n\n");
		do { swiWaitForVBlank(); getHeader(ntrHeader); } while (ntrHeader[0] == 0xffffffff);
		consoleClear();
		Delay half a second for the DS card to stabilise
		DoWait();*/
		printf("Cart reset required!\n\nPlease eject and\nreinsert Cart.\n\n");
		printf("Press A when finished....\n");
		DoWait();
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)break;
		}
	}
	const u32 status = cardInit((sNDSHeaderExt*)cartHeader, SkipSlotReset);
	if (status) return status;
	char gameCode[7] = {0};
	tonccpy(gameTitle, cartHeader->ndshdr.gameTitle, 12);
	tonccpy(gameCode, cartHeader->ndshdr.gameCode, 6);
	ndsHeader = loadHeader(cartHeader); // copy twlHeaderTemp to ndsHeader location
	u32 CardType = *(u32*)(DSI_HEADER + 0x08);
	InitCartNandReadMode(CardType);
	DoWait();
	if (!Silent) {
		if (wasDSi && SCFGUnlocked) { iprintf("SCFG_MC Status:  %2X \n\n", REG_SCFG_MC); }
		iprintf("Detected Cart Name: %12s \n\n", gameTitle);
		iprintf("Detected Cart Game Id: %6s \n\n", gameCode);
		iprintf("Detected Cart Type: %8X \n\n", (unsigned int)CardType);
		printf("Press any button to continue...");
		do { swiWaitForVBlank(); scanKeys(); } while (!keysDown());
	}
	return 0;
}

static const DISC_INTERFACE *GetSlot2Storage() {
	const DISC_INTERFACE *storage = dldiGetInternal();
	const u32 required = FEATURE_SLOT_GBA | FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE;
	const u32 mask = required | FEATURE_SLOT_NDS;
	return storage && (storage->features & mask) == required ? storage : nullptr;
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
		if (!fatMounted) {
			const DISC_INTERFACE *slot2 = GetSlot2Storage();
			if (slot2) {
				sysSetCartOwner(BUS_OWNER_ARM9);
				fatMounted = fatMountSimple("fat", slot2);
			} else {
				fatMounted = fatInitDefault();
			}
		}
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
	const char* FilePath = "NOTSUPPORTED";
	if (cartSize == 2011584)FilePath = "sd:/nrioFiles/nrio_basicimage_2011584_16g.img";
	if (cartSize == 1995712)FilePath = "sd:/nrioFiles/nrio_basicimage_1995712_16g.img";
	if (access("sd:/nrioFiles/nrio_customimage.img", F_OK) == 0)FilePath = "sd:/nrioFiles/nrio_customimage.img"; // if present, will write this file over any other.
	if (access(FilePath, F_OK) != 0 && nitroFSMounted) {
		if (cartSize == 2011584)FilePath = "nitro:/nrio_basicimage_2011584_16g.img";
		if (cartSize == 1995712)FilePath = "nitro:/nrio_basicimage_1995712_16g.img";
	}
	if (!memcmp(FilePath, "NOTSUPPORTED", 12)) {
		printf("\nERROR: Cart capacity not\nsupported!\n");
		printf("\nPress [A] to abort.\n");
		while(1) {
			swiWaitForVBlank();
			scanKeys();
			if(keysDown() & KEY_A)return;
		}
	}
	puts("Back up this cart before writes\nwhenever possible.\n");
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
	puts("Back up this cart before writes\nwhenever possible.\n");
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
	InitCartNandReadMode(*(u32*)(DSI_HEADER + 0x08));
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
	printf("Press [A] to return to menu\n\n");
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

static void RefreshSrlLayout() {
	nrio_raw_info info;
	rawNandAvailable = nrio_srl_open(&info);
	srlLayout = rawNandAvailable ? nrio_srl_detect(info.sectors) : nrio_srl_layout{};
	nrio_recovery_shutdown();
}

static void DoSrlDump(const char *name, u32 table = 0) {
	static u32 addresses[NRIO_SRL_MAP_ENTRIES];
	alignas(4) static u8 buffer[NRIO_SRL_MAP_BLOCK];

	consoleClear();
	printf("Dump %s?\n\n", name);
	puts("A Continue  B Back");
	for (;;) {
		swiWaitForVBlank();
		scanKeys();
		const u32 keys = keysDown();
		if (keys & KEY_B) return;
		if (keys & KEY_A) break;
	}

	const char *root = wasDSi ? "sd:/nrioFiles" : "fat:/nrioFiles";
	char path[48], temporary[48];
	snprintf(path, sizeof(path), "%s/%s", root, name);
	snprintf(temporary, sizeof(temporary), "%s/%s.part", root, name);
	if (access(path, F_OK) == 0 || access(temporary, F_OK) == 0) {
		DoError("Move the existing dump or .part\nfile before dumping again.", false);
		return;
	}

	nrio_raw_info info;
	if (!nrio_srl_open(&info)) {
		DoError("Could not probe Slot-1 NAND.", false);
		return;
	}
	u32 blocks = 1;
	if (table && !nrio_srl_plan(table, info.sectors, addresses, &blocks)) {
		nrio_recovery_shutdown();
		DoError("Invalid or unreadable SRL map.", false);
		return;
	}

	FILE *dest = fopen(temporary, "wb");
	if (!dest) {
		nrio_recovery_shutdown();
		DoError("Could not create the dump file.", false);
		return;
	}
	const u32 sectors = table ? NRIO_SRL_MAP_BLOCK / SECTOR_SIZE : STAGE1SECTORCOUNT;
	bool ok = true;
	bool cancelled = false;
	for (u32 i = 0; i < blocks; ++i) {
		scanKeys();
		if (keysDown() & KEY_B) {
			cancelled = true;
			break;
		}
		if (i % 32 == 0 || i + 1 == blocks) {
			consoleClear();
			printf("Dumping %s\nBlock %lu of %lu\n\nB Cancel",
			       name, (unsigned long)i + 1, (unsigned long)blocks);
		}
		const u32 address = table ? addresses[i] : STAGE1OFFSET;
		const size_t bytes = sectors * SECTOR_SIZE;
		ok = nrio_recovery_read(address / SECTOR_SIZE, sectors, buffer) &&
		     fwrite(buffer, 1, bytes, dest) == bytes;
		if (!ok) break;
	}

	if (fclose(dest)) ok = false;
	nrio_recovery_shutdown();
	if (ok && !cancelled) ok = rename(temporary, path) == 0;
	if (!ok || cancelled) {
		remove(temporary);
		if (!cancelled) DoError("Could not read or save the SRL.", false);
		return;
	}
	consoleClear();
	printf("Saved %s\n\nA Back", name);
	do {
		swiWaitForVBlank();
		scanKeys();
	} while (!(keysDown() & (KEY_A | KEY_B)));
}

extern "C" void nrioTool_sd_recovery(int action, const char *root);

static const char *RecoveryStorageRoot() {
	if (wasDSi) return sdMounted ? "sd:/nrioFiles" : nullptr;
	return fatMounted && GetSlot2Storage() ? "fat:/nrioFiles" : nullptr;
}

static void DoStorageRecovery(int action) {
	if (const char *root = RecoveryStorageRoot()) {
		nrioTool_sd_recovery(action, root);
		CardInit();
	}
}

static bool UsbMountAvailable() {
	return !wasDSi && usb_mount_bin && usb_mount_bin_end && nrio_slot2_usb_present();
}

static void DoUsbMount() {
	if (!UsbMountAvailable()) {
		DoError("Insert the N-Card USB writer\nin Slot-2 to mount.", false);
		return;
	}
	u32 size = (uintptr_t)usb_mount_bin_end - (uintptr_t)usb_mount_bin;
	runLaunchEngineFromMemory(usb_mount_bin, size);
	DoError("Could not start USB FAT mount.");
}

enum class MenuAction {
	Back,
	Utility, Dldi,
	DumpMain, DumpStage2, DumpUdisk, TestDump,
	BootCart, BootGm9, Usb,
	CheckCart, SaveBlock0, Advanced, BuildUpdate,
	PatchSafeMode, InstallBanner, RestoreArm7, RestoreBlock0, ReplaceSafeMode, RepairUdisk,
	DumpFatSector, RestoreFatSector, DumpFatImage, WriteFatImage,
};

struct MenuItem {
	MenuAction action;
	const char *label;
	bool available = true;
};

template <size_t count>
static MenuAction PickAction(const char *title, const MenuItem (&items)[count], bool exits = false) {
	const MenuItem *visible[count];
	size_t visibleCount = 0;
	for (const auto &item : items) {
		if (item.available) visible[visibleCount++] = &item;
	}

	size_t selected = 0;
	bool redraw = true;
	SelectBotConsole();
	for (;;) {
		if (redraw) {
			consoleClear();
			printf("%s\n\n", title);
			size_t row = 2;
			for (size_t i = 0; i < visibleCount; ++i) {
				if (i && (visibleCount <= 7 || i % 2 == 0)) {
					putchar('\n');
					++row;
				}
				printf("%c %s\n", i == selected ? '>' : ' ', visible[i]->label);
				++row;
			}
			for (; row < 16; ++row) putchar('\n');
			puts(exits ? "Up/Down  A Select  B Exit" : "Up/Down  A Select  B Back");
			redraw = false;
		}

		swiWaitForVBlank();
		scanKeys();
		const u32 keys = keysDown();
		if (keys & KEY_B) return MenuAction::Back;
		if (!visibleCount) continue;
		if (keys & KEY_UP) {
			selected = selected ? selected - 1 : visibleCount - 1;
			redraw = true;
		} else if (keys & KEY_DOWN) {
			selected = (selected + 1) % visibleCount;
			redraw = true;
		} else if (keys & KEY_A) {
			return visible[selected]->action;
		}
	}
}

static void RunRecoveryAction(int action, const char *startupRecoveryRoot) {
	if (startupRecoveryRoot) nrioTool_sd_recovery(action, startupRecoveryRoot);
	else DoStorageRecovery(action);
}

static void RunAdvancedMenu(const char *startupRecoveryRoot, bool recoveryAvailable) {
	const bool regular = !startupRecoveryRoot;
	const MenuItem items[] = {
		{MenuAction::PatchSafeMode, "Patch safe mode for DSi", recoveryAvailable},
		{MenuAction::InstallBanner, "Install custom banner", recoveryAvailable},
		{MenuAction::RestoreArm7, "Restore banner ARM7", recoveryAvailable},
		{MenuAction::RestoreBlock0, "Restore block 0", recoveryAvailable},
		{MenuAction::ReplaceSafeMode, "Replace safe-mode ROM", recoveryAvailable},
		{MenuAction::RepairUdisk, "Repair stock uDisk", recoveryAvailable},
		{MenuAction::BuildUpdate, "Build update file", regular},
	};
	while (!ErrorState) {
		switch (PickAction("Advanced NAND tools", items)) {
			case MenuAction::PatchSafeMode: RunRecoveryAction(0, startupRecoveryRoot); break;
			case MenuAction::InstallBanner: RunRecoveryAction(1, startupRecoveryRoot); break;
			case MenuAction::RestoreArm7: RunRecoveryAction(3, startupRecoveryRoot); break;
			case MenuAction::RestoreBlock0: RunRecoveryAction(4, startupRecoveryRoot); break;
			case MenuAction::ReplaceSafeMode: RunRecoveryAction(6, startupRecoveryRoot); break;
			case MenuAction::RepairUdisk: RunRecoveryAction(7, startupRecoveryRoot); break;
			case MenuAction::BuildUpdate: DoUpdateConvert(); break;
			default: return;
		}
	}
}

static void RunUtilityMenu(const char *startupRecoveryRoot = nullptr) {
	if (!WarningPosted) LoadTopScreenUtilitySplash();
	const bool recoveryOnly = startupRecoveryRoot != nullptr;
	while (!ErrorState) {
		const bool recoveryAvailable = recoveryOnly || RecoveryStorageRoot() != nullptr;
		const MenuItem items[] = {
			{MenuAction::CheckCart, "Check cart", recoveryAvailable},
			{MenuAction::SaveBlock0, "Back up block 0", recoveryAvailable},
			{MenuAction::Usb, "Mount USB drive", UsbMountAvailable()},
			{MenuAction::BootGm9, "Boot GodMode9Nrio", wasDSi && !recoveryOnly},
			{MenuAction::Advanced, "Advanced NAND tools"},
		};
		switch (PickAction("Utility Menu", items, recoveryOnly)) {
			case MenuAction::CheckCart: RunRecoveryAction(5, startupRecoveryRoot); break;
			case MenuAction::SaveBlock0: RunRecoveryAction(2, startupRecoveryRoot); break;
			case MenuAction::Usb: DoUsbMount(); break;
			case MenuAction::BootGm9: SetSCFG(); DoGM9iBoot(); break;
			case MenuAction::Advanced: RunAdvancedMenu(startupRecoveryRoot, recoveryAvailable); break;
			default: return;
		}
	}
}

static void RunStorageRecoveryMenu(const char *root) {
	if (SCFGUnlocked) enableSlot1();
	if (access(root, F_OK) != 0) mkdir(root, 0777);
	RunUtilityMenu(root);
}

static void RunDldiMenu() {
	consoleClear();
	if (!dldiWarned) {
		puts("Restart required after entering");
		puts("the DLDI Menu.\n");
		puts("A Continue  B Back");
		for (;;) {
			swiWaitForVBlank();
			scanKeys();
			const u32 keys = keysDown();
			if (keys & KEY_B) return;
			if (keys & KEY_A) break;
		}
		dldiWarned = true;
	}
	if (WarningPosted) consoleClearTop(false);
	LoadTopScreenDLDISplash();
	swiWaitForVBlank();
	if (!ntrMode) {
		ntrMode = true;
		SetSCFG();
	}
	if (!nrioDLDIMounted) {
		nrioDLDIMounted = fatInitDefault();
		if (!nrioDLDIMounted) {
			DoError("DLDI Init Failed!\n");
			return;
		}
		if (!uDiskFileFound) uDiskFileFound = (access("sd:/nrioFiles/xBootStrap.nds", F_OK) == 0);
	}
	struct statvfs st;
	if (!cartSize && statvfs("fat:/", &st) == 0) cartSize = (st.f_bsize * st.f_blocks) / 1024;

	const MenuItem items[] = {
		{MenuAction::BootCart, "Boot cart menu"},
		{MenuAction::DumpFatSector, "Dump FAT sector 0"},
		{MenuAction::DumpFatImage, "Dump FAT image"},
		{MenuAction::RestoreFatSector, "Restore FAT sector 0"},
		{MenuAction::WriteFatImage, "Write default FAT image"},
	};
	while (!ErrorState) {
		switch (PickAction("DLDI Menu", items, true)) {
			case MenuAction::BootCart: DoCartBoot(); break;
			case MenuAction::DumpFatSector: DoSector0Dump(); break;
			case MenuAction::DumpFatImage: DoImageDump(); break;
			case MenuAction::RestoreFatSector: DoSector0Restore(); break;
			case MenuAction::WriteFatImage: DoImageRestore(); break;
			default: ErrorState = true; break;
		}
	}
}

static MenuAction MainMenu() {
	const MenuItem items[] = {
		{MenuAction::Utility, "Utility Menu"},
		{MenuAction::DumpMain, "Dump main SRL", rawNandAvailable},
		{MenuAction::DumpStage2, srlLayout.bootleg ? "Dump bootleg SRL" : "Dump Stage2 SRL", srlLayout.stage2_table != 0},
		{MenuAction::DumpUdisk, "Dump uDisk SRL", srlLayout.udisk_table != 0},
		{MenuAction::TestDump, "Test NAND dump"},
		{MenuAction::Dldi, "DLDI Menu", wasDSi},
		{MenuAction::BootCart, "Boot cart menu", wasDSi},
	};
	return PickAction("Main Menu", items, true);
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
	const bool usbOnly = (keysHeld() & KEY_X) && UsbMountAvailable();
	bool recoveryOnly = (keysHeld() & KEY_L) != 0;
	if (wasDSi && (REG_SCFG_EXT & BIT(31)))SCFGUnlocked = true;
	defaultExceptionHandler();
	BootSplashInit();
	if (usbOnly) {
		DoUsbMount();
		fifoSendValue32(FIFO_USER_03, 1);
		return 0;
	}
	
	if (autoBootCart) {
		printf("\n\n\n\n\n\n       Launching XuluMenu.\n          Please Wait...");
	} else {
		printf("\n\n\n\n\n\n Checking cart. Please Wait...");
	}
	
	sysSetCardOwner(BUS_OWNER_ARM9);
	if (!wasDSi)sysSetCartOwner(BUS_OWNER_ARM7);
	if (recoveryOnly) {
		if (wasDSi || GetSlot2Storage()) MountFATDevices();
		if (const char *root = RecoveryStorageRoot()) RunStorageRecoveryMenu(root);
		else if (wasDSi) DoFATerror(true, true);
		else DoError("Recovery needs writable Slot-2\n"
		             "storage and its DLDI driver.");
		fifoSendValue32(FIFO_USER_03, 1);
		return 0;
	}
	// A Slot-1 DLDI probes the cart during mount. Reinitialize the bus
	// after the reinsert prompt before letting that driver use it.
	const bool mountAfterCardInit = !wasDSi && !GetSlot2Storage();
	if (!mountAfterCardInit) MountFATDevices();
	toncset((void*)0x02000000, 0, 0x800);
	const u32 cardStatus = CardInit();
	if (cardStatus) {
		LoadTopScreenDebugSplash();
		PrintToTop("Slot-1 init error: %02X\n", (int)cardStatus, true);
		WarningPosted = true;
	}
	if (mountAfterCardInit) MountFATDevices();

	if ((!sdMounted && wasDSi) || (!fatMounted && !wasDSi)) {
		DoFATerror(true, wasDSi);
		consoleClear();
		fifoSendValue32(FIFO_USER_03, 1);
		return 0;
	}
	RefreshSrlLayout();
	if (!rawNandAvailable) {
		if (!WarningPosted) LoadTopScreenDebugSplash();
		PrintToTop("Could not probe Slot-1 NAND.\n", -1, !WarningPosted);
		WarningPosted = true;
	}
	if (autoBootCart) { SetSCFG(); DoCartBoot(); }
	// Enable vblank handler
	irqSet(IRQ_VBLANK, vblankHandler);
	
	if (wasDSi) { if(access("sd:/nrioFiles", F_OK) != 0)mkdir("sd:/nrioFiles", 0777); } else { if(access("fat:/nrioFiles", F_OK) != 0)mkdir("fat:/nrioFiles", 0777); }
	
	while(1) {
		if (ErrorState) {
			consoleClear();
			fifoSendValue32(FIFO_USER_03, 1);
			if (!isDSiMode())systemShutDown();
			return 0;
		}
		switch (MainMenu()) {
			case MenuAction::Utility:
				RunUtilityMenu();
				if (!ErrorState) RefreshSrlLayout();
				if (!WarningPosted) LoadTopScreenSplash();
				break;
			case MenuAction::Dldi: RunDldiMenu(); break;
			case MenuAction::DumpMain: DoSrlDump("stage1.bin"); break;
			case MenuAction::DumpStage2:
				DoSrlDump(srlLayout.bootleg ? "bootleg.nds" : "stage2.nds", srlLayout.stage2_table);
				break;
			case MenuAction::DumpUdisk: DoSrlDump("udisk.nds", srlLayout.udisk_table); break;
			case MenuAction::TestDump: DoTestDump(); break;
			case MenuAction::BootCart: SetSCFG(); DoCartBoot(); break;
			default: ErrorState = true; break;
		}
		swiWaitForVBlank();
    }
	return 0;
}

