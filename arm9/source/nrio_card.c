#include <nds.h>
#include <nds/card.h>

#include "nrio_card.h"

enum { SECTOR_WORDS = 512 / sizeof(u32), TRANSFER_POLL_LIMIT = 2000000 };

static bool wait_for_idle(void)
{
    for (unsigned attempt = 0; attempt < TRANSFER_POLL_LIMIT; ++attempt) {
        if (!(REG_ROMCTRL & (u32)CARD_BUSY)) return true;
    }
    return false;
}

// The legacy D2 path is also useful as an independent read for diagnostics.
static bool read_command(u8 command, u32 address, u32 flags, u32 *out, unsigned words)
{
    if (!wait_for_idle()) return false;
    const u8 frame[8] = {0, 0, 0, (u8)address, (u8)(address >> 8),
                         (u8)(address >> 16), (u8)(address >> 24), command};
    cardWriteCommand(frame);
    REG_ROMCTRL = flags | CARD_ACTIVATE;

    unsigned count = 0;
    for (unsigned idle = 0; idle < TRANSFER_POLL_LIMIT; ++idle) {
        const u32 status = REG_ROMCTRL;
        if (status & CARD_DATA_READY) {
            const u32 value = REG_CARD_DATA_RD;
            if (count < words && out) out[count] = value;
            if (++count > words) return false;
            idle = 0;
        } else if (!(status & (u32)CARD_BUSY)) {
            return count == words;
        }
    }
    return false;
}

bool InitCartNandReadMode(u32 card_type)
{
    if (!card_type || card_type == UINT32_MAX) card_type = 0x10830000;

    const u32 probe_flags = CARD_nRESET | CARD_BLK_SIZE(7) | CARD_SEC_CMD |
                            BIT(20) | BIT(19) | BIT(14) | BIT(13);
    const u32 mode_flags = CARD_nRESET | CARD_BLK_SIZE(7) | BIT(17);
    return read_command(0x66, 0, probe_flags, NULL, 1) &&
           read_command(0xc1, 0x0d210000, mode_flags, NULL, 1) &&
           read_command(0xc1, 0x0fb00000, mode_flags, NULL, 1) &&
           read_command(0xc1, card_type, mode_flags, NULL, 1);
}

bool nrio_readSector(void *destination, u32 rom_offset)
{
    return read_command(0xd2, rom_offset, 0xbb00014a, destination, SECTOR_WORDS);
}
