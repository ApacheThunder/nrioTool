#include <nds.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "region-recovery.h"
#include "udisk/updater.h"
#include "udisk-upgrade-check.h"
#include "bundle-assets.h"

enum { SECTOR = 512, PAGE = 2048, SPARE = 64, MAP_BYTES = 0x4000,
       SAFE_LIMIT = 0x40000, UDISK_BYTES = 0xc0000 };

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

struct boot_payload_range {
    const char *label;
    uint32_t header_crc, bytes, image_crc;
};

/* Sourced from the USB images in Apache Thunder's published XuluMenu
   releases. Check the whole declared ROM, not optional erased padding. */
static const struct boot_payload_range xulu_releases[] = {
    {"XuluMenu 1.3",   0x0ac69a9d, 0xf7840, 0x1ee75518},
    {"XuluMenu 1.4",   0xc80f49be, 0xf7840, 0x2b3455a3},
    {"XuluMenu 1.4.1", 0x4387aaeb, 0xf1c40, 0xbb8618fb},
};
static const struct boot_payload_range stock_payload = {"stock uDisk 1.45", 0x5d592e83,
                                                        UDISK_BYTES, 0x3dbef02e};

static const struct boot_payload_range *bypass_source_payload(const struct nrio_raw_info *info,
                                                               const uint8_t *table);

struct verified_source {
    const uint8_t *table;
    const struct boot_payload_range *payload;
};

static const struct boot_payload_range *known_xulu_header(const uint8_t *header)
{
    uint32_t crc = nrio_update_crc32(UINT32_MAX, header, SECTOR) ^ UINT32_MAX;
    for (unsigned i = 0; i < sizeof(xulu_releases) / sizeof(*xulu_releases); ++i)
        if (crc == xulu_releases[i].header_crc && word(header + 0x80) == xulu_releases[i].bytes)
            return xulu_releases + i;
    return NULL;
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
    bool xulu = known_xulu_header(data) != NULL;
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
        rom.kind = xulu ? NRIO_BOOT_XULU : udisk ? NRIO_BOOT_UDISK : NRIO_BOOT_COMPACT;
    }
    return rom;
}

const char *nrio_boot_kind_name(enum nrio_boot_kind kind)
{
    static const char *const names[] = {
        "Read failed", "Skipped (QAFE)", "Unknown layout", "Stock safe mode",
        "Patched safe mode", "uDisk 1.45 layout", "Other compact ROM", "First map entry erased",
        "Hidden XuluMenu header",
    };
    return names[kind];
}

static uint8_t *load_file(const char *root, const char *name, size_t max, size_t *length)
{
    char path[56];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        region_report("Could not open %s\n", name);
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

static bool load_bios_block(uint32_t lba, uint32_t sectors, uint8_t *buffer)
{
    const uint32_t address = lba * SECTOR;
    if (address == UDISK_SECONDARY_MAP_NAND_OFFSET) return true;
    if (address < UDISK_PAYLOAD_NAND_OFFSET || !buffer ||
        sectors * SECTOR % UDISK_ALLOCATION_BYTES) return false;
    uint32_t chunk = (address - UDISK_PAYLOAD_NAND_OFFSET) / UDISK_ALLOCATION_BYTES;
    for (uint32_t offset = 0; offset < sectors * SECTOR; offset += UDISK_ALLOCATION_BYTES) {
        if (chunk >= NRIO_ASSET_UDISK_CHUNKS ||
            !nrio_asset_expand(NRIO_ASSET_UDISK_0 + chunk, buffer + offset,
                               UDISK_ALLOCATION_BYTES)) return false;
        ++chunk;
    }
    return true;
}

/* Spool the complete physical block snapshots before authorizing any erase. */
static void run_plan(const char *root, const struct nrio_raw_info *info, uint8_t *snapshot,
                     const struct nrio_sector_patch *patches, uint32_t count,
                     const char *title, bool allow_padding, uint8_t *bios_data,
                     const struct verified_source *source)
{
    if (!count) {
        region_report("%s\n", "The mapped bytes already match.");
        return;
    }
    uint32_t block_sectors = nrio_recovery_pages_per_block() * PAGE / SECTOR;
    const bool bios = bios_data != NULL;
    uint32_t ram_crc[7];
    char directory[48] = {0}, path[72] = {0}, temporary[72] = {0};
    FILE *backup = NULL, *log = NULL;
    if (root) {
        if (!make_backup_dir(root, directory)) {
            region_report("%s\n", "Could not create backup folder.");
            return;
        }
        snprintf(path, sizeof(path), "%s/blocks.bin", directory);
        snprintf(temporary, sizeof(temporary), "%s/blocks.part", directory);
        backup = fopen(temporary, "wb");
        if (!backup) {
            region_report("%s\n", "Could not open backup file.");
            return;
        }

        char log_path[72];
        snprintf(log_path, sizeof(log_path), "%s/SESSION.TXT", directory);
        log = fopen(log_path, "w");
        if (log) fprintf(log, "%s\nNAND ID %08lX\n", title, (unsigned long)info->nand_id);
    }
    const char signature[8] = "NRIOREG1";
    uint32_t expected_blocks = 0;
    for (uint32_t i = 0; i < count; ++expected_blocks) {
        uint32_t lba = patches[i].lba / block_sectors * block_sectors;
        i = batch_limit(patches, count, i, lba, block_sectors);
    }
    if (!root && (!bios || expected_blocks > sizeof(ram_crc) / sizeof(*ram_crc))) return;
    if (backup && (fwrite(signature, sizeof(signature), 1, backup) != 1 ||
        fwrite(&expected_blocks, sizeof(expected_blocks), 1, backup) != 1)) {
        region_report("%s\n", "Could not write backup header.");
        goto done;
    }

    uint32_t block_count = 0;
    bool leave_padding_erased = false;
    for (uint32_t i = 0; i < count;) {
        uint32_t lba = patches[i].lba / block_sectors * block_sectors;
        uint32_t end = batch_limit(patches, count, i, lba, block_sectors);
        if (bios && !load_bios_block(lba, block_sectors, bios_data)) {
            region_report("%s\n", "Could not decode uDisk block.");
            goto done;
        }
        if (!nrio_recovery_read_snapshot(lba, snapshot, info->workspace_bytes)) {
            region_report("%s\n", "Stopped during read-only check.");
            report_region_write_error();
            goto done;
        }
        const bool preflight = bios
            ? nrio_recovery_patch_udisk_snapshot(lba, snapshot, patches + i, end - i, false)
            : nrio_recovery_patch_snapshot(lba, snapshot, patches + i, end - i, false, false);
        if (!preflight) {
            if (!allow_padding || lba ||
                !nrio_recovery_patch_snapshot(lba, snapshot, patches + i, end - i, false, true)) {
                region_report("%s\n", "Stopped during read-only check.");
                report_region_write_error();
                goto done;
            }
            if (!nrio_confirm_padding_erase()) {
                region_report("%s\n", "Padding exception declined.");
                if (log) fputs("Padding exception declined.\n", log);
                goto done;
            }
            leave_padding_erased = true;
            region_report("%s\n", "Page 0xE will be left erased.");
            if (log) fputs("Page 0xE will be left erased.\n", log);
        }

        uint32_t header[] = {lba, info->nand_id, info->workspace_bytes,
                             nrio_update_crc32(UINT32_MAX, snapshot, info->workspace_bytes)};
        if (!backup) ram_crc[block_count] = header[3];
        if (backup && (fwrite(header, sizeof(header), 1, backup) != 1 ||
            fwrite(snapshot, info->workspace_bytes, 1, backup) != 1)) {
            region_report("%s\n", "Could not save block backup.");
            goto done;
        }
        if (block_count < 8) region_report("Block 0x%08lX ready\n", (unsigned long)lba * SECTOR);
        if (log) fprintf(log, "Block 0x%08lX backed up, CRC %08lX\n",
                         (unsigned long)lba * SECTOR, (unsigned long)header[3]);
        ++block_count;
        i = end;
    }
    if (backup) {
        int flush_result = fclose(backup);
        backup = NULL;
        if (flush_result || rename(temporary, path)) {
            region_report("%s\n", "Could not save block backup.");
            goto done;
        }
        temporary[0] = '\0';
    }
    nrio_region_screen(title);
    if (root) region_report("Saved %lu block(s) to\n%s\n", (unsigned long)block_count, directory);
    else region_report("%s\n", "No separate backup storage.\nOnly in-memory checks.\nKeep power on during update.");
    if (leave_padding_erased) puts("Page 0xE will be left erased.");
    if (log) fflush(log);
    if (!nrio_confirm_write()) {
        nrio_region_screen("Write cancelled");
        region_report("%s\n", "Cancelled before write.");
        if (log) fputs("Cancelled before write.\n", log);
        goto done;
    }

    if (source && bypass_source_payload(info, source->table) != source->payload) {
        nrio_region_screen("Write stopped");
        region_report("%s\n", "Source firmware changed.\nNo NAND writes were made.");
        goto done;
    }
    nrio_region_screen("Writing NAND");
    if (root) {
        puts("Opening verified backup.\nDo not turn off the console.");
        backup = fopen(path, "rb");
        char tag[sizeof(signature)];
        uint32_t saved_blocks;
        if (!backup || fread(tag, sizeof(tag), 1, backup) != 1 || memcmp(tag, signature, sizeof(tag)) ||
            fread(&saved_blocks, sizeof(saved_blocks), 1, backup) != 1 || saved_blocks != block_count) {
            nrio_region_screen("Write stopped");
            region_report("%s\n", "Could not reopen block backup.");
            goto done;
        }
    } else {
        puts("Rechecking NAND in memory.\nDo not turn off the console.");
    }

    uint32_t current_block = 1;
    for (uint32_t i = 0; i < count; ++current_block) {
        uint32_t lba = patches[i].lba / block_sectors * block_sectors;
        uint32_t end = batch_limit(patches, count, i, lba, block_sectors);
        uint32_t header[4];
        bool matching;
        if (backup) {
            matching = fread(header, sizeof(header), 1, backup) == 1 &&
                       fread(snapshot, info->workspace_bytes, 1, backup) == 1 &&
                       header[0] == lba && header[1] == info->nand_id && header[2] == info->workspace_bytes &&
                       header[3] == nrio_update_crc32(UINT32_MAX, snapshot, info->workspace_bytes);
        } else {
            matching = nrio_recovery_read_snapshot(lba, snapshot, info->workspace_bytes) &&
                       ram_crc[current_block - 1] == nrio_update_crc32(UINT32_MAX, snapshot, info->workspace_bytes);
        }
        if (!matching || (bios && !load_bios_block(lba, block_sectors, bios_data))) {
            nrio_region_screen("Write stopped");
            region_report("%s\n", "Saved data changed or read failed.");
            goto done;
        }
        nrio_region_screen("Writing NAND");
        printf("Block %lu/%lu at 0x%08lX\n", (unsigned long)current_block,
               (unsigned long)block_count, (unsigned long)lba * SECTOR);
        puts("Checking, writing, verifying.\nDo not turn off the console.");
        if (log) fprintf(log, "Checking and writing block 0x%08lX.\n",
                         (unsigned long)lba * SECTOR);
        const bool verified = bios
            ? nrio_recovery_patch_udisk_snapshot(lba, snapshot, patches + i, end - i, true)
            : nrio_recovery_patch_snapshot(lba, snapshot, patches + i, end - i, true,
                                           !lba && leave_padding_erased);
        if (!verified) {
            nrio_region_screen("Write stopped");
            report_region_write_error();
            goto done;
        }
        if (log) {
            fprintf(log, "Block 0x%08lX verified.\n", (unsigned long)lba * SECTOR);
            fflush(log);
        }
        i = end;
    }
    nrio_region_screen("Write complete");
    const char *result = leave_padding_erased
        ? "Other pages verified exactly.\nPage 0xE left erased."
        : "All rewritten blocks verified.";
    region_report("%s\n", result);
    if (log) fprintf(log, "%s\n", result);
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

    size_t replacement_length = 0;
    uint8_t *replacement = load_file(root, "safe-replacement.nds", SAFE_LIMIT, &replacement_length);
    uint8_t *original = NULL, *expected = NULL, *desired = NULL;
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

    /* The current mapped ROM is the baseline. A previous SD dump may
       predate a patch or belong to another cart. Never use it here. */
    region_report("%s\n", "Reading current safe ROM.");
    original = read_original_rom(mapped, stock_used);
    if (!original) goto done;
    if (!replacement_header(original, stock_used) || word(original + 0x80) != stock_used) {
        region_report("%s\n", "Live safe ROM header is invalid.");
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
            region_report("Live reread failed or changed\nat ROM offset 0x%08lX.\n", (unsigned long)offset);
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
    run_plan(root, info, snapshot, patches, count, "Install safe-mode replacement", true, NULL, NULL);

done:
    free(patches);
    free(desired);
    free(expected);
    free(replacement);
    free(original);
}

static bool read_upgrade_sector(void *unused, uint32_t lba, uint8_t sector[512])
{
    (void)unused;
    return nrio_recovery_read(lba, 1, sector);
}

/* Match the installed executable through the live map, not just its
   header. Read only within the early allocations, allowing skipped blocks. */
static const struct boot_payload_range *bypass_source_payload(const struct nrio_raw_info *info,
                                                               const uint8_t *table)
{
    uint8_t sector[SECTOR];
    if (!nrio_recovery_read(NRIO_RAW_SECONDARY_TABLE / SECTOR - 1, 1, sector) ||
        !memcmp(sector + SECTOR - 4, "QAFE", 4)) return NULL;
    for (uint32_t offset = 0; offset < NRIO_RAW_TABLE_BYTES; offset += SECTOR) {
        if (!nrio_recovery_read((NRIO_RAW_SECONDARY_TABLE + offset) / SECTOR, 1, sector) ||
            memcmp(table + offset, sector, SECTOR)) return NULL;
    }

    uint32_t first = word(table);
    if (first < UDISK_PAYLOAD_NAND_OFFSET || first >= 0x200000 ||
        !full_map_entry(first, info->sectors) ||
        !nrio_recovery_read(first / SECTOR, 1, sector) || !compact(sector)) return NULL;
    const struct boot_payload_range *payload = known_xulu_header(sector);
    if (!payload && (nrio_update_crc32(UINT32_MAX, sector, SECTOR) ^ UINT32_MAX) == stock_payload.header_crc &&
        nrio_udisk_classify(read_upgrade_sector, NULL, info->sectors) == NRIO_UDISK_LAYOUT_STOCK_145)
        payload = &stock_payload;
    if (!payload) return NULL;

    uint32_t crc = UINT32_MAX, previous = 0;
    for (uint32_t offset = 0; offset < payload->bytes; offset += MAP_BYTES) {
        uint32_t address = word(table + offset / MAP_BYTES * 4);
        if (address < UDISK_PAYLOAD_NAND_OFFSET || address >= 0x200000 ||
            address <= previous || !full_map_entry(address, info->sectors)) return NULL;
        previous = address;
        for (unsigned j = 0; j < MAP_BYTES && offset + j < payload->bytes; j += SECTOR) {
            if (!nrio_recovery_read((address + j) / SECTOR, 1, sector)) return NULL;
            unsigned bytes = payload->bytes - offset - j;
            crc = nrio_update_crc32(crc, sector, bytes < SECTOR ? bytes : SECTOR);
        }
    }
    return (crc ^ UINT32_MAX) == payload->image_crc ? payload : NULL;
}

void nrio_bypass_safe_mode(const char *root, const struct nrio_raw_info *info,
                           uint8_t *snapshot, nrio_block0_saver save)
{
    enum { TABLE_SECTORS = NRIO_RAW_TABLE_BYTES / SECTOR, PRIMARY_LBA = NRIO_RAW_PRIMARY_TABLE / SECTOR };
    uint8_t *table = malloc(NRIO_RAW_TABLE_BYTES);
    struct nrio_sector_patch *patch = malloc(TABLE_SECTORS * sizeof(*patch));
    if (!root || !save) {
        puts("Need independent backup storage.");
        goto done;
    }
    if (!table || !patch) { puts("Could not allocate table plan."); goto done; }

    struct nrio_boot_rom primary = nrio_inspect_boot(NRIO_RAW_PRIMARY_TABLE, info->sectors);
    if (primary.kind != NRIO_BOOT_STOCK && primary.kind != NRIO_BOOT_PATCHED &&
        primary.kind != NRIO_BOOT_UDISK && primary.kind != NRIO_BOOT_XULU) {
        puts("Primary safe mode unrecognized.\nNo changes made.");
        goto done;
    }

    puts("Checking mapped boot firmware...");
    const struct boot_payload_range *payload =
        nrio_recovery_read(NRIO_RAW_SECONDARY_TABLE / SECTOR, TABLE_SECTORS, table)
        ? bypass_source_payload(info, table) : NULL;
    if (!payload) {
        puts("Boot firmware unrecognized\nor unreadable. No changes made.");
        goto done;
    }
    if (!nrio_recovery_read_snapshot(0, snapshot, info->workspace_bytes)) {
        puts("Could not read boot block.");
        report_region_write_error();
        goto done;
    }

    /* Match the main boot header rule used by Restore block 0. */
    const uint8_t *header = snapshot + 2 * SECTOR;
    if (memcmp(header, "DSGB", 4) || memcmp(header + 0x0c, "DSGB", 4)) {
        puts("Boot backup would not be\nrestorable. No changes made.");
        goto done;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < TABLE_SECTORS; ++i) {
        uint32_t lba = PRIMARY_LBA + i;
        const uint8_t *expected = snapshot + (lba / (PAGE / SECTOR)) * (PAGE + SPARE) +
                                 lba % (PAGE / SECTOR) * SECTOR;
        const uint8_t *source = table + i * SECTOR;
        if (memcmp(expected, source, SECTOR))
            patch[count++] = (struct nrio_sector_patch){lba, source, expected};
    }
    if (!count) { printf("Already boots %s.\n", payload->label); goto done; }
    if (primary.kind == NRIO_BOOT_UDISK || primary.kind == NRIO_BOOT_XULU) {
        puts("Already routes to another map.\nNo changes made.");
        goto done;
    }
    if (!nrio_recovery_patch_snapshot(0, snapshot, patch, count, false, false)) {
        puts("Boot block preflight failed.");
        report_region_write_error();
        goto done;
    }
    if (!save(root, info, snapshot)) {
        puts("Could not save block 0 backup.");
        goto done;
    }
    nrio_region_screen("Bypass safe mode");
    printf("Boot %s directly.\n", payload->label);
    puts("Safe-mode ROM is not erased.\nUse Restore block 0 to undo.");
    if (!nrio_confirm_write()) { puts("Cancelled before write."); goto done; }
    nrio_region_screen("Rechecking boot map");
    if (bypass_source_payload(info, table) != payload) {
        puts("Source map or firmware changed.\nNo changes made.");
        goto done;
    }
    nrio_region_screen("Writing boot block");
    if (!nrio_recovery_patch_snapshot(0, snapshot, patch, count, true, false)) {
        report_region_write_error();
        goto done;
    }
    nrio_region_screen("Write complete");
    puts("Primary table verified.\nRestart to test direct boot.\nKeep your block 0 backup.");

done:
    free(patch);
    free(table);
}

static void install_stock_udisk(const char *root, const struct nrio_raw_info *info,
                                uint8_t *snapshot, bool bios)
{
    struct nrio_boot_rom primary = nrio_inspect_boot(NRIO_RAW_PRIMARY_TABLE, info->sectors);
    struct nrio_boot_rom secondary = nrio_inspect_boot(NRIO_RAW_SECONDARY_TABLE, info->sectors);
    enum nrio_udisk_layout layout = bios
        ? nrio_udisk_classify(read_upgrade_sector, NULL, info->sectors) : NRIO_UDISK_LAYOUT_UNKNOWN;
    bool known_safe_mode = primary.kind == NRIO_BOOT_STOCK || primary.kind == NRIO_BOOT_PATCHED;
    if (layout == NRIO_UDISK_LAYOUT_STOCK_145 &&
        (known_safe_mode || (primary.kind == NRIO_BOOT_UDISK && primary.address == secondary.address))) {
        region_report("%s\n", "Stock uDisk 1.45 detected.\nNo upgrade needed.");
        return;
    }
    if (primary.kind == NRIO_BOOT_UDISK || primary.kind == NRIO_BOOT_XULU) {
        region_report("%s\n", "Cart boots firmware directly.\nRestore its safe-mode route\nbefore replacing hidden code.");
        return;
    }
    if (!known_safe_mode) {
        region_report("%s\n", "Primary safe-mode route\ncould not be verified.");
        return;
    }
    bool replace_xulu = bios && secondary.kind == NRIO_BOOT_XULU;
    if (!replace_xulu && secondary.kind != NRIO_BOOT_UDISK && secondary.kind != NRIO_BOOT_UNKNOWN &&
        secondary.kind != NRIO_BOOT_ERASED) {
        region_report("%s\n", "Secondary firmware unrecognized.\nNo changes made.");
        return;
    }
    const uint32_t pages = nrio_recovery_pages_per_block();
    if (bios && isDSiMode() && primary.kind != NRIO_BOOT_PATCHED) {
        region_report("%s\n", "On DSi, patch this cart's\nsafe mode before upgrading.");
        return;
    }
    if (bios && !replace_xulu && (layout != NRIO_UDISK_LAYOUT_LEGACY_123 ||
                                  info->nand_id != 0x2555d5ec || pages != 128)) {
        region_report("%s\n", "Need a supported stock 1.23\nor hidden XuluMenu layout.");
        return;
    }
    if (pages != 64 && pages != 128) {
        region_report("%s\n", "Unsupported NAND geometry.");
        return;
    }
    if (bios && !replace_xulu) region_report("%s\n", "Recognized legacy 1.23 map.");

    uint8_t *map = NULL, *payload = NULL, *source_table = NULL;
    struct nrio_sector_patch *patches = NULL;
    struct verified_source source = {0};
    if (replace_xulu) {
        source_table = malloc(NRIO_RAW_TABLE_BYTES);
        if (!source_table ||
            !nrio_recovery_read(NRIO_RAW_SECONDARY_TABLE / SECTOR,
                                 NRIO_RAW_TABLE_BYTES / SECTOR, source_table) ||
            !(source.payload = bypass_source_payload(info, source_table)) ||
            source.payload == &stock_payload) {
            region_report("%s\n", "Installed XuluMenu failed\nverification. No changes made.");
            goto done;
        }
        source.table = source_table;
        region_report("Verified hidden %s.\n", source.payload->label);
    }
    const uint32_t block_sectors = nrio_recovery_pages_per_block() * PAGE / SECTOR;
    payload = malloc(block_sectors * SECTOR);
    if (!payload) {
        region_report("%s\n", "Not enough RAM for uDisk block.");
        goto done;
    }
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < NRIO_ASSET_UDISK_CHUNKS; ++i) {
        if (!nrio_asset_expand(NRIO_ASSET_UDISK_0 + i, payload, UDISK_ALLOCATION_BYTES)) {
            region_report("%s\n", "Stock 1.45 data missing or bad.");
            goto done;
        }
        crc = nrio_update_crc32(crc, payload, UDISK_ALLOCATION_BYTES);
    }
    if ((crc ^ UINT32_MAX) != 0x3dbef02e) {
        region_report("%s\n", "Wrong stock uDisk payload.");
        goto done;
    }
    region_report("%s\n", "Using built-in 1.45 data.");

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
        region_report("%s\n", replace_xulu
            ? "XuluMenu map is fragmented.\nStock replacement needs the\nstandard contiguous layout."
            : "Secondary map is neither stock\ncontiguous nor fully erased.");
        goto done;
    }

    /* An erased map cannot establish ownership of nonblank NAND. Check the
       entire intended stock write extent before making a plan. */
    if (empty_map) {
        for (uint32_t offset = 0; offset < UDISK_BYTES; offset += SECTOR) {
            if ((offset % UDISK_ALLOCATION_BYTES == 0 &&
                 !nrio_asset_expand(NRIO_ASSET_UDISK_0 + offset / UDISK_ALLOCATION_BYTES,
                                    payload, UDISK_ALLOCATION_BYTES)) ||
                !nrio_recovery_read((UDISK_PAYLOAD_NAND_OFFSET + offset) / SECTOR, 1, sector) ||
                (!blank(sector, SECTOR) && memcmp(sector, payload + offset % UDISK_ALLOCATION_BYTES, SECTOR))) {
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

    for (uint32_t offset = NRIO_RAW_TABLE_BYTES; !bios && offset < UDISK_ALLOCATION_BYTES; offset += SECTOR) {
        if (!nrio_recovery_read((NRIO_RAW_SECONDARY_TABLE + offset) / SECTOR, 1, sector) ||
            (!blank(sector, SECTOR) && memcmp(sector, map + offset, SECTOR))) {
            region_report("%s\n", "Secondary map tail contains\nother data or is unreadable.");
            goto done;
        }
    }

    for (uint32_t i = 0; i < payload_sectors; ++i)
        patches[i] = (struct nrio_sector_patch){UDISK_PAYLOAD_NAND_OFFSET / SECTOR + i,
                                               payload + (i % block_sectors) * SECTOR, NULL};
    for (uint32_t i = 0; i < map_sectors; ++i)
        patches[payload_sectors + i] = (struct nrio_sector_patch){NRIO_RAW_SECONDARY_TABLE / SECTOR + i,
                                                                 map + i * SECTOR, NULL};

    run_plan(root, info, snapshot, patches, payload_sectors + map_sectors,
             replace_xulu ? "Replace XuluMenu with stock" :
             bios ? "Install stock uDisk 1.45" : "Repair stock uDisk 1.45",
             false, payload, replace_xulu ? &source : NULL);

done:
    free(patches);
    free(map);
    free(payload);
    free(source_table);
}

void nrio_install_region(const char *root, const struct nrio_raw_info *info,
                         void *snapshot, bool udisk, bool bios)
{
    if (root) {
        mkdir(root, 0777);
        char path[56];
        snprintf(path, sizeof(path), "%s/ADVANCED.TXT", root);
        region_log = fopen(path, "a");
        if (!region_log) puts("Could not open ADVANCED.TXT.");
        if (region_log) fprintf(region_log, "\n%s, NAND %08lX\n",
                                bios ? "uDisk BIOS update" : udisk ? "uDisk repair" : "Safe-mode replacement",
                                (unsigned long)info->nand_id);
    }
    if (udisk) install_stock_udisk(root, info, snapshot, bios);
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
