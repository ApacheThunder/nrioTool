#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "region-recovery.h"
#include "udisk/updater.h"

enum { SECTOR = 512, PAGE = 2048, SPARE = 64, MAP_BYTES = 0x4000,
       SAFE_LIMIT = 0x40000, UDISK_PACKED_BYTES = 0xc1200, UDISK_BYTES = 0xc0000 };

static FILE *region_log;

static void region_report(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    if (region_log) {
        va_start(args, format);
        vfprintf(region_log, format, args);
        va_end(args);
    }
}

static uint32_t word(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool read_mapping(uint32_t table, uint32_t offset, uint32_t *address)
{
    uint8_t data[SECTOR];
    uint32_t entry = offset / MAP_BYTES * 4;
    if (entry >= NRIO_RAW_TABLE_BYTES ||
        !nrio_recovery_read(table / SECTOR + entry / SECTOR, 1, data))
        return false;
    *address = word(data + entry % SECTOR);
    return true;
}

static bool full_map_entry(uint32_t address, uint32_t sectors)
{
    return address >= MAP_BYTES && address % MAP_BYTES == 0 &&
           sectors >= MAP_BYTES / SECTOR && address / SECTOR <= sectors - MAP_BYTES / SECTOR;
}

static bool compact(const uint8_t *sector)
{
    return word(sector) == 0xea00002e && word(sector + 0x0c) == 0x23232323 &&
           !sector[0x12] && word(sector + 0x84) == SECTOR &&
           nrio_safe_mode_crc16(sector, 0x15e) == (sector[0x15e] | sector[0x15f] << 8);
}

static bool section_valid(const uint8_t *header, uint32_t size, unsigned field)
{
    uint32_t start = word(header + field), length = word(header + field + 12);
    return start >= SECTOR && start < size && length && length <= size - start;
}

struct nrio_boot_rom nrio_inspect_boot(uint32_t table, uint32_t sectors)
{
    uint8_t data[SECTOR];
    struct nrio_boot_rom rom = {.kind = NRIO_BOOT_READ_ERROR};
    if (!nrio_recovery_read(table / SECTOR - 1, 1, data)) return rom;
    if (!memcmp(data + SECTOR - 4, "QAFE", 4)) {
        rom.kind = NRIO_BOOT_SKIPPED;
        return rom;
    }
    if (!read_mapping(table, 0, &rom.address)) return rom;
    if (rom.address == UINT32_MAX) {
        rom.address = 0;
        rom.kind = NRIO_BOOT_ERASED;
        return rom;
    }
    if (!full_map_entry(rom.address, sectors)) {
        rom.kind = NRIO_BOOT_UNKNOWN;
        return rom;
    }
    if (!nrio_recovery_read(rom.address / SECTOR, 1, data)) return rom;
    rom.kind = NRIO_BOOT_UNKNOWN;
    if (!compact(data)) return rom;
    rom.size = word(data + 0x80);
    if (!section_valid(data, rom.size, 0x20) || !section_valid(data, rom.size, 0x30)) return rom;
    uint32_t src = word(data + 0x30), entry = word(data + 0x34);
    uint32_t dest = word(data + 0x38), length = word(data + 0x3c), mapped;
    if (src % SECTOR || !read_mapping(table, src, &mapped)) return rom;
    if (!full_map_entry(mapped, sectors)) return rom;
    rom.arm7_address = mapped + src % MAP_BYTES;
    bool stock = entry == dest && dest == 0x03800000 && length == 0x38;
    bool patched = entry == dest && dest == 0x02380000 && length == SECTOR;
    bool udisk = rom.size == 0x92240 && src == 0x8ba00 && length == 0x5e00;
    if (!nrio_recovery_read(rom.arm7_address / SECTOR, 1, data)) {
        rom.kind = NRIO_BOOT_READ_ERROR;
        return rom;
    }
    if (stock) {
        rom.handoff = nrio_safe_mode_handoff(data);
        if (rom.handoff) rom.kind = NRIO_BOOT_STOCK;
    } else if (patched) {
        if (nrio_safe_mode_crc16(data, 0x6c) == 0x1a2d)
            rom.handoff = nrio_safe_mode_handoff(data + 0x6c);
        else if (nrio_safe_mode_crc16(data, 0x3c) == 0x2382 &&
                 word(data + 0x74) == 0x38 && word(data + 0x78) == 0x03800000)
            rom.handoff = nrio_safe_mode_handoff(data + 0x3c);
        if (rom.handoff) rom.kind = NRIO_BOOT_PATCHED;
    } else {
        rom.kind = udisk ? NRIO_BOOT_UDISK : NRIO_BOOT_COMPACT;
    }
    return rom;
}

const char *nrio_boot_kind_name(enum nrio_boot_kind kind)
{
    static const char *const names[] = {
        "Read failed", "Skipped (QAFE)", "Unknown layout", "Stock safe mode",
        "Patched safe mode", "uDisk 1.45 layout", "Other compact ROM", "First map entry erased",
    };
    return names[kind];
}

static uint8_t *load_file(const char *root, const char *name, size_t max, size_t *length,
                          bool *missing)
{
    char path[56];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    if (missing) *missing = false;
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (missing && errno == ENOENT) *missing = true;
        else region_report("Could not open %s\n", name);
        return NULL;
    }
    uint8_t *data = NULL;
    if (!fseek(file, 0, SEEK_END)) {
        long size = ftell(file);
        if (size >= SECTOR && (unsigned long)size <= max && !fseek(file, 0, SEEK_SET)) {
            data = malloc(size);
            if (data && fread(data, 1, size, file) == (size_t)size && fgetc(file) == EOF && !ferror(file)) {
                *length = size;
                fclose(file);
                return data;
            }
        }
    }
    free(data);
    fclose(file);
    region_report("Invalid or unreadable %s\n", name);
    return NULL;
}

static bool blank(const uint8_t *data, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
        if (data[i] != 0xff) return false;
    return true;
}

static int compare_patch(const void *left, const void *right)
{
    uint32_t a = ((const struct nrio_sector_patch *)left)->lba;
    uint32_t b = ((const struct nrio_sector_patch *)right)->lba;
    return (a > b) - (a < b);
}

static bool make_backup_dir(const char *root, char directory[48])
{
    for (unsigned i = 0; i < 1000; ++i) {
        snprintf(directory, 48, "%s/region-%03u", root, i);
        if (mkdir(directory, 0777) == 0) return true;
        if (errno != EEXIST) break;
    }
    return false;
}

static uint32_t batch_limit(const struct nrio_sector_patch *patches, uint32_t count,
                            uint32_t start, uint32_t lba, uint32_t block_sectors)
{
    while (start < count && patches[start].lba >= lba &&
           patches[start].lba < lba + block_sectors) ++start;
    return start;
}

void nrio_format_write_error(char *message, size_t size, const struct nrio_raw_write_error *error)
{
    if (error->stage == NRIO_RAW_WRITE_SNAPSHOT_READBACK) {
        uint32_t offset = NRIO_RAW_SNAPSHOT_OFFSET(error->detail);
        bool spare = offset >= PAGE;
        snprintf(message, size, "Snapshot changed at page 0x%lX\n"
                 "%s byte +0x%lX\nSaved %02lX, read %02lX.\n",
                 (unsigned long)error->page, spare ? "Spare" : "Main",
                 (unsigned long)(spare ? offset - PAGE : offset),
                 (unsigned long)NRIO_RAW_SNAPSHOT_SAVED(error->detail),
                 (unsigned long)NRIO_RAW_SNAPSHOT_LIVE(error->detail));
    } else {
        snprintf(message, size, "Stopped at stage %lu, page %lX,\ndetail %lX.\n",
                 (unsigned long)error->stage, (unsigned long)error->page, (unsigned long)error->detail);
    }
}

static void report_region_write_error(void)
{
    struct nrio_raw_write_error error;
    char message[128];
    nrio_recovery_get_write_error(&error);
    nrio_format_write_error(message, sizeof(message), &error);
    region_report("%s", message);
}

/* Spool the complete physical block snapshots before authorizing any erase. */
static void run_plan(const char *root, const struct nrio_raw_info *info, uint8_t *snapshot,
                     const struct nrio_sector_patch *patches, uint32_t count,
                     const char *title)
{
    if (!count) {
        region_report("%s\n", "The mapped bytes already match.");
        return;
    }
    uint32_t block_sectors = nrio_recovery_pages_per_block() * PAGE / SECTOR;
    char directory[48];
    if (!make_backup_dir(root, directory)) {
        region_report("%s\n", "Could not create backup folder.");
        return;
    }
    char path[72], temporary[72];
    snprintf(path, sizeof(path), "%s/blocks.bin", directory);
    snprintf(temporary, sizeof(temporary), "%s/blocks.part", directory);
    FILE *backup = fopen(temporary, "wb");
    if (!backup) {
        region_report("%s\n", "Could not open backup file.");
        return;
    }

    char log_path[72];
    snprintf(log_path, sizeof(log_path), "%s/SESSION.TXT", directory);
    FILE *log = fopen(log_path, "w");
    if (log) fprintf(log, "%s\nNAND ID %08lX\n", title, (unsigned long)info->nand_id);
    const char signature[8] = "NRIOREG1";
    uint32_t expected_blocks = 0;
    for (uint32_t i = 0; i < count; ++expected_blocks) {
        uint32_t lba = patches[i].lba / block_sectors * block_sectors;
        i = batch_limit(patches, count, i, lba, block_sectors);
    }
    if (fwrite(signature, sizeof(signature), 1, backup) != 1 ||
        fwrite(&expected_blocks, sizeof(expected_blocks), 1, backup) != 1) {
        region_report("%s\n", "Could not write backup header.");
        goto done;
    }

    uint32_t block_count = 0;
    for (uint32_t i = 0; i < count;) {
        uint32_t lba = patches[i].lba / block_sectors * block_sectors;
        uint32_t end = batch_limit(patches, count, i, lba, block_sectors);
        if (!nrio_recovery_read_snapshot(lba, snapshot, info->workspace_bytes) ||
            !nrio_recovery_patch_snapshot(lba, snapshot, patches + i, end - i, false)) {
            region_report("%s\n", "Stopped during read-only check.");
            report_region_write_error();
            goto done;
        }

        uint32_t header[] = {lba, info->nand_id, info->workspace_bytes,
                             nrio_update_crc32(UINT32_MAX, snapshot, info->workspace_bytes)};
        if (fwrite(header, sizeof(header), 1, backup) != 1 ||
            fwrite(snapshot, info->workspace_bytes, 1, backup) != 1) {
            region_report("%s\n", "Could not save block backup.");
            goto done;
        }
        if (block_count < 8) region_report("Block 0x%08lX ready\n", (unsigned long)lba * SECTOR);
        if (log) fprintf(log, "Block 0x%08lX backed up, CRC %08lX\n",
                         (unsigned long)lba * SECTOR, (unsigned long)header[3]);
        ++block_count;
        i = end;
    }
    int flush_result = fclose(backup);
    backup = NULL;
    if (flush_result || rename(temporary, path)) {
        region_report("%s\n", "Could not save block backup.");
        goto done;
    }
    temporary[0] = '\0';
    region_report("Saved %lu block(s) to\n%s\n", (unsigned long)block_count, directory);
    region_report("%s\n", title);
    if (log) fflush(log);
    if (!nrio_confirm_write()) {
        if (log) fputs("Cancelled before write.\n", log);
        goto done;
    }
    backup = fopen(path, "rb");
    char tag[sizeof(signature)];
    uint32_t saved_blocks;
    if (!backup || fread(tag, sizeof(tag), 1, backup) != 1 || memcmp(tag, signature, sizeof(tag)) ||
        fread(&saved_blocks, sizeof(saved_blocks), 1, backup) != 1 || saved_blocks != block_count) {
        region_report("%s\n", "Could not reopen block backup.");
        goto done;
    }

    for (uint32_t i = 0; i < count;) {
        uint32_t lba = patches[i].lba / block_sectors * block_sectors;
        uint32_t end = batch_limit(patches, count, i, lba, block_sectors);
        uint32_t header[4];
        if (fread(header, sizeof(header), 1, backup) != 1 ||
            fread(snapshot, info->workspace_bytes, 1, backup) != 1 ||
            header[0] != lba || header[1] != info->nand_id || header[2] != info->workspace_bytes ||
            header[3] != nrio_update_crc32(UINT32_MAX, snapshot, info->workspace_bytes)) {
            region_report("%s\n", "Backup changed or read failed.");
            goto done;
        }
        region_report("Verifying block at 0x%08lX\n", (unsigned long)lba * SECTOR);
        if (!nrio_recovery_patch_snapshot(lba, snapshot, patches + i, end - i, true)) {
            report_region_write_error();
            goto done;
        }
        if (log) {
            fprintf(log, "Block 0x%08lX verified.\n", (unsigned long)lba * SECTOR);
            fflush(log);
        }
        i = end;
    }
    region_report("%s\n", "All rewritten blocks verified.");
    if (log) fputs("All rewritten blocks verified.\n", log);
done:
    if (log) {
        struct nrio_raw_write_error error;
        nrio_recovery_get_write_error(&error);
        if (error.stage) {
            char message[128];
            nrio_format_write_error(message, sizeof(message), &error);
            fputs(message, log);
        }
        fclose(log);
    }
    if (backup) fclose(backup);
    if (*temporary) remove(temporary);
}

static bool load_range(const uint8_t *header, unsigned field, uint32_t lo, uint32_t hi)
{
    uint32_t entry = word(header + field + 4) & ~1u;
    uint32_t load = word(header + field + 8), bytes = word(header + field + 12);
    return !(load % 4) && load >= lo && load < hi && bytes <= hi - load &&
           entry >= load && entry - load < bytes;
}

static bool replacement_header(const uint8_t *rom, size_t length)
{
    if (!compact(rom)) return false;
    uint32_t size = word(rom + 0x80);
    if (size > length || !section_valid(rom, size, 0x20) || !section_valid(rom, size, 0x30))
        return false;
    bool arm7_main = load_range(rom, 0x30, 0x02000000, 0x023bfe00);
    if (!load_range(rom, 0x20, 0x02000000, 0x023bfe00) ||
        (!arm7_main && !load_range(rom, 0x30, 0x037f8000, 0x03810000)))
        return false;
    uint32_t arm9 = word(rom + 0x28), arm7 = word(rom + 0x38);
    if (arm7_main && arm9 < arm7 + word(rom + 0x3c) && arm7 < arm9 + word(rom + 0x2c))
        return false;
    return size == length || blank(rom + size, length - size);
}

static uint8_t *read_original_rom(const uint32_t *mapped, uint32_t size)
{
    uint32_t sectors = (size + SECTOR - 1) / SECTOR;
    uint8_t *rom = malloc(sectors * SECTOR);
    if (!rom) {
        region_report("%s\n", "Could not allocate original ROM.");
        return NULL;
    }
    const uint32_t chunk = MAP_BYTES / SECTOR;
    for (uint32_t i = 0; i < sectors; i += chunk) {
        uint32_t count = sectors - i < chunk ? sectors - i : chunk;
        if (!nrio_recovery_read(mapped[i / chunk] / SECTOR, count, rom + i * SECTOR)) {
            region_report("Could not read original ROM\nat offset 0x%lX.\n", (unsigned long)i * SECTOR);
            free(rom);
            return NULL;
        }
    }
    return rom;
}

static void install_safe_replacement(const char *root, const struct nrio_raw_info *info,
                                     uint8_t *snapshot)
{
    struct nrio_boot_rom boot = nrio_inspect_boot(NRIO_RAW_PRIMARY_TABLE, info->sectors);
    if (boot.kind != NRIO_BOOT_STOCK && boot.kind != NRIO_BOOT_PATCHED) {
        region_report("Primary: %s\n", nrio_boot_kind_name(boot.kind));
        region_report("%s\n", "Need a recognized live safe ROM.");
        return;
    }

    uint32_t stock_used = boot.size;
    if (stock_used < SECTOR || stock_used > SAFE_LIMIT) {
        region_report("%s\n", "Live safe ROM size is invalid.");
        return;
    }

    size_t original_length = 0, replacement_length = 0;
    bool original_missing;
    uint8_t *original = load_file(root, "safe-original.nds", SAFE_LIMIT, &original_length,
                                  &original_missing);
    if (!original && !original_missing) return;
    uint8_t *replacement = load_file(root, "safe-replacement.nds", SAFE_LIMIT, &replacement_length, NULL);
    uint8_t *expected = NULL, *desired = NULL;
    struct nrio_sector_patch *patches = NULL;
    if (!replacement) goto done;
    if (!replacement_header(replacement, replacement_length)) {
        region_report("%s\n", "Invalid replacement ROM or CRC.");
        goto done;
    }
    uint32_t used = word(replacement + 0x80);
    uint32_t mapped_blocks = (stock_used + MAP_BYTES - 1) / MAP_BYTES;
    if (used > mapped_blocks * MAP_BYTES) {
        region_report("%s\n", "Replacement exceeds the live\nROM's verified mapping.");
        goto done;
    }

    uint32_t mapped[SAFE_LIMIT / MAP_BYTES];
    for (uint32_t i = 0; i < mapped_blocks; ++i) {
        uint32_t address;
        if (!read_mapping(NRIO_RAW_PRIMARY_TABLE, i * MAP_BYTES, &address) ||
            !full_map_entry(address, info->sectors) ||
            address < 0x18000 || (address >= NRIO_RAW_SECONDARY_TABLE && address < 0x50000)) {
            region_report("Unsafe primary map entry %lu.\n", (unsigned long)i);
            goto done;
        }
        for (uint32_t prior = 0; prior < i; ++prior) {
            if (mapped[prior] == address) {
                region_report("%s\n", "Primary map reuses a block.");
                goto done;
            }
        }
        mapped[i] = address;
    }

    /* A candidate may grow within its final mapped chunk. Do not claim a
       chunk also referenced by the secondary boot route. */
    struct nrio_boot_rom secondary = nrio_inspect_boot(NRIO_RAW_SECONDARY_TABLE, info->sectors);
    if (secondary.kind == NRIO_BOOT_READ_ERROR ||
        (secondary.size > NRIO_RAW_TABLE_BYTES / 4 * MAP_BYTES)) {
        region_report("%s\n", "Could not verify secondary map.");
        goto done;
    }
    for (uint32_t offset = 0; offset < secondary.size; offset += MAP_BYTES) {
        uint32_t address;
        if (!read_mapping(NRIO_RAW_SECONDARY_TABLE, offset, &address) ||
            !full_map_entry(address, info->sectors)) {
            region_report("%s\n", "Invalid secondary boot map.");
            goto done;
        }
        for (uint32_t i = 0; i < mapped_blocks; ++i) {
            if (mapped[i] == address) {
                region_report("%s\n", "The two boot maps overlap.");
                goto done;
            }
        }
    }

    if (original_missing) {
        region_report("%s\n", "Reading original ROM from NAND.");
        original = read_original_rom(mapped, stock_used);
        if (!original) goto done;
        original_length = stock_used;
    }
    if (!replacement_header(original, original_length) || word(original + 0x80) != stock_used) {
        region_report("%s\n", "Original ROM or size is invalid.");
        goto done;
    }

    uint32_t edit_sectors = (used + SECTOR - 1) / SECTOR;
    uint32_t verify_bytes = stock_used > used ? stock_used : used;
    expected = malloc(edit_sectors * SECTOR);
    desired = malloc(edit_sectors * SECTOR);
    patches = malloc(edit_sectors * sizeof(*patches));
    if (!expected || !desired || !patches) {
        region_report("%s\n", "Could not allocate ROM buffers.");
        goto done;
    }
    uint32_t count = 0;
    for (uint32_t offset = 0; offset < verify_bytes; offset += SECTOR) {
        uint8_t live[SECTOR];
        uint32_t lba = (mapped[offset / MAP_BYTES] + offset % MAP_BYTES) / SECTOR;
        uint32_t original_bytes = offset < stock_used ? stock_used - offset : 0;
        if (original_bytes > SECTOR) original_bytes = SECTOR;
        if (!nrio_recovery_read(lba, 1, live) ||
            (original_bytes && memcmp(live, original + offset, original_bytes))) {
            region_report("Original differs at ROM\n0x%08lX or NAND read failed.\n", (unsigned long)offset);
            goto done;
        }
        if (offset >= used) continue;
        uint32_t bytes = used - offset < SECTOR ? used - offset : SECTOR;
        if (!memcmp(live, replacement + offset, bytes)) continue;
        memcpy(expected + count * SECTOR, live, SECTOR);
        memcpy(desired + count * SECTOR, live, SECTOR);
        memcpy(desired + count * SECTOR, replacement + offset, bytes);
        patches[count] = (struct nrio_sector_patch){lba, desired + count * SECTOR,
                                                     expected + count * SECTOR};
        ++count;
    }
    qsort(patches, count, sizeof(*patches), compare_patch);
    free(original);
    original = NULL;
    free(replacement);
    replacement = NULL;
    run_plan(root, info, snapshot, patches, count, "Install safe-mode replacement");

done:
    free(patches);
    free(desired);
    free(expected);
    free(replacement);
    free(original);
}

static void install_stock_udisk(const char *root, const struct nrio_raw_info *info, uint8_t *snapshot)
{
    struct nrio_boot_rom primary = nrio_inspect_boot(NRIO_RAW_PRIMARY_TABLE, info->sectors);
    struct nrio_boot_rom secondary = nrio_inspect_boot(NRIO_RAW_SECONDARY_TABLE, info->sectors);
    if ((primary.kind != NRIO_BOOT_STOCK && primary.kind != NRIO_BOOT_PATCHED) ||
        (secondary.kind != NRIO_BOOT_UDISK && secondary.kind != NRIO_BOOT_UNKNOWN &&
         secondary.kind != NRIO_BOOT_ERASED)) {
        region_report("%s\n", "Need a recognized safe-mode\nroute and stock uDisk layout.");
        return;
    }
    size_t packed_size = 0;
    uint8_t *payload = load_file(root, "udisk_eng.nds", UDISK_PACKED_BYTES, &packed_size, NULL);
    if (!payload) return;
    uint8_t *map = NULL;
    struct nrio_sector_patch *patches = NULL;
    if (packed_size != UDISK_PACKED_BYTES ||
        nrio_update_crc32(UINT32_MAX, payload, packed_size) != 0xffd9f7f7 ||
        !udisk_unpack_payload(payload, packed_size, payload, packed_size)) {
        region_report("%s\n", "Expected verified stock\nudisk_eng.nds from 1.45.");
        goto done;
    }

    for (uint32_t offset = 0; offset < primary.size; offset += MAP_BYTES) {
        uint32_t address;
        if (!read_mapping(NRIO_RAW_PRIMARY_TABLE, offset, &address) ||
            !full_map_entry(address, info->sectors) || address >= NRIO_RAW_SECONDARY_TABLE) {
            region_report("%s\n", "Primary safe mode leaves its\nverified early NAND region.");
            goto done;
        }
    }

    bool stock_map = true, empty_map = true;
    uint8_t sector[SECTOR];
    for (uint32_t offset = 0; offset < NRIO_RAW_TABLE_BYTES; offset += SECTOR) {
        if (!nrio_recovery_read((NRIO_RAW_SECONDARY_TABLE + offset) / SECTOR, 1, sector)) {
            region_report("%s\n", "Could not read secondary map.");
            goto done;
        }
        empty_map &= blank(sector, SECTOR);
        for (uint32_t i = 0; i < SECTOR && (offset + i) / 4 < UDISK_BYTES / MAP_BYTES; i += 4)
            stock_map &= word(sector + i) == UDISK_PAYLOAD_NAND_OFFSET + (offset + i) / 4 * MAP_BYTES;
    }
    if (!stock_map && !empty_map) {
        region_report("%s\n", "Secondary map is neither stock\ncontiguous nor fully erased.");
        goto done;
    }

    /* An erased map cannot establish ownership of nonblank NAND. Check the
       entire intended stock write extent before making a plan. */
    if (empty_map) {
        for (uint32_t offset = 0; offset < UDISK_BYTES; offset += SECTOR) {
            if (!nrio_recovery_read((UDISK_PAYLOAD_NAND_OFFSET + offset) / SECTOR, 1, sector) ||
                (!blank(sector, SECTOR) && memcmp(sector, payload + offset, SECTOR))) {
                region_report("%s\n", "Unmapped uDisk area has\nother data or cannot be read.");
                goto done;
            }
        }
    }

    map = malloc(UDISK_ALLOCATION_BYTES);
    const uint32_t payload_sectors = UDISK_BYTES / SECTOR;
    const uint32_t map_sectors = UDISK_ALLOCATION_BYTES / SECTOR;
    patches = malloc((payload_sectors + map_sectors) * sizeof(*patches));
    if (!map || !patches) {
        region_report("%s\n", "Could not allocate uDisk plan.");
        goto done;
    }
    uint32_t allocations[UDISK_ALLOCATION_COUNT];
    for (unsigned i = 0; i < UDISK_ALLOCATION_COUNT; ++i)
        allocations[i] = i < UDISK_BYTES / UDISK_ALLOCATION_BYTES
            ? UDISK_PAYLOAD_NAND_OFFSET + i * UDISK_ALLOCATION_BYTES : UINT32_MAX;
    udisk_build_secondary_chunk(allocations, map);

    for (uint32_t offset = NRIO_RAW_TABLE_BYTES; offset < UDISK_ALLOCATION_BYTES; offset += SECTOR) {
        if (!nrio_recovery_read((NRIO_RAW_SECONDARY_TABLE + offset) / SECTOR, 1, sector) ||
            (!blank(sector, SECTOR) && memcmp(sector, map + offset, SECTOR))) {
            region_report("%s\n", "Secondary map tail contains\nother data or is unreadable.");
            goto done;
        }
    }

    for (uint32_t i = 0; i < payload_sectors; ++i)
        patches[i] = (struct nrio_sector_patch){UDISK_PAYLOAD_NAND_OFFSET / SECTOR + i,
                                               payload + i * SECTOR, NULL};
    for (uint32_t i = 0; i < map_sectors; ++i)
        patches[payload_sectors + i] = (struct nrio_sector_patch){NRIO_RAW_SECONDARY_TABLE / SECTOR + i,
                                                                 map + i * SECTOR, NULL};

    run_plan(root, info, snapshot, patches, payload_sectors + map_sectors, "Repair stock uDisk 1.45");

done:
    free(patches);
    free(map);
    free(payload);
}

void nrio_install_region(const char *root, const struct nrio_raw_info *info,
                         void *snapshot, bool udisk)
{
    char path[56];
    snprintf(path, sizeof(path), "%s/ADVANCED.TXT", root);
    region_log = fopen(path, "a");
    if (!region_log) puts("Could not open ADVANCED.TXT.");
    if (region_log) fprintf(region_log, "\n%s, NAND %08lX\n",
                            udisk ? "uDisk repair" : "Safe-mode replacement", (unsigned long)info->nand_id);
    if (udisk) install_stock_udisk(root, info, snapshot);
    else install_safe_replacement(root, info, snapshot);
    if (region_log) {
        struct nrio_raw_write_error error;
        nrio_recovery_get_write_error(&error);
        if (error.stage) fprintf(region_log, "Write stage %lu, page 0x%lX, detail 0x%lX.\n",
                                 (unsigned long)error.stage, (unsigned long)error.page,
                                 (unsigned long)error.detail);
        if (fclose(region_log)) puts("Could not save ADVANCED.TXT.");
        region_log = NULL;
    }
}
