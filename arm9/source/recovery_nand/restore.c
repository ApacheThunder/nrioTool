#include "raw_nand.h"
#include "banner_plan.h"
#include "recovery.h"
#include "../../source/card/nand_ecc.h"

static u32 load_word(const u8 *in)
{
    return in[0] | ((u32)in[1] << 8) | ((u32)in[2] << 16) | ((u32)in[3] << 24);
}

static void regenerate_ecc(u8 *record, u32 column)
{
    const u8 *data = record + column * SECTOR_SIZE;
    u32 all = 0, bit0 = 0, bit1 = 0, bit2 = 0, row = 0;
    bool odd = false;
    for (u32 group = 0; group < 16; ++group) {
        u32 parity = 0;
        for (u32 i = 0; i < 8; ++i) {
            u32 word = load_word(data);
            data += 4;
            parity ^= word;
            if (i & 1) bit0 ^= word;
            if (i & 2) bit1 ^= word;
            if (i & 4) bit2 ^= word;
        }
        all ^= parity;
        if (odd_parity(parity)) {
            row ^= group;
            odd = !odd;
        }
    }

    u8 *spare = record + PAGE_SIZE + column * 16;
    finish_ecc(spare + 8, all, bit0, bit1, bit2, row, odd);
    for (u32 i = 0; i < 3; ++i) {
        spare[4 + i] = spare[8 + i];
        spare[12 + i] = spare[8 + i];
    }
}

// Patched offline in the shipped template. Keep the plan local for PC-relative access.
static volatile struct nrio_banner_plan nrio_banner_plan = {
    .signature = "NRIO-BANNER-v1",
};

bool banner_is_armed(void)
{
    return nrio_banner_plan.armed == 1;
}

static bool read_page(u32 page, u8 *record)
{
    if (!nand_load_page(page, false)) {
        return false;
    }
    for (u32 i = 0; i < SECTORS_PER_PAGE; ++i) {
        if (!nand_read_sector(i, record + i * SECTOR_SIZE)) {
            return false;
        }
    }
    return nand_load_page(page, true) && nand_read_spare(record + PAGE_SIZE);
}

static bool equal(const u8 *a, const volatile u8 *b, u32 size)
{
    for (u32 i = 0; i < size; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static bool erased(const u8 *record)
{
    for (u32 i = 0; i < PAGE_RECORD_SIZE; ++i) {
        if (record[i] != 0xff) return false;
    }
    return true;
}

static bool fail_write(struct nrio_raw_write_error *error, u32 stage, u32 page, u32 detail)
{
    error->stage = stage;
    error->page = page;
    error->detail = detail;
    return false;
}

static bool erased_bytes(const u8 *data, u32 size)
{
    for (u32 i = 0; i < size; ++i)
        if (data[i] != 0xff) return false;
    return true;
}

static bool looks_erased_padding(const u8 *record)
{
    // In captured faulty padding the first half and spare were entirely FF.
    // The second half showed sparse, isolated cleared bits.
    if (!erased_bytes(record, PAGE_SIZE / 2) || !erased_bytes(record + PAGE_SIZE, SPARE_SIZE))
        return false;
    unsigned total = 0;
    for (u32 i = PAGE_SIZE / 2; i < PAGE_SIZE; ++i) {
        u8 zero_bits = (u8)~record[i];
        unsigned count = 0;
        while (zero_bits) {
            zero_bits &= zero_bits - 1;
            ++count;
        }
        total += count;
        if (count > 2 || total > 80) return false;
    }
    return true;
}

static bool program_and_verify_block(u32 page, u32 pages, const u8 *records,
                                     bool leave_padding_erased, struct nrio_raw_write_error *error)
{
    if (!nand_erase_block(page))
        return fail_write(error, NRIO_RAW_WRITE_ERASE, page, 0);
    for (u32 i = 0; i < pages; ++i) {
        const u8 *record = records + i * PAGE_RECORD_SIZE;
        if (!erased(record) && !nand_program_page(page + i, record))
            return fail_write(error, NRIO_RAW_WRITE_PROGRAM, page + i, 0);
    }

    u8 verified[PAGE_RECORD_SIZE];
    for (u32 i = 0; i < pages; ++i) {
        if (!read_page(page + i, verified))
            return fail_write(error, NRIO_RAW_WRITE_VERIFY_READ, page + i, 0);
        if (leave_padding_erased && i == NRIO_RAW_PADDING_PAGE) {
            if (!looks_erased_padding(verified))
                return fail_write(error, NRIO_RAW_WRITE_VERIFY_DATA, page + i, 0);
        } else if (!equal(verified, records + i * PAGE_RECORD_SIZE, PAGE_RECORD_SIZE)) {
            return fail_write(error, NRIO_RAW_WRITE_VERIFY_DATA, page + i, 0);
        }
    }
    return true;
}

bool read_block_snapshot(u32 page, u32 pages, u8 *records, struct nrio_raw_write_error *error)
{
    for (u32 i = 0; i < pages; ++i) {
        if (!read_page(page + i, records + i * PAGE_RECORD_SIZE))
            return fail_write(error, NRIO_RAW_WRITE_READ_BLOCK, page + i, 0);
    }
    return true;
}

static bool check_snapshot_page(u32 page, u8 *record, const u8 *expected,
                                struct nrio_raw_write_error *error)
{
    if (equal(record, expected, PAGE_RECORD_SIZE)) return true;

    // A mismatching attempt breaks the streak but need not exhaust the retries.
    unsigned consecutive_matches = 0;
    u32 mismatch = 0;
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        if (!equal(record, expected, PAGE_RECORD_SIZE)) {
            consecutive_matches = 0;
            for (u32 i = 0; i < PAGE_RECORD_SIZE; ++i) {
                if (record[i] != expected[i]) {
                    mismatch = i << 16 | (u32)expected[i] << 8 | record[i];
                    break;
                }
            }
        } else if (++consecutive_matches == 2) {
            return true;
        }

        if (attempt == 4) break;
        if (!read_page(page, record))
            return fail_write(error, NRIO_RAW_WRITE_READ_BLOCK, page, 0);
    }
    return fail_write(error, NRIO_RAW_WRITE_SNAPSHOT_READBACK, page, mismatch);
}

bool patch_block_snapshot(u32 page, u32 pages, u8 *records, const u8 *snapshot,
                          const struct nrio_sector_patch *patches, u32 count, bool install,
                          bool leave_padding_erased, struct nrio_raw_write_error *error)
{
    if (!read_block_snapshot(page, pages, records, error)) return false;
    for (u32 i = 0; i < pages; ++i) {
        u8 *record = records + i * PAGE_RECORD_SIZE;
        const u8 *saved = snapshot + i * PAGE_RECORD_SIZE;
        if (leave_padding_erased && i == NRIO_RAW_PADDING_PAGE) {
            if (!looks_erased_padding(saved) || !looks_erased_padding(record))
                return fail_write(error, NRIO_RAW_WRITE_MARKER, page + i, 0);
            for (u32 j = 0; j < PAGE_RECORD_SIZE; ++j) record[j] = 0xff;
        } else if (!check_snapshot_page(page + i, record, saved, error)) {
            return false;
        }
        u8 marker = record[PAGE_SIZE];
        if (page && i < 2 && marker != 0xff && marker != 0x55)
            return fail_write(error, NRIO_RAW_WRITE_MARKER, page + i, marker);
    }

    bool changed = false;
    for (u32 i = 0; i < count; ++i) {
        u32 index = patches[i].lba - page * SECTORS_PER_PAGE;
        u32 row = index / SECTORS_PER_PAGE, column = index % SECTORS_PER_PAGE;
        u8 *record = records + row * PAGE_RECORD_SIZE;
        u8 *data = record + column * SECTOR_SIZE;
        if (patches[i].expected && !equal(data, patches[i].expected, SECTOR_SIZE))
            return fail_write(error, NRIO_RAW_WRITE_SNAPSHOT, page + row, column);
        if (equal(data, patches[i].data, SECTOR_SIZE)) continue;
        u8 *spare = record + PAGE_SIZE + column * 16;
        if (page + row < 8)
            return fail_write(error, NRIO_RAW_WRITE_MARKER, page + row, column);

        if (!erased_bytes(spare, 16)) {
            u8 saved[16];
            for (u32 j = 0; j < sizeof(saved); ++j) saved[j] = spare[j];
            regenerate_ecc(record, column);
            if (!equal(spare, saved, sizeof(saved)))
                return fail_write(error, NRIO_RAW_WRITE_MARKER, page + row, column);
        }
        for (u32 j = 0; j < SECTOR_SIZE; ++j) data[j] = patches[i].data[j];
        regenerate_ecc(record, column);
        changed = true;
    }
    return !install || !changed || program_and_verify_block(page, pages, records, leave_padding_erased, error);
}

bool patch_udisk_block_snapshot(u32 page, u32 pages, u8 *records, const u8 *snapshot,
                                const struct nrio_sector_patch *patches, u32 count, bool install,
                                struct nrio_raw_write_error *error)
{
    enum { MAP = 0x40000, MAP_SIZE = 0x20000, RUNTIME = 0x80000, RUNTIME_END = 0x140000 };
    if ((pages != 64 && pages != 128) || page % pages || page > RUNTIME_END / PAGE_SIZE)
        return false;
    u32 address = page * PAGE_SIZE, erase_bytes = pages * PAGE_SIZE;
    bool map = address == MAP && count == MAP_SIZE / SECTOR_SIZE;
    bool payload = address >= RUNTIME && address < RUNTIME_END &&
                   erase_bytes <= RUNTIME_END - address && count == erase_bytes / SECTOR_SIZE;
    if (!map && !payload) return false;
    for (u32 i = 0; i < count; ++i)
        if (patches[i].lba != page * SECTORS_PER_PAGE + i || !patches[i].data)
            return false;

    if (!read_block_snapshot(page, pages, records, error)) return false;
    bool changed = false;
    for (u32 row = 0; row < pages; ++row) {
        u8 *record = records + row * PAGE_RECORD_SIZE;
        const u8 *saved = snapshot + row * PAGE_RECORD_SIZE;
        if (!check_snapshot_page(page + row, record, saved, error)) return false;
        if (row >= count / SECTORS_PER_PAGE) continue;

        for (u32 column = 0; column < SECTORS_PER_PAGE; ++column) {
            const struct nrio_sector_patch *patch = patches + row * SECTORS_PER_PAGE + column;
            u8 *data = record + column * SECTOR_SIZE;
            u8 *spare = record + PAGE_SIZE + column * 16;
            if (patch->expected && !equal(data, patch->expected, SECTOR_SIZE))
                return fail_write(error, NRIO_RAW_WRITE_SNAPSHOT, page + row, column);

            /* The stock BIOS writer sends only the four main sectors. Accept
               erased spare or exactly the generic ECC written by our old repair
               tool. Do not erase other markers or bad-block information. */
            if (!erased_bytes(spare, 16)) {
                u8 before[16];
                for (u32 i = 0; i < sizeof(before); ++i) before[i] = spare[i];
                if (!erased_bytes(before, 4) || before[7] != 0xff ||
                    before[11] != 0xff || before[15] != 0xff)
                    return fail_write(error, NRIO_RAW_WRITE_MARKER, page + row, column);
                regenerate_ecc(record, column);
                if (!equal(spare, before, sizeof(before)))
                    return fail_write(error, NRIO_RAW_WRITE_MARKER, page + row, column);
                for (u32 i = 0; i < 16; ++i) spare[i] = 0xff;
                changed = true;
            }
            if (!equal(data, patch->data, SECTOR_SIZE)) {
                for (u32 i = 0; i < SECTOR_SIZE; ++i) data[i] = patch->data[i];
                changed = true;
            }
        }
    }
    return !install || !changed || program_and_verify_block(page, pages, records, false, error);
}

static bool rewrite_block(u32 page, u32 pages, u32 offset, u32 count,
                          const u8 *input, u8 *records, struct nrio_raw_write_error *error,
                          enum nrio_raw_mode mode, const u8 *expected_spare)
{
    bool spare = mode == NRIO_RAW_SPARE_WRITE;
    u32 update_size = spare ? SPARE_SIZE : SECTOR_SIZE;
    u32 update_offset = spare ? PAGE_SIZE : 0;
    bool banner = mode == NRIO_RAW_BANNER_WRITE;
    if (banner && (page != 0 || offset != BANNER_LBA || count != SECTORS_PER_PAGE ||
                   !equal(input, nrio_banner_plan.replacement_main, PAGE_SIZE))) return false;
    for (u32 i = 0; i < pages; ++i) {
        u8 *record = records + i * PAGE_RECORD_SIZE;
        if (!read_page(page + i, record))
            return fail_write(error, NRIO_RAW_WRITE_READ_BLOCK, page + i, 0);
        if (expected_spare) {
            if (i < SPARES_PER_SECTOR && !equal(record + PAGE_SIZE, expected_spare + i * SPARE_SIZE, SPARE_SIZE))
                return fail_write(error, NRIO_RAW_WRITE_MARKER, page + i, record[PAGE_SIZE]);
            continue;
        }
        if (banner) {
            if (i < BANNER_EXPECTED_PAGES &&
                !equal(record + PAGE_SIZE, nrio_banner_plan.expected_spare[i], SPARE_SIZE))
                return fail_write(error, NRIO_RAW_WRITE_MARKER, page + i, record[PAGE_SIZE]);
            continue;
        }
        // Refuse unfamiliar markers on the first two physical pages.
        u8 marker = record[PAGE_SIZE];
        if (i < 2 && marker != 0xff && marker != 0x55)
            return fail_write(error, NRIO_RAW_WRITE_MARKER, page + i, marker);
    }

    bool changed = false;
    for (u32 i = 0; i < count; ++i) {
        u32 index = offset + i;
        u32 column = spare ? 0 : index % SECTORS_PER_PAGE;
        u32 page_index = spare ? index : index / SECTORS_PER_PAGE;
        u8 *record = records + page_index * PAGE_RECORD_SIZE;
        u8 *data = record + update_offset + column * update_size;
        const u8 *source = input + i * update_size;
        if (!equal(data, source, update_size)) {
            for (u32 j = 0; j < update_size; ++j) data[j] = source[j];
            if (!spare && mode != NRIO_RAW_BANNER_WRITE) regenerate_ecc(record, column);
            changed = true;
        }
    }
    u8 *banner_spare = records + BANNER_PAGE * PAGE_RECORD_SIZE + PAGE_SIZE;
    if (banner && !equal(banner_spare, nrio_banner_plan.replacement_spare, SPARE_SIZE)) {
        for (u32 i = 0; i < SPARE_SIZE; ++i) banner_spare[i] = nrio_banner_plan.replacement_spare[i];
        changed = true;
    }
    if (!changed) return true;

    return program_and_verify_block(page, pages, records, false, error);
}

bool restore_sectors(u32 sector, u32 count, const u8 *buffer, u32 pages_per_block, u8 *workspace,
                     struct nrio_raw_write_error *error, enum nrio_raw_mode mode)
{
    bool spare = mode == NRIO_RAW_SPARE_WRITE;
    u32 updates_per_sector = spare ? SPARES_PER_SECTOR : 1;
    u32 block_sectors = spare ? pages_per_block / SPARES_PER_SECTOR : pages_per_block * SECTORS_PER_PAGE;
    while (count) {
        u32 offset = sector & (block_sectors - 1);
        u32 batch = block_sectors - offset;
        if (batch > count) batch = count;
        u32 first_sector = sector - offset;
        u32 page = spare ? first_sector * SPARES_PER_SECTOR : first_sector / SECTORS_PER_PAGE;
        if (!rewrite_block(page, pages_per_block, offset * updates_per_sector, batch * updates_per_sector,
                           buffer, workspace, error, mode, 0)) return false;
        sector += batch;
        count -= batch;
        buffer += batch * SECTOR_SIZE;
    }
    return true;
}

bool restore_boot_table(const struct nrio_raw_table_plan *plan, u32 pages_per_block, u8 *workspace,
                        struct nrio_raw_write_error *error)
{
    if (plan->expected_spare[0] != 0xff)
        return fail_write(error, NRIO_RAW_WRITE_MARKER, 0, plan->expected_spare[0]);
    if (plan->expected_spare[SPARE_SIZE] != 0)
        return fail_write(error, NRIO_RAW_WRITE_MARKER, 1, plan->expected_spare[SPARE_SIZE]);

    u8 sector[SECTOR_SIZE];
    for (u32 offset = 0; offset < NRIO_RAW_TABLE_BYTES; offset += PAGE_SIZE) {
        u32 page = (NRIO_RAW_SECONDARY_TABLE + offset) / PAGE_SIZE;
        if (!nand_load_page(page, false))
            return fail_write(error, NRIO_RAW_WRITE_TABLE_SOURCE, page, 0);
        for (u32 column = 0; column < SECTORS_PER_PAGE; ++column) {
            if (!nand_read_sector(column, sector) ||
                !equal(sector, plan->table + offset + column * SECTOR_SIZE, SECTOR_SIZE))
                return fail_write(error, NRIO_RAW_WRITE_TABLE_SOURCE, page, column);
        }
    }

    // Both supported erase geometries place the entire primary table in block zero.
    return rewrite_block(0, pages_per_block, NRIO_RAW_PRIMARY_TABLE / SECTOR_SIZE,
                         NRIO_RAW_TABLE_BYTES / SECTOR_SIZE, plan->table, workspace, error,
                         NRIO_RAW_TABLE_REPAIR, plan->expected_spare);
}

// The desktop toolkit fills this fixed-size plan in a copy of the DS app.
static volatile struct {
    u8 signature[16];
    u32 pages;
    u32 sectors[2];
    u8 expected[NRIO_RAW_WORKSPACE_BYTES];
    u8 replacement[2][SECTOR_SIZE];
} safe_mode_plan = {
    .signature = "NRIO-SAFE-v1",
};

static bool live_safe_mode_plan;
static u32 live_restore_pages;
static const u8 *live_restore_backup;
static u32 live_banner_pages;
enum { BANNER_COPIES = 2 };
static bool live_banner_has_ecc[BANNER_COPIES];
static bool live_banner_spare_only;
static u8 live_banner_spare[SPARE_SIZE];
static u8 live_banner[PAGE_SIZE];

bool nrio_recovery_safe_mode_ready(void)
{
    return safe_mode_plan.pages == 64 || safe_mode_plan.pages == 128;
}

uint32_t nrio_recovery_backup_pages(void)
{
    if (live_safe_mode_plan) return safe_mode_plan.pages;
    return live_restore_pages ? live_restore_pages : live_banner_pages;
}

bool nrio_recovery_copy_backup_page(uint32_t page, void *main, void *spare)
{
    if (page >= nrio_recovery_backup_pages() || !main || !spare) return false;
    const volatile u8 *source = safe_mode_plan.expected + page * PAGE_RECORD_SIZE;
    for (unsigned i = 0; i < PAGE_SIZE; ++i) ((u8 *)main)[i] = source[i];
    for (unsigned i = 0; i < SPARE_SIZE; ++i) ((u8 *)spare)[i] = source[PAGE_SIZE + i];
    return true;
}

static const u8 *main_sector(const u8 *records, u32 sector)
{
    return records + sector / SECTORS_PER_PAGE * PAGE_RECORD_SIZE +
           sector % SECTORS_PER_PAGE * SECTOR_SIZE;
}

enum { SAFE_MODE_ROM_BLOCK = 0x4000, SAFE_MODE_MIN_ADDRESS = 0x18000,
       SAFE_MODE_STOCK_RAM = 0x03800000, SAFE_MODE_STAGE_RAM = 0x02380000,
       SAFE_MODE_PAYLOAD_BYTES = 0x38 };

void discard_live_safe_mode(void)
{
    if (live_safe_mode_plan) safe_mode_plan.pages = 0;
    live_safe_mode_plan = false;
    live_banner_pages = 0;
    live_restore_pages = 0;
    live_restore_backup = NULL;
}

static bool safe_mode_locations(const u8 *records, u32 sectors, u32 locations[2])
{
    const u8 *table = main_sector(records, NRIO_RAW_PRIMARY_TABLE / SECTOR_SIZE);
    const u8 *before_table = main_sector(records, NRIO_RAW_PRIMARY_TABLE / SECTOR_SIZE - 1);
    u32 first = load_word(table);
    if (first < SAFE_MODE_MIN_ADDRESS || first / SECTOR_SIZE >= sectors ||
        first % SAFE_MODE_ROM_BLOCK ||
        load_word(before_table + SECTOR_SIZE - 4) == UINT32_C(0x45464151))
        return false;

    u32 header_sector = first / SECTOR_SIZE;
    const u8 *header = main_sector(records, header_sector);
    if (load_word(header) != UINT32_C(0xea00002e) ||
        load_word(header + 0x0c) != UINT32_C(0x23232323) ||
        load_word(header + 0x84) != SECTOR_SIZE ||
        load_word(header + 0x38) != SAFE_MODE_STOCK_RAM ||
        load_word(header + 0x3c) != SAFE_MODE_PAYLOAD_BYTES) return false;

    u32 source = load_word(header + 0x30);
    u32 size = load_word(header + 0x80);
    u32 entry_offset = source / SAFE_MODE_ROM_BLOCK * sizeof(u32);
    if (source < SECTOR_SIZE || source % SECTOR_SIZE || source >= size ||
        size - source < SECTOR_SIZE || entry_offset >= NRIO_RAW_TABLE_BYTES) return false;
    u32 entry_sector = (NRIO_RAW_PRIMARY_TABLE + entry_offset) / SECTOR_SIZE;
    u32 mapped = load_word(main_sector(records, entry_sector) + entry_offset % SECTOR_SIZE);
    if (mapped < SAFE_MODE_MIN_ADDRESS || mapped / SECTOR_SIZE >= sectors ||
        mapped % SAFE_MODE_ROM_BLOCK) return false;

    u32 arm7_sector = (mapped + source % SAFE_MODE_ROM_BLOCK) / SECTOR_SIZE;
    if (arm7_sector >= sectors || arm7_sector == header_sector) return false;
    locations[0] = header_sector;
    locations[1] = arm7_sector;
    return true;
}

uint16_t nrio_safe_mode_crc16(const uint8_t *data, unsigned size)
{
    uint16_t crc = 0xffff;
    for (unsigned i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (crc & 1 ? 0xa001 : 0);
    }
    return crc;
}

uint32_t nrio_safe_mode_handoff(const uint8_t *payload)
{
    uint32_t word = load_word(payload + 0x34);
    // The Fire Line and DS Linker bodies differ only in this handoff word.
    if (nrio_safe_mode_crc16(payload, 0x34) != 0x4b78) return 0;
    return word == 0x1918027e || word == 0x1118027e ? word : 0;
}

bool nrio_recovery_check_safe_mode_header(const uint8_t sector[512])
{
    return live_safe_mode_plan && sector && equal(sector, safe_mode_plan.replacement[0], SECTOR_SIZE);
}

bool nrio_recovery_select_safe_mode_payload(const uint8_t sector[512])
{
    if (!live_safe_mode_plan || !sector) return false;
    if (equal(sector, safe_mode_plan.replacement[1], SECTOR_SIZE)) return true;

    // Apache's relocation stub, carrying this exact cart's original 56 bytes.
    u32 index = safe_mode_plan.sectors[1];
    const volatile u8 *original = safe_mode_plan.expected +
        index / SECTORS_PER_PAGE * PAGE_RECORD_SIZE + index % SECTORS_PER_PAGE * SECTOR_SIZE;
    if (nrio_safe_mode_crc16(sector, 0x3c) != 0x2382 ||
        load_word(sector + 0x74) != SAFE_MODE_PAYLOAD_BYTES ||
        load_word(sector + 0x78) != SAFE_MODE_STOCK_RAM ||
        !equal(sector + 0x3c, original, SAFE_MODE_PAYLOAD_BYTES) ||
        !equal(sector + 0x7c, original + 0x7c, SECTOR_SIZE - 0x7c))
        return false;
    for (unsigned i = 0; i < SECTOR_SIZE; ++i) safe_mode_plan.replacement[1][i] = sector[i];
    return true;
}

static bool safe_mode_slot_available(const u8 *header)
{
    u32 source = load_word(header + 0x30);
    u32 size = load_word(header + 0x80);
    u32 arm9_ram = load_word(header + 0x28), arm9_size = load_word(header + 0x2c);
    if (header[0x12] || load_word(header + 0x34) != SAFE_MODE_STOCK_RAM ||
        nrio_safe_mode_crc16(header, 0x15e) != (header[0x15e] | header[0x15f] << 8) ||
        !arm9_size || arm9_ram < 0x02000000 || arm9_ram >= 0x023bfe00 ||
        arm9_size > 0x023bfe00 - arm9_ram ||
        (arm9_ram < SAFE_MODE_STAGE_RAM + SECTOR_SIZE && arm9_ram + arm9_size > SAFE_MODE_STAGE_RAM))
        return false;

    // The two known compact ROMs have no FAT payloads.
    if (load_word(header + 0x4c)) return false;
    static const u8 sections[][2] = {
        {0x20, 0x2c}, {0x40, 0x44}, {0x50, 0x54}, {0x58, 0x5c},
    };
    for (unsigned i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
        u32 offset = load_word(header + sections[i][0]);
        u32 length = load_word(header + sections[i][1]);
        if (length && (offset < SECTOR_SIZE || offset >= size || length > size - offset ||
                       (offset < source + SECTOR_SIZE && offset + length > source)))
            return false;
    }
    u32 debug = load_word(header + 0x164);
    u32 banner = load_word(header + 0x68);
    return !debug && (!banner || (banner >= source + SECTOR_SIZE && banner < size));
}

static void store_safe_mode_word(volatile u8 *out, u32 word)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = (u8)(word >> (i * 8));
}

bool prepare_live_safe_mode(u32 pages, u8 *records, struct nrio_raw_write_error *error,
                            u32 locations[2])
{
    if (pages != 64 && pages != 128)
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, 0, pages);
    for (u32 i = 0; i < pages; ++i) {
        if (!read_page(i, records + i * PAGE_RECORD_SIZE))
            return fail_write(error, NRIO_RAW_WRITE_READ_BLOCK, i, 0);
    }
    if (!safe_mode_locations(records, pages * SECTORS_PER_PAGE, locations))
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, 0, 0);
    const u8 *header = main_sector(records, locations[0]);
    const u8 *payload = main_sector(records, locations[1]);
    if (!safe_mode_slot_available(header) || !nrio_safe_mode_handoff(payload))
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, locations[1] / SECTORS_PER_PAGE, 0);

    u8 padding = payload[SAFE_MODE_PAYLOAD_BYTES];
    if (padding != 0 && padding != 0xff)
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, locations[1] / SECTORS_PER_PAGE, 0);
    for (unsigned i = SAFE_MODE_PAYLOAD_BYTES; i < SECTOR_SIZE; ++i) {
        if (payload[i] != padding)
            return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, locations[1] / SECTORS_PER_PAGE, 0);
    }

    // Same register-preserving ARM7 stub as patch_safe_mode.py.
    static const u32 stub[] = {
        0xe58f0050, 0xe58f1050, 0xe58f2050, 0xe58f3050, 0xe58fc050,
        0xe10f3000, 0xe28f004c, 0xe59f102c, 0xe3a0200e, 0xe490c004,
        0xe481c004, 0xe2522001, 0x1afffffb, 0xe128f003, 0xe59f0018,
        0xe59f1018, 0xe59f2018, 0xe59f3018, 0xe59fc018, 0xe59ff000,
        SAFE_MODE_STOCK_RAM, SAFE_MODE_STOCK_RAM, 0, 0, 0, 0, 0,
    };
    u8 patched_header[SECTOR_SIZE];
    for (unsigned i = 0; i < SECTOR_SIZE; ++i) patched_header[i] = header[i];
    store_safe_mode_word(patched_header + 0x34, SAFE_MODE_STAGE_RAM);
    store_safe_mode_word(patched_header + 0x38, SAFE_MODE_STAGE_RAM);
    store_safe_mode_word(patched_header + 0x3c, SECTOR_SIZE);
    uint16_t crc = nrio_safe_mode_crc16(patched_header, 0x15e);
    patched_header[0x15e] = (u8)crc;
    patched_header[0x15f] = (u8)(crc >> 8);

    for (unsigned i = 0; i < SECTOR_SIZE; ++i) {
        safe_mode_plan.replacement[0][i] = patched_header[i];
        safe_mode_plan.replacement[1][i] = payload[i];
    }
    for (unsigned i = 0; i < sizeof(stub) / sizeof(stub[0]); ++i)
        store_safe_mode_word(safe_mode_plan.replacement[1] + i * sizeof(u32), stub[i]);
    for (unsigned i = 0; i < SAFE_MODE_PAYLOAD_BYTES; ++i)
        safe_mode_plan.replacement[1][sizeof(stub) + i] = payload[i];
    for (u32 i = 0; i < pages * PAGE_RECORD_SIZE; ++i)
        safe_mode_plan.expected[i] = records[i];

    safe_mode_plan.sectors[0] = locations[0];
    safe_mode_plan.sectors[1] = locations[1];
    live_safe_mode_plan = true;
    safe_mode_plan.pages = pages;
    return true;
}

static bool read_matching_snapshot(u32 pages, u8 *records, struct nrio_raw_write_error *error)
{
    for (u32 i = 0; i < pages; ++i) {
        u8 *record = records + i * PAGE_RECORD_SIZE;
        if (!read_page(i, record)) return fail_write(error, NRIO_RAW_WRITE_READ_BLOCK, i, 0);
        if (!equal(record, safe_mode_plan.expected + i * PAGE_RECORD_SIZE, PAGE_SIZE))
            return fail_write(error, NRIO_RAW_WRITE_SNAPSHOT, i, 0);
        if (!equal(record + PAGE_SIZE, safe_mode_plan.expected + i * PAGE_RECORD_SIZE + PAGE_SIZE, SPARE_SIZE))
            return fail_write(error, NRIO_RAW_WRITE_SNAPSHOT, i, 1);
    }

    return true;
}

bool restore_safe_mode(u32 pages, u8 *records, struct nrio_raw_write_error *error)
{
    if (!nrio_recovery_safe_mode_ready() || safe_mode_plan.pages != pages)
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, 0, pages);
    if (!read_matching_snapshot(pages, records, error)) return false;
    u32 locations[2];
    if (!safe_mode_locations(records, pages * SECTORS_PER_PAGE, locations) ||
        locations[0] != safe_mode_plan.sectors[0] || locations[1] != safe_mode_plan.sectors[1])
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, 0, 0);
    bool changed = false;
    for (u32 i = 0; i < 2; ++i) {
        u32 sector = safe_mode_plan.sectors[i];
        u32 column = sector % SECTORS_PER_PAGE;
        u8 *record = records + (sector / SECTORS_PER_PAGE) * PAGE_RECORD_SIZE;
        u8 *target = record + column * SECTOR_SIZE;
        if (equal(target, safe_mode_plan.replacement[i], SECTOR_SIZE)) continue;
        for (u32 j = 0; j < SECTOR_SIZE; ++j) target[j] = safe_mode_plan.replacement[i][j];
        regenerate_ecc(record, column);
        changed = true;
    }
    return !changed || program_and_verify_block(0, pages, records, false, error);
}


static bool banner_spare_format(const u8 *records, unsigned copy, bool *has_ecc)
{
    const u8 *record = records + (BANNER_PAGE + copy) * PAGE_RECORD_SIZE;
    const u8 *spare = record + PAGE_SIZE;
    const u8 *page1 = records + PAGE_RECORD_SIZE + PAGE_SIZE;
    const u8 *other = records + (BANNER_PAGE + 1 - copy) * PAGE_RECORD_SIZE + PAGE_SIZE;
    *has_ecc = false;
    if (nrio_safe_mode_handoff(spare) ||
        (load_word(spare) == UINT32_C(0xe3a00000) &&
         load_word(spare + 4) == UINT32_C(0xe59f1020) &&
         (equal(spare, page1, SPARE_SIZE) || equal(spare, other, SPARE_SIZE))))
        return true;

    if (erased_bytes(spare, SPARE_SIZE)) return true;

    // Rewrite ECC only when the existing triplets match this page's main data.
    u8 expected[PAGE_RECORD_SIZE];
    for (unsigned i = 0; i < PAGE_RECORD_SIZE; ++i) expected[i] = record[i];
    for (unsigned column = 0; column < SECTORS_PER_PAGE; ++column)
        regenerate_ecc(expected, column);
    *has_ecc = equal(expected + PAGE_SIZE, spare, SPARE_SIZE);
    return *has_ecc;
}

// Fire Line and DS Linker store two 0x800-byte copies. Their v1 banner
// CRCs cover the copied bytes followed by 0x40 logical zero bytes.
static bool valid_v1_banner(const u8 *banner)
{
    if (banner[0] != 1 || banner[1]) return false;
    u8 crc_data[0x820];
    for (unsigned i = 0; i < PAGE_SIZE - 0x20; ++i) crc_data[i] = banner[i + 0x20];
    for (unsigned i = 0; i < 0x40; ++i) crc_data[PAGE_SIZE - 0x20 + i] = 0;
    return nrio_safe_mode_crc16(crc_data, sizeof(crc_data)) == (banner[2] | banner[3] << 8);
}

static bool read_banner_block(u32 pages, u8 *records, struct nrio_raw_write_error *error)
{
    if (pages != 64 && pages != 128)
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, 0, pages);
    for (u32 i = 0; i < pages; ++i) {
        if (!read_page(i, records + i * PAGE_RECORD_SIZE))
            return fail_write(error, NRIO_RAW_WRITE_READ_BLOCK, i, 0);
    }
    const u8 *header = main_sector(records, 2);
    if (load_word(header) != UINT32_C(0x42475344) ||
        load_word(header + 0x0c) != UINT32_C(0x42475344))
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, BANNER_PAGE, 0);
    return true;
}

static void stage_banner_snapshot(u32 pages, const u8 *records, bool spare_only)
{
    for (u32 i = 0; i < pages * PAGE_RECORD_SIZE; ++i) safe_mode_plan.expected[i] = records[i];
    live_banner_spare_only = spare_only;
    live_banner_pages = pages;
}

bool prepare_sd_banner(u32 pages, u8 *records, const u8 *banner, struct nrio_raw_write_error *error)
{
    if (!read_banner_block(pages, records, error)) return false;
    if (!valid_v1_banner(banner))
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, BANNER_PAGE, 0);
    for (unsigned copy = 0; copy < BANNER_COPIES; ++copy) {
        const u32 page = BANNER_PAGE + copy;
        const u8 *current = records + page * PAGE_RECORD_SIZE;
        if (current[0] != 1 || current[1])
            return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, page, 0);
        if (!banner_spare_format(records, copy, &live_banner_has_ecc[copy]))
            return fail_write(error, NRIO_RAW_WRITE_MARKER, page, 0);
    }
    for (u32 i = 0; i < PAGE_SIZE; ++i) live_banner[i] = banner[i];
    stage_banner_snapshot(pages, records, false);
    return true;
}

bool prepare_banner_spare_restore(u32 pages, u8 *records, struct nrio_raw_write_error *error)
{
    if (!read_banner_block(pages, records, error)) return false;
    const u8 *record = records + BANNER_PAGE * PAGE_RECORD_SIZE;
    const u8 *current = record + PAGE_SIZE;
    const u8 *page1 = records + PAGE_RECORD_SIZE + PAGE_SIZE;
    const u8 *page5 = records + 5 * PAGE_RECORD_SIZE + PAGE_SIZE;
    if (!nrio_safe_mode_handoff(page1) || !equal(page1, page5, SPARE_SIZE) ||
        !valid_v1_banner(record))
        return fail_write(error, NRIO_RAW_WRITE_MARKER, BANNER_PAGE, 0);
    if (equal(current, page1, SPARE_SIZE))
        return fail_write(error, NRIO_RAW_WRITE_MARKER, BANNER_PAGE, 1);

    // The first banner build regenerated ECC only for changed main sectors.
    // Both untouched and regenerated chunks must match exactly.
    u8 damaged[PAGE_RECORD_SIZE];
    for (unsigned i = 0; i < PAGE_SIZE; ++i) damaged[i] = record[i];
    for (unsigned i = 0; i < SPARE_SIZE; ++i) damaged[PAGE_SIZE + i] = page1[i];
    for (unsigned column = 0; column < SECTORS_PER_PAGE; ++column) {
        unsigned offset = column * 16;
        regenerate_ecc(damaged, column);
        if (!equal(current + offset, page1 + offset, 16) &&
            !equal(current + offset, damaged + PAGE_SIZE + offset, 16))
            return fail_write(error, NRIO_RAW_WRITE_MARKER, BANNER_PAGE, 0);
    }
    for (unsigned i = 0; i < SPARE_SIZE; ++i) live_banner_spare[i] = page1[i];
    stage_banner_snapshot(pages, records, true);
    return true;
}

bool install_sd_banner(u32 pages, u8 *records, struct nrio_raw_write_error *error)
{
    if (!live_banner_pages || pages != live_banner_pages ||
        !read_matching_snapshot(pages, records, error)) return false;
    if (live_banner_spare_only) {
        u8 *record = records + BANNER_PAGE * PAGE_RECORD_SIZE;
        for (unsigned i = 0; i < SPARE_SIZE; ++i) record[PAGE_SIZE + i] = live_banner_spare[i];
        return program_and_verify_block(0, pages, records, false, error);
    }
    bool changed = false;
    for (unsigned copy = 0; copy < BANNER_COPIES; ++copy) {
        u8 *record = records + (BANNER_PAGE + copy) * PAGE_RECORD_SIZE;
        for (unsigned column = 0; column < SECTORS_PER_PAGE; ++column) {
            unsigned offset = column * SECTOR_SIZE;
            if (equal(record + offset, live_banner + offset, SECTOR_SIZE)) continue;
            for (unsigned i = 0; i < SECTOR_SIZE; ++i) record[offset + i] = live_banner[offset + i];
            if (live_banner_has_ecc[copy]) regenerate_ecc(record, column);
            changed = true;
        }
    }
    return !changed || program_and_verify_block(0, pages, records, false, error);
}


bool prepare_block0_restore(u32 pages, u8 *records, const u8 *backup,
                            struct nrio_raw_write_error *error)
{
    const u8 *header = main_sector(backup, 2);
    const u8 *table = main_sector(backup, NRIO_RAW_PRIMARY_TABLE / SECTOR_SIZE);
    u32 rom = load_word(table);
    if ((pages != 64 && pages != 128) ||
        load_word(header) != UINT32_C(0x42475344) ||
        load_word(header + 0x0c) != UINT32_C(0x42475344) ||
        rom < 0x4000 || rom == UINT32_MAX || rom % 0x4000)
        return fail_write(error, NRIO_RAW_WRITE_SAFE_MODE_PLAN, 0, 0);
    bool changed = false;
    for (u32 i = 0; i < pages; ++i) {
        u8 *record = records + i * PAGE_RECORD_SIZE;
        if (!read_page(i, record))
            return fail_write(error, NRIO_RAW_WRITE_READ_BLOCK, i, 0);
        if (!equal(record, backup + i * PAGE_RECORD_SIZE, PAGE_RECORD_SIZE)) changed = true;
    }
    if (!changed) return true;

    for (u32 i = 0; i < pages * PAGE_RECORD_SIZE; ++i) safe_mode_plan.expected[i] = records[i];
    live_restore_backup = backup;
    live_restore_pages = pages;
    return true;
}

bool install_block0_restore(u32 pages, u8 *records, struct nrio_raw_write_error *error)
{
    if (!live_restore_backup || pages != live_restore_pages ||
        !read_matching_snapshot(pages, records, error))
        return false;
    for (u32 i = 0; i < pages * PAGE_RECORD_SIZE; ++i) records[i] = live_restore_backup[i];
    return program_and_verify_block(0, pages, records, false, error);
}
