#include "raw_nand.h"

#ifndef REG_ROMCTRL
// Register spellings used by the libnds shipped with devkitARM r20.
#define REG_ROMCTRL CARD_CR2
#define REG_AUXSPICNTH CARD_CR1H
#define REG_CARD_COMMAND CARD_COMMAND
#define REG_CARD_DATA_RD CARD_DATA_RD
#define CARD_SPICNTH_ENABLE CARD_CR1_ENABLE
#define CARD_SPICNTH_IRQ CARD_CR1_IRQ
#endif

enum {
    POLL_LIMIT = 2000000,
    NAND_POLL_LIMIT = 8192,
};

static bool wait_ready(void)
{
    for (u32 i = 0; i < POLL_LIMIT; ++i) {
        u32 status = REG_ROMCTRL;
        if (status & CARD_DATA_READY) {
            return true;
        }
        if (!(status & (u32)CARD_BUSY)) {
            return false;
        }
    }
    return false;
}

static bool wait_idle(void)
{
    for (u32 i = 0; i < POLL_LIMIT; ++i) {
        if (!(REG_ROMCTRL & (u32)CARD_BUSY)) {
            return true;
        }
    }
    return false;
}

static bool receive_word(u32 *word)
{
    if (!wait_ready()) {
        return false;
    }
    *word = REG_CARD_DATA_RD;
    return true;
}

static void frame(u8 first, u8 second, u32 address, u8 extra)
{
    REG_CARD_COMMAND[0] = first;
    REG_CARD_COMMAND[1] = second;
    REG_CARD_COMMAND[2] = (u8)address;
    REG_CARD_COMMAND[3] = (u8)(address >> 8);
    REG_CARD_COMMAND[4] = (u8)(address >> 16);
    REG_CARD_COMMAND[5] = (u8)(address >> 24);
    REG_CARD_COMMAND[6] = extra;
    REG_CARD_COMMAND[7] = 0;
}

static bool command_word(u32 control, u32 *reply)
{
    REG_ROMCTRL = control;
    return receive_word(reply) && wait_idle();
}

static bool opcode(u8 value)
{
    u32 reply;
    frame(0xd1, 0x61, value, 0);
    return command_word(0xa7000000, &reply);
}

static bool address_command(u8 command, u32 address, u8 extra)
{
    u32 reply;
    frame(0xd1, command, address, extra);
    return command_word(0xb7000001, &reply);
}

static bool wait_nand(void)
{
    u32 reply;
    REG_CARD_COMMAND[0] = 0xc2;
    for (u32 i = 0; i < NAND_POLL_LIMIT; ++i) {
        if (!command_word(0xa7000000, &reply)) {
            return false;
        }
        if ((reply & 0x80808080u) == 0x80808080u) {
            return true;
        }
    }
    return false;
}

bool nand_read_probe_id(u32 *id)
{
    REG_AUXSPICNTH = CARD_SPICNTH_ENABLE | CARD_SPICNTH_IRQ;
    if (!wait_idle()) {
        return false;
    }
    frame(0xd1, 0x61, 0x90, 0);
    if (!command_word(0xa7180000, id)) {
        return false;
    }
    frame(0xd1, 0x51, 0, 0);
    if (!command_word(0xa7180000, id)) {
        return false;
    }
    frame(0xd0, 0, 0, 0);
    return command_word(0xa7180000, id);
}

static bool page_address(u32 page, bool spare)
{
    return address_command(0x55, (page << 16) | (spare ? 0x800u : 0), (u8)(page >> 16));
}

bool nand_load_page(u32 page, bool spare)
{
    return opcode(0x00) && page_address(page, spare) && opcode(0x30) && wait_nand();
}

static void store_word(u8 *out, u32 word)
{
    out[0] = (u8)word;
    out[1] = (u8)(word >> 8);
    out[2] = (u8)(word >> 16);
    out[3] = (u8)(word >> 24);
}

bool nand_read_sector(u32 column, u8 *buffer)
{
    if (!opcode(0x05) || !address_command(0x52, column * SECTOR_SIZE, 0) || !opcode(0xe0)) {
        return false;
    }

    REG_CARD_COMMAND[0] = 0xd0;
    REG_CARD_COMMAND[1] = 0;
    REG_ROMCTRL = 0xa1000000;
    for (u32 i = 0; i < SECTOR_SIZE; i += 4) {
        u32 word;
        if (!receive_word(&word)) {
            return false;
        }
        store_word(buffer + i, word);
    }
    return wait_idle();
}

bool nand_read_spare(u8 *buffer)
{
    REG_CARD_COMMAND[0] = 0xd0;
    REG_CARD_COMMAND[1] = 0;
    for (u32 i = 0; i < SPARE_SIZE; i += 4) {
        u32 word;
        if (!command_word(0xa7000000, &word)) {
            return false;
        }
        store_word(buffer + i, word);
    }
    return true;
}

static bool successful_status(void)
{
    u32 reply;
    if (!wait_nand() || !opcode(0x70)) {
        return false;
    }
    frame(0xd0, 0, 0, 0);
    return command_word(0xa7000000, &reply) && (reply & 0xc1) == 0xc0;
}

bool nand_erase_block(u32 page)
{
    return opcode(0x60) && address_command(0x53, page, 0) &&
           opcode(0xd0) && successful_status();
}

static bool send_sector(const u8 *data)
{
    REG_CARD_COMMAND[0] = 0xd3;
    REG_CARD_COMMAND[1] = 0;
    REG_CARD_COMMAND[7] = data[0];
    REG_ROMCTRL = 0xe1000008;
    for (u32 i = 1; i <= SECTOR_SIZE; i += 4) {
        if (!wait_ready()) {
            return false;
        }
        // The bridge takes the first byte in the frame and then 128 FIFO words.
        // The last word uses one lookahead byte from the page record.
        REG_CARD_DATA_RD = (u32)data[i] | ((u32)data[i + 1] << 8) |
                      ((u32)data[i + 2] << 16) | ((u32)data[i + 3] << 24);
    }
    return wait_idle();
}

bool nand_program_page(u32 page, const u8 *record)
{
    if (!opcode(0x80) || !page_address(page, false)) {
        return false;
    }
    for (u32 i = 0; i < PAGE_SIZE; i += SECTOR_SIZE) {
        if (!send_sector(record + i)) {
            return false;
        }
    }
    REG_CARD_COMMAND[0] = 0xd1;
    REG_CARD_COMMAND[1] = 0x44;
    REG_CARD_COMMAND[6] = 0;
    for (u32 i = PAGE_SIZE; i < PAGE_RECORD_SIZE; i += 4) {
        u32 reply;
        for (u32 j = 0; j < 4; ++j) {
            REG_CARD_COMMAND[2 + j] = record[i + j];
        }
        if (!command_word(0xa7000000, &reply)) {
            return false;
        }
    }
    return opcode(0x10) && successful_status();
}

bool nand_probe(struct nand_geometry *geometry)
{
    geometry->sectors = 0;
    if (!nand_read_probe_id(&geometry->id)) {
        return false;
    }
    u8 maker = geometry->id;
    if (maker != 0xec && maker != 0xad && maker != 0x2c && maker != 0x98) {
        return false;
    }

    // The original NRIO geometry subtracts 64 blocks for the hidden region.
    // Recover the full physical capacity by using the complete block count.
    u8 device = geometry->id >> 8;
    u32 sectors_per_128_mib = 128 * 1024 * 1024 / SECTOR_SIZE;
    if (device == 0x74 || device == 0x79 || device == 0xc1 || device == 0xf1) {
        geometry->sectors = sectors_per_128_mib;
    } else if (device == 0x71 || device == 0xca || device == 0xda) {
        geometry->sectors = sectors_per_128_mib * 2;
    } else if (device == 0xcc || device == 0xdc) {
        geometry->sectors = sectors_per_128_mib * 4;
    } else if (device == 0xc3 || device == 0xd3) {
        geometry->sectors = sectors_per_128_mib * 8;
    } else if (device == 0xc5 || device == 0xd5) {
        geometry->sectors = sectors_per_128_mib * 16;
    }
    geometry->pages_per_block = geometry->id & (0x20u << 24) ? 128 : 64;
    return geometry->sectors != 0;
}

bool nand_read_main(u32 sector, u32 count, u8 *output)
{
    u32 loaded_page = (u32)-1;
    while (count--) {
        u32 page = sector / SECTORS_PER_PAGE;
        if (page != loaded_page && !nand_load_page(page, false)) {
            return false;
        }
        loaded_page = page;
        if (!nand_read_sector(sector % SECTORS_PER_PAGE, output)) {
            return false;
        }
        output += SECTOR_SIZE;
        ++sector;
    }
    return true;
}
