#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nds.h>

#include "recovery_nand/recovery.h"

enum { SECTOR = 512, PAGE = 2048, SPARE = 64, MAP_BLOCK = 0x4000 };
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

static bool open_backup(char *directory, char *main_path, char *spare_path)
{
    for (int i = 0; i < 1000; ++i) {
        snprintf(directory, 40, "sd:/nrioFiles/block0-%03d", i);
        if (mkdir(directory, 0777) == 0) {
            snprintf(main_path, 56, "%s/main.bin", directory);
            snprintf(spare_path, 56, "%s/spare.bin", directory);
            return true;
        }
        if (errno != EEXIST) return false;
    }
    return false;
}

static bool save_backup(bool prepared, const struct nrio_raw_info *initial)
{
    uint32_t pages = prepared ? nrio_recovery_backup_pages() : initial->workspace_bytes / (PAGE + SPARE);
    if (pages != 64 && pages != 128) return false;

    char directory[40], main_path[56], spare_path[56];
    if (!open_backup(directory, main_path, spare_path)) return false;
    FILE *main = fopen(main_path, "wb");
    FILE *spare = fopen(spare_path, "wb");
    bool ok = main && spare;
    for (uint32_t i = 0; ok && i < pages; ++i) {
        ok = prepared ? nrio_recovery_copy_backup_page(i, page_data, spare_data)
                      : nrio_recovery_read(i * (PAGE / SECTOR), PAGE / SECTOR, page_data);
        if (ok) ok = fwrite(page_data, 1, PAGE, main) == PAGE;
        if (ok && prepared) ok = fwrite(spare_data, 1, SPARE, spare) == SPARE;
    }
    if (ok && !prepared) {
        struct nrio_raw_info info;
        ok = select_view(NRIO_RAW_SPARE_READ, &info) && info.nand_id == initial->nand_id;
        for (uint32_t i = 0; ok && i < pages * SPARE / SECTOR; ++i) {
            ok = nrio_recovery_read(i, 1, spare_data) &&
                 fwrite(spare_data, 1, SECTOR, spare) == SECTOR;
        }
    }
    if (main && fclose(main)) ok = false;
    if (spare && fclose(spare)) ok = false;
    if (!ok) {
        remove(main_path);
        remove(spare_path);
        rmdir(directory);
        return false;
    }
    printf("Saved block 0 to\n%s\n", directory);
    return true;
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
    return fgetc(input) == EOF && !ferror(input);
}

static bool confirm_write(void)
{
    printf("\nHold L + R and press A to write.\nB: cancel\n");
    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        uint32_t down = keysDown();
        if (down & KEY_B) return false;
        if ((down & KEY_A) && (keysHeld() & (KEY_L | KEY_R)) == (KEY_L | KEY_R)) return true;
    }
}

static void show_write_error(void)
{
    struct nrio_raw_write_error error;
    nrio_recovery_get_write_error(&error);
    printf("Stopped at stage %lu, page %lX,\ndetail %lX.\n",
           (unsigned long)error.stage, (unsigned long)error.page, (unsigned long)error.detail);
}

static void install_safe_mode(const struct nrio_raw_info *info)
{
    uint32_t sectors[2];
    if (!nrio_recovery_prepare_safe_mode(sectors)) {
        puts("Unsupported stock safe-mode ROM.");
        show_write_error();
        return;
    }
    FILE *input = fopen("sd:/nrioFiles/safe-mode.nds", "rb");
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
    if (!save_backup(true, info)) {
        puts("Could not save the block-0 backup.");
        return;
    }
    if (confirm_write()) {
        puts("Rewriting and verifying block 0.");
        if (nrio_recovery_install_safe_mode()) puts("Safe-mode install verified.");
        else show_write_error();
    }
}

static void install_banner(const struct nrio_raw_info *info)
{
    FILE *input = fopen("sd:/nrioFiles/banner.bin", "rb");
    if (!input) {
        puts("Put a v1 banner at\nsd:/nrioFiles/banner.bin.");
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
        show_write_error();
        return;
    }
    if (!save_backup(true, info)) {
        puts("Could not save the block-0 backup.");
        return;
    }
    puts("First banner page at 0x2000.");
    if (confirm_write()) {
        puts("Rewriting and verifying block 0.");
        if (nrio_recovery_install_sd_banner()) puts("Banner install verified.");
        else show_write_error();
    }
}

void nrioTool_sd_recovery(int action)
{
    struct nrio_raw_info info;
    consoleClear();
    enum nrio_raw_mode mode = action == 1 ? NRIO_RAW_SD_BANNER_INSTALL : NRIO_RAW_SAFE_MODE_INSTALL;
    if (!select_view(mode, &info)) {
        puts("Could not probe Slot-1 NAND.");
    } else if (action == 2) {
        if (!save_backup(false, &info)) puts("Could not save the block-0 backup.");
    } else if (!nrio_recovery_bind_workspace(workspace, sizeof(workspace))) {
        puts("Could not reserve the erase buffer.");
    } else if (action == 1) {
        install_banner(&info);
    } else {
        install_safe_mode(&info);
    }
    nrio_recovery_shutdown();
    puts("\nPress A to return.");
    do {
        swiWaitForVBlank();
        scanKeys();
    } while (!(keysDown() & KEY_A));
}
