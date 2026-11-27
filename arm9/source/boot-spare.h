#ifndef NRIO_BOOT_SPARE_H
#define NRIO_BOOT_SPARE_H

#include <stdbool.h>
#include <stdint.h>

enum { NRIO_BOOT_SPARE_BYTES = 64, NRIO_BOOT_SPARE_PAGES = 6 };

enum nrio_spare_record {
    NRIO_SPARE_UNREADABLE,
    NRIO_SPARE_UNSTABLE,
    NRIO_SPARE_ERASED,
    NRIO_SPARE_OTHER,
    NRIO_SPARE_ARM7_19,
    NRIO_SPARE_ARM7_11,
    NRIO_SPARE_TABLE_MATCH,
};

enum nrio_spare_comparison {
    NRIO_SPARE_NOT_COMPARED,
    NRIO_SPARE_AGREE,
    NRIO_SPARE_DIFFER,
};

struct nrio_boot_spare_report {
    enum nrio_spare_record page[NRIO_BOOT_SPARE_PAGES];
    enum nrio_spare_comparison arm7_copies;
    enum nrio_spare_comparison table_overlap;
};

typedef bool (*nrio_spare_reader)(void *, unsigned, uint8_t [NRIO_BOOT_SPARE_BYTES]);
void nrio_check_boot_spare(nrio_spare_reader read_page, void *context,
                           struct nrio_boot_spare_report *report);

#endif
