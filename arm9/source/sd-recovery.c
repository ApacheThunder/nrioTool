#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nds.h>

#include "region-recovery.h"
#include "boot-report.h"

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
    nrio_recovery_shutdown();
    return nrio_recovery_startup() && nrio_recovery_select_mode(mode) && nrio_recovery_get_info(info);
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
        .pages = prepared ? nrio_recovery_backup_pages() : initial->workspace_bytes / (PAGE + SPARE),
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
        ok = select_view(NRIO_RAW_SPARE_READ, &spare_info) && spare_info.nand_id == initial->nand_id;
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
    if (info.nand_id != live->nand_id || *bytes != live->workspace_bytes ||
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
        action = nrio_boot_report(root, detected ? &info : NULL);
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
            info.workspace_bytes = nrio_recovery_pages_per_block() * (PAGE + SPARE);
            enum backup_result result = load_backup_image(root, index, &info, &length, anchors);
            consoleClear();
            printf("block0-%03d\n\n%s\n\nPress A to return.\n", index, summary[result]);
        }
        do { swiWaitForVBlank(); scanKeys(); } while (!(keysDown() & (KEY_A | KEY_B)));
    }
done:
    nrio_recovery_shutdown();
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
    if (action == 4) mode = NRIO_RAW_SD_BLOCK0_RESTORE;
    if (action >= 6 && action <= 9) mode = NRIO_RAW_SD_REGION_INSTALL;
    if (!select_view(mode, &info)) {
        puts("Could not probe Slot-1 NAND.");
    } else if (action == 2) {
        if (!save_backup(root, false, &info)) puts("Could not save the block-0 backup.");
    } else if (!nrio_recovery_bind_workspace(workspace, sizeof(workspace))) {
        puts("Could not reserve the erase buffer.");
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
