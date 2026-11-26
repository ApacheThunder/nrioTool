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

enum nrio_boot_fingerprint_status nrio_boot_fingerprint(
    nrio_read_nand_sector read, void *context, uint32_t sectors,
    uint32_t address, uint32_t bytes, uint32_t *result)
{
    const uint32_t lba = address / SECTOR, count = bytes / SECTOR;
    if (!result) return NRIO_BOOT_HASH_UNAVAILABLE;
    *result = 0;
    if (!read || !bytes || bytes > MAP_CHUNK || bytes % SECTOR || address % SECTOR ||
        lba >= sectors || count > sectors - lba)
        return NRIO_BOOT_HASH_UNAVAILABLE;

    uint8_t a[SECTOR], b[SECTOR];
    uint32_t crc = UINT32_MAX;
    bool erased = true, zero = true;
    for (uint32_t i = 0; i < count; ++i) {
        if (!read(context, lba + i, a) || !read(context, lba + i, b))
            return NRIO_BOOT_HASH_READ_ERROR;
        if (memcmp(a, b, SECTOR)) return NRIO_BOOT_HASH_UNSTABLE;
        for (unsigned j = 0; j < SECTOR && (erased || zero); ++j) {
            erased &= a[j] == 0xff;
            zero &= a[j] == 0;
        }
        crc = nrio_update_crc32(crc, a, SECTOR);
    }
    *result = crc ^ UINT32_MAX;
    return erased ? NRIO_BOOT_HASH_ERASED : zero ? NRIO_BOOT_HASH_ZERO : NRIO_BOOT_HASH_OK;
}

enum nrio_udisk_capture nrio_identify_udisk_capture(nrio_read_nand_sector read, void *context,
                                                    uint32_t sectors)
{
    uint8_t sector[SECTOR];
    if (!read || sectors <= OLD_ROM_ADDRESS / SECTOR ||
        !read(context, MAP_ADDRESS / SECTOR, sector) || word(sector) != OLD_ROM_ADDRESS ||
        !read(context, OLD_ROM_ADDRESS / SECTOR, sector) || crc_of(sector) != 0x5d592e83)
        return NRIO_UDISK_CAPTURE_UNKNOWN;

    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < MAP_CHUNK / SECTOR; ++i) {
        if (!read(context, MAP_ADDRESS / SECTOR + i, sector)) return NRIO_UDISK_CAPTURE_UNKNOWN;
        crc = nrio_update_crc32(crc, sector, SECTOR);
    }
    switch (crc ^ UINT32_MAX) {
        case 0xaf0a3fdb: return NRIO_UDISK_CAPTURE_FIRE8;
        case 0xd9d5e30a: return NRIO_UDISK_CAPTURE_FIRE16;
        default: return NRIO_UDISK_CAPTURE_UNKNOWN;
    }
}

static bool read_mapped(nrio_read_nand_sector read, void *ctx, uint32_t sectors,
                        uint32_t table, uint32_t offset, uint8_t out[SECTOR])
{
    enum { CHUNK = 0x4000, LOWEST_ROM = 0x18000 };
    uint32_t entry = offset / CHUNK * 4;
    if (entry >= CHUNK || !read(ctx, table / SECTOR + entry / SECTOR, out)) return false;
    uint32_t address = word(out + entry % SECTOR);
    if (address < LOWEST_ROM || address % CHUNK || sectors < CHUNK / SECTOR ||
        address / SECTOR > sectors - CHUNK / SECTOR) return false;
    if (address >= 0x40000 && address < 0x60000) {
        /* A direct-boot game here is readable, but not one of these firmware layouts. */
        return false;
    }
    return read(ctx, address / SECTOR + offset % CHUNK / SECTOR, out);
}

const struct nrio_udisk_archive *nrio_identify_archived_udisk(
    nrio_read_nand_sector read, void *context, uint32_t physical_sectors, uint32_t table)
{
    static const struct {
        struct nrio_udisk_archive archive;
        uint32_t header_crc, second_crc;
    } known[] = {
        {{"DS Linker 1.22G", 0xca240, true, {0x6c6fb904, 0x7476355f}}, 0x52fb6b25, 0x785c3c0a},
        {{"DS Linker 1.22N (16G)", 0xcbe40, true, {0xd97b481f}}, 0x52fb6b25, 0xe298c97e},
        {{"DS Linker 1.22N (8G)", 0xcbe40, true, {0x1e3ef963, 0x5d1ecdf7}}, 0x52fb6b25, 0x70e79b15},
        {{"Linker / UFPEX 1.23A", 0xcc240, true, {0xfa353a3f, 0x80a4369c}}, 0x52fb6b25, 0x3f9cccbb},
        {{"Linker / UFPEX 1.23A", 0xcc240, true, {0x4ec67bc7, 0xc9f07325, 0x65f6a24a}}, 0x52fb6b25, 0x45deeccb},
        {{"UFPEX 1.23A (16G B)", 0xcc640, true, {0x08c16b0d}}, 0x52fb6b25, 0xbd08d9a8},
        {{"UFPEX 1.23A (1G)", 0xcc440, true, {0x99effc8e}}, 0x52fb6b25, 0xb2e4fa9d},
        {{"UFPEX 1.23A (128K)", 0xcc640, true, {0x29694a6c, 0x15106f66, 0xae0a577c}}, 0x52fb6b25, 0x3731a19a},
        {{"UFPEX 1.23A (8G B)", 0xcc440, true, {0xde8577ac}}, 0x52fb6b25, 0xca1bd5db},
        {{"uDisk 1.23 family", 0xcc240, true, {0x06ef0114, 0x002dbf65, 0x909c5699}}, 0x52fb6b25, 0x945c8219},
        {{"uDisk 1.23 family", 0xcc240, true, {0x69c14414, 0x3e85040f}}, 0x52fb6b25, 0x56666099},
        {{"uDisk 1.23 family", 0xcc640, true,
          {0xe6127417, 0x649d01f2, 0x791a9e77, 0x521d0c43, 0xd09279a6, 0xcd15e623}}, 0x52fb6b25, 0xdd5d3138},
        {{"uDisk 1.23 family", 0xcc240, true, {0x8ab0fd5c, 0x7d201a10, 0x71cc1e0a}}, 0x52fb6b25, 0x522e3131},
        {{"Fire Link 1.23o", 0x118240, true, {0}}, 0x52fb6b25, 0x87e45870},
        {{"Early Fire Link dump", 0x6ec40, true, {0}}, 0x52fb6b25, 0xcdf180ec},
        {{"UFPEX 1.21H", 0x70a40, true, {0xdbba7a69, 0x8ee5adda}}, 0x52fb6b25, 0x509f0714},
        {{"uDisk 1.33 beta", 0x8fc40, false, {0xe053174e}}, 0x7b89635b, 0x146d52b3},
        {{"uDisk 1.41 / Linker 1.40", 0x90440, false, {0x992a7515, 0xebc41b7a}}, 0x2b48d9e9, 0x7c908e1e},
        {{"uDisk 1.43", 0x91e40, false, {0xa9832196}}, 0xdf310827, 0x67634b4e},
        {{"uDisk 1.44", 0x91c40, false, {0x0703601d}}, 0x4d9daf2c, 0xc3ec801b},
    };
    uint8_t sector[SECTOR];
    if (!read || (table != 0x8000 && table != MAP_ADDRESS) || table / SECTOR >= physical_sectors ||
        !read(context, table / SECTOR - 1, sector) ||
        !memcmp(sector + SECTOR - 4, "QAFE", 4) ||
        !read_mapped(read, context, physical_sectors, table, 0, sector)) return NULL;
    const uint32_t header_crc = crc_of(sector);
    uint32_t second_crc = 0;
    bool have_second = false;
    for (unsigned i = 0; i < sizeof(known) / sizeof(*known); ++i) {
        if (known[i].header_crc != header_crc) continue;
        if (!have_second) {
            if (!read_mapped(read, context, physical_sectors, table, 0x8000, sector)) return NULL;
            second_crc = crc_of(sector);
            have_second = true;
        }
        if (second_crc == known[i].second_crc) return &known[i].archive;
    }
    return NULL;
}
