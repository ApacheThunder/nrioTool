// SPDX-License-Identifier: CC0-1.0
// SPDX-FileContributor: Adrian "asie" Siekierka, 2024

#include <nds.h>

#include "slot2-usb.h"

#define D12_COMMAND (*(vu16 *)0x09fffffe)
#define D12_DATA (*(vu16 *)0x09fdfffe)
#define D14_CHIP_ID_LOW GBA_BUS[0x70 * 0x10000]
#define D14_CHIP_ID_HIGH GBA_BUS[0x72 * 0x10000]

enum {
    SLOT2_TIMING_MASK = 0x1f,
    D14_TIMING = (1 << 4) | (2 << 2) | 2,
    D12_TIMING = (3 << 2) | 3,
    D12_READ_ID = 0xfd,
    D12_CHIP_ID = 0x1012,
};

bool nrio_slot2_usb_present(void)
{
    /* DSi mode has no Slot-2 bus. Even reading its mapped address aborts. */
    if (isDSiMode()) return false;

    const int irq_state = enterCriticalSection();
    const u16 saved = REG_EXMEMCNT;
    sysSetCartOwner(BUS_OWNER_ARM9);
    REG_EXMEMCNT = (REG_EXMEMCNT & ~SLOT2_TIMING_MASK) | D14_TIMING;

    const u32 high = D14_CHIP_ID_HIGH & 0xff;
    const u32 low = D14_CHIP_ID_LOW;
    const u32 id = (high << 16) | low;
    bool present = id >= 0x158100 && id < 0x158400;
    // A GBA ROM has a fixed header byte at 0xB2. Avoid sending it the D12 command.
    if (!present && (GBA_BUS[0xb2 / 2] & 0xff) != 0x96) {
        REG_EXMEMCNT = (REG_EXMEMCNT & ~SLOT2_TIMING_MASK) | D12_TIMING;
        D12_COMMAND = D12_READ_ID;
        const u16 first = D12_DATA & 0xff;
        const u16 second = D12_DATA & 0xff;
        present = (first | (second << 8)) == D12_CHIP_ID;
    }

    const u16 changed = SLOT2_TIMING_MASK | ARM7_OWNS_ROM;
    REG_EXMEMCNT = (REG_EXMEMCNT & ~changed) | (saved & changed);
    leaveCriticalSection(irq_state);
    return present;
}
