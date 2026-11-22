#ifndef NRIO_RAW_NAND_H
#define NRIO_RAW_NAND_H

#include <nds.h>

enum {
    SECTOR_SIZE = 512,
    SECTORS_PER_PAGE = 4,
    PAGE_SIZE = SECTOR_SIZE * SECTORS_PER_PAGE,
    SPARE_SIZE = 64,
    SPARES_PER_SECTOR = SECTOR_SIZE / SPARE_SIZE,
    PAGE_RECORD_SIZE = PAGE_SIZE + SPARE_SIZE,
};

struct nand_geometry {
    u32 id;
    u32 sectors;
    u32 pages_per_block;
};

bool nand_probe(struct nand_geometry *geometry);
bool nand_read_main(u32 sector, u32 count, u8 *output);
bool nand_load_page(u32 page, bool spare);
bool nand_read_sector(u32 column, u8 *buffer);
bool nand_read_spare(u8 *buffer);

bool nand_erase_block(u32 page);
bool nand_program_page(u32 page, const u8 *record);

#endif
