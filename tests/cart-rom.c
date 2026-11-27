#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cart-rom.h"

static void put32(uint8_t *where, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) where[i] = value >> (i * 8);
}

int main(int argc, char **argv)
{
    uint8_t header[NRIO_ROM_HEADER_BYTES] = {0};
    struct nrio_cart_rom_plan plan;
    header[0x14] = 6;
    put32(header + 0x20, 0x4000);
    put32(header + 0x2c, 0x4000);
    put32(header + 0x30, 0x8800);
    put32(header + 0x3c, 0x40);
    put32(header + 0x68, 0x10000);
    assert(nrio_plan_cart_rom(header, &plan));
    assert(!plan.uses_declared_size && plan.bytes == 0x800000 && plan.capacity == 0x800000);

    header[0x14] = 0;
    put32(header + 0x80, 0x10840);
    assert(nrio_plan_cart_rom(header, &plan));
    assert(plan.uses_declared_size && plan.bytes == 0x10840 && plan.capacity == 0x20000);
    put32(header + 0x80, 0x1083f);
    assert(nrio_plan_cart_rom(header, &plan) && !plan.uses_declared_size);
    put32(header + 0x80, 0xffffffff);
    assert(nrio_plan_cart_rom(header, &plan) && !plan.uses_declared_size);
    put32(header + 0x40, 0x1fff0);
    put32(header + 0x44, 0x20);
    assert(!nrio_plan_cart_rom(header, &plan));
    put32(header + 0x44, 0);
    put32(header + 0x3c, 0xfffffffc);
    assert(!nrio_plan_cart_rom(header, &plan));
    put32(header + 0x3c, 0x40);
    header[0x12] = 2;
    assert(!nrio_plan_cart_rom(header, &plan));
    header[0x12] = 0;
    header[0x14] = 13;
    assert(!nrio_plan_cart_rom(header, &plan));

    uint8_t spare[NRIO_ROM_COMPARE_BYTES], main_data[NRIO_ROM_MAIN_COMPARE_BYTES] = {0};
    memset(spare, 0xff, sizeof(spare));
    memset(header + 0x400, 0xff, sizeof(spare));
    memset(header + 0x480, 0xff, sizeof(spare));
    struct nrio_rom_spare_compare match = nrio_compare_cart_spare(header, spare, main_data);
    assert(match.spare_erased && match.matches == 64 && !match.following_repeats);
    assert(match.main_matches == 1024);
    main_data[0x3ff] = 1;
    for (unsigned i = 0; i < sizeof(spare); ++i) spare[i] = (uint8_t)i;
    for (unsigned off = 0x400; off <= 0x580; off += 0x80)
        memcpy(header + off, spare, sizeof(spare));
    spare[12] ^= 1;
    match = nrio_compare_cart_spare(header, spare, main_data);
    assert(!match.spare_erased && match.matches == 63 && match.following_repeats == 3);
    header[0x500] ^= 1;
    match = nrio_compare_cart_spare(header, spare, main_data);
    assert(match.following_repeats == 1 && match.main_matches == 1023);
    for (unsigned off = 0x800; off < 0xc00; off += 0x80) {
        memset(header + off, 0xff, 32);
        memcpy(header + off + 32, spare + 32, 32);
    }
    match = nrio_compare_cart_spare(header, spare, main_data);
    assert(match.masked_copies == 8);
    header[0xa20] ^= 1;
    assert(nrio_compare_cart_spare(header, spare, main_data).masked_copies == 7);

    /* Optional private raw block-0 captures use the DS header at offset 0x400. */
    for (int i = 1; i < argc; ++i) {
        FILE *raw = fopen(argv[i], "rb");
        assert(raw && !fseek(raw, 0x400, SEEK_SET));
        assert(fread(header, 1, 512, raw) == 512 && !fclose(raw));
        assert(nrio_plan_cart_rom(header, &plan));
    }
    printf("ROM layout and spare comparison tests passed (%d capture headers)\n", argc - 1);
    return 0;
}
