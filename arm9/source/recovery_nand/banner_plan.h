#ifndef NRIO_BANNER_PLAN_H
#define NRIO_BANNER_PLAN_H

#include "raw_nand.h"

enum {
    BANNER_PAGE = 4,
    BANNER_LBA = BANNER_PAGE * SECTORS_PER_PAGE,
    BANNER_EXPECTED_PAGES = 8,
};

struct nrio_banner_plan {
    u8 signature[16];
    u32 armed;
    u8 expected_spare[BANNER_EXPECTED_PAGES][SPARE_SIZE];
    u8 replacement_main[PAGE_SIZE];
    u8 replacement_spare[SPARE_SIZE];
};

bool banner_is_armed(void);

#endif
