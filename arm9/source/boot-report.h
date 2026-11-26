#ifndef NRIO_BOOT_REPORT_H
#define NRIO_BOOT_REPORT_H

#include "region-recovery.h"

enum nrio_report_action {
    NRIO_REPORT_BACK, NRIO_REPORT_BACKUP, NRIO_REPORT_RETRY,
};

/* A later direct-boot route (e.g. at 0x80000) does not overlap the table.
   Only a recognized ROM starting at the table, or the game marker there,
   rules that table out. This is for diagnostics, not write authorization. */
static inline bool nrio_report_primary_occupies_secondary(struct nrio_boot_rom rom, bool game_marker)
{
    if (rom.address != NRIO_RAW_SECONDARY_TABLE) return false;
    switch (rom.kind) {
        case NRIO_BOOT_DIRECT_DS:
        case NRIO_BOOT_STOCK:
        case NRIO_BOOT_PATCHED:
        case NRIO_BOOT_COMPACT:
        case NRIO_BOOT_UDISK:
        case NRIO_BOOT_XULU:
            return true;
        default:
            return game_marker;
    }
}

enum nrio_report_action nrio_boot_report(const char *root, const struct nrio_raw_info *info);
#endif
