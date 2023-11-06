/*
	iointerface.c
	
 Copyright (c) 2006 Michael "Chishm" Chisholm
	
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation and/or
     other materials provided with the distribution.
  3. The name of the author may not be used to endorse or promote products derived
     from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <nds.h>
#include <nds/ndstypes.h>
#include <nds/system.h>
#include <nds/card.h>
#include <stdio.h>

#include "tonccpy.h"

/*#define BYTES_PER_READ 512

#ifndef NULL
 #define NULL 0
#endif

//---------------------------------------------------------------
// Functions needed for the external interface

#define	nrio_fattab		((u8*)0x023FEE00)				//FIXME: DLDI could not alloc global memory ??
#define	nrio_buf		((u8*)(0x023FEE00-0x800))
#define CARD_CR2_D2		(*(vu32*)(0x023FEE00-0x800-8))
#define nrio_fattabaddr (*(vu32*)(0x023FEE00-0x800-4))
#define nrio_bufaddr	(*(vu32*)(0x023FEE00-0x800-12))


u32 CalcCARD_CR2_D2() {
	u32 da,db,dc,ncr2;
	if ((REG_SCFG_EXT & BIT(14)) && (REG_SCFG_EXT & BIT(15))) { da=*(vu32*)0x02FFFE60; } else { da=*(vu32*)0x027FFE60; }
	if ((REG_SCFG_EXT & BIT(14)) && (REG_SCFG_EXT & BIT(15))) { db=*(vu32*)0x02FFFE60; } else { db=*(vu32*)0x027FFE60; }
	da=da&0x00001FFF;
	db=db&0x003F0000;
	db=db>>16;
	dc=da+db;
	dc=(dc/2)-1;
	if ((REG_SCFG_EXT & BIT(14)) && (REG_SCFG_EXT & BIT(15))) { ncr2=*(vu32*)0x02FFFE60; } else { ncr2=*(vu32*)0x027FFE60; }
	ncr2&=~0x003F1FFF;
	ncr2|=dc;
	return ncr2;
}

void cardreadpage(unsigned int addr,unsigned int dst,unsigned char cmd,unsigned int card_cr2) {
	*(volatile unsigned char*)0x40001A1 = 0xC0;
	*(volatile unsigned char*)0x40001A8 = cmd;
	*(volatile unsigned char*)0x40001A9 = (addr >> 24);
	*(volatile unsigned char*)0x40001AA = (addr >> 16);
	*(volatile unsigned char*)0x40001AB = (addr >> 8);
	*(volatile unsigned char*)0x40001AC = addr;
	*(volatile unsigned char*)0x40001AD = 0;
	*(volatile unsigned char*)0x40001AE = 0;
	*(volatile unsigned char*)0x40001AF = 0;
	
	
	*(volatile unsigned int*)0x40001A4 = card_cr2;
	do {
		// Read data if available
		if ((*(volatile unsigned int*)0x40001A4) & 0x800000) {
			*(volatile unsigned int*)dst=*(volatile unsigned int*)0x4100010;
			dst+=4;
		}
	} while ((*(volatile unsigned int*)0x40001A4) & 0x80000000);
}

__inline
// void cardreadpage_B7(unsigned int addr,unsigned int dst) { cardreadpage(addr,dst,0xB7,*(vu32*)0x027FFE60); }
void cardreadpage_B7(unsigned int addr,unsigned int dst) { 
	if ((REG_SCFG_EXT & BIT(14)) && (REG_SCFG_EXT & BIT(15))) {
		cardreadpage(addr,dst,0xB7,*(vu32*)0x02FFFE60); 
	} else {
		cardreadpage(addr,dst,0xB7,*(vu32*)0x027FFE60);
	}
}

__inline
void cardreadpage_D2(unsigned int addr,unsigned int dst) { cardreadpage(addr,dst,0xD2,CARD_CR2_D2); }

u32 nrio_addrlog2phy(u32 addr) {
	u32 indexaddr,subaddr;
	indexaddr=(addr/16384)*4+0x02000000;
	subaddr=addr%16384;
	if(indexaddr!=nrio_fattabaddr) {
		nrio_fattabaddr=indexaddr;
		cardreadpage_B7((indexaddr/512)*512,(u32)nrio_fattab);
	}
	indexaddr=*(vu32*)(nrio_fattab+(indexaddr%512));
	if(indexaddr==0xFFFFFFFF)return indexaddr;
	return indexaddr+subaddr;
}

void _nrio_readSector (u32 sector, void* buffer) {
	u32 phyaddr;
	sector<<=9;

	if((sector/0x800)!=nrio_bufaddr) {
		phyaddr=nrio_addrlog2phy((sector/0x800)*0x800);
		if(phyaddr!=0xFFFFFFFF) {
			cardreadpage_D2(phyaddr,(u32)nrio_buf);
		} else {
			u32 i;
			u32*pbuf=(u32*)buffer;
			for(i=0;i<512;i++)*pbuf++=0;
		}
		nrio_bufaddr=(sector/0x800);
	}
	swiFastCopy((void*)(nrio_buf+sector%0x800),(void*)buffer,128);
	// tonccpy((void*)(nrio_buf+sector%0x800),(void*)buffer,128);
}

void _nrio_readSectorTest (u32 sector, void* buffer) {
	u32 phyaddr;
	sector<<=9;

	if((sector/0x800)!=nrio_bufaddr) {
		phyaddr=nrio_addrlog2phy((sector/0x800)*0x800);
		if(phyaddr!=0xFFFFFFFF) {
			cardreadpage_B7(phyaddr,(u32)nrio_buf);
		} else {
			u32 i;
			u32*pbuf=(u32*)buffer;
			for(i=0;i<512;i++)*pbuf++=0;
		}
		nrio_bufaddr=(sector/0x800);
	}
	swiFastCopy((void*)(nrio_buf+sector%0x800),(void*)buffer,128);
	// tonccpy((void*)(nrio_buf+sector%0x800),(void*)buffer,128);
}

bool _nrio_startUp (void) {
	CARD_CR2_D2=CalcCARD_CR2_D2();
	CARD_CR2_D2=(u32)((CARD_CR2_D2)&(~0x07000000))|(3<<24);

	nrio_fattabaddr=0xFFFFFFFF;
	nrio_bufaddr=0xFFFFFFFF;
	return true;
}

bool _nrio_isInserted (void) {	return true; }

bool _nrio_readSectors (u32 sector, u32 numSecs, void* buffer) {
	u32 *u32_buffer = (u32*)buffer, i;
	for (i = 0; i < numSecs; i++) {
		_nrio_readSector(sector,u32_buffer);
		sector++;
		u32_buffer += 128;
	}
	return true;
}

bool _nrio_readSectorsTest (u32 sector, u32 numSecs, void* buffer) {
	u32 *u32_buffer = (u32*)buffer, i;
	for (i = 0; i < numSecs; i++) {
		_nrio_readSectorTest(sector,u32_buffer);
		sector++;
		u32_buffer += 128;
	}
	return true;
}

bool _nrio_writeSectors(u32 sector, u32 numSectors, void* buffer) { return false; }

bool _nrio_clearStatus (void) {	return true; }

bool _nrio_shutdown (void) { return true; }*/

void readCardB7Mode(void* destination, u32 rom_offset, u32 num_words) {
	ALIGN(4) u32 read_buffer[128];
	u8 command[8];
	command[7] = 0xB7;
	command[6] = (rom_offset >> 24) & 0xFF;
	command[5] = (rom_offset >> 16) & 0xFF;
	command[4] = (rom_offset >> 8)  & 0xFF;
	command[3] = (rom_offset)       & 0xFF;
	command[2] = 0;
	command[1] = 0;
	command[0] = 0;
	u32 last_read_size = num_words % 128;
	while(num_words > 0) {
		cardPolledTransfer(0xB918027E, read_buffer, 128, command);
		memcpy(destination, read_buffer, num_words == 128 ? 128 : last_read_size);
		destination = (u8*)destination + 0x200;
		rom_offset += 0x200;
		command[6] = (rom_offset >> 24) & 0xFF;
		command[5] = (rom_offset >> 16) & 0xFF;
		command[4] = (rom_offset >> 8)  & 0xFF;
		command[3] = (rom_offset)       & 0xFF;
		if(num_words < 128)num_words = 128;
		num_words -= 128;
	}
}


/*void readSectorB7Mode(void* destination, u32 rom_offset) {
	u8 command[8];
	command[7] = 0xB7;
	command[6] = (rom_offset >> 24) & 0xFF;
	command[5] = (rom_offset >> 16) & 0xFF;
	command[4] = (rom_offset >> 8) & 0xFF;
	command[3] = rom_offset & 0xFF;
	command[2] = 0;
	command[1] = 0;
	command[0] = 0;
	cardPolledTransfer(0xB918027E, destination, 128, command);
}*/


void readSectorB7Mode(void* destination, u32 rom_offset) {
	cardParamCommand (0xB7, rom_offset, 0xB918027E, destination, 128);
}


/*void readCardB7(void* destination, u32 rom_offset, u32 num_words) {
	u8 command[8];
	command[7] = 0xB7;
	command[6] = (rom_offset >> 24) & 0xFF;
	command[5] = (rom_offset >> 16) & 0xFF;
	command[4] = (rom_offset >> 8) & 0xFF;
	command[3] = rom_offset & 0xFF;
	command[2] = 0;
	command[1] = 0;
	command[0] = 0;
	while(num_words > 0) {
		cardPolledTransfer(0xB918027E, destination, 128, command);
		destination = (u8*)destination + 0x200;
		rom_offset += 512;
		command[6] = (rom_offset >> 24) & 0xFF;
		command[5] = (rom_offset >> 16) & 0xFF;
		command[4] = (rom_offset >> 8) & 0xFF;
		command[3] = rom_offset & 0xFF;
		if(num_words < 128)num_words = 128;
		num_words -= 128;
	}
}*/

