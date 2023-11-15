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

static void DoWait(int waitTime) {
	if (waitTime > 0)for (int i = 0; i < waitTime; i++) { swiWaitForVBlank(); }
}

void InitCartNandReadMode() {
	cardParamCommand (0x66, 0, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | CARD_SEC_CMD | BIT(20) | BIT(19) | BIT(14) | BIT(13), (u32*)INITBUFFER, 128);
	cardParamCommand (0xC1, 0x0D210000, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17), (u32*)(INITBUFFER + 0x10), 128);
	cardParamCommand (0xC1, 0x0FB00000, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17), (u32*)(INITBUFFER + 0x10), 128);
	cardParamCommand (0xC1, 0x10830000, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17), (u32*)(INITBUFFER + 0x20), 128);
	DoWait(8);
}

u32 CalcCARD_CR2_D2() {
	u32 da,db,dc,ncr2;
	da=CARDD2FLAGS; // 0x27FFE60 ?
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
	cardreadpage(rom_offset, (u32)destination, CARD_CMD_DATA_READ, CARDD2FLAGS);
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

