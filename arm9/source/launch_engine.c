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

#include <string.h>
#include <nds.h>

#include "load_bin.h"
#include "launch_engine.h"
#include "read_card.h"

#define LCDC_BANK_D (u16*)0x06860000

#define TMP_DATA 0x02FFC000

typedef struct sLauncherSettings { u32 cachedChipID; u32 language; unsigned long fileCluster; } tLauncherSettings;

void vramcpy (void* dst, const void* src, int len) {
	u16* dst16 = (u16*)dst;
	u16* src16 = (u16*)src;
	for ( ; len > 0; len -= 2) { *dst16++ = *src16++; }
}

ITCM_CODE void setSCFG() {
	if (REG_SCFG_EXT & BIT(31)) {
		// MBK settings for NTR mode games
		*((vu32*)REG_MBK1)=0x8D898581;
		*((vu32*)REG_MBK2)=0x91898581;
		*((vu32*)REG_MBK3)=0x91999591;
		*((vu32*)REG_MBK4)=0x91898581;
		*((vu32*)REG_MBK5)=0x91999591;
		REG_MBK6 = 0x00003000;
		REG_MBK7 = 0x00003000;
		REG_MBK8 = 0x00003000;
	}
	REG_SCFG_EXT=0x03000000;
	for (int i = 0; i < 8; i++) { while(REG_VCOUNT!=191); while(REG_VCOUNT==191); }
}

void runLaunchEngine (int language, u32 fileCluster) {

	u32 chipID = cardGetId();

	irqDisable(IRQ_ALL);
	// Direct CPU access to VRAM bank D
	VRAM_D_CR = (VRAM_ENABLE | VRAM_D_LCD);
	// Load the loader/patcher into the correct address
	vramcpy (LCDC_BANK_D, load_bin, load_bin_size);
	// Give the VRAM to the ARM7
	VRAM_D_CR = (VRAM_ENABLE | VRAM_D_ARM7_0x06020000);
	// Reset into a passme loop
	REG_EXMEMCNT |= (ARM7_OWNS_ROM | ARM7_OWNS_CARD);
		
	tLauncherSettings* tmpData = (tLauncherSettings*)TMP_DATA;
	tmpData->cachedChipID = chipID;
	tmpData->language = 0xFF;
	if (language != -1)tmpData->language = language;
	tmpData->fileCluster = fileCluster;
	
	setSCFG();
	
	// Return to passme loop
	*(vu32*)0x02FFFFFC = 0;
	*(vu32*)0x02FFFE04 = (u32)0xE59FF018; // ldr pc, 0x02FFFE24
	*(vu32*)0x02FFFE24 = (u32)0x02FFFE04; // Set ARM9 Loop address --> resetARM9(0x02FFFE04);
	resetARM7(0x06020000);
	swiSoftReset();
}

