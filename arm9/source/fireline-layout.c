#include "fireline-layout.h"
#include <string.h>

enum { SECTOR = 512, CHUNK = 0x4000, MIN_DATA = 0x20000 };

static uint32_t word(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool valid(uint32_t address, uint32_t sectors)
{
    return address >= MIN_DATA && !(address % CHUNK) &&
           address != NRIO_FIRELINE_OS_MAP && address != NRIO_FIRELINE_DIR_MAP &&
           address / SECTOR < sectors && sectors - address / SECTOR >= CHUNK / SECTOR;
}

static uint32_t crc32(const uint8_t *p)
{
    uint32_t value = UINT32_MAX;
    for (unsigned i = 0; i < SECTOR; ++i) {
        value ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return ~value;
}

static bool read_stable(bool (*read)(void *, uint32_t, uint8_t *), void *context,
                        uint32_t lba, uint8_t out[SECTOR])
{
    uint8_t repeat[SECTOR];
    return read(context, lba, out) && read(context, lba, repeat) &&
           !memcmp(out, repeat, SECTOR);
}

uint32_t nrio_fireline_block(const struct nrio_fireline_layout *r, unsigned index)
{
    return index < NRIO_FIRELINE_OS_BLOCKS ? word(r->sectors[0] + index * 4) : 0;
}

void nrio_fireline_inspect(bool (*read)(void *, uint32_t, uint8_t *), void *context,
                           uint32_t sectors, struct nrio_fireline_layout *r)
{
    memset(r, 0, sizeof(*r));
    if (!read) return;
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t lba = (i ? NRIO_FIRELINE_DIR_MAP : NRIO_FIRELINE_OS_MAP) / SECTOR;
        if (lba < sectors && read_stable(read, context, lba, r->sectors[i]))
            r->present |= 1u << i;
    }

    uint32_t os = nrio_fireline_block(r, 0), dir = word(r->sectors[1]);
    if ((r->present & 1) && valid(os, sectors)) {
        r->os_address = os;
        if (read_stable(read, context, os / SECTOR, r->sectors[2])) {
            uint8_t second[SECTOR];
            r->present |= 4;
            /* Fingerprints from the archived FireLine os.nds, not a generic SRL. */
            r->known_os = crc32(r->sectors[2]) == UINT32_C(0xa396b61f) &&
                read_stable(read, context, os / SECTOR + 1, second) &&
                crc32(second) == UINT32_C(0xdf814225);
        }
    }
    if ((r->present & 2) && valid(dir, sectors)) {
        r->dir_address = dir;
        if (read_stable(read, context, dir / SECTOR, r->sectors[3])) {
            r->present |= 8;
            r->known_dir = !memcmp(r->sectors[3], "\0SIC", 4);
        }
    }
    if (!r->known_os || !r->known_dir) return;
    for (unsigned i = 0; i < NRIO_FIRELINE_OS_BLOCKS; ++i) {
        uint32_t address = nrio_fireline_block(r, i);
        if (!valid(address, sectors) || address == dir ||
            (i && address <= nrio_fireline_block(r, i - 1))) return;
    }
    r->dumpable = true;
}
