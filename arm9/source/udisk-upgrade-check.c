#include "udisk-upgrade-check.h"

#include <stddef.h>
#include <string.h>

enum { SECTOR = 512, MAP_ADDRESS = 0x40000, MAP_CHUNK = 0x20000, OLD_ROM_ADDRESS = 0x80000 };
uint32_t nrio_update_crc32(uint32_t crc, const uint8_t *data, size_t size);

static uint32_t word(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t crc_of(const uint8_t sector[SECTOR])
{
    return nrio_update_crc32(UINT32_MAX, sector, SECTOR) ^ UINT32_MAX;
}

enum nrio_udisk_layout nrio_udisk_classify(nrio_read_nand_sector read, void *context,
                                          uint32_t physical_sectors)
{
    uint8_t sector[SECTOR];
    if (!read || physical_sectors < 0x180000 / SECTOR) return NRIO_UDISK_LAYOUT_UNKNOWN;
    if (!read(context, MAP_ADDRESS / SECTOR, sector)) return NRIO_UDISK_LAYOUT_READ_ERROR;
    uint32_t first = word(sector);
    if (first < OLD_ROM_ADDRESS || first % 0x4000 || first / SECTOR >= physical_sectors)
        return NRIO_UDISK_LAYOUT_UNKNOWN;
    if (!read(context, first / SECTOR, sector)) return NRIO_UDISK_LAYOUT_READ_ERROR;
    const uint32_t header_crc = crc_of(sector);
    if (header_crc != 0x5d592e83 && (header_crc != 0x52fb6b25 || first != OLD_ROM_ADDRESS))
        return NRIO_UDISK_LAYOUT_UNKNOWN;

    if (!read(context, (MAP_ADDRESS + MAP_CHUNK) / SECTOR - 1, sector))
        return NRIO_UDISK_LAYOUT_READ_ERROR;
    if (memcmp(sector + SECTOR - 4, "gao\0", 4)) return NRIO_UDISK_LAYOUT_UNKNOWN;
    const uint32_t revision = word(sector + SECTOR - 8);
    if (header_crc == 0x5d592e83)
        return revision == 3 ? NRIO_UDISK_LAYOUT_STOCK_145 : NRIO_UDISK_LAYOUT_UNKNOWN;
    if (revision != 2) return NRIO_UDISK_LAYOUT_UNKNOWN;

    /* The 1.23 wrapper loads an embedded SRL from logical ROM offset 0x8000.
       The tested map is contiguous, including the mapping of that header. */
    if (!read(context, (OLD_ROM_ADDRESS + 0x8000) / SECTOR, sector))
        return NRIO_UDISK_LAYOUT_READ_ERROR;
    if (crc_of(sector) != 0x945c8219) return NRIO_UDISK_LAYOUT_UNKNOWN;

    uint32_t map_crc = UINT32_MAX;
    for (unsigned offset = 0; offset < MAP_CHUNK; offset += SECTOR) {
        if (!read(context, (MAP_ADDRESS + offset) / SECTOR, sector))
            return NRIO_UDISK_LAYOUT_READ_ERROR;
        map_crc = nrio_update_crc32(map_crc, sector, SECTOR);
    }
    return (map_crc ^ UINT32_MAX) == 0xa6af3c33
         ? NRIO_UDISK_LAYOUT_LEGACY_123 : NRIO_UDISK_LAYOUT_UNKNOWN;
}
