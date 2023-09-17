/*
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

#include <nds.h>
// #include <stdio.h>
// #include <stdlib.h>


#include "bios_decompress_callback.h"

#include "bootsplash.h"

#include "topLogo.h"
#include "botLogo.h"
#include "font.h"

#define CONSOLE_SCREEN_WIDTH 32
#define CONSOLE_SCREEN_HEIGHT 24

void vramcpy_ui (void* dest, const void* src, int size) 
{
	u16* destination = (u16*)dest;
	u16* source = (u16*)src;
	while (size > 0) {
		*destination++ = *source++;
		size-=2;
	}
}

void LoadScreen() {

	// Display Load Screen
	swiDecompressLZSSVram ((void*)topLogoTiles, (void*)CHAR_BASE_BLOCK(2), 0, &decompressBiosCallback);
	// swiDecompressLZSSVram ((void*)botLogoTiles, (void*)CHAR_BASE_BLOCK_SUB(2), 0, &decompressBiosCallback);
	vramcpy_ui (&BG_PALETTE[0], topLogoPal, topLogoPalLen);
	// vramcpy_ui (&BG_PALETTE_SUB[0], botLogoPal, botLogoPalLen);

}

void BootSplashInit() {

	videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE);
	// videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);
	vramSetBankA (VRAM_A_MAIN_BG_0x06000000);
	// vramSetBankC (VRAM_C_SUB_BG_0x06200000);
	REG_BG0CNT = BG_MAP_BASE(0) | BG_COLOR_256 | BG_TILE_BASE(2);
	// REG_BG0CNT_SUB = BG_MAP_BASE(0) | BG_COLOR_256 | BG_TILE_BASE(2);
	BG_PALETTE[0]=0;
	BG_PALETTE[255]=0xffff;
	u16* bgMapTop = (u16*)SCREEN_BASE_BLOCK(0);
	// u16* bgMapSub = (u16*)SCREEN_BASE_BLOCK_SUB(0);
	for (int i = 0; i < CONSOLE_SCREEN_WIDTH*CONSOLE_SCREEN_HEIGHT; i++) {
		bgMapTop[i] = (u16)i;
		// bgMapSub[i] = (u16)i;
	}
	
	LoadScreen();
	
	/*const int tile_base = 0;
	const int map_base = 20;
	
	PrintConsole *console = consoleInit(0,1, BgType_Text4bpp, BgSize_T_256x256, map_base, tile_base, false, false);

	ConsoleFont font;

	font.gfx = (u16*)fontTiles;
	font.pal = (u16*)fontPal;
	font.numChars = 95;
	font.numColors =  fontPalLen / 2;
	font.bpp = 4;
	font.asciiOffset = 32;
	font.convertSingleColor = false;
	
	consoleSetFont(console, &font);*/
	consoleDemoInit();
	// consoleInit(NULL, (defaultConsole.bgLayer + 1), BgType_Bmp8, BgSize_B8_256x256, defaultConsole.mapBase, defaultConsole.gfxBase, false, true);
	// consoleInit(NULL, defaultConsole.bgLayer, BgType_Text4bpp, BgSize_T_256x256, defaultConsole.mapBase, defaultConsole.gfxBase, false, true);
}

