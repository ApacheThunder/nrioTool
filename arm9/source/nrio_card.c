#include <nds.h>
#include <nds/card.h>

#include "nrio_card.h"

enum { TRANSFER_POLL_LIMIT = 2000000 };

static bool wait_for_idle(void)
{
    for (unsigned attempt = 0; attempt < TRANSFER_POLL_LIMIT; ++attempt) {
        if (!(REG_ROMCTRL & (u32)CARD_BUSY)) return true;
    }
    return false;
}

static bool send_setup_command(u8 command, u32 address, u32 flags)
{
    if (!wait_for_idle()) return false;
    const u8 frame[8] = {0, 0, 0, (u8)address, (u8)(address >> 8),
                         (u8)(address >> 16), (u8)(address >> 24), command};
    cardWriteCommand(frame);
    REG_ROMCTRL = flags | CARD_ACTIVATE;

    bool received_word = false;
    for (unsigned idle = 0; idle < TRANSFER_POLL_LIMIT; ++idle) {
        const u32 status = REG_ROMCTRL;
        if (status & CARD_DATA_READY) {
            (void)REG_CARD_DATA_RD;
            if (received_word) return false;
            received_word = true;
            idle = 0;
        } else if (!(status & (u32)CARD_BUSY)) {
            return true;
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
    // Setup response data is unused. Some boards may complete without a word.
    return send_setup_command(0x66, 0, probe_flags) &&
           send_setup_command(0xc1, 0x0d210000, mode_flags) &&
           send_setup_command(0xc1, 0x0fb00000, mode_flags) &&
           send_setup_command(0xc1, card_type, mode_flags);
}
