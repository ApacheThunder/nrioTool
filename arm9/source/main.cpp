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
#include "file-browser.h"
#include "button-legend.h"
#include "gm9-bundle.h"
extern "C" const u8 usb_mount_bin[] __attribute__((weak));
extern "C" const u8 usb_mount_bin_end[] __attribute__((weak));

#define NDS_HEADER 0x02FFFE00
#define DSI_HEADER 0x02FFE000
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
DTCM_DATA volatile bool uDiskFileFound = false;
DTCM_DATA volatile bool WarningPosted = false;
DTCM_DATA volatile bool UpdateProgressText = false;
DTCM_DATA volatile bool UpdateDebugText = false;

DTCM_DATA char gameTitle[13] = {0};

static nrio_srl_layout srlLayout;
static bool rawNandAvailable;

const char* textBuffer = "X------------------------------X\nX------------------------------X";
const char* textProgressBuffer = "X------------------------------X\nX------------------------------X";

bool debugMode = false;
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
	// Set up the cart after ROM init so the raw NAND backend can probe it.
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

// Stream bundled files. A fixed RAM buffer can overlap the BIOS payload.
static bool CopyBundledFile(FILE *source, FILE *dest, u32 bytes) {
    u8 buffer[2048];
    while (bytes) {
        u32 chunk = bytes < sizeof(buffer) ? bytes : sizeof(buffer);
        if (fread(buffer, 1, chunk, source) != chunk ||
            fwrite(buffer, 1, chunk, dest) != chunk) return false;
        bytes -= chunk;
    }
    return true;
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
					bool copied = CopyBundledFile(src, dest, fSize);
					fclose(src);
					if (fclose(dest)) copied = false;
					if (!copied) remove("sd:/nrioFiles/xBootStrap.nds");
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
	const char* filename = "sd:/nrioFiles/GM9Nrio.nds";
	if (access(filename, F_OK) != 0 && nitroFSMounted) {
		const char* partial = "sd:/nrioFiles/GM9Nrio.nds.part";
		FILE *src = fopen("nitro:/GM9Nrio.z", "rb");
		if (src) {
			printf("Unpacking GodMode9Nrio to SD.\nPlease wait...\n");
			FILE *dest = fopen(partial, "wb");
			if (dest) {
				bool copied = nrio_unpack_gm9(src, dest);
				if (fclose(dest)) copied = false;
				if (copied && access(filename, F_OK) != 0)
					copied = rename(partial, filename) == 0;
				else
					copied = false;
				if (!copied) {
					remove(partial);
					puts("Could not unpack GodMode9Nrio.");
				}
			}
			fclose(src);
		}
	}
	struct stat st;
	if (stat(filename, &st) == 0 && S_ISREG(st.st_mode)) {
		runLaunchEngine(-1, st.st_ino, true);
		ErrorState = true;
		return;
	}
	puts("GM9Nrio.nds not found.\n\nPress A to return.");
	do {
		swiWaitForVBlank();
		scanKeys();
	} while (!(keysDown() & KEY_A));
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
					bool copied = CopyBundledFile(src, dest, fSize);
					fclose(src);
					if (fclose(dest)) copied = false;
					if (!copied) remove("sd:/nrioFiles/nrioSaveTester.nds");
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
	UpdateDebugText = debugMode;
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
			return; 
		} else if(keysDown() & KEY_B) { 
			ErrorState = true;
			return;
		}
	}
}

void DoTestDump() {
	consoleClear();
	DoWait(60);
	iprintf("About to dump %d sectors.\n\n", NUM_SECTORS);
	puts("A Continue  B Back");
	for (;;) {
		swiWaitForVBlank();
		scanKeys();
		const u32 keys = keysDown();
		if (keys & KEY_B) return;
		if (keys & KEY_A) break;
	}
	if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) {
		MountFATDevices(wasDSi);
		if ((wasDSi && !sdMounted) || (!wasDSi && !fatMounted)) { DoFATerror(true, wasDSi); return; }
	}
	consoleClear();
	puts("Probing raw NAND...");
	nrio_raw_info info;
	if (!nrio_srl_open(&info)) {
		DoError("Could not probe Slot-1 NAND.", false);
		return;
	}
	if (info.sectors < NUM_SECTORS) {
		nrio_recovery_shutdown();
		DoError("NAND smaller than dump range.", false);
		return;
	}
	const char *path = sdMounted ? "sd:/nrioFiles/nrio_data.bin" : "fat:/nrioFiles/nrio_data.bin";
	puts("Opening nrio_data.bin...");
	FILE *dest = fopen(path, "wb");
	if (!dest) {
		nrio_recovery_shutdown();
		DoError("Could not open nrio_data.bin.", false);
		return;
	}
	puts("Reading first NAND sector...");
	ProgressTracker = NUM_SECTORS;
	textBuffer = "Dumping sectors to nrio_data.bin\n\nPress [B] to abort...\n\n\n";
	textProgressBuffer = "Sectors Remaining: ";
	bool readFailed = false;
	bool saveFailed = false;
	bool cancelled = false;
	for (int i = 0; i < NUM_SECTORS; ++i) {
		scanKeys();
		if (keysDown() & KEY_B) {
			cancelled = true;
			break;
		}
		if (!nrio_recovery_read(i, 1, ReadBuffer)) {
			readFailed = true;
			break;
		}
		if (fwrite(ReadBuffer, SECTOR_SIZE, 1, dest) != 1) {
			saveFailed = true;
			break;
		}
		--ProgressTracker;
		UpdateProgressText = true;
	}
	if (fclose(dest)) saveFailed = true;
	nrio_recovery_shutdown();
	while (UpdateProgressText) swiWaitForVBlank();
	if (saveFailed) {
		DoError("Could not save nrio_data.bin.\nFile may be incomplete.", false);
		return;
	}
	if (readFailed) {
		DoError("Could not read NAND.\nFile may be incomplete.", false);
		return;
	}
	consoleClear();
	puts(cancelled ? "Dump cancelled.\nPartial file saved.\n" : "Sector dump finished!\n");
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

static void DoNandDump(const char *name, u32 address, u32 directSectors = 0, bool padBanner = false) {
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
	char path[56], temporary[56], outputName[32];
	const bool numbered = !strcmp(name, "safe-original.nds");
	bool available = false;
	for (unsigned i = 0; i <= (numbered ? 999u : 0u); ++i) {
		if (i == 0) snprintf(outputName, sizeof(outputName), "%s", name);
		else snprintf(outputName, sizeof(outputName), "safe-original-%03u.nds", i);
		snprintf(path, sizeof(path), "%s/%s", root, outputName);
		snprintf(temporary, sizeof(temporary), "%s/%s.part", root, outputName);
		if (access(path, F_OK) != 0 && access(temporary, F_OK) != 0) {
			available = true;
			break;
		}
	}
	if (!available) {
		DoError(numbered ? "No free safe-mode dump name." :
		        "Move the existing dump or .part\nfile before dumping again.", false);
		return;
	}

	nrio_raw_info info;
	if (!nrio_srl_open(&info)) {
		DoError("Could not probe Slot-1 NAND.", false);
		return;
	}
	u32 blocks = 1;
	if (!directSectors && !nrio_srl_plan(address, info.sectors, addresses, &blocks)) {
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
	const u32 sectors = directSectors ? directSectors : NRIO_SRL_MAP_BLOCK / SECTOR_SIZE;
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
		const u32 blockAddress = directSectors ? address : addresses[i];
		const size_t bytes = sectors * SECTOR_SIZE;
		ok = nrio_recovery_read(blockAddress / SECTOR_SIZE, sectors, buffer) &&
		     fwrite(buffer, 1, bytes, dest) == bytes;
		if (!ok) break;
	}

	if (ok && !cancelled && padBanner) {
		const u8 padding[0x40] = {};
		ok = fwrite(padding, 1, sizeof(padding), dest) == sizeof(padding);
	}
	if (fclose(dest)) ok = false;
	nrio_recovery_shutdown();
	if (ok && !cancelled) ok = rename(temporary, path) == 0;
	if (!ok || cancelled) {
		remove(temporary);
		if (!cancelled) DoError("Could not read or save the dump.", false);
		return;
	}
	consoleClear();
	printf("Saved %s\n\nA Back", outputName);
	do {
		swiWaitForVBlank();
		scanKeys();
	} while (!(keysDown() & (KEY_A | KEY_B)));
}

extern "C" void nrioTool_sd_recovery(int action, const char *root);
#include "udisk-setup.h"
#include "format-origin.h"
#include "sector-zero.h"

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
	Utility, Dldi, FileBrowser,
	DumpMain, DumpStage2, DumpUdisk, DumpBanner, TestDump,
	BootCart, BootGm9, Usb,
	CheckCart, SaveBlock0, Advanced, BuildUpdate,
	PatchSafeMode, PatchMainMarker, BypassSafeMode, InstallBanner, RestoreArm7, RestoreBlock0, ReplaceSafeMode, RepairUdisk, UdiskSetup, QuickFormat, FullFormat,
	InstallUdisk, InstallCustomUdisk, InstallXulu, SetupStockMenu, CopyXMenu, RemoveUpdater,
	DumpFatSector, RestoreFatSector, DumpFatImage, FormatHelp,
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
			const size_t footer = MENU_CONTROLS_ROW - MENU_CONSOLE_TOP;
			if (2 + visibleCount * 2 > footer + 1) {
				// Long menus use a uniform one-row list. Keep the footer
				// inside the visible screen even with older splash art.
				const size_t footerRow = 24 - BottomConsoleRow() - 4;
				const size_t rows = footerRow - 3;
				const size_t from = selected < rows ? 0 : selected - rows + 1;
				const size_t to = from + rows < visibleCount ? from + rows : visibleCount;
				printf("%s\n", title);
				if (visibleCount > rows)
					printf("Options %lu-%lu of %lu\n", (unsigned long)(from + 1),
					       (unsigned long)to, (unsigned long)visibleCount);
				else putchar('\n');
				for (size_t i = from; i < to; ++i)
					printf("%c %s\n", i == selected ? '>' : ' ', visible[i]->label);
				for (size_t row = 2 + to - from; row < footerRow; ++row) putchar('\n');
				nrio_button_legend(exits ? "Up/Down A Select B Exit" : "Up/Down A Select B Back", nullptr);
			} else {
				printf("%s\n\n", title);
				size_t row = 2;
				for (size_t i = 0; i < visibleCount; ++i) {
					if (i) { putchar('\n'); ++row; }
					printf("%c %s\n", i == selected ? '>' : ' ', visible[i]->label);
					++row;
				}
				for (; row < footer; ++row) putchar('\n');
				puts(exits ? "Up/Down  A Select  B Exit" : "Up/Down  A Select  B Back");
			}
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

static const char *launchPath;

static void RunUdiskSetupMenu(const char *backupRoot) {
	const bool canFormat = nrio_can_format(launchPath, wasDSi, sdMounted,
	                                       !wasDSi && GetSlot2Storage(), fatMounted);
	const MenuItem items[] = {
		{MenuAction::QuickFormat, "Quick Format N-Card", canFormat},
		{MenuAction::FullFormat, "Full Format N-Card", canFormat},
		{MenuAction::FormatHelp, "Format unavailable", !canFormat},
		{MenuAction::CheckCart, "Check Cart", !backupRoot},
		{MenuAction::InstallXulu, "XuluMenu NAND (recommended)"},
		{MenuAction::InstallUdisk, "Install stock uDisk 1.45"},
		{MenuAction::InstallCustomUdisk, "Install custom uDisk", backupRoot != nullptr},
		{MenuAction::SetupStockMenu, "Set up stock XMenu"},
		{MenuAction::CopyXMenu, "Copy stock/custom XMenu"},
		{MenuAction::RemoveUpdater, "Delete old uDisk installer"},
	};
	for (;;) {
		switch (PickAction("uDisk setup", items)) {
			case MenuAction::QuickFormat: nrioTool_quick_format(backupRoot); break;
			case MenuAction::FullFormat: nrioTool_full_format(backupRoot); break;
			case MenuAction::FormatHelp:
				DoError("Format needs a separate launch.\n"
				        "Mount DSi SD or use Slot-2.\n"
				        "DS mode needs separate media.\n"
				        "No changes made.", false);
				break;
			case MenuAction::CheckCart: nrioTool_sd_recovery(5, backupRoot); break;
			case MenuAction::InstallUdisk: nrioTool_sd_recovery(8, backupRoot); break;
			case MenuAction::InstallCustomUdisk: nrioTool_sd_recovery(11, backupRoot); break;
			case MenuAction::InstallXulu: nrioTool_sd_recovery(10, backupRoot); break;
			case MenuAction::SetupStockMenu: nrioTool_udisk_setup(NRIO_UDISK_STOCK_MENU, backupRoot); break;
			case MenuAction::CopyXMenu: nrioTool_udisk_setup(NRIO_UDISK_COPY_MENU, backupRoot); break;
			case MenuAction::RemoveUpdater: nrioTool_udisk_setup(NRIO_UDISK_REMOVE_UPDATER, backupRoot); break;
			default: return;
		}
	}
}

static void RunAdvancedMenu(const char *startupRecoveryRoot) {
	LoadTopScreenAdvancedSplash(WarningPosted || debugMode);
	const MenuItem items[] = {
		{MenuAction::InstallBanner, "Install custom banner"},
		{MenuAction::PatchSafeMode, "Patch safe mode for DSi"},
		{MenuAction::ReplaceSafeMode, "Install/replace safe-mode ROM"},
		{MenuAction::BypassSafeMode, "Bypass safe mode"},
		{MenuAction::UdiskSetup, "uDisk setup"},
		{MenuAction::RepairUdisk, "Repair stock uDisk"},
		{MenuAction::SaveBlock0, "Dump block 0"},
		{MenuAction::RestoreBlock0, "Restore block 0"},
		{MenuAction::RestoreArm7, "Restore banner ARM7"},
		{MenuAction::PatchMainMarker, "Patch main ARM9 marker"},
	};
	while (!ErrorState) {
		switch (PickAction("Advanced NAND tools", items)) {
			case MenuAction::InstallBanner: RunRecoveryAction(1, startupRecoveryRoot); break;
			case MenuAction::PatchSafeMode: RunRecoveryAction(0, startupRecoveryRoot); break;
			case MenuAction::ReplaceSafeMode: RunRecoveryAction(6, startupRecoveryRoot); break;
			case MenuAction::BypassSafeMode: RunRecoveryAction(9, startupRecoveryRoot); break;
			case MenuAction::UdiskSetup: RunUdiskSetupMenu(startupRecoveryRoot ? startupRecoveryRoot : RecoveryStorageRoot()); break;
			case MenuAction::RepairUdisk: RunRecoveryAction(7, startupRecoveryRoot); break;
			case MenuAction::SaveBlock0: RunRecoveryAction(2, startupRecoveryRoot); break;
			case MenuAction::RestoreBlock0: RunRecoveryAction(4, startupRecoveryRoot); break;
			case MenuAction::RestoreArm7: RunRecoveryAction(3, startupRecoveryRoot); break;
			case MenuAction::PatchMainMarker: RunRecoveryAction(12, startupRecoveryRoot); break;
			default: return;
		}
	}
}

static void RunDldiMenu();

static void RunUtilityMenu(const char *startupRecoveryRoot = nullptr) {
	LoadTopScreenUtilitySplash(WarningPosted || debugMode);
	const bool recoveryOnly = startupRecoveryRoot != nullptr;
	while (!ErrorState) {
		const bool recoveryAvailable = recoveryOnly || RecoveryStorageRoot() != nullptr;
		const bool dsiToolsAvailable = wasDSi && !recoveryOnly;
		const MenuItem items[] = {
			{MenuAction::Advanced, "Advanced NAND tools", recoveryAvailable},
			{MenuAction::Dldi, "DLDI Menu", dsiToolsAvailable},
			{MenuAction::CheckCart, "Check Cart"},
			{MenuAction::BootCart, "Boot Cart", dsiToolsAvailable},
			{MenuAction::BootGm9, "Boot GodMode9Nrio", dsiToolsAvailable},
			{MenuAction::BuildUpdate, "Build Update File", !recoveryOnly},
			{MenuAction::Usb, "Mount USB drive", UsbMountAvailable()},
		};
		switch (PickAction("Utility Menu", items, recoveryOnly)) {
			case MenuAction::Advanced:
				RunAdvancedMenu(startupRecoveryRoot);
				LoadTopScreenUtilitySplash(WarningPosted || debugMode);
				break;
			case MenuAction::Dldi:
				RunDldiMenu();
				if (!ErrorState) LoadTopScreenUtilitySplash(WarningPosted || debugMode);
				break;
			case MenuAction::CheckCart:
				nrioTool_sd_recovery(5, startupRecoveryRoot ? startupRecoveryRoot : RecoveryStorageRoot());
				break;
			case MenuAction::BootCart: SetSCFG(); DoCartBoot(); break;
			case MenuAction::BootGm9: SetSCFG(); DoGM9iBoot(); break;
			case MenuAction::BuildUpdate: DoUpdateConvert(); break;
			case MenuAction::Usb: DoUsbMount(); break;
			default: return;
		}
	}
}

static void RunStorageRecoveryMenu(const char *root) {
	if (SCFGUnlocked) enableSlot1();
	if (access(root, F_OK) != 0) mkdir(root, 0777);
	RunUtilityMenu(root);
}

static bool PrepareDldiAction() {
	if (nrioDLDIMounted) return true;
	consoleClear();
	puts("This action needs DLDI mode.");
	puts("After switching, B exits to");
	puts("the system menu.\n");
	nrio_button_legend("A Continue  B Cancel", nullptr);
	for (;;) {
		swiWaitForVBlank();
		scanKeys();
		const u32 keys = keysDown();
		if (keys & KEY_B) return false;
		if (keys & KEY_A) break;
	}

	ntrMode = true;
	SetSCFG();
	nrioDLDIMounted = fatInitDefault();
	if (!nrioDLDIMounted) {
		DoError("DLDI Init Failed!\n");
		return false;
	}
	if (!uDiskFileFound) uDiskFileFound = (access("sd:/nrioFiles/xBootStrap.nds", F_OK) == 0);
	struct statvfs st;
	if (!cartSize && statvfs("fat:/", &st) == 0) cartSize = (st.f_bsize * st.f_blocks) / 1024;
	return true;
}

static void RunDldiMenu() {
	LoadTopScreenDLDISplash();
	const MenuItem items[] = {
		{MenuAction::BootCart, "Boot cart menu"},
		{MenuAction::DumpFatSector, "Dump FAT sector 0"},
		{MenuAction::DumpFatImage, "Dump FAT image"},
		{MenuAction::RestoreFatSector, "Restore FAT sector 0"},
	};
	while (!ErrorState) {
		const MenuAction action = PickAction("DLDI Menu", items, ntrMode);
		if (action == MenuAction::Back) {
			if (ntrMode) ErrorState = true;
			return;
		}
		if (action == MenuAction::DumpFatSector || action == MenuAction::RestoreFatSector) {
			if (nrioDLDIMounted) {
				DoError("Exit DLDI mode and relaunch\n"
				        "before accessing FAT sector 0.", false);
			} else {
				nrioTool_sector0_action(RecoveryStorageRoot(), action == MenuAction::RestoreFatSector);
			}
			continue;
		}
		if (!PrepareDldiAction()) continue;
		switch (action) {
			case MenuAction::BootCart: DoCartBoot(); break;
			case MenuAction::DumpFatImage: DoImageDump(); break;
			default: break;
		}
	}
}

static MenuAction MainMenu() {
	const MenuItem items[] = {
		{MenuAction::FileBrowser, "File Browser"},
		{MenuAction::Utility, "Utility Menu"},
		{MenuAction::DumpMain, "Dump main SRL", rawNandAvailable},
		{MenuAction::DumpStage2, srlLayout.bootleg ? "Dump bootleg SRL" : "Dump Safe Mode SRL", srlLayout.stage2_table != 0},
		{MenuAction::DumpUdisk, "Dump uDisk SRL", srlLayout.udisk_table != 0},
		{MenuAction::DumpBanner, "Dump banner", rawNandAvailable},
		{MenuAction::TestDump, "Test NAND dump"},
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

int main(int argc, char **argv) {
	launchPath = argc > 0 && argv ? argv[0] : nullptr;
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
		else {
			if (SCFGUnlocked) enableSlot1();
			RunUdiskSetupMenu(nullptr);
		}
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
		if (cardStatus == CARD_INIT_HEADER_CRC) {
			const auto *header = reinterpret_cast<const sNDSHeaderExt *>(cartHeader);
			PrintToTop("Slot-1 header CRC error (16)\n", -1, true);
			PrintToTop("Header CRC %04X\n", header->headerCRC16, false);
			PrintToTop("Read CRC   %04X\n", swiCRC16(0xFFFF, cartHeader, 0x15E), false);
		} else {
			PrintToTop("Slot-1 init error: %02X\n", (int)cardStatus, true);
		}
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
	if (rawNandAvailable && cardStatus)
		PrintToTop("Raw NAND available.\n", -1, false);
	if (autoBootCart && !cardStatus) { SetSCFG(); DoCartBoot(); }
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
            case MenuAction::FileBrowser:
                nrio_file_browser(wasDSi && sdMounted, !wasDSi && fatMounted && GetSlot2Storage(),
                                  !wasDSi && fatMounted && !GetSlot2Storage());
                if (WarningPosted) LoadTopScreenDebugSplash();
                else LoadTopScreenSplash();
                break;
			case MenuAction::Utility:
				RunUtilityMenu();
				if (!ErrorState) RefreshSrlLayout();
				if (WarningPosted) LoadTopScreenDebugSplash();
				else LoadTopScreenSplash();
				break;
			case MenuAction::DumpMain: DoNandDump("stage1.bin", STAGE1OFFSET, STAGE1SECTORCOUNT); break;
			case MenuAction::DumpStage2:
				DoNandDump(srlLayout.bootleg ? "bootleg.nds" : "safe-original.nds", srlLayout.stage2_table);
				break;
			case MenuAction::DumpUdisk: DoNandDump("udisk.nds", srlLayout.udisk_table); break;
			case MenuAction::DumpBanner:
				DoNandDump("banner.bin", NRIO_RAW_BANNER_OFFSET, NRIO_RAW_BANNER_BYTES / SECTOR_SIZE, /*padBanner=*/true);
				break;
			case MenuAction::TestDump: DoTestDump(); break;
			default: ErrorState = true; break;
		}
		swiWaitForVBlank();
    }
	return 0;
}

