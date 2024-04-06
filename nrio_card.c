#include <nds.h>
#include <nds/ndstypes.h>
#include <nds/system.h>
#include <nds/card.h>
#include <stdio.h>

#include "tonccpy.h"
#include "read_card.h"

#define INITBUFFER  0x02000000

#define CARD_CMD_D2 0xD2
#define CARDD2FLAGS (u32)0xB918027E
#define CARDB7FLAGS (u32)0xB9180000
// #define CARDB7FLAGS (u32)0xBF180000

/*u32 nrioInit(u8 cmd, u32 cmdFlags) {
	*(vu8*)(0x040001A1) = 0xC0;
	*(vu32*)0x040001A4 = cmdFlags;
	*(vu8*)(0x040001A8) = cmd;
	while (((*(vu32*)0x40001A4) & 0x800000) == 0)swiWaitForVBlank();
	return (*(vu32*)0x04100010);
}


void nrioSendCommand(u32 cmdbuffer, u32 cmdData, u8 cmd, u32 cmdflags) {
	*(vu8*)0x40001A1 = 0xC0;
	*(vu8*)0x40001A8 = cmd;	
	*(vu8*)0x40001A9 = (cmdData >> 24);
	*(vu8*)0x40001AA = (cmdData >> 16);
	*(vu8*)0x40001AB = (cmdData >> 8);
	*(vu8*)0x40001AC = cmdData;
	*(vu16*)0x40001AD = 0;
	*(vu8*)0x40001AE = 0;
	*(vu8*)0x40001AF = 0;
	*(vu32*)0x040001A4 = cmdflags;
	while ((*(vu32*)0x40001A4) & 0x80000000) {
		// Read data if available
		if ((*(vu32*)0x040001A4) & 0x800000) {
			*(vu32*)cmdbuffer=*(vu32*)0x4100010;
			cmdbuffer+=4;
		}
	}
}*/


void InitCartNandReadMode() {
	cardParamCommand (0x66, 0, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | CARD_SEC_CMD | BIT(20) | BIT(19) | BIT(14) | BIT(13), (u32*)INITBUFFER, 128);
	cardParamCommand (0xC1, 0x0D210000, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17), (u32*)(INITBUFFER + 0x10), 128);
	cardParamCommand (0xC1, 0x0FB00000, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17), (u32*)(INITBUFFER + 0x10), 128);
	cardParamCommand (0xC1, 0x10830000, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17), (u32*)(INITBUFFER + 0x20), 128);
	// DoWait(8);
	/**(vu32*)INITBUFFER = nrioInit(0x66, 0xAF586000);
	nrioSendCommand((u32)(INITBUFFER + 0x10), 0x0D210000, 0xC1, 0xA7020000);
	nrioSendCommand((u32)(INITBUFFER + 0x20), 0x0FB00000, 0xC1, 0xA7020000);
	nrioSendCommand((u32)(INITBUFFER + 0x30), 0x10830000, 0xC1, 0xA7020000);*/
	// nrioSendCommand((u32)(INITBUFFER + 0x30), 0x10430000, 0xC1, 0xA7020000);
	// nrioSendCommand((u32)(INITBUFFER + 0x40), 0x10BF0000, 0xC1, 0xA7020000); // 0x107F0000 is alt
}



u32 CalcCARD_CR2_D2() {
	u32 da,db,dc,ncr2;
	da=CARDD2FLAGS; // Originally obtained from 0x27FFE60?
	db=CARDD2FLAGS;
	da=da&0x00001FFF;
	db=db&0x003F0000;
	db=db>>16;
	dc=da+db;
	dc=(dc/2)-1;
	ncr2=CARDD2FLAGS;
	ncr2&=~0x003F1FFF;
	ncr2|=dc;
	ncr2 = (u32)((ncr2) & (~0x07000000)) | (3<<24);
	return ncr2;
}


void cardreadpage(u32 addr, u32 dst, u8 cmd, u32 card_cr2) {
	cardParamCommand (cmd, addr, card_cr2, (u32*)dst, 128);
}

void nrio_readSector(void* destination, u32 rom_offset) {
	cardreadpage(rom_offset, (u32)destination, CARD_CMD_D2, CalcCARD_CR2_D2());
}

void nrio_readSectorB7(void* destination, u32 rom_offset) {
	cardreadpage(rom_offset, (u32)destination, CARD_CMD_DATA_READ, CARDB7FLAGS);
}

void nrio_readSectors(void* destination, u32 rom_offset, u32 num_words) {
	ALIGN(4) u32 read_buffer[128];
	u32 last_read_size = num_words % 128;
	while(num_words > 0) {
		nrio_readSector(read_buffer, rom_offset);
		memcpy(destination, read_buffer, num_words == 128 ? 128 : last_read_size);
		destination = (u8*)destination + 0x200;
		rom_offset += 0x200;
		if(num_words < 128)num_words = 128;
		num_words -= 128;
	}
}



u32 FUN_02000ff4(u8 cmd, u32 data, u32 data2) {
	*(u8*)0x040001a1 = 0xc0;
	*(u32*)0x040001a4 = 0xaf000000; // 0xaf000000 romctrl being set here?
	*(u8*)0x040001a8 = cmd;
	*(u8*)0x040001a9 = (u8)(data >> 0x18);
	*(u8*)0x040001aa = (u8)(data >> 0x10);
	*(u8*)0x040001ab = (u8)(data >> 8);
	*(u8*)0x040001ac = (u8)data;
	*(u8*)0x040001ad = (u8)(data2 >> 8);
	*(u8*)0x040001ae = (u8)data2;
	*(u8*)0x040001af = 0;
	while ((*(u8*)0x040001a4 & 0x800000) == 0); // while (REG_ROMCTRL & CARD_BUSY);
	return *(u32*)0x04100010;
}

u32 FUN_02001074(u32 cmddata, u32 cmddata2) {
	*(u8*)(0x040001a1) = 0xc0;
	*(u32*)0x040001a4 = 0xaf000000; // 0xaf000000 romctrl being set here?
	*(u8*)0x040001a8 = (u8)(cmddata >> 0x18);
	*(u8*)0x040001a9 = (u8)(cmddata >> 0x10);
	*(u8*)0x040001aa = (u8)(cmddata >> 8);
	*(u8*)0x040001ab = (u8)cmddata;
	*(u8*)0x040001ac = (u8)(cmddata2 >> 0x18);
	*(u8*)0x040001ad = (u8)(cmddata2 >> 0x10);
	*(u8*)0x040001ae = (u8)(cmddata2 >> 8);
	*(u8*)0x040001af = (u8)cmddata2;
	while ((*(u32*)0x040001a4 & 0x800000) == 0);
	return *(u32*)0x04100010; // REG_CARD_DATA_RD
}

u32 FUN_02001174(void) {
	FUN_02001074(0xD1617000,0);
	*(u8*)0x040001a1 = 0xc0;	
	*(u32*)0x040001a4 = 0xaf000000; // 0xaf000000 romctrl being set here?
	*(u8*)0x040001a8 = 0xd0;
	*(u8*)0x040001a9 = 0;
	*(u8*)0x040001aa = 0;
	*(u8*)0x040001ab = 0;
	*(u8*)0x040001ac = 0;
	*(u8*)0x040001ad = 0;
	*(u8*)0x040001ae = 0;
	*(u8*)0x040001af = 0;
	while ((*(u32*)0x040001a4 & 0x800000) == 0);
	return *(u32*)0x04100010;
}

u32 FUN_020011a0(u32 dest) {
	// u32 iVar1;
	u32 result = 0xD1616000;
	
	// iVar1 = 0x0206258C;
	// if ((dest & 0xfffc0000) != 0) {
		// if (dest <= 0xa00000U - *(u32*)0x02062598) {
			FUN_02001074(0xD1616000, 0);
			FUN_02001074((dest >> 0xb & 0xff) << 8 | ((dest >> 0xb) << 0x10) >> 0x18 | 0xd1520000, 0);
			/*if (*(u32 *)(iVar1 + 8) == 1) {
				FUN_02001074((dest >> 0x1b) << 8 | 0xd1510000,0);
			}*/
			*(u8*)0x040001a1 = 0xc0;
			*(u32*)0x040001a4 = 0xaf000000; // 0xaf000000 romctrl being set here?
			*(u8*)0x040001a8 = (u8)(0xD161D000 >> 0x18);
			*(u8*)0x040001a9 = (u8)(0xD161D000 >> 0x10);
			*(u8*)0x040001aa = (u8)(0xD161D000 >> 8);
			*(u8*)0x040001ab = (u8)0xD161D000;
			*(u8*)0x040001ac = 0;
			*(u8*)0x040001ad = 0;
			*(u8*)0x040001ae = 0;
			*(u8*)0x040001af = 0;
			while ((*(u32*)0x040001a4 & 0x800000) == 0); // (just gonna assume it's doing this like the other functions but nested inside this if check)
		// } else { result = dest; }
	// }
	return result;
}


bool FUN_020017ac(u32 dest, u32 *result) {
	u32 uVar1 = 0;
	u32 uVar2 = 0x80808080;
	
	FUN_020011a0(dest);
	
	do {
		uVar1 = FUN_02000ff4(0xc2,0,0);
	} while ((uVar1 & 0x80808080) != uVar2);
	uVar2 = FUN_02001174();
	*result = uVar2 & 0xC0C0C0C0;
	return true;
}


void FUN_0200292c(u8 buff /*u32 unaff_r6, u32 unaff_r8*/) {
	u32 uVar2;
	u32 uVar3;
	u32 *buffAdjust;
	do {
		*(u8*)0x040001a1 = 0xc0;
		*(u8*)0x040001a8 = 0xd3;
		*(u8*)0x040001a9 = 0;
		*(u8*)0x040001aa = 0xff;
		*(u8*)0x040001ab = 0xff;
		*(u8*)0x040001ac = 0xff;
		*(u8*)0x040001ad = 0xff;
		*(u8*)0x040001ae = 0xff;
		*(u8*)0x040001af = *buff;
		*(vu32*)0x040001a4 = 0xC1000008; // 0xC9000008
		buffAdjust = (u32*)(buff + 5);
		uVar2 = *(u32*)(buff + 1);
		do {
			uVar3 = uVar2;
			if ((*(u32*)0x040001a4 & 0x800000) != 0) { // if ((0xC1000008 & 0x800000) != 0) { // another odd setup where it tries to reference something that was set to 0x40001a4
				uVar3 = *buffAdjust;
				buffAdjust = buffAdjust + 1;
				*(u32*)0x04100010 = uVar2;
			}
			uVar2 = uVar3;
		} while (*(u32*)0x040001a4 < 0); // } while (*(u32*)DAT_02002a98 < 0); // This originally referenced 0xC1000008 which doesn't make sense to be used here.
		Var5 = iVar5 + 0x200;
		buffAdjust = buffAdjust + 0x200;
	} while (iVar5 != 0x800);
	FUN_02001074(0xD1611000, 0);
	return;
	
	/*u32 uVar2;
	u32 uVar3;
	u32 *puVar4;	
	int iVar5 = 0;
	
	bool unaff_r7 = true;
	
	while ((*(u32*)(0x0400011a4) & 0x800000) == 0);
	if (unaff_r7) { // if (*(int *)(unaff_r7 + 8) == 1) { // I don't know what unaff_r7 was supposed to be. Maybe a value checked in ram to change nand to a new page or something?
		*(u8*)0x040001a1 = 0xc0;
		*(u8*)0x040001a8 = 0xd1;
		*(u8*)0x040001a9 = 0x51;
		*(u8*)0x040001aa = (u8)(unaff_r6 >> 0x1b);
		*(u8*)0x040001ab = 0;
		*(u8*)0x040001ac = 0;
		*(u8*)0x040001ad = 0;
		*(u8*)0x040001ae = 0;
		*(u32*)0x040001a4 = 0xbf000000;
		while ((0x040001a4 & 0x800000) == 0);
	}
	do {		
		*(u8*)(0x040001a1) = 0xc0;
		*(u8*)(0x040001a8) = 0xd3;
		*(u8*)(0x040001a9) = 0;
		*(u8*)(0x040001aa) = 0xff;
		*(u8*)(0x040001ab) = 0xff;
		*(u8*)(0x040001ac) = 0xff;
		*(u8*)(0x040001ad) = 0xff;
		*(u8*)(0x040001ae) = 0xff;
		*(u8*)(0x040001af) = *(u8)unaff_r8;
		*(u32*)0x040001a4 = 0xC1000008;
		puVar4 = (u32*)(unaff_r8 + 5);
		uVar2 = *(u32*)(unaff_r8 + 1);
		do {
			uVar3 = uVar2;
			if ((*(u32*)0x040001a4 & 0x800000) != 0) { // if ((0xC1000008 & 0x800000) != 0) { // another odd setup where it tries to reference something that was set to 0x40001a4
				uVar3 = *puVar4;
				puVar4 = puVar4 + 1;
				*(u32*)0x04100010 = uVar2;
			}
			uVar2 = uVar3;
		} while (*(u32*)0x040001a4 < 0); // } while (*(u32*)DAT_02002a98 < 0); // This originally referenced 0xC1000008 which doesn't make sense to be used here.
		iVar5 = iVar5 + 0x200;
		unaff_r8 = unaff_r8 + 0x200;
	} while (iVar5 != 0x800);
	FUN_02001074(0xD1611000, 0);
	return;*/
}

u32 FUN_0200287c(u32 dest, u32 *buff) {
	u32 iVar1;
	
	// if (((dest & 0xfffc0000) != 0) && (dest <= 0xa00000U - *(int *)0x02062598)) { // this appears to check if it's a safe/valid location to write to and within the area of ram udisk was located in ram?
		iVar1 = (dest & 0xfffff800) << 5;
		FUN_02001074(0xD1618000,0);
		*(u8*)(0x040001a1) = 0xc0;
		
		*(u32*)0x040001a4 = 0xbf000000; // 0xbf000000 romctrl being set here?
		
		*(u8*)(0x040001a8) = 0xd1;
		*(u8*)(0x040001a9) = 0x54;
		*(u8*)(0x040001aa) = (u8)(dest & 0x7ff);
		*(u8*)(0x040001ab) = (u8)((dest & 0x7ff) >> 8);
		*(u8*)(0x040001ac) = (u8)(iVar1 >> 0x10);
		*(u8*)(0x040001ad) = (u8)(iVar1 >> 0x18);
		*(u8*)(0x040001ae) = 0;
		while ((*(u32*)0x040001a4 & 0x800000) == 0);
		// FUN_0200292c(dest, buff); // code created from LAB_0200292c gets run here?
		FUN_0200292c(buff); // code created from LAB_0200292c gets run here?
		
	// }
	return dest;
}

bool FUN_02002aa0(u32 dest, u32 *buff, u32 length) {
	u32 uVar2;
	
	if (length != 0) {
		do {
			FUN_0200287c(dest, buff);
			dest = dest + 0x800;
			buff = buff + 0x800;
			length = length + -0x800;
			do {
				uVar2 = FUN_02000ff4(0xc2,0,0);
			} while ((uVar2 & 0x80808080) != 0x80808080);
			FUN_02001174();
		} while (length != 0);
	}
	return true;
}


u32 FUN_02000614(uint dest, u32 *buff, u32 length) {
	u32 result = 0;	
	/*if (*(int *)0x02062598 == 0x40000) {
		if ((dest & 0x3ffff) == 0) { FUN_020017ac(dest, &result); }
		FUN_02002aa0(dest, buff, length);
	} else if (*(int *)0x02062598 == length) {
		FUN_020017ac(dest,&result);
		FUN_02002aa0(dest, buff, length);
	}*/
	FUN_020017ac(dest, &result);
	FUN_02002aa0(dest, buff, length); // 0x20000
	return result;
}


u32 FUN_02001384(u32 dest) {
	u32 result = 0;
	
	*(u8*)(0x040001a1) = 0xc0;
	*(u32*)0x040001a4 = 0xBB00014A; // rom ctrl flags
	*(u8*)(0x040001a8) = 0xd2;
	*(u8*)(0x040001a9) = (u8)(dest >> 0x18);
	*(u8*)(0x040001aa) = (u8)(dest >> 0x10);
	*(u8*)(0x040001ab) = (u8)(dest >> 8);
	*(u8*)(0x040001ac) = (u8)dest;
	*(u8*)(0x040001ad) = 0;
	*(u8*)(0x040001ae) = 0;
	*(u8*)(0x040001af) = 0;
	do {
		if ((*(u32*)0x040001a4 & 0x800000) != 0) {
			result = (result + (*(u32*)0x04100010 >> 0x10 & 0xff) + (*(u32*)0x04100010 & 0xff) + (*(u32*)0x04100010 >> 8 & 0xff) + (*(u32*)0x04100010 >> 0x18));
		}
	} while (*(u32*)0x040001a4 < 0);
	return result;
}

void FUN_020006a4(u32 dest, u32 length) {
	do {
		FUN_02001384(dest);
		length = length + -0x800;
		dest = length + 0x800;
	} while (length != 0);
	return;
}

void FUN_020006dc(u32 dest, u32 *buff, u32 length) {
	// u32 uVar12 = 0x80000; // hidden uDisk location on NAND. Editing this value confirmed that was the case
	
	FUN_02000614(dest, buff, length); // iVar13 refers to what may be the uDisk location in ram after read from NitroFS.
	FUN_020006a4(dest, length); // 0x20000
	// iVar6 = FUN_020006a4(dest,0x800); // 0x20000
		
	// Further code omitted as it is likely related to console output of progress text print out
}


// Write functions pulled from stage2 rom
// Appears to be setup to write a single u8 value to nand. A different function must be determining the write location?
// According to Ghidra this function isn't referenced by anything? I will assume FUN_02001504 is calling this...
void FUN_02001204(u8 *buff) {
	u32 uVar2;
	u32 uVar3;
	u32 *buffAdjust;
	
	*(u8*)0x040001a1 = 0xc0;
	*(u8*)0x040001a8 = 0xd3;
	*(u8*)0x040001a9 = 0;
	*(u8*)0x040001aa = 0xff;
	*(u8*)0x040001ab = 0xff;
	*(u8*)0x040001ac = 0xff;
	*(u8*)0x040001ad = 0xff;
	*(u8*)0x040001ae = 0xff;
	*(u8*)0x040001af = *buff;
	*(vu32*)0x040001a4 = 0xC9000008;
	buffAdjust = (u32*)(buff + 5);
	uVar2 = *(u32*)(buff + 1);
	do {
		uVar3 = uVar2;
		// if ((0xC9000008 & 0x800000) != 0) { // This if check doesn't make sense.... I think it was supposd to use 0x040001a4 instead?
		if ((*(vu32*)0x040001a4 & 0x800000) != 0)
			uVar3 = *buffAdjust;
			buffAdjust = buffAdjust + 1;
			*(vu32*)0x04100010 = uVar2;
		}
		uVar2 = uVar3;
	} while (*(vu32*)0x040001a4 < 0); // } while ((int)0xC9000008 < 0);
	return;
}

u32 FUN_02001504(u32 buff) {
	u8 iVar1;
	// This if check appaers to limit write regions. 0xfc0001 represents the highest block.
	// I'm not sure what the "buff & 0xfffc0000" check does. Given my exprience messing with the uDisk updators it appears to prevent writing to regions below 0x40000
	if (((buff & 0xfffc0000) != 0) && (buff < 0xfc0001)) { 
	
		iVar1 = (buff & 0xfffff800) << 5;
		FUN_02001074(0xD1618000, 0); // FUN_02000e3c
		*(u8*)0x040001a1 = 0xc0;
		*(u8*)0x040001a8 = 0xd1;
		*(u8*)0x040001a9 = 0x54;
		*(u8*)0x040001aa = (u8)(buff & 0x7ff);
		*(u8*)0x040001ab = (u8)((buff & 0x7ff) >> 8);
		*(u8*)0x040001ac = (u8)(iVar1 >> 0x10);
		*(u8*)0x040001ad = (u8)(iVar1 >> 0x18);
		*(u8*)0x040001ae = 0;
		while ((*(u32*)0x040001a4 & 0x800000) == 0);
		FUN_02001204(buff);
	}
	return buff;
}


void FUN_02005158(int param_1,int param_2,int param_3) {
	int *piVar1;
	uint uVar2;
	uint uVar3 = 0;
	int iVar4;
	uint uVar5;
	
	uVar5 = *(uint *)(param_1 + 4);
	uVar5 = (uVar5 & 0xff00) << 8 | uVar5 << 0x18 | uVar5 >> 8 & 0xff00 | uVar5 >> 0x18;
	do {
		uVar2 = FUN_02000dbc(0xc2,0,0);
	} while ((uVar2 & 0x80808080) != 0x80808080);
	FUN_02000f3c();
	if (*(short *)(param_1 + 2) == 8) {
		FUN_02001504(uVar5,*DAT_020052b0);
	} else {
		FUN_020012d8(uVar5,*DAT_020052b0,DAT_020052b4);
	}
	piVar1 = DAT_020052b8;
	iVar4 = uVar5 + param_2;
	*(char *)(param_1 + 4) = (char)((uint)iVar4 >> 0x18);
	*(char *)(param_1 + 7) = (char)((uint)(iVar4 * 0x1000000) >> 0x18);
	*(char *)(param_1 + 5) = (char)((uint)iVar4 >> 0x10);
	*(char *)(param_1 + 6) = (char)((uint)(iVar4 * 0x100) >> 0x10);
	param_2 = param_2 + *piVar1;
	*piVar1 = param_2;
	iVar4 = DAT_020052bc;
	uVar5 = DAT_020052ac;
	if (param_2 == param_3) {
		*(undefined *)(DAT_020052bc + 0xb) = 0;
		*(undefined *)(iVar4 + 0xc) = 0;
		*(undefined *)(iVar4 + 8) = 0;
		*(undefined *)(iVar4 + 9) = 0;
		*(undefined *)(iVar4 + 10) = 0;
		FUN_02001948();
		do {
			uVar3 = FUN_02000ff4(0xc2, 0, 0); // Originally FUN_02000dbc but I already obtained FUN_02000ff4 from udisk updator which does the same thing.
		} while ((uVar3 & DAT_020052ac) != uVar5);
		FUN_02000f3c();
	}
	return;
}

/*static void cardPolledTransferWrite(u32 flags, u32 *buffer, u32 length, const u8 *command) {
//---------------------------------------------------------------------------------
	cardWriteCommand(command);
	REG_ROMCTRL = flags | CARD_BUSY;
	u32 * target = buffer + length;
	do {
		// Read/write data if available
		if (REG_ROMCTRL & CARD_DATA_READY) {
			if (flags & CARD_WR) { // Write
				if (NULL != buffer && buffer < target)
					REG_CARD_DATA_RD = *buffer++;
				else
					REG_CARD_DATA_RD = 0;
			} else { // Read
				u32 data = REG_CARD_DATA_RD;
				if (NULL != buffer && buffer < target)
					*buffer++ = REG_CARD_DATA_RD;
				else
					(void)data;
			}
		}
	} while (REG_ROMCTRL & CARD_BUSY);
}

#define CARD_CMD_NAND_WRITE_BUFFER   0x81
#define CARD_CMD_NAND_COMMIT_BUFFER  0x82
#define CARD_CMD_NAND_DISCARD_BUFFER 0x84
#define CARD_CMD_NAND_WRITE_ENABLE   0x85
#define CARD_CMD_NAND_READ_STATUS    0xD6
// #define CARD_CMD_NAND_ROM_MODE       0x8B
// #define CARD_CMD_NAND_RW_MODE        0xB2
// #define CARD_CMD_NAND_UNKNOWN        0xBB
// #define CARD_CMD_NAND_READ_ID        0x94


// Test
void nrio_writeSector(u32 rom_dest, void* source) {
	const u8 cmdData[8] = {
		0,
		0,
		0,
		rom_dest,
		rom_dest >> 8,
		rom_dest >> 16,
		rom_dest >> 24,
		// CARD_CMD_DATA_READ
		CARD_CMD_NAND_WRITE_BUFFER
	};
	cardParamCommand(CARD_CMD_NAND_WRITE_ENABLE, 0, CARD_ACTIVATE | CARD_nRESET, NULL, 0);
//	cardPolledTransferWrite(CARD_ACTIVATE | CARD_WR | CARD_nRESET | CARD_SEC_LARGE | CARD_CLK_SLOW | CARD_BLK_SIZE(1) |
//													  BIT(20) | BIT(19) | BIT(9) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1), source, 128, cmdData);
	cardPolledTransferWrite(CalcCARD_CR2_D2_WRITE(), source, 128, cmdData);
	
	cardParamCommand(CARD_CMD_NAND_COMMIT_BUFFER, 0, CARD_ACTIVATE | CARD_nRESET, NULL, 0);
	u32 status;
	do {
		cardParamCommand(CARD_CMD_NAND_READ_STATUS, 0, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7), &status, 1);
	} while((status & BIT(5)) == 0);
	cardParamCommand(CARD_CMD_NAND_DISCARD_BUFFER, 0, CARD_ACTIVATE | CARD_nRESET, NULL, 0);
}*/

