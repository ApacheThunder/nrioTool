#include "cart-rom.h"

#include <string.h>

static uint32_t word(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool nrio_plan_cart_rom(const uint8_t header[512], struct nrio_cart_rom_plan *plan)
{
    /* This reader is for NTR ROMs. It does not switch to a second TWL session. */
    if (header[0x12] || header[0x14] > 12) return false;
    const uint32_t capacity = UINT32_C(0x20000) << header[0x14];
    uint32_t required = 0x8000;
    for (unsigned off = 0x20; off <= 0x30; off += 0x10) {
        const uint32_t start = word(header + off), size = word(header + off + 12);
        if (start < 0x4000 || start >= capacity || !size || size > capacity - start)
            return false;
        if (start + size > required) required = start + size;
    }
    for (unsigned off = 0x40; off <= 0x58; off += 8) {
        const uint32_t start = word(header + off), size = word(header + off + 4);
        if (!size) continue;
        if (start < 0x8000 || start >= capacity || size > capacity - start)
            return false;
        if (start + size > required) required = start + size;
    }
    const uint32_t banner = word(header + 0x68);
    if (banner) {
        if (banner < 0x8000 || banner >= capacity || 0x840 > capacity - banner)
            return false;
        if (banner + 0x840 > required) required = banner + 0x840;
    }
    const uint32_t used = word(header + 0x80);
    plan->uses_declared_size = used >= required && used <= capacity;
    plan->capacity = capacity;
    plan->minimum_bytes = required;
    plan->bytes = plan->uses_declared_size ? used : capacity;
    return true;
}

struct nrio_rom_spare_compare nrio_compare_cart_spare(const uint8_t header[NRIO_ROM_HEADER_BYTES],
                                 const uint8_t spare[NRIO_ROM_COMPARE_BYTES],
                                 const uint8_t main_data[NRIO_ROM_MAIN_COMPARE_BYTES])
{
    const uint8_t *first = header + 0x400;
    struct nrio_rom_spare_compare result = {.spare_erased = true};
    bool uniform = true;
    for (unsigned i = 0; i < NRIO_ROM_COMPARE_BYTES; ++i) {
        result.matches += first[i] == spare[i];
        result.spare_erased &= spare[i] == 0xff;
        uniform &= first[i] == first[0];
    }
    for (unsigned i = 0; i < NRIO_ROM_MAIN_COMPARE_BYTES; ++i)
        result.main_matches += header[0xc00 + i] == main_data[i];
    if (!uniform) {
        for (unsigned off = 0x480; off + NRIO_ROM_COMPARE_BYTES <= NRIO_ROM_HEADER_BYTES; off += 0x80) {
            if (memcmp(first, header + off, NRIO_ROM_COMPARE_BYTES)) break;
            ++result.following_repeats;
        }
    }
    /* The next 1 KiB window shows bytes 0x20-0x3F of spare page 0,
       preceded by 32 FF bytes in each 0x80-byte slot in paired captures. */
    for (unsigned off = 0x800; off < 0xc00; off += 0x80) {
        bool masked = !memcmp(header + off + 32, spare + 32, 32);
        for (unsigned i = 0; i < 32; ++i) masked &= header[off + i] == 0xff;
        if (masked) ++result.masked_copies;
    }
    return result;
}
