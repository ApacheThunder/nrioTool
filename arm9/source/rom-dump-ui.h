#ifndef NRIO_ROM_DUMP_UI_H
#define NRIO_ROM_DUMP_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "cart-rom.h"

struct nrio_rom_dump {
    uint32_t bytes;
    uint32_t crc;
    uint32_t chip_id;
    uint16_t header_secure_crc, dump_secure_crc;
    int16_t early_b7_spare_matches, early_b7_main_matches;
    bool uses_declared_size;
};

bool nrio_dump_cart_rom(const char *root, bool numbered, struct nrio_rom_dump *result);

#endif
