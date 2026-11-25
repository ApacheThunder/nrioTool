#include "nand-health.h"
#include "udisk/updater.h"
#include "udisk/runtime.h"

#include <string.h>

enum { PAGE = 2048, SPARE = 64, SECTOR = 512, BOOT_END = 0x200000 };

static int marker(const uint8_t spare[SPARE])
{
    unsigned i = 0;
    while (i < SPARE && spare[i] == 0xff) ++i;
    if (i == SPARE) return -1;
    unsigned tag = spare[11];
    if (tag != spare[15] && spare[7] == spare[15]) tag = spare[7];
    if ((spare[0] == 0x55 || spare[2] == 0x55) &&
        (spare[1] == 0xaa || spare[3] == 0xaa) && tag != 0xff) return (int)tag;
    return -2;
}

static unsigned read_with_retry(bool (*read)(void *, uint32_t, uint8_t *),
                                 void *ctx, uint32_t page, uint8_t *data)
{
    if (read(ctx, page, data)) return 0;
    for (unsigned i = 0; i < 2; ++i)
        if (read(ctx, page, data)) return NRIO_HEALTH_RETRIED;
    return NRIO_HEALTH_IO;
}

static unsigned scan_page(const struct nrio_health_io *io, uint32_t page,
                          bool data_area, bool main_sample)
{
    uint8_t spare[SPARE], data[PAGE];
    unsigned result = read_with_retry(io->read_spare, io->context, page, spare);
    int kind = result & NRIO_HEALTH_IO ? -1 : marker(spare);
    if (data_area && kind == -2) result |= NRIO_HEALTH_SPARE;
    if (main_sample || (data_area && kind >= 0)) {
        unsigned main = read_with_retry(io->read_main, io->context, page, data);
        result |= main;
        if (data_area && kind >= 0 && !(main & NRIO_HEALTH_IO)) {
            for (unsigned c = 0; c < PAGE / SECTOR; ++c) {
                const uint8_t *tags = spare + c * 16;
                uint8_t ecc[3];
                udisk_payload_ecc(data + c * SECTOR, ecc);
                /* Three identical copies identify a usable stored tag.
                   Mixed or absent tags are not called bad data here. */
                if (!memcmp(tags + 4, tags + 8, 3) &&
                    !memcmp(tags + 4, tags + 12, 3) &&
                    (tags[4] != 0xff || tags[5] != 0xff || tags[6] != 0xff) &&
                    memcmp(tags + 4, ecc, 3))
                    result |= NRIO_HEALTH_ECC;
            }
        }
    }
    return result;
}

static void read_bitmap(const struct nrio_health_io *io,
                        const struct udisk_ftl_metadata *meta, uint32_t step,
                        uint8_t *bits, struct nrio_health_report *r)
{
    uint8_t spare[SPARE];
    bool missing_reads = false;
    for (uint32_t at = meta->bad_block_begin; at < meta->physical_table_begin; at += step) {
        uint32_t page = at / PAGE;
        unsigned first = read_with_retry(io->read_spare, io->context, page, spare);
        if (first & NRIO_HEALTH_IO) { missing_reads = true; continue; }
        if (marker(spare) != 0xaa) continue;
        unsigned second = read_with_retry(io->read_spare, io->context, page + 1, spare);
        if (second & NRIO_HEALTH_IO) { missing_reads = true; continue; }
        if (marker(spare) != 0xaa) continue;
        first = read_with_retry(io->read_main, io->context, page, bits);
        second = read_with_retry(io->read_main, io->context, page + 1, bits + PAGE);
        if ((first | second) & NRIO_HEALTH_IO) { missing_reads = true; continue; }
        r->bitmap = NRIO_BITMAP_FOUND;
        r->bitmap_address = at;
        return;
    }
    r->bitmap = missing_reads ? NRIO_BITMAP_READ_ERROR : NRIO_BITMAP_ABSENT;
}

bool nrio_scan_nand_health(const struct nrio_health_io *io, uint32_t sectors,
                           unsigned erase_pages, struct nrio_health_report *r)
{
    if (!r) return false;
    *r = (struct nrio_health_report){0};
    if (!io || !io->read_main || !io->read_spare ||
        (erase_pages != 64 && erase_pages != 128) ||
        !sectors || sectors > 0x400000) return false;
    uint32_t step = erase_pages * PAGE, sectors_per_block = step / SECTOR;
    uint32_t base = erase_pages == 128 ? 0x1000000 : 0x800000;
    struct udisk_ftl_metadata meta;
    if (sectors <= base / SECTOR || sectors % sectors_per_block ||
        !udisk_ftl_metadata_ranges(base, step, &meta)) return false;
    r->total = sectors / sectors_per_block;
    uint8_t bits[UDISK_FTL_BAD_BITMAP_BYTES];
    if (r->total - base / step > sizeof(bits) * 8) return false;
    read_bitmap(io, &meta, step, bits, r);

    for (uint32_t block = 0; block < r->total; ++block) {
        if (io->progress && !io->progress(io->context, block, r->total)) {
            r->cancelled = true;
            return true;
        }
        uint32_t address = block * step, first = block * erase_pages;
        enum nrio_health_area area = address < BOOT_END ? NRIO_HEALTH_BOOT :
            address < meta.bad_block_begin ? NRIO_HEALTH_RESERVED :
            address < base ? NRIO_HEALTH_METADATA : NRIO_HEALTH_DATA;
        bool data_area = area == NRIO_HEALTH_DATA;
        uint32_t bit = data_area ? (address - base) / step : 0;
        bool listed = data_area && r->bitmap == NRIO_BITMAP_FOUND &&
                      (bits[bit / 8] & (1u << (bit % 8)));
        unsigned flags = listed ? NRIO_HEALTH_LISTED : 0;
        /* Don't probe known bad data blocks. Never interpret proprietary
           boot spare contents as a factory marker. Read every page of
           block 0, then sample the ends of each other block. */
        if (!listed) {
            if (block == 0) {
                for (unsigned p = 0; p < erase_pages; ++p)
                    flags |= scan_page(io, first + p, false, true);
            } else {
                flags |= scan_page(io, first, data_area, !data_area);
                flags |= scan_page(io, first + erase_pages - 1, data_area, !data_area);
            }
        }
        if (flags) {
            r->listed += (flags & NRIO_HEALTH_LISTED) != 0;
            r->spare += (flags & NRIO_HEALTH_SPARE) != 0;
            r->ecc += (flags & NRIO_HEALTH_ECC) != 0;
            r->io += (flags & NRIO_HEALTH_IO) != 0;
            r->retried += (flags & NRIO_HEALTH_RETRIED) != 0;
            if (flags & ~NRIO_HEALTH_LISTED) {
                r->boot_issues += area == NRIO_HEALTH_BOOT;
                r->data_issues += data_area;
            }
            if (io->event) io->event(io->context, address, area, flags);
        }
        ++r->scanned;
    }
    return true;
}


bool nrio_recheck_nand_block(const struct nrio_health_io *io, uint32_t sectors,
                              unsigned erase_pages, uint32_t address,
                              enum nrio_health_area area, unsigned *flags)
{
    if (!io || !flags || !io->read_spare || !io->read_main ||
        (erase_pages != 64 && erase_pages != 128) ||
        address % (erase_pages * PAGE) ||
        (uint64_t)address / SECTOR + erase_pages * (PAGE / SECTOR) > sectors)
        return false;
    *flags = 0;
    for (unsigned p = 0; p < erase_pages; ++p) {
        if (io->progress && !io->progress(io->context, p, erase_pages)) return false;
        *flags |= scan_page(io, address / PAGE + p, area == NRIO_HEALTH_DATA, true);
    }
    return true;
}
