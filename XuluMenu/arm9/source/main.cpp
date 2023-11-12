/*-----------------------------------------------------------------
 Copyright (C) 2005 - 2013
	Michael "Chishm" Chisholm
	Dave "WinterMute" Murphy
	Claudio "sverx"

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

------------------------------------------------------------------*/
#include <nds.h>
#include <stdio.h>
#include <fat.h>
#include <sys/stat.h>
#include <limits.h>

#include <string.h>
#include <unistd.h>

#include "args.h"
#include "file_browse.h"
#include "hbmenu_banner.h"
#include "font.h"
#include "consolebg.h"
#include "iconTitle.h"
#include "nds_loader_arm9.h"

static bool isAutoBoot = false;

using namespace std;

int exitProgram(void) {
	iprintf("Press START to power off.");
	while(1) {
		swiWaitForVBlank();
		scanKeys();
		int pressed = keysDown();
		if(pressed & KEY_START) break;
	}
	fifoSendValue32(FIFO_USER_01, 1); // turn off ARM7
	return 0;
}

void InitGUI (void) {
	iconTitleInit();
	videoSetModeSub(MODE_4_2D);
	vramSetBankC(VRAM_C_SUB_BG);
	int bgSub = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 1, 0);
	PrintConsole *console = consoleInit(0, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 6, false, false);
	dmaCopy(consolebgBitmap, bgGetGfxPtr(bgSub), 256*256);
	ConsoleFont font;
	font.gfx = (u16*)fontTiles;
	font.pal = (u16*)fontPal;
	font.numChars = 95;
	font.numColors = (fontPalLen / 2);
	font.bpp = 4;
	font.asciiOffset = 32;
	font.convertSingleColor = true;
	consoleSetFont(console, &font);
	dmaCopy(consolebgPal, BG_PALETTE_SUB, 256*2);
	BG_PALETTE_SUB[255] = RGB15(31,31,31);
	keysSetRepeat(25,5);
}

int FileBrowser() {
	InitGUI();
	vector<string> extensionList = argsGetExtensionList();
	while(1) {
		string filename = browseForFile(extensionList);
		// Construct a command line
		vector<string> argarray;
		if (!argsFillArray(filename, argarray)) {
			iprintf("Invalid NDS or arg file selected\n");
		} else {
			iprintf("Running %s with %d parameters\n", argarray[0].c_str(), argarray.size());
			// Make a copy of argarray using C strings, for the sake of runNdsFile
			vector<const char*> c_args;
			for (const auto& arg: argarray) { c_args.push_back(arg.c_str()); }
			// Try to run the NDS file with the given arguments
			int err = runNdsFile(c_args[0], c_args.size(), &c_args[0]);
			iprintf("Start failed. Error %i\n", err);
		}
		argarray.clear();
		while (1) {
			swiWaitForVBlank();
			scanKeys();
			if (!(keysHeld() & KEY_A)) break;
		}
	}
}

int main(void) {
	if (!isAutoBoot) { extern u64 *fake_heap_end; *fake_heap_end = 0; }
	if (!fatInitDefault()) {
		InitGUI();
		iprintf ("\n\n\nFAT init failed!\n\n");
		return exitProgram();
	}
	if (isAutoBoot) {
		scanKeys();
		if((access("/xmenu.srl", F_OK) == 0) && !(keysHeld() & KEY_B)) {
			int err = runNdsFile("/xmenu.srl", 0, NULL);
			InitGUI();
			iprintf("Bootloader returned error %d\n", err);
			return exitProgram();
		} else if((access("/udisk.nds", F_OK) == 0) && (keysHeld() & KEY_Y)) {
			int err = runNdsFile("/udisk.nds", 0, NULL);
			InitGUI();
			iprintf("Bootloader returned error %d\n", err);
			return exitProgram();
		}
	}
	return FileBrowser();
}

