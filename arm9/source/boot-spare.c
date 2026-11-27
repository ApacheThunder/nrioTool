#include "boot-spare.h"

#include <string.h>
#include <zlib.h>

static bool all_ff(const uint8_t *bytes)
{
    for (unsigned i = 0; i < NRIO_BOOT_SPARE_BYTES; ++i)
        if (bytes[i] != 0xff) return false;
    return true;
}

static enum nrio_spare_record identify(unsigned page, const uint8_t *bytes)
{
    if (all_ff(bytes)) return NRIO_SPARE_ERASED;
    if (page == 0) return NRIO_SPARE_OTHER; /* Varies even on working carts. */

    /* Full-record diagnostic fingerprints from Apache Thunder's bootleg dumps
       and the older 8 Gbit DS Linker capture. Never use them to authorize a write. */
    const uint32_t crc = crc32(0, bytes, NRIO_BOOT_SPARE_BYTES);
    if (page == 2) return crc == 0x1dd74597 ? NRIO_SPARE_TABLE_MATCH : NRIO_SPARE_OTHER;
    if (page == 3) return crc == 0x8f7076f1 ? NRIO_SPARE_TABLE_MATCH : NRIO_SPARE_OTHER;
    if (crc == 0xd046221c) return NRIO_SPARE_ARM7_19;
    if (crc == 0x6b9e8004) return NRIO_SPARE_ARM7_11;
    return NRIO_SPARE_OTHER;
}

void nrio_check_boot_spare(nrio_spare_reader read_page, void *context,
                           struct nrio_boot_spare_report *report)
{
    uint8_t records[NRIO_BOOT_SPARE_PAGES][NRIO_BOOT_SPARE_BYTES], retry[NRIO_BOOT_SPARE_BYTES];
    memset(report, 0, sizeof(*report));
    for (unsigned i = 0; i < NRIO_BOOT_SPARE_PAGES; ++i) {
        if (!read_page(context, i, records[i]) || !read_page(context, i, retry)) continue;
        report->page[i] = memcmp(records[i], retry, sizeof(retry)) ? NRIO_SPARE_UNSTABLE :
                          identify(i, records[i]);
    }

    if (report->page[1] >= NRIO_SPARE_ERASED && report->page[4] >= NRIO_SPARE_ERASED &&
        report->page[5] >= NRIO_SPARE_ERASED &&
        (report->page[1] != NRIO_SPARE_ERASED || report->page[4] != NRIO_SPARE_ERASED ||
         report->page[5] != NRIO_SPARE_ERASED))
        report->arm7_copies = !memcmp(records[1], records[4], sizeof(retry)) &&
                              !memcmp(records[1], records[5], sizeof(retry)) ?
                              NRIO_SPARE_AGREE : NRIO_SPARE_DIFFER;

    if (report->page[2] >= NRIO_SPARE_ERASED && report->page[3] >= NRIO_SPARE_ERASED &&
        (report->page[2] != NRIO_SPARE_ERASED || report->page[3] != NRIO_SPARE_ERASED))
        report->table_overlap = !memcmp(records[2], records[3] + 8, 0x38) ?
                                NRIO_SPARE_AGREE : NRIO_SPARE_DIFFER;
}
