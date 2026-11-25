#include "format-native.h"
#include "runtime.h"
#include "updater.h"

#include <stdlib.h>
#include <string.h>

enum {
    PAGE = UDISK_NATIVE_PAGE,
    RECORD = UDISK_NATIVE_RECORD,
    SECTOR = 512,
    FAT_PREFIX_BYTES = 0x40000,
    BLOCKS_PER_ZONE = 64,
    LOGICAL_PER_ZONE = 62,
};

struct format_work {
    uint8_t bitmap[UDISK_FTL_BAD_BITMAP_BYTES];
    uint8_t table[UDISK_FTL_TABLE_BYTES];
    uint8_t fat_work[UDISK_FORMAT_WORKSPACE_BYTES];
    uint8_t fat[FAT_PREFIX_BYTES];
    uint8_t page[RECORD], verify[RECORD];
    uint32_t used_sectors;
};

static bool erased(const uint8_t *bytes, size_t count)
{
    while (count--) if (*bytes++ != 0xff) return false;
    return true;
}

static int spare_marker(const uint8_t *spare)
{
    if (erased(spare, UDISK_NATIVE_SPARE)) return -1;
    unsigned marker = spare[11];
    if (marker != spare[15] && spare[7] == spare[15]) marker = spare[7];
    return ((spare[0] == 0x55 || spare[2] == 0x55) &&
            (spare[1] == 0xaa || spare[3] == 0xaa) && marker != 0xff) ? (int)marker : -2;
}

static void build_record(uint8_t record[RECORD], const uint8_t *data, size_t bytes, uint8_t marker)
{
    memset(record, 0xff, RECORD);
    if (bytes) memcpy(record, data, bytes);
    uint8_t *spare = record + PAGE;
    spare[0] = spare[2] = 0x55;
    spare[1] = spare[3] = 0xaa;
    spare[7] = spare[11] = spare[15] = marker;
    for (unsigned sector = 0; sector < PAGE / SECTOR; ++sector) {
        uint8_t ecc[3];
        udisk_payload_ecc(record + sector * SECTOR, ecc);
        uint8_t *out = spare + sector * 16;
        memcpy(out + 4, ecc, 3);
        memcpy(out + 8, ecc, 3);
        memcpy(out + 12, ecc, 3);
    }
}

static bool stage_fat_write(void *opaque, uint32_t lba, uint32_t count, const uint8_t *data)
{
    struct format_work *w = opaque;
    if (lba >= FAT_PREFIX_BYTES / SECTOR || count > FAT_PREFIX_BYTES / SECTOR - lba) return false;
    memcpy(w->fat + lba * SECTOR, data, count * SECTOR);
    if (w->used_sectors < lba + count) w->used_sectors = lba + count;
    return true;
}

static void progress(const struct udisk_native_format_io *io, struct udisk_native_format_report *r,
                     enum udisk_native_format_stage stage, uint32_t done, uint32_t total)
{
    r->stage = stage;
    if (io->progress) io->progress(io->context, stage, done, total);
}

static bool read_bad_bitmap(const struct udisk_native_format_io *io, struct format_work *w,
                            const struct udisk_ftl_metadata *m, uint32_t step, bool merge)
{
    memset(w->bitmap, 0, sizeof(w->bitmap));
    for (uint32_t at = m->bad_block_begin; at < m->physical_table_begin; at += step) {
        const uint32_t page = at / PAGE;
        if (!io->read(io->context, page, w->page)) return false;
        if (spare_marker(w->page + PAGE) != 0xaa) continue;
        if (!io->read(io->context, page + 1, w->verify)) return false;
        if (spare_marker(w->verify + PAGE) != 0xaa) {
            if (merge) return false;
            continue;
        }
        if (!merge) {
            memcpy(w->bitmap, w->page, PAGE);
            memcpy(w->bitmap + PAGE, w->verify, PAGE);
            break;
        }
        for (unsigned i = 0; i < PAGE; ++i) {
            w->bitmap[i] |= w->page[i];
            w->bitmap[PAGE + i] |= w->verify[i];
        }
    }
    return true;
}

static bool program_and_read(const struct udisk_native_format_io *io, struct format_work *w,
                             struct udisk_native_format_report *r, uint32_t page)
{
    r->page = page;
    return io->program(io->context, page, w->page) && io->read(io->context, page, w->verify) &&
           !memcmp(w->page, w->verify, RECORD);
}

struct format_metadata {
    uint32_t begin, end, bytes;
    uint8_t marker;
    const uint8_t *data;
};

enum metadata_slot { SLOT_SKIP, SLOT_OK, SLOT_STOP };

static enum metadata_slot check_metadata_slot(const struct udisk_native_format_io *io,
    struct format_work *w, uint32_t page, uint32_t pages, const struct format_metadata *m,
    bool *active)
{
    *active = false;
    for (uint32_t i = 0; i < pages; ++i) {
        if (!io->read(io->context, page + i, w->verify)) return SLOT_STOP;
        int mark = spare_marker(w->verify + PAGE);
        if (!i) *active = mark == m->marker;
        if (erased(w->verify, RECORD)) continue;
        if (*active && i < m->bytes / PAGE && mark == m->marker) continue;
        /* Never erase a block with unrelated data, or publish behind a
           possibly active record that we can't safely replace. */
        return *active ? SLOT_STOP : SLOT_SKIP;
    }
    return SLOT_OK;
}

static bool metadata_space_available(const struct udisk_native_format_io *io, struct format_work *w,
                                     const struct format_metadata *m, uint32_t step,
                                     struct udisk_native_format_report *r)
{
    for (uint32_t at = m->begin; at < m->end; at += step) {
        bool active;
        r->page = at / PAGE;
        enum metadata_slot result = check_metadata_slot(io, w, r->page, step / PAGE, m, &active);
        if (result != SLOT_SKIP) return result == SLOT_OK;
    }
    return false;
}

static bool publish_metadata(const struct udisk_native_format_io *io, struct format_work *w,
                             const struct format_metadata *m, uint32_t step,
                             struct udisk_native_format_report *r, uint32_t *published)
{
    for (uint32_t at = m->begin; at < m->end; at += step) {
        const uint32_t page = at / PAGE;
        r->page = page;
        bool active;
        enum metadata_slot result = check_metadata_slot(io, w, page, step / PAGE, m, &active);
        if (result == SLOT_STOP) return false;
        if (result == SLOT_SKIP) continue;
        if (!io->erase(io->context, page)) {
            if (active) return false;
            continue;
        }

        bool ok = true;
        for (uint32_t i = 0; i < m->bytes / PAGE; ++i) {
            build_record(w->page, m->data + i * PAGE, PAGE, m->marker);
            if (!program_and_read(io, w, r, page + i)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            *published = at;
            return true;
        }
        if (!io->erase(io->context, page) || !io->read(io->context, page, w->verify) ||
            !erased(w->verify, RECORD))
            return false;
    }
    return false;
}

enum block_result { BLOCK_GOOD, BLOCK_BAD, BLOCK_IO_ERROR };

static enum block_result verify_erased_block(const struct udisk_native_format_io *io,
                                             struct format_work *w, uint32_t first,
                                             uint32_t pages, struct udisk_native_format_report *r)
{
    for (uint32_t i = 0; i < pages; ++i) {
        r->page = first + i;
        if (!io->read(io->context, r->page, w->verify)) return BLOCK_IO_ERROR;
        if (!erased(w->verify, RECORD)) return BLOCK_BAD;
    }
    return BLOCK_GOOD;
}

static void scratch_record(uint8_t *record, uint32_t row)
{
    /* Row-dependent checkerboard with both bit states. Leave the first
       spare record (including factory bad and FTL markers) untouched. */
    uint32_t value = (row << 1) ^ ((row & 1) ? 0x55aa55aau : 0xaa55aa55u);
    for (unsigned i = 0; i < RECORD; i += 4) {
        uint32_t word = value ^ (i * 0x01010101u);
        for (unsigned j = 0; j < 4; ++j) record[i + j] = word >> (j * 8);
    }
    memset(record + PAGE, 0xff, 16);
}

static enum block_result failed_mutation(const struct udisk_native_format_io *io,
                                         struct format_work *w, uint32_t page)
{
    /* Distinguish a failed erase/program from a lost read path. */
    return io->read(io->context, page, w->verify) ? BLOCK_BAD : BLOCK_IO_ERROR;
}

static enum block_result stress_block(const struct udisk_native_format_io *io,
                                      struct format_work *w, uint32_t first, uint32_t pages,
                                      struct udisk_native_format_report *r)
{
    r->page = first;
    if (!io->erase(io->context, first)) return failed_mutation(io, w, first);
    enum block_result result = verify_erased_block(io, w, first, pages, r);
    if (result != BLOCK_GOOD) return result;
    for (uint32_t i = 0; i < pages; ++i) {
        r->page = first + i;
        scratch_record(w->page, r->page);
        if (!io->program(io->context, r->page, w->page))
            return failed_mutation(io, w, r->page);
    }
    for (uint32_t i = 0; i < pages; ++i) {
        r->page = first + i;
        scratch_record(w->page, r->page);
        if (!io->read(io->context, r->page, w->verify)) return BLOCK_IO_ERROR;
        if (memcmp(w->page, w->verify, RECORD)) return BLOCK_BAD;
    }
    r->page = first;
    if (!io->erase(io->context, first)) return failed_mutation(io, w, first);
    return verify_erased_block(io, w, first, pages, r);
}

static bool format(const struct udisk_native_format_io *io, uint32_t chip_sectors,
                   uint32_t erase_pages, struct udisk_native_format_report *r, bool full)
{
    if (!r) return false;
    *r = (struct udisk_native_format_report){.stage = UDISK_FORMAT_CHECK};
    if (!io || !io->read || !io->erase || !io->program || (erase_pages != 64 && erase_pages != 128))
        return false;
    const uint32_t erase_bytes = erase_pages * PAGE, block_sectors = erase_bytes / SECTOR;
    const uint32_t base = erase_pages == 128 ? 0x1000000 : 0x800000;
    struct udisk_ftl_metadata metadata;
    if (chip_sectors > 0x400000 || chip_sectors <= base / SECTOR ||
        (chip_sectors - base / SECTOR) % block_sectors ||
        !udisk_ftl_metadata_ranges(base, erase_bytes, &metadata))
        return false;
    const uint32_t blocks = (chip_sectors - base / SECTOR) / block_sectors;
    if (blocks < BLOCKS_PER_ZONE || blocks >= UDISK_FTL_TABLE_BYTES / 2) return false;

    struct format_work *w = malloc(sizeof(*w));
    if (!w) return false;
    bool ok = false;

    const struct format_metadata bitmap = {
        metadata.bad_block_begin, metadata.physical_table_begin, sizeof(w->bitmap), 0xaa, w->bitmap
    };
    const struct format_metadata map = {
        metadata.physical_table_begin, metadata.data_begin, sizeof(w->table), 0xab, w->table
    };
    if (!metadata_space_available(io, w, &map, erase_bytes, r) ||
        (full && !metadata_space_available(io, w, &bitmap, erase_bytes, r))) goto done;

    progress(io, r, UDISK_FORMAT_BITMAP, 0, 0);
    if (!read_bad_bitmap(io, w, &metadata, erase_bytes, full)) goto done;

    enum udisk_native_format_stage block_stage = full ? UDISK_FORMAT_TEST : UDISK_FORMAT_ERASE;
    progress(io, r, block_stage, 0, blocks);
    for (uint32_t i = 0; i < blocks; ++i) {
        const uint32_t page = base / PAGE + i * erase_pages;
        const uint32_t last = page + erase_pages - 1;
        const uint8_t bit = 1u << (i & 7);
        if (w->bitmap[i / 8] & bit) {
            ++r->known_bad_blocks;
        } else {
            r->page = page;
            if (!io->read(io->context, page, w->page) ||
                !io->read(io->context, last, w->verify)) goto done;
            if (spare_marker(w->page + PAGE) == -2 || spare_marker(w->verify + PAGE) == -2) {
                w->bitmap[i / 8] |= bit;
                ++r->known_bad_blocks;
            } else if (full) {
                ++r->tested_blocks;
                enum block_result tested = stress_block(io, w, page, erase_pages, r);
                if (tested == BLOCK_IO_ERROR) goto done;
                if (tested == BLOCK_BAD) w->bitmap[i / 8] |= bit;
            } else if (!io->erase(io->context, page)) {
                w->bitmap[i / 8] |= bit;
            } else {
                if (!io->read(io->context, page, w->page) ||
                    !io->read(io->context, last, w->verify)) goto done;
                if (!erased(w->page, RECORD) || !erased(w->verify, RECORD)) w->bitmap[i / 8] |= bit;
            }
        }
        if (w->bitmap[i / 8] & bit) ++r->bad_blocks;
        if ((i + 1) % (full ? 8 : 64) == 0 || i + 1 == blocks)
            progress(io, r, block_stage, i + 1, blocks);
    }

    progress(io, r, UDISK_FORMAT_FAT, 0, 0);
    if (!udisk_prepare_physical_table(base, erase_bytes, (uint16_t)blocks,
                                      w->bitmap, w->table, &r->sectors)) goto done;
    w->used_sectors = 0;
    memset(w->fat, 0xff, sizeof(w->fat));
    const struct udisk_format_io fat_io = {w, stage_fat_write};
    if (!udisk_format_volume(&fat_io, r->sectors, w->fat_work)) goto done;

    /* Create the first logical blocks on clean physical blocks in each zone.
       nrio's standard reader discovers these from their first and last page
       markers. The sector-base reservation is 62 pages, in sector units. */
    const uint32_t sector_base = LOGICAL_PER_ZONE * erase_pages;
    const uint32_t first_block = sector_base / block_sectors;
    const uint32_t last_block = (sector_base + w->used_sectors - 1) / block_sectors;
    for (uint32_t logical = first_block; logical <= last_block; ++logical) {
        const uint32_t zone = logical / LOGICAL_PER_ZONE, marker = logical % LOGICAL_PER_ZONE;
        const uint32_t entry = 2 * (zone * BLOCKS_PER_ZONE + marker);
        const uint32_t address = (uint32_t)w->table[entry] << 24 | (uint32_t)w->table[entry + 1] << 16;
        if (address < base || address / SECTOR >= chip_sectors || address % erase_bytes) goto done;
        for (uint32_t i = 0; i < erase_pages; ++i) {
            uint32_t at = logical * block_sectors + i * (PAGE / SECTOR);
            uint32_t lba = at >= sector_base ? at - sector_base : UINT32_MAX;
            uint32_t count = lba < w->used_sectors ? w->used_sectors - lba : 0;
            if (count > PAGE / SECTOR) count = PAGE / SECTOR;
            const uint8_t *data = count ? w->fat + lba * SECTOR : NULL;
            build_record(w->page, data, count * SECTOR, (uint8_t)marker);
            if (!program_and_read(io, w, r, address / PAGE + i)) goto done;
        }
    }

    if (full) {
        progress(io, r, UDISK_FORMAT_SAVE_BITMAP, 0, 0);
        if (!publish_metadata(io, w, &bitmap, erase_bytes, r, &r->bitmap_address)) goto done;
    }
    progress(io, r, UDISK_FORMAT_MAP, 0, 0);
    if (!publish_metadata(io, w, &map, erase_bytes, r, &r->map_address)) goto done;
    progress(io, r, UDISK_FORMAT_COMPLETE, 0, 0);
    ok = true;
done:
    free(w);
    return ok;
}

bool udisk_native_quick_format(const struct udisk_native_format_io *io, uint32_t chip_sectors,
                               uint32_t erase_pages, struct udisk_native_format_report *r)
{
    return format(io, chip_sectors, erase_pages, r, false);
}

bool udisk_native_full_format(const struct udisk_native_format_io *io, uint32_t chip_sectors,
                              uint32_t erase_pages, struct udisk_native_format_report *r)
{
    return format(io, chip_sectors, erase_pages, r, true);
}
