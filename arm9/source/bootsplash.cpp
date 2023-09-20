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

void BootSplashInit() {
	videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE);
	vramSetBankA (VRAM_A_MAIN_BG_0x06000000);
	REG_BG0CNT = BG_MAP_BASE(0) | BG_COLOR_256 | BG_TILE_BASE(2);
	BG_PALETTE[0]=0;
	BG_PALETTE[255]=0xffff;
	u16* bgMapTop = (u16*)SCREEN_BASE_BLOCK(0);
	for (int i = 0; i < CONSOLE_SCREEN_WIDTH*CONSOLE_SCREEN_HEIGHT; i++) {
		bgMapTop[i] = (u16)i;
	}
	// Display DSX Logo Screen
	swiDecompressLZSSVram ((void*)topLogoTiles, (void*)CHAR_BASE_BLOCK(2), 0, &decompressBiosCallback);
	vramcpy_ui (&BG_PALETTE[0], topLogoPal, topLogoPalLen);
	// Enable console
	consoleDemoInit();
	// Change console palette so text is black and background is white to match top screen logo color scheme
	BG_PALETTE_SUB[0] = RGB15(31,31,31);
	BG_PALETTE_SUB[255] = RGB15(0,0,0);
}

