#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nds.h>

#include "region-recovery.h"
#include "boot-report.h"
#include "fireline-layout.h"
#include "boot-spare.h"
#include "c1-diagnostics.h"
#include "cart-probe.h"
#include "block0-backup-check.h"
#include "rom-dump-ui.h"
#include "recovery_nand/raw_nand.h"

enum { SECTOR = 512, PAGE = 2048, SPARE = 64, MAP_BLOCK = 0x4000, BACKUP_LIMIT = 1000 };
static const char backup_tag[8] = "NRIOB01";
static const uint32_t anchor_lbas[] = {0x40000 / SECTOR, 0x80000 / SECTOR};

struct backup_info {
    char tag[8];
    uint32_t nand_id, pages, main_crc, spare_crc;
    uint8_t anchors[2][SECTOR];
};

static uint8_t restore_image[NRIO_RAW_WORKSPACE_BYTES];
static uint32_t workspace[NRIO_RAW_WORKSPACE_BYTES / sizeof(uint32_t)];
static uint8_t page_data[PAGE];
static uint8_t spare_data[SECTOR];

static uint32_t word(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool select_view(enum nrio_raw_mode mode, struct nrio_raw_info *info)
{
    return nrio_open_cart_view(mode, info);
}

static bool open_backup(const char *root, char *directory, char *main_path, char *spare_path)
{
    for (int i = 0; i < BACKUP_LIMIT; ++i) {
        snprintf(directory, 40, "%s/block0-%03d", root, i);
        if (mkdir(directory, 0777) == 0) {
            snprintf(main_path, 56, "%s/main.bin", directory);
            snprintf(spare_path, 56, "%s/spare.bin", directory);
            return true;
        }
        if (errno != EEXIST) return false;
    }
    return false;
}

uint32_t nrio_update_crc32(uint32_t crc, const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (crc & 1 ? UINT32_C(0xedb88320) : 0);
    }
    return crc;
}

static bool read_anchors(uint8_t anchors[2][SECTOR])
{
    for (unsigned i = 0; i < 2; ++i) {
        if (!nrio_recovery_read(anchor_lbas[i], 1, anchors[i])) return false;
    }
    return true;
}

static bool informative_anchors(const uint8_t anchors[2][SECTOR])
{
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned j = 1; j < SECTOR; ++j) {
            if (anchors[i][j] != anchors[i][0]) return true;
        }
    }
    return false;
}

static bool write_chunk(FILE *file, const uint8_t *data, size_t size, uint32_t *crc)
{
    if (fwrite(data, 1, size, file) != size) return false;
    *crc = nrio_update_crc32(*crc, data, size);
    return true;
}

static bool save_backup_impl(const char *root, bool prepared, const struct nrio_raw_info *initial,
                             const uint8_t *records)
{
    struct backup_info info = {
        .nand_id = initial->nand_id,
        .pages = prepared ? nrio_recovery_backup_pages() : nrio_recovery_pages_per_block(),
        .main_crc = UINT32_MAX,
        .spare_crc = UINT32_MAX,
    };
    if (info.pages != 64 && info.pages != 128) return false;
    if (!read_anchors(info.anchors)) return false;
    memcpy(info.tag, backup_tag, sizeof(info.tag));

    char directory[40], main_path[56], spare_path[56], info_path[56];
    if (!open_backup(root, directory, main_path, spare_path)) return false;
    snprintf(info_path, sizeof(info_path), "%s/backup.info", directory);
    FILE *main = fopen(main_path, "wb");
    FILE *spare = fopen(spare_path, "wb");
    bool ok = main && spare;
    for (uint32_t i = 0; ok && i < info.pages; ++i) {
        if (records) {
            const uint8_t *record = records + i * (PAGE + SPARE);
            memcpy(page_data, record, PAGE);
            memcpy(spare_data, record + PAGE, SPARE);
        } else {
            ok = prepared ? nrio_recovery_copy_backup_page(i, page_data, spare_data)
                          : nrio_recovery_read(i * (PAGE / SECTOR), PAGE / SECTOR, page_data);
        }
        if (ok) ok = write_chunk(main, page_data, PAGE, &info.main_crc);
        if (ok && (prepared || records)) ok = write_chunk(spare, spare_data, SPARE, &info.spare_crc);
    }
    if (ok && !prepared && !records) {
        struct nrio_raw_info spare_info;
        ok = select_view(NRIO_RAW_SPARE_READ, &spare_info) && spare_info.nand_id == initial->nand_id &&
             nrio_recovery_pages_per_block() == info.pages;
        for (uint32_t i = 0; ok && i < info.pages * SPARE / SECTOR; ++i) {
            ok = nrio_recovery_read(i, 1, spare_data) &&
                 write_chunk(spare, spare_data, SECTOR, &info.spare_crc);
        }
    }
    if (main && fclose(main)) ok = false;
    if (spare && fclose(spare)) ok = false;
    FILE *metadata = ok ? fopen(info_path, "wb") : NULL;
    ok = metadata && fwrite(&info, 1, sizeof(info), metadata) == sizeof(info);
    if (metadata && fclose(metadata)) ok = false;
    if (ok) {
        uint32_t metadata_crc = nrio_update_crc32(UINT32_MAX, (const uint8_t *)&info, sizeof(info));
        ok = nrio_backup_check_file(main_path, info.pages * PAGE, info.main_crc, nrio_update_crc32) &&
             nrio_backup_check_file(spare_path, info.pages * SPARE, info.spare_crc, nrio_update_crc32) &&
             nrio_backup_check_file(info_path, sizeof(info), metadata_crc, nrio_update_crc32);
    }
    if (!ok) {
        remove(main_path);
        remove(spare_path);
        remove(info_path);
        rmdir(directory);
        return false;
    }
    printf("Saved block 0 to\n%s\n", directory);
    return true;
}

static bool save_backup(const char *root, bool prepared, const struct nrio_raw_info *initial)
{
    return save_backup_impl(root, prepared, initial, NULL);
}

static bool save_snapshot_block0(const char *root, const struct nrio_raw_info *info,
                                 const uint8_t *records)
{
    return save_backup_impl(root, false, info, records);
}

static int next_backup(const bool available[BACKUP_LIMIT], int index, int direction)
{
    for (unsigned i = 0; i < BACKUP_LIMIT; ++i) {
        index = (index + direction + BACKUP_LIMIT) % BACKUP_LIMIT;
        if (available[index]) return index;
    }
    return -1;
}

static int choose_backup(const char *root, const char *title)
{
    DIR *directory = opendir(root);
    if (!directory) return -1;
    static bool available[BACKUP_LIMIT];
    memset(available, 0, sizeof(available));
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strlen(entry->d_name) != 10 || strncmp(entry->d_name, "block0-", 7)) continue;
        const char *digits = entry->d_name + 7;
        if (digits[0] < '0' || digits[0] > '9' || digits[1] < '0' || digits[1] > '9' ||
            digits[2] < '0' || digits[2] > '9') continue;
        char path[48];
        snprintf(path, sizeof(path), "%s/block0-%.3s/backup.info", root, digits);
        if (access(path, F_OK) == 0) available[atoi(digits)] = true;
    }
    closedir(directory);
    int index = next_backup(available, 0, -1);
    if (index < 0) {
        puts("No restore-ready backups.\nUse SELECT on a working cart.");
        return -1;
    }
    for (;;) {
        consoleClear();
        printf("%s\n\nBackup: block0-%03d\n\nLeft / Right: choose backup\nA: inspect   B: cancel\n", title, index);
        uint32_t down;
        do {
            swiWaitForVBlank();
            scanKeys();
            down = keysDown();
        } while (!(down & (KEY_A | KEY_B | KEY_LEFT | KEY_RIGHT)));
        if (down & KEY_B) return -1;
        if (down & KEY_A) return index;
        index = next_backup(available, index, (down & KEY_LEFT) ? -1 : 1);
    }
}

enum backup_result {
    BACKUP_VALID, BACKUP_MANIFEST_INVALID, BACKUP_CART_MISMATCH, BACKUP_FILE_INVALID,
    BACKUP_MAIN_CRC, BACKUP_SPARE_CRC, BACKUP_BOTH_CRC,
};

static enum backup_result load_backup_image(const char *root, int index, const struct nrio_raw_info *live, uint32_t *bytes,
                              uint8_t anchors[2][SECTOR])
{
    char path[56];
    struct backup_info info;
    snprintf(path, sizeof(path), "%s/block0-%03d/backup.info", root, index);
    FILE *file = fopen(path, "rb");
    if (!file) return BACKUP_MANIFEST_INVALID;
    bool ok = fread(&info, 1, sizeof(info), file) == sizeof(info) &&
        fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!ok || memcmp(info.tag, backup_tag, sizeof(info.tag)) ||
        (info.pages != 64 && info.pages != 128)) return BACKUP_MANIFEST_INVALID;

    snprintf(path, sizeof(path), "%s/block0-%03d/main.bin", root, index);
    FILE *main = fopen(path, "rb");
    snprintf(path, sizeof(path), "%s/block0-%03d/spare.bin", root, index);
    FILE *spare = fopen(path, "rb");
    ok = main && spare;
    uint32_t main_crc = UINT32_MAX, spare_crc = UINT32_MAX;
    for (uint32_t i = 0; ok && i < info.pages; ++i) {
        uint8_t *record = restore_image + i * (PAGE + SPARE);
        ok = fread(record, 1, PAGE, main) == PAGE &&
             fread(record + PAGE, 1, SPARE, spare) == SPARE;
        if (ok) {
            main_crc = nrio_update_crc32(main_crc, record, PAGE);
            spare_crc = nrio_update_crc32(spare_crc, record + PAGE, SPARE);
        }
    }
    if (ok) ok = fgetc(main) == EOF && !ferror(main) && fgetc(spare) == EOF && !ferror(spare);
    if (main) fclose(main);
    if (spare) fclose(spare);
    *bytes = info.pages * (PAGE + SPARE);
    memcpy(anchors, info.anchors, sizeof(info.anchors));
    if (!ok) return BACKUP_FILE_INVALID;
    if (main_crc != info.main_crc && spare_crc != info.spare_crc) return BACKUP_BOTH_CRC;
    if (main_crc != info.main_crc) return BACKUP_MAIN_CRC;
    if (spare_crc != info.spare_crc) return BACKUP_SPARE_CRC;
    uint8_t current[2][SECTOR];
    if (info.nand_id != live->nand_id || info.pages != nrio_recovery_pages_per_block() ||
        !informative_anchors(info.anchors) || !read_anchors(current) ||
        memcmp(current, info.anchors, sizeof(current))) return BACKUP_CART_MISMATCH;
    return BACKUP_VALID;
}

static bool valid_sd_safe_mode(const struct nrio_raw_info *info, const uint32_t sectors[2], FILE *input)
{
    uint8_t table[SECTOR], live[SECTOR], replacement[SECTOR];
    if (!nrio_recovery_read(sectors[0], 1, live)) return false;
    uint32_t size = word(live + 0x80);
    uint32_t source = word(live + 0x30);
    if (size <= source || size > NRIO_RAW_TABLE_BYTES / 4 * MAP_BLOCK) return false;
    uint32_t table_lba = NRIO_RAW_PRIMARY_TABLE / SECTOR;
    uint32_t mapped = 0;
    for (uint32_t offset = 0; offset < size; offset += SECTOR) {
        if (offset % MAP_BLOCK == 0) {
            uint32_t entry = offset / MAP_BLOCK * 4;
            if (!nrio_recovery_read(table_lba + entry / SECTOR, 1, table)) return false;
            mapped = word(table + entry % SECTOR);
            if (mapped < MAP_BLOCK || mapped % MAP_BLOCK || mapped / SECTOR >= info->sectors)
                return false;
        }
        uint32_t lba = mapped / SECTOR + offset % MAP_BLOCK / SECTOR;
        uint32_t length = size - offset < SECTOR ? size - offset : SECTOR;
        if (fread(replacement, 1, length, input) != length) return false;
        if (offset == 0) {
            if (lba != sectors[0] || !nrio_recovery_check_safe_mode_header(replacement)) return false;
        } else if (offset == source) {
            if (lba != sectors[1] || !nrio_recovery_select_safe_mode_payload(replacement)) return false;
        } else if (!nrio_recovery_read(lba, 1, live) || memcmp(live, replacement, length)) {
            return false;
        }
    }
    // Raw ROM dumps can pad the used length to cartridge capacity.
    uint32_t padded_size = 0x20000;
    while (padded_size < size) padded_size *= 2;
    uint32_t padding = padded_size - size;
    int byte;
    while ((byte = fgetc(input)) != EOF) {
        if (byte != 0xff || !padding) return false;
        --padding;
    }
    return !ferror(input);
}

void nrio_region_screen(const char *heading)
{
    consoleClear();
    printf("%s\n\n", heading);
    swiWaitForVBlank();
}

bool nrio_confirm_write(void)
{
    puts("\nBack up this cart before writes\n"
         "whenever possible.\n"
         "\nHold L + R and press A to write.\n"
         "B: cancel");
    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        uint32_t down = keysDown();
        if (down & KEY_B) return false;
        if ((down & KEY_A) && (keysHeld() & (KEY_L | KEY_R)) == (KEY_L | KEY_R)) return true;
    }
}

bool nrio_confirm_padding_erase(void)
{
    nrio_region_screen("Unstable NAND padding");
    puts("Page 0xE looks like padding.\n"
         "Its reads disagree.\n"
         "Leave it erased if we write?\n\n"
         "A: allow   B: cancel");
    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        uint32_t down = keysDown();
        if (down & KEY_B) return false;
        if (down & KEY_A) return true;
    }
}

void nrio_show_write_error(void)
{
    struct nrio_raw_write_error error;
    char message[128];
    nrio_recovery_get_write_error(&error);
    nrio_format_write_error(message, sizeof(message), &error);
    printf("%s", message);
}

static void install_safe_mode(const char *root, const struct nrio_raw_info *info)
{
    uint32_t sectors[2];
    if (!nrio_recovery_prepare_safe_mode(sectors)) {
        puts("Unsupported stock safe-mode ROM.");
        nrio_show_write_error();
        return;
    }
    char path[56];
    snprintf(path, sizeof(path), "%s/safe-mode.nds", root);
    FILE *input = fopen(path, "rb");
    if (input) {
        bool valid = valid_sd_safe_mode(info, sectors, input);
        if (fclose(input)) valid = false;
        if (!valid) {
            puts("safe-mode.nds does not match\nthis cart and the supported patch.");
            return;
        }
        puts("Using verified safe-mode.nds.");
    } else if (errno != ENOENT) {
        puts("Could not open safe-mode.nds.");
        return;
    }
    printf("Safe-mode sectors %lX and %lX.\n", (unsigned long)sectors[0], (unsigned long)sectors[1]);
    if (!save_backup(root, true, info)) {
        puts("Could not save the block-0 backup.");
        return;
    }
    if (nrio_confirm_write()) {
        puts("Rewriting and verifying block 0.");
        if (nrio_recovery_install_safe_mode()) puts("Safe-mode install verified.");
        else nrio_show_write_error();
    }
}

static void install_banner(const char *root, const struct nrio_raw_info *info)
{
    char path[56];
    snprintf(path, sizeof(path), "%s/banner.bin", root);
    FILE *input = fopen(path, "rb");
    if (!input) {
        printf("Put a v1 banner at\n%s\n", path);
        return;
    }
    bool valid = fread(page_data, 1, PAGE, input) == PAGE;
    if (valid) {
        size_t tail = fread(spare_data, 1, SPARE + 1, input);
        valid = !ferror(input) && (tail == 0 || tail == SPARE);
        for (size_t i = 0; valid && i < tail; ++i) valid = spare_data[i] == 0;
    }
    if (fclose(input)) valid = false;
    if (!valid || !nrio_recovery_prepare_sd_banner(page_data)) {
        puts("Banner size, CRC, or target\nspare format check failed.");
        nrio_show_write_error();
        return;
    }
    if (!save_backup(root, true, info)) {
        puts("Could not save the block-0 backup.");
        return;
    }
    puts("Both banner copies will update.");
    if (nrio_confirm_write()) {
        puts("Rewriting and verifying block 0.");
        if (nrio_recovery_install_sd_banner()) puts("Banner install verified.");
        else nrio_show_write_error();
    }
}

static void restore_banner_spare(const char *root, const struct nrio_raw_info *info)
{
    if (!nrio_recovery_prepare_banner_spare_restore()) {
        puts("No matching banner ECC damage.");
        nrio_show_write_error();
        return;
    }
    if (!save_backup(root, true, info)) {
        puts("Could not save the block-0 backup.");
        return;
    }
    puts("Restore banner page ARM7 spare.\nCurrent banner data is preserved.");
    if (nrio_confirm_write()) {
        puts("Rewriting and verifying block 0.");
        if (nrio_recovery_install_sd_banner()) puts("ARM7 spare restore verified.");
        else nrio_show_write_error();
    }
}

static void patch_main_marker(const char *root, const struct nrio_raw_info *info)
{
    if (!root) { puts("Need SD or Slot-2 for backup."); return; }
    if (!nrio_recovery_prepare_main_marker()) {
        puts("Main ROM/layout not recognized.\nNo NAND changes made.");
        nrio_show_write_error();
        return;
    }
    if (!nrio_recovery_backup_pages()) {
        puts("Main ROM marker already patched.");
        return;
    }
    uint8_t anchors[2][SECTOR];
    if (!read_anchors(anchors) || !informative_anchors(anchors)) {
        puts("Backup would not be restorable.\nBoot anchors are missing.");
        return;
    }
    if (!save_backup(root, true, info)) {
        puts("Could not verify the backup.");
        return;
    }
    puts("Remove duplicate ARM9 marker.\nKeep the original spare data.");
    if (!nrio_confirm_write()) return;
    uint8_t current[2][SECTOR];
    if (!read_anchors(current) || memcmp(current, anchors, sizeof(current))) {
        puts("Boot anchors changed. No write.");
        return;
    }
    nrio_region_screen("Writing boot block");
    if (nrio_recovery_install_main_marker())
        puts("Block 0 verified.\nRestart to test boot.");
    else nrio_show_write_error();
}

static void restore_saved_block0(const char *root, const struct nrio_raw_info *info)
{
    int index = choose_backup(root, "Restore block 0");
    if (index < 0) return;
    uint32_t bytes;
    uint8_t anchors[2][SECTOR];
    if (load_backup_image(root, index, info, &bytes, anchors) != BACKUP_VALID) {
        puts("Backup integrity or cart\nmatch check failed.");
        return;
    }
    if (!nrio_recovery_prepare_block0_restore(restore_image, bytes)) {
        puts("Could not stage the restore.");
        nrio_show_write_error();
        return;
    }
    if (!nrio_recovery_backup_pages()) {
        puts("Block 0 already matches backup.");
        return;
    }
    if (!save_backup(root, true, info)) {
        puts("Could not save current block 0.");
        return;
    }
    printf("Restore all of block 0 from\nblock0-%03d.\n", index);
    if (nrio_confirm_write()) {
        uint8_t current[2][SECTOR];
        if (!read_anchors(current) || memcmp(current, anchors, sizeof(current))) {
            puts("Cart data changed after the check.");
            return;
        }
        puts("Rewriting and verifying block 0.");
        if (nrio_recovery_install_block0_restore()) puts("Block-0 restore verified.");
        else nrio_show_write_error();
    }
}

static void check_cart(const char *root)
{
    struct nrio_raw_info info;
    uint32_t id;
    enum nrio_report_action action;
    do {
        consoleClear();
        puts("Check Cart\n\nProbing Slot-1 NAND...");
        const bool detected = select_view(NRIO_RAW_MAIN_READ, &info);
        id = detected ? info.nand_id : 0;
        action = nrio_boot_report(root, detected ? &info : NULL, false);
    } while (action == NRIO_REPORT_RETRY);
    if (action == NRIO_REPORT_BACKUP) {
        if (!select_view(NRIO_RAW_MAIN_READ, &info) || info.nand_id != id) {
            puts("Could not reread the same cart.\nPress A to return.");
        } else {
            int index = choose_backup(root, "Check block-0 backup");
            if (index < 0) goto done;
            uint32_t length;
            uint8_t anchors[2][SECTOR];
            static const char *const summary[] = {
                "CRCs and cart anchors match.", "Invalid backup.info.",
                "CRCs pass. Cart anchors differ.", "Bad file size or read failed.",
                "Main CRC mismatch.", "Spare CRC mismatch.", "Both CRCs mismatch.",
            };
            enum backup_result result = load_backup_image(root, index, &info, &length, anchors);
            consoleClear();
            printf("block0-%03d\n\n%s\n\nPress A to return.\n", index, summary[result]);
        }
        do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & (KEY_A | KEY_B)));
    }
done:
    nrio_recovery_shutdown();
}

struct reference_state { const char *root; int index; uint32_t id; bool checked, valid; };

static bool compare_reference(void *context, const struct nrio_raw_info *info, unsigned page,
                              const uint8_t *main, const uint8_t *spare)
{
    struct reference_state *state = context;
    if (!state->checked) {
        uint32_t length;
        uint8_t anchors[2][SECTOR];
        state->checked = true;
        state->id = info->nand_id;
        state->valid = load_backup_image(state->root, state->index, info, &length, anchors) == BACKUP_VALID;
    }
    if (!state->valid || state->id != info->nand_id || page >= nrio_recovery_pages_per_block()) return false;
    const uint8_t *saved = restore_image + page * (PAGE + SPARE);
    return !memcmp(saved, main, PAGE) && !memcmp(saved + PAGE, spare, SPARE);
}

bool nrioTool_c1_read_test(const char *root)
{
    struct reference_state state = {.root = root, .index = -1};
    consoleClear();
    puts("Compare NAND reads with and\nwithout the last C1 command.\nUse a working N-Card.\n\nA Choose known-good backup\nX Run without a backup\nB Back");
    unsigned key;
    do { swiWaitForVBlank(); scanKeys(); key = keysDown(); } while (!(key & (KEY_A | KEY_B | KEY_X)));
    if (key & KEY_B) return true;
    if (key & KEY_A) {
        state.index = choose_backup(root, "Known-good backup");
        if (state.index < 0) return true;
    }
    return nrio_c1_diagnostics(root, state.index >= 0 ? compare_reference : NULL, &state);
}

static bool new_capture_directory(const char *root, char directory[40])
{
    for (unsigned i = 0; i < BACKUP_LIMIT; ++i) {
        int len = snprintf(directory, 40, "%s/capture-%03u", root, i);
        if (len < 0 || len >= 40) break;
        if (mkdir(directory, 0777) == 0) return true;
        if (errno != EEXIST) break;
    }
    return false;
}

static bool capture_spare_comparison(const char *directory, struct nrio_rom_spare_compare *comparison,
                                     struct nrio_arm7_plan *plan)
{
    /* Check the manifest/anchors again after the ROM session. This detects
       many swaps but is not a unique hardware identity check. */
    struct nrio_raw_info info;
    uint32_t length;
    uint8_t anchors[2][SECTOR];
    if (!select_view(NRIO_RAW_MAIN_READ, &info) ||
        load_backup_image(directory, 0, &info, &length, anchors) != BACKUP_VALID)
        return false;
    uint8_t live[SPARE_SIZE], early[PAGE] __attribute__((aligned(4)));
    if (!nrio_recovery_read(0, PAGE / SECTOR, early) || memcmp(restore_image, early, PAGE) ||
        !nand_load_page(0, true) || !nand_read_spare(live) ||
        memcmp(restore_image + PAGE, live, SPARE))
        return false;

    char path[56];
    uint8_t header[NRIO_ROM_HEADER_BYTES];
    snprintf(path, sizeof(path), "%s/cart-rom.nds", directory);
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    const bool ok = fread(header, 1, sizeof(header), file) == sizeof(header);
    const uint32_t arm7_offset = ok ? word(header + 0x30) : 0;
    uint8_t arm7[SPARE];
    const bool arm7_read = ok && arm7_offset >= NRIO_ROM_HEADER_BYTES && arm7_offset <= LONG_MAX &&
                          word(header + 0x3c) == sizeof(arm7) &&
                          !fseek(file, arm7_offset, SEEK_SET) &&
                          fread(arm7, 1, sizeof(arm7), file) == sizeof(arm7);
    if (fclose(file) || !ok) return false;
    *plan = (struct nrio_arm7_plan){.result = NRIO_ARM7_PREVIEW_INCOMPLETE};
    if (arm7_read) {
        const unsigned pages[] = {1, 4, 5};
        uint8_t copies[3][SPARE], retry[SPARE];
        unsigned stable = 0;
        for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) {
            const unsigned pg = pages[i];
            if (nand_load_page(pg, true) && nand_read_spare(copies[i]) &&
                nand_load_page(pg, true) && nand_read_spare(retry) &&
                !memcmp(copies[i], retry, SPARE) &&
                !memcmp(copies[i], restore_image + pg * (PAGE + SPARE) + PAGE, SPARE))
                stable |= 1u << i;
        }
        nrio_preview_arm7(arm7, copies[0], stable,
                          !memcmp(header, early + 0x400, SECTOR), plan);
    }
    *comparison = nrio_compare_cart_spare(header, live, early);
    return true;
}

static bool capture_read_sector(void *context, uint32_t lba, uint8_t *data)
{
    (void)context;
    return nrio_recovery_read(lba, 1, data);
}

static unsigned save_fireline_capture(const char *directory,
                                      const struct nrio_fireline_layout *layout)
{
    static const char *const names[] = {
        "OS-MAP.BIN", "DIR-MAP.BIN", "OS-HEAD.BIN", "DIR-HEAD.BIN",
    };
    unsigned saved = 0;
    for (unsigned i = 0; i < NRIO_FIRELINE_CAPTURE_SECTORS; ++i) {
        if (!(layout->present & (1u << i))) continue;
        char path[64], temporary[72];
        snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
        snprintf(temporary, sizeof(temporary), "%s.part", path);
        FILE *file = fopen(temporary, "wb");
        if (!file) continue;
        bool ok = fwrite(layout->sectors[i], 1, SECTOR, file) == SECTOR;
        if (fclose(file)) ok = false;
        if (ok && rename(temporary, path) == 0) saved |= 1u << i;
        else remove(temporary);
    }
    return saved;
}

void nrioTool_export_recovery(const char *root)
{
    char directory[40], path[64];
    consoleClear();
    puts("Export recovery bundle\n\nReads the cart only.\nSaves to separate storage.\n\nA Continue  B Cancel");
    do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & (KEY_A | KEY_B)));
    if (keysDown() & KEY_B) return;
    if (!new_capture_directory(root, directory)) {
        puts("Could not create capture folder.");
        goto done;
    }
    struct nrio_raw_info info;
    const bool detected = select_view(NRIO_RAW_MAIN_READ, &info);
    const bool saved_block = detected && save_backup(directory, false, &info);
    const bool still_detected = select_view(NRIO_RAW_MAIN_READ, &info);
    struct nrio_fireline_layout fireline = {0};
    if (still_detected)
        nrio_fireline_inspect(capture_read_sector, NULL, info.sectors, &fireline);
    const unsigned saved_maps = save_fireline_capture(directory, &fireline);
    const bool saved_report = nrio_boot_report(directory, still_detected ? &info : NULL, true)
                              != NRIO_REPORT_SAVE_FAILED;
    nrio_recovery_shutdown();
    consoleClear();
    printf("Saved to %s\n\n", strrchr(directory, '/') + 1);
    printf("Block 0: %s\nCheck Cart: %s\n\n", saved_block ? "saved" : "not saved",
           saved_report ? "saved" : "not saved");
    puts("A Add ROM dump  B Skip");
    do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & (KEY_A | KEY_B)));
    struct nrio_rom_dump rom;
    const bool tried_rom = !(keysDown() & KEY_B);
    const bool saved_rom = tried_rom && nrio_dump_cart_rom(directory, false, &rom);
    struct nrio_rom_spare_compare comparison;
    struct nrio_arm7_plan arm7;
    const bool compared = saved_block && saved_rom &&
                          capture_spare_comparison(directory, &comparison, &arm7);
    nrio_recovery_shutdown();

    snprintf(path, sizeof(path), "%s/CAPTURE.TXT", directory);
    FILE *log = fopen(path, "w");
    bool saved_log = false;
    if (log) {
        fprintf(log, "N-Card recovery capture (read-only)\nBlock 0: %s\nCheck Cart: %s\nROM: %s\n",
                saved_block ? "saved" : "not saved", saved_report ? "saved" : "not saved",
                saved_rom ? "saved" : tried_rom ? "stopped or cancelled" : "skipped");
        fprintf(log, "Legacy maps/headers captured (mask): %X.\n"
                     "OS pointer %08lX (%s). Directory pointer %08lX (%s).\n",
                saved_maps, (unsigned long)fireline.os_address, fireline.known_os ? "match" : "unconfirmed",
                (unsigned long)fireline.dir_address, fireline.known_dir ? "match" : "unconfirmed");
        if (saved_rom) {
            fprintf(log, "ROM bytes: %lu (%s)\nROM CRC32: %08lX\nROM chip ID: %08lX\n",
                    (unsigned long)rom.bytes, rom.uses_declared_size ? "used size" : "header capacity",
                    (unsigned long)rom.crc, (unsigned long)rom.chip_id);
            fprintf(log, "Secure-area CRC16: header %04X, decoded dump %04X (may differ).\n",
                    (unsigned)rom.header_secure_crc, (unsigned)rom.dump_secure_crc);
            fputs("ROM 0x1000-0x3FFF is zero-filled; secure area is in dumper form.\n", log);
            if (rom.early_b7_spare_matches < 0)
                fputs("Early B7 0x400 comparison: read failed.\n", log);
            else {
                fprintf(log, "Early B7 0x400 vs. header read: %d of 64 bytes match.\n",
                        rom.early_b7_spare_matches);
                if (rom.early_b7_main_matches >= 0)
                    fprintf(log, "Early B7 0xC00-0xFFF vs. header read: %d of 1024 bytes match.\n",
                            rom.early_b7_main_matches);
                else fputs("Early B7 0xC00 comparison: read failed.\n", log);
            }

            if (!compared)
                fputs("ROM/spare comparison not verified against the original raw capture.\n", log);
            else {
                fprintf(log, "ROM 0x400-0x43F vs. spare page 0: %u of 64 bytes match (spare %s).\n",
                        comparison.matches, comparison.spare_erased ? "all FF" : "populated");
                fprintf(log, "Consecutive repeats of the ROM record at 0x80-byte intervals: %u.\n",
                        comparison.following_repeats);
                fprintf(log, "ROM 0x800-0xBFF masked spare copies: %u of 8.\n", comparison.masked_copies);
                if (arm7.handoff)
                    fprintf(log, "ROM ARM7 %08lX matches spare: page 1 %s, page 4 %s, page 5 %s.\n",
                            (unsigned long)arm7.handoff, arm7.match_mask & 1 ? "yes" : "no",
                            arm7.match_mask & 2 ? "yes" : "no", arm7.match_mask & 4 ? "yes" : "no");
                if (arm7.result == NRIO_ARM7_PREVIEW_CANDIDATE)
                    fprintf(log, "ARM7 preview: page %u is a repair candidate. No write staged.\n", arm7.page);
                else if (arm7.result == NRIO_ARM7_PREVIEW_INTACT)
                    fputs("ARM7 preview: all three copies intact.\n", log);
                else if (arm7.result == NRIO_ARM7_PREVIEW_CONFLICT)
                    fputs("ARM7 preview: copies disagree; no candidate.\n", log);
                else fputs("ARM7 preview unavailable: header, variant or reads not verified.\n", log);
                fprintf(log, "ROM 0xC00-0xFFF vs. main NAND 0x000-0x3FF: %u of 1024 bytes match.\n",
                        comparison.main_matches);
            }
        }
        const bool ok = !ferror(log);
        saved_log = fclose(log) == 0 && ok;
    }
    consoleClear();
    printf("Capture %s\n\n", strrchr(directory, '/') + 1);
    printf("Block 0: %s\nCheck Cart: %s\nROM: %s\n", saved_block ? "saved" : "not saved",
           saved_report ? "saved" : "not saved", saved_rom ? "saved" : tried_rom ? "not saved" : "skipped");
    if (saved_maps) puts("Legacy map sectors saved.");
    if (!saved_log) puts("Could not save CAPTURE.TXT.");
    if (compared) printf("ROM/spare match: %u/64 bytes.\n", comparison.matches);
    if (compared && arm7.result == NRIO_ARM7_PREVIEW_CANDIDATE)
        printf("ARM7 candidate: page %u.\n", arm7.page);
done:
    puts("\nA or B Back");
    do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & (KEY_A | KEY_B)));
}

void nrioTool_sd_recovery(int action, const char *root)
{
    struct nrio_raw_info info;
    consoleClear();
    if (action == 5) {
        check_cart(root);
        return;
    }
    enum nrio_raw_mode mode = (action == 1 || action == 3)
        ? NRIO_RAW_SD_BANNER_INSTALL : NRIO_RAW_SAFE_MODE_INSTALL;
    if (action == 2) mode = NRIO_RAW_MAIN_READ;
    if (action == 4 || action == 12) mode = NRIO_RAW_SD_BLOCK0_RESTORE;
    if (action >= 6 && action <= 11) mode = NRIO_RAW_SD_REGION_INSTALL;
    if (!select_view(mode, &info)) {
        puts("Could not probe Slot-1 NAND.");
    } else if (action == 2) {
        if (!save_backup(root, false, &info)) puts("Could not save the block-0 backup.");
    } else if (!nrio_recovery_bind_workspace(workspace, sizeof(workspace))) {
        puts("Could not reserve the erase buffer.");
    } else if (action == 12) {
        patch_main_marker(root, &info);
    } else if (action == 11) {
        nrio_install_custom_udisk(root, &info, restore_image);
    } else if (action == 10) {
        nrio_install_hidden_xulu(root, &info, restore_image);
    } else if (action == 9) {
        nrio_bypass_safe_mode(root, &info, restore_image, save_snapshot_block0);
    } else if (action >= 6 && action <= 8) {
        nrio_install_region(root, &info, restore_image, action >= 7, action == 8);
    } else if (action == 4) {
        restore_saved_block0(root, &info);
    } else if (action == 3) {
        restore_banner_spare(root, &info);
    } else if (action == 1) {
        install_banner(root, &info);
    } else {
        install_safe_mode(root, &info);
    }
    nrio_recovery_shutdown();
    puts("\nPress A to return.");
    do {
        swiWaitForVBlank();
        scanKeys();
    } while (!(keysDown() & KEY_A));
}
