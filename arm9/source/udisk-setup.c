#include <nds.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#include "region-recovery.h"
#include "bundle-assets.h"
#include "recovery_nand/raw_nand.h"
#include "srl-dump.h"
#include "udisk-setup.h"
#include "xulu-install.h"
#include "udisk-setup-io.h"

#define CART_UPDATER "cart:/autorun1.nds"

static const struct nrio_file_id stock_updater = {0xe5400, UINT32_C(0x1994e314)};
static FILE *log_file;

static void report(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    if (log_file) {
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fflush(log_file);
    }
}

static void close_log(void)
{
    if (log_file) fclose(log_file);
    log_file = NULL;
}

static bool table_spare_ok(const uint8_t spare[SPARE_SIZE])
{
    uint8_t marker = spare[11];
    if (marker != spare[15] && spare[7] == spare[15]) marker = spare[7];
    return (spare[0] == 0x55 || spare[2] == 0x55) &&
           (spare[1] == 0xaa || spare[3] == 0xaa) && marker == 0xab;
}

/* Quick spare/footer preflight for both mount modes. The shared driver
   still verifies the full candidate map before accepting any I/O. */
static bool filesystem_table_present(const struct nrio_raw_info *info, struct nrio_fat_check *check)
{
    struct nrio_fat_check unused = {0};
    if (!check) check = &unused;
    uint32_t pages = nrio_recovery_pages_per_block();
    if (pages != 64 && pages != 128) return false;
    uint32_t erase_size = pages * PAGE_SIZE;
    uint32_t base = pages == 128 ? 0x1000000 : 0x800000;
    if (info->sectors <= base / SECTOR_SIZE) return false;
    uint32_t data_blocks = (info->sectors - base / SECTOR_SIZE) / (erase_size / SECTOR_SIZE);
    uint8_t spare[SPARE_SIZE], sector[SECTOR_SIZE];

    for (uint32_t address = base - 0x100000; address < base; address += erase_size) {
        uint32_t page = address / PAGE_SIZE;
        if (!nand_load_page(page, true) || !nand_read_spare(spare)) {
            ++check->read_errors;
            continue;
        }
        if (!table_spare_ok(spare)) continue;
        ++check->markers;
        if (!nand_load_page(page + 31, true) || !nand_read_spare(spare)) {
            ++check->read_errors;
            continue;
        }
        if (!table_spare_ok(spare)) {
            ++check->rejected;
            continue;
        }
        if (!nrio_recovery_read((address + 0xfe00) / SECTOR_SIZE, 1, sector)) {
            ++check->read_errors;
            continue;
        }
        uint32_t end = ((uint32_t)sector[510] << 24) | ((uint32_t)sector[511] << 16);
        bool valid = end >= base && !((end - base) % erase_size);
        if (valid) {
            uint32_t count = (end - base) / erase_size;
            valid = count >= 64 && count <= data_blocks;
        }
        if (valid) {
            check->map_address = address;
            return true;
        }
        ++check->rejected;
    }
    return false;
}

static enum nrio_cart_filesystem check_filesystem(struct nrio_fat_check *check)
{
    struct nrio_raw_info info;
    if (!nrio_srl_open(&info)) return NRIO_CART_FS_PROBE_FAILED;
    bool ready = filesystem_table_present(&info, check);
    nrio_recovery_shutdown();
    return ready ? NRIO_CART_FS_READY : NRIO_CART_FS_NO_MAP;
}

enum nrio_cart_filesystem nrioTool_check_cart_filesystem(void) { return check_filesystem(NULL); }
enum nrio_cart_filesystem nrioTool_check_report_filesystem(struct nrio_fat_check *check)
{
    *check = (struct nrio_fat_check){0};
    return check_filesystem(check);
}

static bool mount_cart(void)
{
    enum nrio_cart_filesystem fs = nrioTool_check_cart_filesystem();
    if (fs != NRIO_CART_FS_READY) {
        report(fs == NRIO_CART_FS_PROBE_FAILED ? "Could not probe Slot-1 NAND.\n"
                                              : "No usable cart FAT map.\n");
        report("No files changed. See Check Cart.\n");
        return false;
    }
    report("Mounting the cart filesystem...\n");
    if (nrioTool_mount_setup_cart()) {
        report("Cart mounted at cart:/.\n");
        return true;
    }
    report("Cart mount failed.\nNo files changed.\n");
    return false;
}

static void remove_updater(void)
{
    if (!mount_cart()) return;
    if (!nrio_file_matches(CART_UPDATER, stock_updater)) {
        report("No matching stock updater.\nNo files removed.\n");
    } else {
        report("\nDelete the old uDisk installer?\nFile: cart:/autorun1.nds\n\n"
               "Stops the installer from\nstarting on cart boot.\n"
               "Keeps installed firmware.\n\nL+R+A Delete   B Back\n");
        if (nrio_udisk_confirm()) {
            if (!nrio_file_matches(CART_UPDATER, stock_updater))
                report("File changed. Nothing removed.\n");
            else if (remove(CART_UPDATER)) report("Could not remove updater.\n");
            else report("Old updater file removed.\nInstalled firmware unchanged.\n");
        }
    }
    nrioTool_unmount_setup_cart();
}

static const char *const custom_assets[] = {
    "INDEXTAB.SDB", "LASTSAVE.SDB", "SAVETYPE.SDB",
    "c_file.bmp", "desktop.bmp", "global.ini",
    "system.ank", "system.fon", "system.l2u",
};
static const char *const starter_assets[] = {
    "c_file.bmp", "desktop.bmp", "global.ini", "savetype.sdb",
};

static bool write_embedded(void *id, FILE *out)
{
    return nrio_asset_stream(*(unsigned *)id, out);
}

static bool custom_set_present(const char *root, struct nrio_file_id *xmenu,
                                struct nrio_file_id ids[])
{
    if (!root) return false;
    char source[80];
    snprintf(source, sizeof(source), "%s/xmenu.dat", root);
    if (!nrio_file_identify(source, 0x200000, xmenu)) return false;
    for (unsigned i = 0; i < sizeof(custom_assets) / sizeof(*custom_assets); ++i) {
        snprintf(source, sizeof(source), "%s/DSYSTEM/%s", root, custom_assets[i]);
        if (!nrio_file_identify(source, 0x200000, ids + i)) return false;
    }
    return true;
}

static bool starter_set_present(struct nrio_file_id *xmenu, struct nrio_file_id ids[])
{
    if (!nrio_asset_info(NRIO_ASSET_XMENU, &xmenu->bytes, &xmenu->crc)) return false;
    for (unsigned i = 0; i < sizeof(starter_assets) / sizeof(*starter_assets); ++i)
        if (!nrio_asset_info(NRIO_ASSET_C_FILE + i, &ids[i].bytes, &ids[i].crc)) return false;
    return true;
}

static enum nrio_file_result copy_from(const char *root, const char *file,
                                       const char *destination, const char *temporary,
                                       unsigned id, struct nrio_file_id expected)
{
    if (!root) return nrio_write_new_file(destination, temporary, expected, write_embedded, &id);
    char source[80];
    snprintf(source, sizeof(source), "%s/%s", root, file);
    struct stat st;
    if (!stat(destination, &st)) return NRIO_FILE_EXISTS;
    return nrio_copy_new_file(source, destination, temporary, expected);
}

static void copy_menu(const char *root, bool force_stock)
{
    struct nrio_file_id xmenu, ids[sizeof(custom_assets) / sizeof(*custom_assets)];
    const bool custom = !force_stock && custom_set_present(root, &xmenu, ids);
    if (!custom && !starter_set_present(&xmenu, ids)) {
        report("Built-in XMenu set missing.\nUse the complete nrioTool ROM.\n");
        return;
    }
    if (!mount_cart()) return;
    report(custom ? "\nCopy complete custom XMenu set?\n" : "\nCopy built-in XMenu starter?\n");
    if (force_stock) report("Known Xulu wrapper is backed up.\n");
    report("Other files are kept.\nL+R+A Copy   B Back\n");
    if (!nrio_udisk_confirm()) goto unmount;

    consoleClear();
    if (mkdir("cart:/DSYSTEM", 0777) && errno != EEXIST) {
        report("Could not make DSYSTEM on cart.\n");
        goto unmount;
    }

    unsigned count = custom ? sizeof(custom_assets) / sizeof(*custom_assets)
                            : sizeof(starter_assets) / sizeof(*starter_assets);
    for (unsigned i = 0; i < count; ++i) {
        const char *name = custom ? custom_assets[i] : starter_assets[i];
        char source[48], destination[56];
        snprintf(source, sizeof(source), "DSYSTEM/%s", name);
        snprintf(destination, sizeof(destination), "cart:/%s", source);
        enum nrio_file_result result = copy_from(custom ? root : NULL, source, destination,
                              "cart:/DSYSTEM/nrio.part", NRIO_ASSET_C_FILE + i, ids[i]);
        if (result == NRIO_FILE_OK) report("Copied %s\n", name);
        else if (result == NRIO_FILE_EXISTS) report("Kept %s\n", name);
        else {
            report("Could not copy %s\n", name);
            goto unmount;
        }
    }
    if (force_stock) {
        struct nrio_file_id launcher;
        if (!nrio_asset_info(NRIO_ASSET_XULU, &launcher.bytes, &launcher.crc)) goto unmount;
        switch (nrio_restore_stock_menu("cart:", launcher, xmenu, write_embedded, NRIO_ASSET_XMENU)) {
        case NRIO_XULU_OK:
            report("Stock XMenu ready.\nOld file kept as xulu-old.nds\nif it was the legacy wrapper.\n");
            break;
        case NRIO_XULU_ALREADY: report("Stock XMenu already present.\n"); break;
        case NRIO_XULU_UNCHANGED: report("Custom xmenu.dat kept.\n"); break;
        case NRIO_XULU_ALIAS_CONFLICT: report("Backup or staging name in use.\nActive menu not changed.\n"); break;
        case NRIO_XULU_RESTORE_FAILED: report("Could not restore xmenu.dat.\nOld file is xulu-old.nds.\n"); break;
        default: report("Could not set up stock XMenu.\n"); break;
        }
    } else {
        enum nrio_file_result result = copy_from(custom ? root : NULL, "xmenu.dat", "cart:/xmenu.dat",
                                                 "cart:/nrxm.part", NRIO_ASSET_XMENU, xmenu);
        if (result == NRIO_FILE_OK) report("XMenu files ready.\n");
        else if (result == NRIO_FILE_EXISTS) report("Kept existing xmenu.dat.\n");
        else report("Could not copy xmenu.dat.\n");
    }

unmount:
    nrioTool_unmount_setup_cart();
}

bool nrio_udisk_confirm(void)
{
    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_B) return false;
        if ((keysDown() & KEY_A) &&
            (keysHeld() & (KEY_L | KEY_R)) == (KEY_L | KEY_R)) return true;
    }
}

void nrio_udisk_wait_back(void)
{
    puts("\nA or B Back");
    do {
        swiWaitForVBlank();
        scanKeys();
    } while (!(keysDown() & (KEY_A | KEY_B)));
}

void nrioTool_udisk_setup(enum nrio_udisk_setup_action action, const char *root)
{
    if (root) {
        char path[64];
        mkdir(root, 0777);
        snprintf(path, sizeof(path), "%s/UDISK-SETUP.TXT", root);
        log_file = fopen(path, "a");
    }
    consoleClear();
    report("\nuDisk setup\n");
    switch (action) {
        case NRIO_UDISK_COPY_MENU: copy_menu(root, false); break;
        case NRIO_UDISK_STOCK_MENU: copy_menu(NULL, true); break;
        case NRIO_UDISK_REMOVE_UPDATER: remove_updater(); break;
    }
    close_log();
    nrio_udisk_wait_back();
}
