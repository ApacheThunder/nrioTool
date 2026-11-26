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
static const struct boot_payload_range fire16_payload = {"Fire Link stock uDisk", 0x5d592e83,
                                                         UDISK_BYTES, 0x8502974b};

static const struct boot_payload_range *bypass_source_payload(const struct nrio_raw_info *info,
                                                               const uint8_t *table, bool allow_capture_padding);

static bool verify_archive_source(const struct nrio_raw_info *info,
                                   const struct nrio_udisk_archive *archive, const uint8_t *map,
                                   uint8_t *snapshot);

struct verified_source {
    const uint8_t *table;
    const struct boot_payload_range *payload;
    const struct nrio_udisk_archive *archive;
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

static uint32_t safe_mode_payload_word(const uint8_t *stub, uint32_t dest, uint32_t bytes)
{
    if (dest == 0x03800000 && bytes == 0x38)
        return nrio_safe_mode_handoff(stub);
    if (dest != 0x02380000 || bytes != SECTOR) return 0;
    if (nrio_safe_mode_crc16(stub, 0x6c) == 0x1a2d)
        return nrio_safe_mode_handoff(stub + 0x6c);
    if (nrio_safe_mode_crc16(stub, 0x3c) == 0x2382 &&
        word(stub + 0x74) == 0x38 && word(stub + 0x78) == 0x03800000)
        return nrio_safe_mode_handoff(stub + 0x3c);
    return 0;
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
    if (stock || patched) {
        rom.handoff = safe_mode_payload_word(data, dest, length);
        if (rom.handoff) rom.kind = stock ? NRIO_BOOT_STOCK : NRIO_BOOT_PATCHED;
    } else {
        rom.kind = xulu ? NRIO_BOOT_XULU : udisk ? NRIO_BOOT_UDISK : NRIO_BOOT_COMPACT;
    }
    return rom;
}

/* Fingerprint the executable separately from the ARM7 patch and banner,
   so a hardware report can distinguish safe-mode revisions. Read only. */
bool nrio_safe_mode_arm9_crc(uint32_t sectors, const struct nrio_boot_rom *rom, uint32_t *result)
{
    uint8_t sector[SECTOR];
    if (!rom || !result || (rom->kind != NRIO_BOOT_STOCK && rom->kind != NRIO_BOOT_PATCHED) ||
        rom->address >= SAFE_LIMIT || !full_map_entry(rom->address, sectors) ||
        !nrio_recovery_read(rom->address / SECTOR, 1, sector) || !compact(sector) ||
        word(sector + 0x80) != rom->size || !section_valid(sector, rom->size, 0x20))
        return false;
    uint32_t start = word(sector + 0x20), left = word(sector + 0x2c), offset = start;
    if (start % SECTOR || rom->size > SAFE_LIMIT) return false;
    uint32_t crc = UINT32_MAX, address;
    while (left) {
        if ((offset == start || offset % MAP_BYTES == 0) &&
            (!read_mapping(NRIO_RAW_PRIMARY_TABLE, offset, &address) ||
             !full_map_entry(address, sectors) || address < 0x18000 || address >= SAFE_LIMIT))
            return false;
        if (!nrio_recovery_read((address + offset % MAP_BYTES) / SECTOR, 1, sector))
            return false;
        const uint32_t used = left < SECTOR ? left : SECTOR;
        crc = nrio_update_crc32(crc, sector, used);
        left -= used;
        offset += used;
    }
    *result = crc ^ UINT32_MAX;
    return true;
}

const char *nrio_boot_kind_name(enum nrio_boot_kind kind)
{
    static const char *const names[] = {
        "Read failed", "Skipped (QAFE)", "Unknown layout", "Stock safe mode",
        "Patched safe mode", "uDisk 1.45 layout", "Other compact ROM", "First map entry erased",
        "Hidden XuluMenu header", "Direct boot DS ROM",
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

enum { XULU_CHUNKS = 8, XULU_BYTES = XULU_CHUNKS * UDISK_ALLOCATION_BYTES,
       TAGGED_CHUNK = UDISK_ALLOCATION_BYTES / SECTOR * UDISK_PACKED_SECTOR_BYTES };

struct hidden_payload {
    FILE *file;
    uint8_t *tagged;
    uint32_t chunks[XULU_CHUNKS];
    uint32_t raw_bytes;
};

static bool read_xulu_chunk(const struct hidden_payload *source, uint32_t i, uint8_t *buffer)
{
    if (i >= XULU_CHUNKS) return false;
    if (source->raw_bytes) {
        uint32_t offset = i * UDISK_ALLOCATION_BYTES;
        if (offset >= source->raw_bytes) {
            memset(buffer, 0xff, UDISK_ALLOCATION_BYTES);
            return true;
        }
        if (fseek(source->file, offset, SEEK_SET)) return false;
        uint32_t bytes = source->raw_bytes - offset;
        if (bytes > UDISK_ALLOCATION_BYTES) bytes = UDISK_ALLOCATION_BYTES;
        memset(buffer, 0xff, UDISK_ALLOCATION_BYTES);
        return fread(buffer, 1, bytes, source->file) == bytes;
    }
    if (fseek(source->file, i * TAGGED_CHUNK, SEEK_SET) ||
        fread(source->tagged, 1, TAGGED_CHUNK, source->file) != TAGGED_CHUNK) return false;

    /* The official payload uses MKR delimiters, not calculated ECC. The
       last record has a separate end marker. The release CRC covers data. */
    for (unsigned sector = 0; sector < UDISK_ALLOCATION_BYTES / SECTOR; ++sector) {
        const uint8_t *record = source->tagged + sector * UDISK_PACKED_SECTOR_BYTES;
        const bool last = i == XULU_CHUNKS - 1 &&
                          sector == UDISK_ALLOCATION_BYTES / SECTOR - 1;
        static const uint8_t end[] = {0xff, 0xff, 0};
        if (memcmp(record + SECTOR, last ? end : (const uint8_t *)"MKR", 3)) return false;
        memcpy(buffer + sector * SECTOR, record, SECTOR);
    }
    return true;
}

static bool load_bios_block(uint32_t lba, uint32_t sectors, uint8_t *buffer,
                            const struct hidden_payload *xulu)
{
    const uint32_t address = lba * SECTOR;
    if (address == UDISK_SECONDARY_MAP_NAND_OFFSET) return true;
    if (address < UDISK_PAYLOAD_NAND_OFFSET || !buffer ||
        sectors * SECTOR % UDISK_ALLOCATION_BYTES) return false;
    uint32_t chunk = (address - UDISK_PAYLOAD_NAND_OFFSET) / UDISK_ALLOCATION_BYTES;
    for (uint32_t offset = 0; offset < sectors * SECTOR; offset += UDISK_ALLOCATION_BYTES, ++chunk) {
        if (xulu) {
            if (!read_xulu_chunk(xulu, chunk, buffer + offset) ||
                (nrio_update_crc32(UINT32_MAX, buffer + offset, UDISK_ALLOCATION_BYTES) ^ UINT32_MAX)
                    != xulu->chunks[chunk]) return false;
        } else if (chunk >= NRIO_ASSET_UDISK_CHUNKS ||
                   !nrio_asset_expand(NRIO_ASSET_UDISK_0 + chunk, buffer + offset,
                                      UDISK_ALLOCATION_BYTES)) return false;
    }
    return true;
}

static bool read_saved_block(FILE *backup, const struct nrio_raw_info *info,
                             uint32_t lba, uint8_t *snapshot)
{
    uint32_t header[4];
    return fread(header, sizeof(header), 1, backup) == 1 &&
           header[0] == lba && header[1] == info->nand_id && header[2] == info->workspace_bytes &&
           fread(snapshot, info->workspace_bytes, 1, backup) == 1 &&
           header[3] == nrio_update_crc32(UINT32_MAX, snapshot, info->workspace_bytes);
}

/* Validate the whole backup before any erase. Checking only as each block
   is rewritten could stop halfway through if a later backup is corrupt. */
static bool validate_saved_region(FILE *backup, const struct nrio_raw_info *info,
                                  const struct nrio_sector_patch *patches, uint32_t count,
                                  uint32_t block_sectors, uint8_t *snapshot)
{
    long start = ftell(backup);
    if (start < 0) return false;
    for (uint32_t i = 0; i < count;) {
        uint32_t lba = patches[i].lba / block_sectors * block_sectors;
        if (!read_saved_block(backup, info, lba, snapshot)) return false;
        i = batch_limit(patches, count, i, lba, block_sectors);
    }
    return fgetc(backup) == EOF && !ferror(backup) && fseek(backup, start, SEEK_SET) == 0;
}

/* Spool the complete physical block snapshots before authorizing any erase. */
static void run_plan(const char *root, const struct nrio_raw_info *info, uint8_t *snapshot,
                     const struct nrio_sector_patch *patches, uint32_t count,
                     const char *title, bool allow_padding, uint8_t *bios_data,
                     const struct verified_source *source, const struct hidden_payload *xulu,
                     const char *warning)
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
        if (bios && lba && !load_bios_block(lba, block_sectors, bios_data, xulu)) {
            region_report("%s\n", "Could not decode uDisk block.");
            goto done;
        }
        if (!nrio_recovery_read_snapshot(lba, snapshot, info->workspace_bytes)) {
            region_report("%s\n", "Stopped during read-only check.");
            report_region_write_error();
            goto done;
        }
        const bool preflight = bios && lba
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
    if (warning) region_report("%s\n", warning);
    if (log) fflush(log);
    if (!nrio_confirm_write()) {
        nrio_region_screen("Write cancelled");
        region_report("%s\n", "Cancelled before write.");
        if (log) fputs("Cancelled before write.\n", log);
        goto done;
    }

    if (source && (source->archive
        ? !verify_archive_source(info, source->archive, source->table, snapshot)
        : bypass_source_payload(info, source->table, false) != source->payload)) {
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
            fread(&saved_blocks, sizeof(saved_blocks), 1, backup) != 1 || saved_blocks != block_count ||
            !validate_saved_region(backup, info, patches, count, block_sectors, snapshot)) {
            nrio_region_screen("Write stopped");
            region_report("%s\n", "Block backup did not verify.\nNo NAND writes were made.");
            goto done;
        }
    } else {
        puts("Rechecking NAND in memory.\nDo not turn off the console.");
    }

    uint32_t current_block = 1;
    for (uint32_t i = 0; i < count; ++current_block) {
        uint32_t lba = patches[i].lba / block_sectors * block_sectors;
        uint32_t end = batch_limit(patches, count, i, lba, block_sectors);
        bool matching;
        if (backup) {
            matching = read_saved_block(backup, info, lba, snapshot);
        } else {
            matching = nrio_recovery_read_snapshot(lba, snapshot, info->workspace_bytes) &&
                       ram_crc[current_block - 1] == nrio_update_crc32(UINT32_MAX, snapshot, info->workspace_bytes);
        }
        if (!matching || (bios && lba && !load_bios_block(lba, block_sectors, bios_data, xulu))) {
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
        const bool verified = bios && lba
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
    if (length < SECTOR || !compact(rom)) return false;
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

static bool recognized_map(struct nrio_boot_rom rom)
{
    return rom.kind == NRIO_BOOT_STOCK || rom.kind == NRIO_BOOT_PATCHED ||
           rom.kind == NRIO_BOOT_UDISK || rom.kind == NRIO_BOOT_XULU ||
           rom.kind == NRIO_BOOT_COMPACT || rom.kind == NRIO_BOOT_DIRECT_DS;
}

/* Some single-game carts route straight to an ordinary DS ROM. A valid
   header bounds the live map so new safe-mode data cannot overlap it. */
struct nrio_boot_rom nrio_inspect_primary_route(uint32_t sectors)
{
    struct nrio_boot_rom rom = nrio_inspect_boot(NRIO_RAW_PRIMARY_TABLE, sectors);
    uint8_t header[SECTOR];
    if (rom.kind != NRIO_BOOT_UNKNOWN || rom.address != NRIO_RAW_SECONDARY_TABLE)
        return rom;
    if (!nrio_recovery_read(rom.address / SECTOR, 1, header)) {
        rom.kind = NRIO_BOOT_READ_ERROR;
        return rom;
    }
    uint32_t size = word(header + 0x80);
    if (compact(header) || size < SECTOR ||
        nrio_safe_mode_crc16(header, 0x15e) != (header[0x15e] | header[0x15f] << 8) ||
        !section_valid(header, size, 0x20) || !section_valid(header, size, 0x30))
        return rom;
    rom.size = size;
    rom.kind = NRIO_BOOT_DIRECT_DS;
    return rom;
}

static bool map_disjoint(uint32_t table, struct nrio_boot_rom rom, uint32_t sectors,
                         uint32_t lo, uint32_t hi)
{
    if (!recognized_map(rom)) return false;
    if (!rom.size || rom.size > NRIO_RAW_TABLE_BYTES / 4 * MAP_BYTES) return false;
    for (uint32_t offset = 0; offset < rom.size; offset += MAP_BYTES) {
        uint32_t address;
        if (!read_mapping(table, offset, &address) || !full_map_entry(address, sectors) ||
            (address >= lo && address < hi)) return false;
    }
    return true;
}

static void put_word(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (8 * i));
}

/* Publish a new safe-mode route without writing over the active ROM. */
static void install_missing_safe(const char *root, const struct nrio_raw_info *info,
                                 uint8_t *snapshot, struct nrio_boot_rom primary)
{
    enum { FIRST = 0x20000, LIMIT = 0x40000, MAX_BYTES = LIMIT - FIRST };
    const uint32_t pages = nrio_recovery_pages_per_block();
    if (!root || (pages != 64 && pages != 128) ||
        !map_disjoint(NRIO_RAW_PRIMARY_TABLE, primary, info->sectors, FIRST, LIMIT)) {
        puts("Cannot verify a separate safe area.\nNo changes made.");
        return;
    }
    struct nrio_boot_rom secondary = nrio_inspect_boot(NRIO_RAW_SECONDARY_TABLE, info->sectors);
    const bool has_secondary = recognized_map(secondary);
    if (secondary.kind == NRIO_BOOT_READ_ERROR ||
        (has_secondary && !map_disjoint(NRIO_RAW_SECONDARY_TABLE, secondary, info->sectors, FIRST, LIMIT))) {
        puts("Secondary boot map conflicts\nor could not be verified.");
        return;
    }

    size_t length;
    uint8_t *image = load_file(root, "safe-replacement.nds", MAX_BYTES, &length);
    uint8_t *table = malloc(NRIO_RAW_TABLE_BYTES), *before = malloc(MAX_BYTES);
    uint8_t *primary_before = malloc(NRIO_RAW_TABLE_BYTES);
    struct nrio_sector_patch *patches = malloc((MAX_BYTES + NRIO_RAW_TABLE_BYTES) / SECTOR * sizeof(*patches));
    if (!image || !table || !before || !primary_before || !patches) goto done;
    if (!replacement_header(image, length)) {
        puts("Invalid safe-mode ROM or CRC.");
        goto done;
    }
    uint32_t used = word(image + 0x80);
    uint32_t rom_sectors = (used + SECTOR - 1) / SECTOR;
    uint32_t count = 0;
    if (!nrio_recovery_read(FIRST / SECTOR, MAX_BYTES / SECTOR, before) ||
        !nrio_recovery_read(NRIO_RAW_PRIMARY_TABLE / SECTOR, NRIO_RAW_TABLE_BYTES / SECTOR, primary_before))
        goto done;

    /* Some bypassed carts still contain their old safe mode. Recognize only
       a contiguous stock/patched ARM7 handoff before replacing its bytes. */
    uint32_t old_size = 0;
    if (replacement_header(before, MAX_BYTES)) old_size = word(before + 0x80);
    else if (compact(before) && word(before + 0x80) >= SECTOR && word(before + 0x80) <= MAX_BYTES &&
             replacement_header(before, word(before + 0x80))) old_size = word(before + 0x80);
    if (old_size) {
        uint32_t arm7 = word(before + 0x30), dest = word(before + 0x38), bytes = word(before + 0x3c);
        const uint8_t *stub = before + arm7;
        if (!safe_mode_payload_word(stub, dest, bytes))
            old_size = 0;
    }
    /* Find a physical chunk for each logical 16 KiB chunk in order. An
       unrelated non-erased byte can only stay if it already matches the
       image. Bytes outside the ROM's declared size are never overlaid.
       Prefer the original contiguous placement whenever it fits. */
    uint32_t mapped[MAX_BYTES / MAP_BYTES];
    uint32_t next = 0, skipped = 0;
    for (uint32_t offset = 0; offset < used; offset += MAP_BYTES) {
        const uint32_t bytes = used - offset < MAP_BYTES ? used - offset : MAP_BYTES;
        while (next < MAX_BYTES) {
            bool writable = true;
            for (uint32_t i = 0; i < bytes; ++i)
                if (next + i >= old_size && before[next + i] != 0xff &&
                    before[next + i] != image[offset + i]) {
                    writable = false;
                    break;
                }
            if (writable) break;
            next += MAP_BYTES;
            ++skipped;
        }
        if (next == MAX_BYTES) {
            region_report("Not enough usable safe chunks.\nNeed %lu, found %lu.\nNo changes made.\n",
                          (unsigned long)((used + MAP_BYTES - 1) / MAP_BYTES),
                          (unsigned long)(offset / MAP_BYTES));
            goto done;
        }
        mapped[offset / MAP_BYTES] = next;
        next += MAP_BYTES;
    }

    uint8_t *padded = malloc(rom_sectors * SECTOR);
    if (!padded) goto done;
    for (uint32_t i = 0; i < rom_sectors; ++i) {
        const uint32_t logical = i * SECTOR;
        const uint32_t physical = mapped[logical / MAP_BYTES] + logical % MAP_BYTES;
        const uint32_t bytes = used - logical < SECTOR ? used - logical : SECTOR;
        memcpy(padded + logical, before + physical, SECTOR);
        memcpy(padded + logical, image + logical, bytes);
        patches[count++] = (struct nrio_sector_patch){(FIRST + physical) / SECTOR,
            padded + logical, before + physical};
    }

    memset(table, 0xff, NRIO_RAW_TABLE_BYTES);
    for (uint32_t offset = 0; offset < used; offset += MAP_BYTES)
        put_word(table + offset / MAP_BYTES * 4, FIRST + mapped[offset / MAP_BYTES]);
    for (uint32_t i = 0; i < NRIO_RAW_TABLE_BYTES / SECTOR; ++i)
        patches[count++] = (struct nrio_sector_patch){NRIO_RAW_PRIMARY_TABLE / SECTOR + i,
            table + i * SECTOR, primary_before + i * SECTOR};
    /* On 128-page NAND both regions share block 0 and must be
       rewritten together. On 64-page NAND publish the map last. */
    if (pages == 128) qsort(patches, count, sizeof(*patches), compare_patch);
    const char *warning = has_secondary
        ? "Direct boot will be replaced.\nSecondary firmware is kept."
        : "No usable secondary uDisk.\nNormal boot may need uDisk.\nOld ROM data is kept.";
    char placement_warning[144];
    if (skipped) {
        snprintf(placement_warning, sizeof(placement_warning), "Skipping %lu occupied chunk(s).\n%s",
                 (unsigned long)skipped, warning);
        warning = placement_warning;
    }
    run_plan(root, info, snapshot, patches, count, "Install missing safe mode", false,
             NULL, NULL, NULL, warning);
    free(padded);
done:
    free(patches);
    free(primary_before);
    free(before);
    free(table);
    free(image);
}

static void install_safe_replacement(const char *root, const struct nrio_raw_info *info,
                                     uint8_t *snapshot)
{
    struct nrio_boot_rom boot = nrio_inspect_primary_route(info->sectors);
    if (boot.kind != NRIO_BOOT_STOCK && boot.kind != NRIO_BOOT_PATCHED) {
        if (recognized_map(boot) && boot.address != 0x20000)
            install_missing_safe(root, info, snapshot, boot);
        else
            region_report("Primary: %s.\nNo verified boot route.\n", nrio_boot_kind_name(boot.kind));
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
    /* Diagnose cross-revision boot risk on the confirmation screen.
       A successful ROM write does not test the uDisk handoff. */
    char handoff_note[120];
    const char *warning = NULL;
    uint32_t dest = word(replacement + 0x38);
    uint32_t incoming = safe_mode_payload_word(replacement + word(replacement + 0x30),
                                               dest, word(replacement + 0x3c));
    if (!incoming) {
        warning = "Replacement handoff unknown.\nBoot compatibility unverified.";
    } else if (incoming != boot.handoff) {
        snprintf(handoff_note, sizeof(handoff_note),
                 "ARM7 handoff differs.\n%08lX -> %08lX\nuDisk may not boot.",
                 (unsigned long)boot.handoff, (unsigned long)incoming);
        warning = handoff_note;
    } else {
        uint32_t old_arm9 = word(original + 0x2c), new_arm9 = word(replacement + 0x2c);
        if (old_arm9 != new_arm9 ||
            memcmp(original + word(original + 0x20), replacement + word(replacement + 0x20), old_arm9))
            warning = "Replacement ARM9 differs.\nUse this cart's own ROM if\nuDisk boot is untested.";
    }
    qsort(patches, count, sizeof(*patches), compare_patch);
    free(original);
    original = NULL;
    free(replacement);
    replacement = NULL;
    run_plan(root, info, snapshot, patches, count, "Install safe-mode replacement",
             true, NULL, NULL, NULL, warning);

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
                                                               const uint8_t *table, bool allow_capture_padding)
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
    crc ^= UINT32_MAX;
    if (crc == payload->image_crc) return payload;
    /* The captured Fire16 differs by one padding bit. This exception is
       limited to redirecting the boot table, not rewriting the runtime. */
    if (allow_capture_padding && payload == &stock_payload && crc == fire16_payload.image_crc &&
        nrio_identify_udisk_capture(read_upgrade_sector, NULL, info->sectors) == NRIO_UDISK_CAPTURE_FIRE16)
        return &fire16_payload;
    return NULL;
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
        ? bypass_source_payload(info, table, true) : NULL;
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
    if (bypass_source_payload(info, table, true) != payload) {
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

/* Archive recognition is intentionally not enough to flash. Require
   the whole archived image, a standard contiguous map, and a known
   footer. The current app builds the canonical map independently. */
static bool verify_archive_source(const struct nrio_raw_info *info,
                                   const struct nrio_udisk_archive *archive, const uint8_t *map,
                                   uint8_t *snapshot)
{
    uint8_t sector[SECTOR];
    if (!archive || !archive->image_crc[0] ||
        nrio_identify_archived_udisk(read_upgrade_sector, NULL, info->sectors,
                                    NRIO_RAW_SECONDARY_TABLE) != archive)
        return false;
    for (uint32_t offset = 0; offset < UDISK_ALLOCATION_BYTES; offset += SECTOR) {
        if (!nrio_recovery_read((NRIO_RAW_SECONDARY_TABLE + offset) / SECTOR, 1, sector))
            return false;
        if (offset == UDISK_ALLOCATION_BYTES - SECTOR && sector[SECTOR - 8] == 2)
            sector[SECTOR - 8] = 3; /* Earlier stock writer uses the same footer with revision 2. */
        if (memcmp(sector, map + offset, SECTOR)) return false;
    }

    uint32_t crc = UINT32_MAX;
    const uint32_t bytes = archive->wrapped ? archive->bytes : UDISK_BYTES;
    const uint32_t old_extent = (bytes + UDISK_ALLOCATION_BYTES - 1) /
                                UDISK_ALLOCATION_BYTES * UDISK_ALLOCATION_BYTES;
    if (old_extent < UDISK_BYTES) {
        /* An unmapped block is not necessarily free. Check both main
           and spare bytes before taking the extra space for 1.45. */
        const uint32_t pages = nrio_recovery_pages_per_block();
        const uint32_t block_bytes = pages * PAGE;
        if ((pages != 64 && pages != 128) || old_extent % block_bytes || UDISK_BYTES % block_bytes)
            return false;
        for (uint32_t offset = old_extent; offset < UDISK_BYTES; offset += block_bytes) {
            if (!nrio_recovery_read_snapshot((UDISK_PAYLOAD_NAND_OFFSET + offset) / SECTOR,
                                              snapshot, info->workspace_bytes) ||
                !blank(snapshot, pages * (PAGE + SPARE)))
                return false;
        }
    }
    for (uint32_t offset = 0; offset < bytes; offset += SECTOR) {
        if (!nrio_recovery_read((UDISK_PAYLOAD_NAND_OFFSET + offset) / SECTOR, 1, sector))
            return false;
        const uint32_t used = bytes - offset < SECTOR ? bytes - offset : SECTOR;
        crc = nrio_update_crc32(crc, sector, used);
    }
    crc ^= UINT32_MAX;
    for (unsigned i = 0; i < sizeof(archive->image_crc) / sizeof(*archive->image_crc); ++i)
        if (archive->image_crc[i] && crc == archive->image_crc[i]) return true;
    return false;
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
    const struct nrio_udisk_archive *archive = NULL;
    if (bios && layout != NRIO_UDISK_LAYOUT_LEGACY_123 &&
        (secondary.kind == NRIO_BOOT_COMPACT || secondary.kind == NRIO_BOOT_UNKNOWN)) {
        archive = nrio_identify_archived_udisk(read_upgrade_sector, NULL, info->sectors,
                                                NRIO_RAW_SECONDARY_TABLE);
        if (archive && !archive->image_crc[0]) archive = NULL;
    }
    if (archive && !root) {
        region_report("%s\n", "Need SD or Slot-2 for backup.");
        return;
    }
    if (!replace_xulu && !archive && secondary.kind != NRIO_BOOT_UDISK && secondary.kind != NRIO_BOOT_UNKNOWN &&
        secondary.kind != NRIO_BOOT_ERASED) {
        region_report("%s\n", "Secondary firmware unrecognized.\nNo changes made.");
        return;
    }
    const uint32_t pages = nrio_recovery_pages_per_block();
    if (bios && isDSiMode() && primary.kind != NRIO_BOOT_PATCHED) {
        region_report("%s\n", "On DSi, patch this cart's\nsafe mode before upgrading.");
        return;
    }
    if (bios && !replace_xulu && !archive && (layout != NRIO_UDISK_LAYOUT_LEGACY_123 ||
                                  info->nand_id != 0x2555d5ec || pages != 128)) {
        region_report("%s\n", "Need a verified stock uDisk\nor hidden XuluMenu layout.");
        return;
    }
    if (pages != 64 && pages != 128) {
        region_report("%s\n", "Unsupported NAND geometry.");
        return;
    }
    if (bios && !replace_xulu && !archive) region_report("%s\n", "Recognized legacy 1.23 map.");

    uint8_t *map = NULL, *payload = NULL, *source_table = NULL, *archive_map = NULL;
    struct nrio_sector_patch *patches = NULL;
    struct verified_source source = {0};
    if (replace_xulu) {
        source_table = malloc(NRIO_RAW_TABLE_BYTES);
        if (!source_table ||
            !nrio_recovery_read(NRIO_RAW_SECONDARY_TABLE / SECTOR,
                                 NRIO_RAW_TABLE_BYTES / SECTOR, source_table) ||
            !(source.payload = bypass_source_payload(info, source_table, false)) ||
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
    if (!archive && !stock_map && !empty_map) {
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
    if (archive) {
        const uint8_t *expected = map;
        if (archive->wrapped) {
            archive_map = malloc(UDISK_ALLOCATION_BYTES);
            if (!archive_map) goto done;
            const uint32_t old = (archive->bytes + UDISK_ALLOCATION_BYTES - 1) / UDISK_ALLOCATION_BYTES;
            if (!old || old > UDISK_ALLOCATION_COUNT) goto done;
            for (unsigned i = 0; i < UDISK_ALLOCATION_COUNT; ++i)
                allocations[i] = i < old ? UDISK_PAYLOAD_NAND_OFFSET + i * UDISK_ALLOCATION_BYTES
                                        : UINT32_MAX;
            udisk_build_secondary_chunk(allocations, archive_map);
            expected = archive_map;
        }
        if (!verify_archive_source(info, archive, expected, snapshot)) {
            region_report("%s\n", "Archived firmware or map differs.\nNo changes made.");
            goto done;
        }
        source.archive = archive;
        source.table = expected;
        region_report("Verified %s.\n", archive->name);
    }


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
             false, payload, (replace_xulu || archive) ? &source : NULL, NULL, NULL);

done:
    free(patches);
    free(map);
    free(payload);
    free(source_table);
    free(archive_map);
}

void nrio_install_hidden_xulu(const char *root, const struct nrio_raw_info *info, uint8_t *snapshot)
{
    static const char title[] = "Install XuluMenu to NAND";
    if (!root) {
        puts("Independent storage required.\nUsed for payload and backup.");
        return;
    }
    struct nrio_boot_rom primary = nrio_inspect_boot(NRIO_RAW_PRIMARY_TABLE, info->sectors);
    if (primary.kind != NRIO_BOOT_PATCHED && (primary.kind != NRIO_BOOT_STOCK || isDSiMode())) {
        puts("Need a verified safe-mode route.\nPatch safe mode on DSi first.");
        return;
    }
    if (!primary.size || primary.size >= NRIO_RAW_SECONDARY_TABLE) return;
    for (uint32_t at = 0; at < primary.size; at += MAP_BYTES) {
        uint32_t address;
        if (!read_mapping(NRIO_RAW_PRIMARY_TABLE, at, &address) ||
            address < 0x18000 || address > NRIO_RAW_SECONDARY_TABLE - MAP_BYTES ||
            !full_map_entry(address, info->sectors)) {
            puts("Safe-mode map leaves its region.\nNo changes made.");
            return;
        }
    }

    const uint32_t pages = nrio_recovery_pages_per_block();
    if ((pages != 64 && pages != 128) || info->sectors < (0x180000 / SECTOR)) return;
    const uint32_t block_sectors = pages * PAGE / SECTOR;
    const uint32_t payload_sectors = XULU_BYTES / SECTOR, map_sectors = UDISK_ALLOCATION_BYTES / SECTOR;
    struct hidden_payload xulu = {0};
    uint8_t *source_table = malloc(NRIO_RAW_TABLE_BYTES), *map = malloc(UDISK_ALLOCATION_BYTES);
    uint8_t *payload = malloc(block_sectors * SECTOR);
    struct nrio_sector_patch *patches = malloc((payload_sectors + map_sectors) * sizeof(*patches));
    xulu.tagged = malloc(TAGGED_CHUNK);
    if (!source_table || !map || !payload || !patches || !xulu.tagged) {
        puts("Not enough RAM for install.");
        goto done;
    }
    if (!nrio_recovery_read(NRIO_RAW_SECONDARY_TABLE / SECTOR,
                             NRIO_RAW_TABLE_BYTES / SECTOR, source_table)) goto done;
    const struct boot_payload_range *existing = bypass_source_payload(info, source_table, false);
    if (existing == &xulu_releases[2]) {
        puts("XuluMenu 1.4.1 is already on NAND.");
        goto done;
    }
    if (existing != &stock_payload) {
        puts("Need verified stock uDisk 1.45.\nNo changes made.");
        goto done;
    }
    struct verified_source source = {.table = source_table, .payload = &stock_payload};
    uint32_t allocations[UDISK_ALLOCATION_COUNT];
    for (unsigned i = 0; i < UDISK_ALLOCATION_COUNT; ++i)
        allocations[i] = i < NRIO_ASSET_UDISK_CHUNKS
                         ? UDISK_PAYLOAD_NAND_OFFSET + i * UDISK_ALLOCATION_BYTES : UINT32_MAX;
    udisk_build_secondary_chunk(allocations, map);
    if (memcmp(source_table, map, NRIO_RAW_TABLE_BYTES)) {
        puts("Stock map is not contiguous.\nNo changes made.");
        goto done;
    }
    uint8_t sector[SECTOR];
    for (uint32_t at = NRIO_RAW_TABLE_BYTES; at < UDISK_ALLOCATION_BYTES; at += SECTOR) {
        if (!nrio_recovery_read((NRIO_RAW_SECONDARY_TABLE + at) / SECTOR, 1, sector) ||
            (!blank(sector, SECTOR) && memcmp(sector, map + at, SECTOR))) {
            puts("Unexpected secondary map data.\nNo changes made.");
            goto done;
        }
    }
    for (uint32_t lba = (UDISK_PAYLOAD_NAND_OFFSET + UDISK_BYTES) / SECTOR;
         lba < (UDISK_PAYLOAD_NAND_OFFSET + XULU_BYTES) / SECTOR; lba += block_sectors) {
        if (!nrio_recovery_read_snapshot(lba, snapshot, info->workspace_bytes) ||
            !blank(snapshot, info->workspace_bytes)) {
            puts("Extra XuluMenu blocks not blank.\nNo changes made.");
            goto done;
        }
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/xulumenu.bin", root);
    xulu.file = fopen(path, "rb");
    if (!xulu.file || fseek(xulu.file, 0, SEEK_END) || ftell(xulu.file) != XULU_CHUNKS * TAGGED_CHUNK) {
        puts("Need normal XuluMenu 1.4.1\nxulumenu.bin in nrioFiles.");
        goto done;
    }
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < XULU_CHUNKS; ++i) {
        if (!read_xulu_chunk(&xulu, i, payload)) {
            puts("XuluMenu payload tags invalid.");
            goto done;
        }
        crc = nrio_update_crc32(crc, payload, UDISK_ALLOCATION_BYTES);
        xulu.chunks[i] = nrio_update_crc32(UINT32_MAX, payload, UDISK_ALLOCATION_BYTES) ^ UINT32_MAX;
    }
    if ((crc ^ UINT32_MAX) != 0x614e2042) {
        puts("Wrong XuluMenu release data.\nNo changes made.");
        goto done;
    }

    for (unsigned i = NRIO_ASSET_UDISK_CHUNKS; i < XULU_CHUNKS; ++i)
        allocations[i] = UDISK_PAYLOAD_NAND_OFFSET + i * UDISK_ALLOCATION_BYTES;
    udisk_build_secondary_chunk(allocations, map);
    for (uint32_t i = 0; i < payload_sectors; ++i)
        patches[i] = (struct nrio_sector_patch){UDISK_PAYLOAD_NAND_OFFSET / SECTOR + i,
                                               payload + i % block_sectors * SECTOR, NULL};
    for (uint32_t i = 0; i < map_sectors; ++i)
        patches[payload_sectors + i] = (struct nrio_sector_patch){NRIO_RAW_SECONDARY_TABLE / SECTOR + i,
                                                                 map + i * SECTOR, NULL};
    run_plan(root, info, snapshot, patches, payload_sectors + map_sectors,
             title, false, payload, &source, &xulu, NULL);

done:
    if (xulu.file) fclose(xulu.file);
    free(xulu.tagged);
    free(patches);
    free(payload);
    free(map);
    free(source_table);
}

/* A raw compact SRL, not a normal .nds or an updater with sector tags. */
void nrio_install_custom_udisk(const char *root, const struct nrio_raw_info *info, uint8_t *snapshot)
{
    const char *title = "Install custom uDisk";
    if (!root) {
        puts("Need independent storage\nfor the ROM and backup.");
        return;
    }
    uint32_t pages = nrio_recovery_pages_per_block();
    if ((pages != 64 && pages != 128) || info->sectors < 0x180000 / SECTOR) return;
    uint32_t block_sectors = pages * PAGE / SECTOR;
    struct nrio_boot_rom primary = nrio_inspect_primary_route(info->sectors);
    struct nrio_boot_rom secondary = nrio_inspect_boot(NRIO_RAW_SECONDARY_TABLE, info->sectors);
    if (!recognized_map(primary) || secondary.kind == NRIO_BOOT_READ_ERROR ||
        secondary.kind == NRIO_BOOT_SKIPPED) {
        puts("Boot route could not be verified.\nNo changes made.");
        return;
    }

    const bool safe = primary.kind == NRIO_BOOT_STOCK || primary.kind == NRIO_BOOT_PATCHED;
    const bool seed_secondary = safe && secondary.kind == NRIO_BOOT_ERASED;
    bool direct = (!safe && secondary.kind == NRIO_BOOT_ERASED) ||
                  (secondary.kind == NRIO_BOOT_UNKNOWN && primary.address == NRIO_RAW_SECONDARY_TABLE);
    if ((!direct && !seed_secondary && !recognized_map(secondary)) || (direct && safe)) {
        puts("No usable custom uDisk route.\nActive safe mode is kept.");
        return;
    }
    uint32_t map_address = direct ? NRIO_RAW_PRIMARY_TABLE : NRIO_RAW_SECONDARY_TABLE;
    struct nrio_boot_rom current = direct ? primary : secondary;
    bool replace_runtime = current.address == UDISK_PAYLOAD_NAND_OFFSET;
    /* Do not confuse a random compact ROM in the secondary slot with a
       writable standard runtime allocation. No relocation is attempted. */
    if (!direct && !seed_secondary && !replace_runtime) {
        puts("Secondary ROM is not in the\nstandard uDisk area.");
        return;
    }
    uint32_t old_chunks = 0;
    if (replace_runtime) {
        if (!current.size || current.size > XULU_BYTES) return;
        old_chunks = (current.size + MAP_BYTES - 1) / MAP_BYTES;
        for (uint32_t i = 0; i < old_chunks; ++i) {
            uint32_t address;
            if (!read_mapping(map_address, i * MAP_BYTES, &address) ||
                address != UDISK_PAYLOAD_NAND_OFFSET + i * MAP_BYTES) {
                puts("Current uDisk map is fragmented.");
                return;
            }
        }
    } else if (!seed_secondary && !map_disjoint(map_address, current, info->sectors,
                              UDISK_PAYLOAD_NAND_OFFSET, UDISK_PAYLOAD_NAND_OFFSET + XULU_BYTES)) {
        puts("Current ROM overlaps uDisk area.");
        return;
    }

    struct hidden_payload input = {0};
    uint8_t *buffer = malloc(block_sectors * SECTOR), *map = malloc(UDISK_ALLOCATION_BYTES);
    uint8_t *before = malloc(UDISK_ALLOCATION_BYTES);
    struct nrio_sector_patch *patches = NULL;
    char path[64];
    snprintf(path, sizeof(path), "%s/udisk-replacement.nds", root);
    input.file = fopen(path, "rb");
    if (!buffer || !map || !before || !input.file || fseek(input.file, 0, SEEK_END)) {
        puts("Need udisk-replacement.nds\nin nrioFiles and free RAM.");
        goto done;
    }
    long length = ftell(input.file);
    if (length < SECTOR || length > XULU_BYTES) {
        puts("Custom ROM size is invalid.");
        goto done;
    }
    input.raw_bytes = (uint32_t)length;
    if (!read_xulu_chunk(&input, 0, buffer)) goto done;
    uint32_t used = word(buffer + 0x80);
    if (used != input.raw_bytes || !replacement_header(buffer, used)) {
        puts("Need a compact ROM whose file\nsize matches its header.");
        goto done;
    }

    uint32_t chunks = (input.raw_bytes + UDISK_ALLOCATION_BYTES - 1) / UDISK_ALLOCATION_BYTES;
    uint32_t blocks = (chunks * UDISK_ALLOCATION_BYTES + block_sectors * SECTOR - 1) /
                      (block_sectors * SECTOR);
    uint32_t payload_sectors = blocks * block_sectors;
    uint32_t map_sectors = (direct ? NRIO_RAW_TABLE_BYTES : UDISK_ALLOCATION_BYTES) / SECTOR;
    uint32_t end = UDISK_PAYLOAD_NAND_OFFSET + payload_sectors * SECTOR;
    if (!direct && !map_disjoint(NRIO_RAW_PRIMARY_TABLE, primary, info->sectors,
                                  UDISK_PAYLOAD_NAND_OFFSET, end)) {
        /* If both routes point to the same contiguous runtime, keep the
           existing primary map. Don't extend beyond what it already maps. */
        if (primary.address != current.address || primary.size != current.size || used > current.size) {
            puts("Primary route overlaps uDisk.");
            goto done;
        }
        for (uint32_t i = 0; i < old_chunks; ++i) {
            uint32_t address;
            if (!read_mapping(NRIO_RAW_PRIMARY_TABLE, i * MAP_BYTES, &address) ||
                address != UDISK_PAYLOAD_NAND_OFFSET + i * MAP_BYTES) goto done;
        }
    }

    /* Outside declared old allocations, accept only erased NAND records. */
    for (uint32_t lba = UDISK_PAYLOAD_NAND_OFFSET / SECTOR; lba < end / SECTOR; lba += block_sectors) {
        if (!nrio_recovery_read_snapshot(lba, snapshot, info->workspace_bytes)) goto done;
        for (uint32_t page = 0; page < pages; ++page) {
            uint32_t address = lba * SECTOR + page * PAGE;
            if (address >= UDISK_PAYLOAD_NAND_OFFSET + old_chunks * MAP_BYTES &&
                !blank(snapshot + page * (PAGE + SPARE), PAGE + SPARE)) {
                puts("Extra uDisk area is not erased.\nNo changes made.");
                goto done;
            }
        }
    }
    for (uint32_t i = 0; i < chunks; ++i) {
        if (!read_xulu_chunk(&input, i, buffer)) goto done;
        input.chunks[i] = nrio_update_crc32(UINT32_MAX, buffer, UDISK_ALLOCATION_BYTES) ^ UINT32_MAX;
    }
    /* A 128-page erase block can include one trailing padding allocation. */
    if (chunks * UDISK_ALLOCATION_BYTES < payload_sectors * SECTOR) {
        memset(buffer, 0xff, UDISK_ALLOCATION_BYTES);
        input.chunks[chunks] = nrio_update_crc32(UINT32_MAX, buffer, UDISK_ALLOCATION_BYTES) ^ UINT32_MAX;
    }

    if (direct && secondary.kind == NRIO_BOOT_ERASED &&
        (!nrio_recovery_read(NRIO_RAW_SECONDARY_TABLE / SECTOR, NRIO_RAW_TABLE_BYTES / SECTOR, before) ||
         !blank(before, NRIO_RAW_TABLE_BYTES))) {
        puts("Secondary table is not empty.\nNo changes made.");
        goto done;
    }
    if (!nrio_recovery_read(map_address / SECTOR, map_sectors, before)) goto done;
    uint32_t allocations[UDISK_ALLOCATION_COUNT];
    for (uint32_t i = 0; i < UDISK_ALLOCATION_COUNT; ++i)
        allocations[i] = i < chunks ? UDISK_PAYLOAD_NAND_OFFSET + i * UDISK_ALLOCATION_BYTES : UINT32_MAX;
    udisk_build_secondary_chunk(allocations, map);
    if (seed_secondary && !blank(before, UDISK_ALLOCATION_BYTES)) {
        puts("Secondary table area not blank.");
        goto done;
    }
    if (!direct) {
        /* Preserve unrelated data outside the boot table; reject unexpected
           data that would be lost in the standard writer's map window. */
        for (uint32_t i = NRIO_RAW_TABLE_BYTES; i < UDISK_ALLOCATION_BYTES; ++i) {
            uint8_t tail = before[i];
            if (tail != 0xff && tail != map[i]) {
                puts("Secondary map tail differs.\nNo changes made.");
                goto done;
            }
        }
    }
    patches = malloc((payload_sectors + map_sectors) * sizeof(*patches));
    if (!patches) goto done;
    for (uint32_t i = 0; i < payload_sectors; ++i)
        patches[i] = (struct nrio_sector_patch){UDISK_PAYLOAD_NAND_OFFSET / SECTOR + i,
            buffer + i % block_sectors * SECTOR, NULL};
    for (uint32_t i = 0; i < map_sectors; ++i)
        patches[payload_sectors + i] = (struct nrio_sector_patch){map_address / SECTOR + i,
            map + i * SECTOR, before + i * SECTOR};
    const char *warning = direct
        ? "No secondary table is available.\nPrimary 0x8000 will boot uDisk."
        : seed_secondary ? "Install missing uDisk table\nat secondary 0x40000."
                         : "Secondary 0x40000 will boot\nthe supplied uDisk ROM.";
    run_plan(root, info, snapshot, patches, payload_sectors + map_sectors, title,
             false, buffer, NULL, &input, warning);
done:
    if (input.file) fclose(input.file);
    free(patches);
    free(before);
    free(map);
    free(buffer);
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
