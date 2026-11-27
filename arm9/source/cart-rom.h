#ifndef NRIO_CART_ROM_H
#define NRIO_CART_ROM_H

#include <stdbool.h>
#include <stdint.h>

enum { NRIO_ROM_HEADER_BYTES = 0x1000, NRIO_ROM_COMPARE_BYTES = 64, NRIO_ROM_MAIN_COMPARE_BYTES = 0x400 };

struct nrio_cart_rom_plan {
    uint32_t bytes;
    uint32_t capacity;
    bool uses_declared_size;
};

bool nrio_plan_cart_rom(const uint8_t header[512], struct nrio_cart_rom_plan *plan);
struct nrio_rom_spare_compare {
    unsigned matches;
    unsigned main_matches;
    unsigned following_repeats;
    bool spare_erased;
};

struct nrio_rom_spare_compare nrio_compare_cart_spare(const uint8_t header[NRIO_ROM_HEADER_BYTES],
                                 const uint8_t spare[NRIO_ROM_COMPARE_BYTES],
                                 const uint8_t main_data[NRIO_ROM_MAIN_COMPARE_BYTES]);

#endif
