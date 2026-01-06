#include <nds.h>
#include <nds/ndstypes.h>
#include <nds/system.h>
#include <nds/card.h>
#include <stdio.h>

#include "tonccpy.h"
#include "read_card.h"

#define INITBUFFER  0x02000000

#define NANDREADPAGE_D2 0xD2 // NAND Read Page Command
#define CARDB7FLAGS (u32)0xB9180000

// Used by a D2 read page command from USB BIOS BURNER 1.0
// #define CARDD2FLAGS (u32)0xB7180132

// Derived from CalcCard_CR2_D2() function. The source data this function used never changes so might as well just hardcode the result.
// Ghidra research into uDisk updators confirms this was the value D2 read function used. uDisk updator hardcodes this value too.
static u32 CARD_CR2_D2 = 0xBB00014A;

void InitCartNandReadMode(u32 CardType, u32 PortFlags) {
	// Only bricked carts would present values like these. Hard Code 1083 if this is the case
	// FYI only other known value that can exist here is 0x10430000 which is used by 1G N-Cards? Only seen it once with a 1g N-Card setup as a bootleg game.
	// Because of how rare 1G cards would be the default value will be 0x10830000 which 2G, 8G, and 16G cards are known to use.
	// In theory a 32G card could end up using 0x10C30000. Interestingly enough using this value on a 16G cart does not break nrioTools ability to return correct data.
	// (using 0x10430000 however does result in just FF data being returned from a 16G cart)
	switch (CardType) {
		case 0x00000000: { CardType = 0x10830000; }break;
		case 0xFFFFFFFF: { CardType = 0x10830000; }break; 
	}
	
	// Original init sequence from stage1 main rom before it goes to read stage2/retail game from nand as defined by a block stbale stored at 0x8000
	cardParamCommand (0x66, 0, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | CARD_SEC_CMD | BIT(20) | BIT(19) | BIT(14) | BIT(13), (u32*)INITBUFFER, 128);
	cardParamCommand (0xC1, 0x0D210000, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17), (u32*)(INITBUFFER + 0x10), 128);
	cardParamCommand (0xC1, 0x0FB00000, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17), (u32*)(INITBUFFER + 0x10), 128);
	cardParamCommand (0xC1, CardType, CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17), (u32*)(INITBUFFER + 0x20), 128);
}


void nrio_readSector(void* destination, u32 rom_offset) {
	cardParamCommand (NANDREADPAGE_D2, rom_offset, CARD_CR2_D2, (u32*)((u32)destination), 128);
}

/*void nrio_readSectorB7(void* destination, u32 rom_offset) {
	cardParamCommand (CARD_CMD_DATA_READ, rom_offset, CARDB7FLAGS, (u32*)((u32)destination), 128);
}

void nrio_readSectors(void* destination, u32 rom_offset, u32 num_words) {
	ALIGN(4) u32 read_buffer[128];
	u32 last_read_size = num_words % 128;
	u32 offset = rom_offset;
	u8* dest = (u8*)destination;
	while(num_words > 0) {
		nrio_readSector(read_buffer, offset);
		memcpy(dest, read_buffer, num_words == 128 ? 128 : last_read_size);
		dest = ((u8*)dest + 0x200);
		offset += 0x200;
		if(num_words < 128)num_words = 128;
		num_words -= 128;
	}
}*/

