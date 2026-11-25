#include <nds.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "boot-report.h"
#include "bundle-assets.h"
#include "button-legend.h"
#include "nand-health.h"
#include "nand-capacity-probe.h"
#include "recovery_nand/raw_nand.h"
#include "region-recovery.h"
#include "udisk-setup.h"
#include "udisk-setup-io.h"
#include "udisk-upgrade-check.h"

enum { PAGE_COUNT = 5, REPORT_LINES = 12, LINE_BYTES = 32, SECTOR = 512 };
struct report_page { char line[REPORT_LINES][LINE_BYTES]; unsigned count; };
enum menu_file { NOT_CHECKED, MISSING, VALID_HEADER, UNKNOWN_HEADER, READ_ERROR };
enum early_state { NO_READ, ERASED, KNOWN_HEADER, UNKNOWN_MAIN };
enum fat_state { PROBE_LOST, MISSING_MAP, MOUNT_FAILED, MOUNTED };

static const char *fat_failure(enum fat_state state, const struct nrio_fat_check *check)
{
    if (state == PROBE_LOST) return "NAND probe failed on recheck.";
    if (state == MOUNT_FAILED) return "Map found. FAT mount failed.";
    if (check->read_errors) return "Could not read all map records.";
    if (check->rejected) return "FAT map records failed checks.";
    return "No FAT map markers found.";
}

static void line(struct report_page *p, const char *fmt, ...)
{
    if (p->count >= REPORT_LINES) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(p->line[p->count++], LINE_BYTES, fmt, args);
    va_end(args);
}

static bool blank_ff(const uint8_t *p, size_t count)
{
    for (size_t i = 0; i < count; ++i) if (p[i] != 0xff) return false;
    return true;
}

static enum early_state check_early(unsigned pages)
{
    uint8_t main[PAGE_SIZE], spare[SPARE_SIZE];
    if (!nrio_recovery_read(0, PAGE_SIZE / SECTOR, main)) return NO_READ;
    if (!memcmp(main + 0x400, "DSGB", 4) && !memcmp(main + 0x40c, "DSGB", 4))
        return KNOWN_HEADER;
    if (!blank_ff(main, sizeof(main))) return UNKNOWN_MAIN;
    if (pages != 64 && pages != 128) return UNKNOWN_MAIN;
    for (unsigned i = 0; i < pages; ++i) {
        if (i && !nrio_recovery_read(i * (PAGE_SIZE / SECTOR), PAGE_SIZE / SECTOR, main)) return NO_READ;
        if (!blank_ff(main, sizeof(main))) return UNKNOWN_MAIN;
        if (!nand_load_page(i, true) || !nand_read_spare(spare)) return NO_READ;
        if (!blank_ff(spare, sizeof(spare))) return UNKNOWN_MAIN;
    }
    return ERASED;
}

static bool read_sector(void *unused, uint32_t lba, uint8_t data[SECTOR])
{
    (void)unused;
    return nrio_recovery_read(lba, 1, data);
}

static uint32_t word(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static enum menu_file check_menu(const char *path)
{
    struct stat st;
    if (stat(path, &st)) return errno == ENOENT ? MISSING : READ_ERROR;
    if (!S_ISREG(st.st_mode) || st.st_size < SECTOR) return UNKNOWN_HEADER;
    FILE *f = fopen(path, "rb");
    if (!f) return READ_ERROR;
    uint8_t head[SECTOR];
    bool ok = fread(head, 1, sizeof(head), f) == sizeof(head);
    if (fclose(f) || !ok) return READ_ERROR;
    for (unsigned off = 0x20; off <= 0x30; off += 0x10) {
        uint32_t start = word(head + off), size = word(head + off + 0xc);
        if (start < SECTOR || !size || (uint64_t)start + size > (uint64_t)st.st_size)
            return UNKNOWN_HEADER;
    }
    uint16_t crc = nrio_safe_mode_crc16(head, 0x15e);
    /* The stock xmenu.dat is a compact N-Card ROM. Its header uses the
       same CRC as a normal NDS file, but not the Nintendo logo. */
    if (crc != (head[0x15e] | head[0x15f] << 8)) return UNKNOWN_HEADER;
    return VALID_HEADER;
}

static const char *file_state(enum menu_file state)
{
    static const char *const labels[] = {"not checked", "missing", "header OK", "bad header/size", "read error"};
    return labels[state];
}

static bool matches_asset(const char *path, unsigned asset)
{
    struct nrio_file_id file;
    return nrio_asset_info(asset, &file.bytes, &file.crc) && nrio_file_matches(path, file);
}

static void add_rom(struct report_page *p, const char *name, const struct nrio_boot_rom *rom)
{
    line(p, "%s", name);
    line(p, " %s", nrio_boot_kind_name(rom->kind));
    if (rom->address) line(p, " ROM %08lX size %lX", (unsigned long)rom->address, (unsigned long)rom->size);
    if (rom->arm7_address) line(p, " ARM7 %08lX", (unsigned long)rom->arm7_address);
    if (rom->handoff) line(p, " Handoff %08lX", (unsigned long)rom->handoff);
}

static void write_report(const char *root, const struct report_page *pages)
{
    if (!root) return;
    char path[64];
    int n = snprintf(path, sizeof(path), "%s/CHECK-CART.TXT", root);
    FILE *f = n >= 0 && (size_t)n < sizeof(path) ? fopen(path, "a") : NULL;
    if (!f) return;
    for (unsigned i = 0; i < PAGE_COUNT; ++i)
        for (unsigned j = 0; j < pages[i].count; ++j) fprintf(f, "%s\n", pages[i].line[j]);
    fputs("\nRead-only check. No cart changes.\n\n", f);
    fclose(f);
}

static enum nrio_report_action show_report(const struct report_page *pages, bool backup_available,
                                            bool scan_available, const char *root)
{
    unsigned selected = 0;
    for (;;) {
        consoleClear();
        printf("Check Cart  %u/%u\n\n", selected + 1, PAGE_COUNT);
        const struct report_page *p = &pages[selected];
        for (unsigned i = 0; i < REPORT_LINES; ++i)
            puts(i < p->count ? p->line[i] : "");
        nrio_button_legend("L Help  R Retry  A/B Back", "Left/Right Page");
        printf("\n%s", backup_available ? "X Backup  Y Scan  SEL 4GiB"
                             : scan_available ? "Y Scan  SEL 4GiB" : "SEL 4GiB probe");
        for (;;) {
            swiWaitForVBlank();
            scanKeys();
            unsigned key = keysDown();
            if (key & (KEY_A | KEY_B)) return NRIO_REPORT_BACK;
            if (key & KEY_R) return NRIO_REPORT_RETRY;
            if (key & KEY_L) { selected = PAGE_COUNT - 1; break; }
            if (backup_available && (key & KEY_X)) return NRIO_REPORT_BACKUP;
            if (scan_available && (key & KEY_Y)) { nrio_nand_health_ui(root); break; }
            if (key & KEY_SELECT) { nrio_capacity_probe_ui(root); break; }
            if (key & KEY_LEFT) { selected = (selected + PAGE_COUNT - 1) % PAGE_COUNT; break; }
            if (key & KEY_RIGHT) { selected = (selected + 1) % PAGE_COUNT; break; }
        }
    }
}

enum nrio_report_action nrio_boot_report(const char *root, const struct nrio_raw_info *info)
{
    struct report_page pages[PAGE_COUNT] = {0};
    struct report_page *summary = &pages[0], *nand = &pages[1], *boot = &pages[2], *files = &pages[3];
    struct report_page *help = &pages[4];
    line(help, "What to try next");
    line(help, "");
    line(summary, "Summary");
    line(summary, "");
    line(nand, "NAND and early boot");
    line(boot, "Boot tables");
    line(files, "FAT and menu files");
    if (!info) {
        uint32_t id;
        if (nand_read_probe_id(&id) && id && id != UINT32_MAX) {
            line(summary, "NAND ID read, setup failed.");
            line(summary, "Raw ID %08lX", (unsigned long)id);
            line(summary, "Geometry may be unsupported.");
            line(nand, "Raw ID %08lX", (unsigned long)id);
            line(help, "Press R to probe again.");
            line(help, "Note the NAND part number.");
            line(help, "If it is 4 GiB, use SELECT");
            line(help, "to test upper addresses.");
            line(help, "Share the ID and probe log.");
        } else {
            line(summary, "No Slot-1 NAND response.");
            line(summary, "Check cart seating and retry.");
            line(summary, "This alone does not prove");
            line(summary, "the NAND is dead.");
            line(nand, "NAND ID unavailable.");
            line(help, "Reseat the cart, then press R.");
            line(help, "If it still won't respond,");
            line(help, "check the Slot-1 contacts");
            line(help, "or try another console.");
        }
        line(boot, "Not checked.");
        line(files, "Not checked.");
        write_report(root, pages);
        return show_report(pages, false, false, root);
    }

    uint32_t erase_pages = nrio_recovery_pages_per_block();
    line(nand, "ID %08lX", (unsigned long)info->nand_id);
    line(nand, "%lu pages per erase block", (unsigned long)erase_pages);
    line(nand, "%lu physical sectors", (unsigned long)info->sectors);
    enum early_state early = check_early(erase_pages);
    line(nand, "");
    line(nand, "%s", early == ERASED ? "Block 0 is all FF." : early == KNOWN_HEADER
                    ? "DSGB boot header found." : early == NO_READ
                    ? "Early NAND read failed." : "Unknown early main data.");
    if (early == ERASED) line(nand, "Main and spare both erased.");

    struct nrio_boot_rom first = nrio_inspect_boot(NRIO_RAW_PRIMARY_TABLE, info->sectors);
    struct nrio_boot_rom second = nrio_inspect_boot(NRIO_RAW_SECONDARY_TABLE, info->sectors);
    add_rom(boot, "Primary 0x8000", &first);
    add_rom(boot, "Secondary 0x40000", &second);
    enum nrio_udisk_layout udisk = nrio_udisk_classify(read_sector, NULL, info->sectors);
    const char *version = udisk == NRIO_UDISK_LAYOUT_STOCK_145 ? "uDisk 1.45 recognized" :
                          udisk == NRIO_UDISK_LAYOUT_LEGACY_123 ? "uDisk 1.23 recognized" :
                          first.kind == NRIO_BOOT_XULU || second.kind == NRIO_BOOT_XULU ?
                          "Hidden XuluMenu header found" :
                          first.kind == NRIO_BOOT_UDISK || second.kind == NRIO_BOOT_UDISK ?
                          "uDisk-like header found" : "uDisk version unrecognized";
    line(boot, "%s", version);

    enum fat_state fat;
    struct nrio_fat_check map;
    line(files, "Driver: NRIO C (read only)");
    enum menu_file menu = NOT_CHECKED, autorun = NOT_CHECKED;
    bool dsystem = false, dsystem_missing = false;
    const char *autorun_name = NULL;
    consoleClear();
    puts("Check Cart\n\nChecking FAT read only...");
    /* The table preflight avoids calling the legacy FAT driver on a
       missing map. Mount a separate read-only copy, never the SD volume. */
    enum nrio_cart_filesystem fs_status = nrioTool_check_report_filesystem(&map);
    if (fs_status == NRIO_CART_FS_PROBE_FAILED) fat = PROBE_LOST;
    else if (fs_status != NRIO_CART_FS_READY) fat = MISSING_MAP;
    else if (!nrioTool_mount_report_cart()) fat = MOUNT_FAILED;
    else {
        fat = MOUNTED;
        line(files, "FAT mounted read only.");
        struct stat st;
        if (!stat("cart:/DSYSTEM", &st)) dsystem = S_ISDIR(st.st_mode);
        else dsystem_missing = errno == ENOENT;
        line(files, "DSYSTEM: %s", dsystem ? "found" : dsystem_missing ? "missing" : "not a readable directory");
        menu = check_menu("cart:/xmenu.dat");
        const char *kind = file_state(menu);
        if (menu != MISSING && menu != READ_ERROR) {
            if (matches_asset("cart:/xmenu.dat", NRIO_ASSET_XULU)) { kind = "XuluMenu"; menu = VALID_HEADER; }
            else if (matches_asset("cart:/xmenu.dat", NRIO_ASSET_XMENU)) { kind = "stock XMenu"; menu = VALID_HEADER; }
        }
        line(files, "xmenu.dat: %s", kind);
        line(files, "xmenu.nds: %s", file_state(check_menu("cart:/xmenu.nds")));
        static const char *const paths[] = { "cart:/autorun1.nds", "cart:/autorun2.nds", "cart:/autorun3.nds" };
        for (unsigned i = 0; i < 3; ++i) {
            enum menu_file state = check_menu(paths[i]);
            if (state != MISSING) {
                autorun = state;
                autorun_name = paths[i] + 6;
                break;
            }
        }
        if (autorun_name) line(files, "%s: %s", autorun_name, file_state(autorun));
        else line(files, "No autorun override.");
        struct statvfs fs;
        if (!statvfs("cart:/", &fs)) {
            uint64_t free = (uint64_t)fs.f_bavail * (fs.f_frsize ? fs.f_frsize : fs.f_bsize);
            line(files, "%lu MiB free", (unsigned long)(free >> 20));
        }
        nrioTool_unmount_setup_cart();
    }
    if (fat != MOUNTED) line(files, "%s", fat_failure(fat, &map));
    if (fat != PROBE_LOST) {
        if (map.map_address) line(files, "Map candidate %08lX", (unsigned long)map.map_address);
        line(files, "Markers %u  Rejected %u", map.markers, map.rejected);
        if (map.read_errors) line(files, "Map read failures %u", map.read_errors);
    }

    if (early == ERASED) {
        line(summary, "NAND responds. Block 0 erased.");
        line(summary, "Restore a verified block-0");
        line(summary, "backup if you have one.");
    } else if (early == NO_READ || first.kind == NRIO_BOOT_READ_ERROR) {
        line(summary, "Early boot data read failed.");
        line(summary, "Retry or save read-only dumps.");
    } else if (first.kind == NRIO_BOOT_ERASED || first.kind == NRIO_BOOT_SKIPPED ||
               first.kind == NRIO_BOOT_UNKNOWN) {
        line(summary, "Primary boot route not found.");
        line(summary, "See Boot tables for details.");
    } else if (first.kind != NRIO_BOOT_UDISK && first.kind != NRIO_BOOT_XULU && second.kind == NRIO_BOOT_ERASED) {
        line(summary, "Secondary boot table is blank.");
        line(summary, "Safe mode may have no uDisk.");
    } else {
        line(summary, "%s", version);
    }
    if (fat != MOUNTED) line(summary, "%s", fat_failure(fat, &map));
    else {
        line(summary, "FAT mounted read only.");
        if (menu != VALID_HEADER) line(summary, "xmenu.dat: %s", file_state(menu));
        else line(summary, "xmenu.dat header looks OK.");
        if (autorun_name) line(summary, "%s takes priority.", autorun_name);
        if (dsystem_missing) line(summary, "DSYSTEM is missing.");
        else if (!dsystem) line(summary, "Check the DSYSTEM folder.");
        if (menu == MISSING) line(summary, "See uDisk setup for menu files.");
    }
    line(summary, "");
    line(summary, "A boot test is still needed.");

    if (early == ERASED) {
        line(help, "Block 0 needs boot data.");
        line(help, "Use a verified same-cart");
        line(help, "backup if you have one.");
        if (root) line(help, "X checks a saved backup.");
        line(help, "Formatting won't fix boot.");
    } else if (early == NO_READ || first.kind == NRIO_BOOT_READ_ERROR ||
               fat == PROBE_LOST || (fat == MISSING_MAP && map.read_errors)) {
        line(help, "Reseat cart, then R to retry.");
        line(help, "Y scans for NAND read errors.");
        line(help, "Save readable data before");
        line(help, "attempting a repair.");
    } else if (fat != MOUNTED) {
        line(help, "R retries the read-only check.");
        line(help, "Y scans for NAND read errors.");
        line(help, "See FAT page for map details.");
        line(help, "Save a raw dump before repair.");
        line(help, "Don't format to test a mount.");
    } else if (menu == MISSING || menu == UNKNOWN_HEADER) {
        line(help, "FAT works. Check menu files.");
        line(help, "Open uDisk setup to copy");
        line(help, "working XMenu files.");
        line(help, "No format needed for that.");
    } else if (menu == READ_ERROR) {
        line(help, "xmenu.dat could not be read.");
        line(help, "R retries. Y scans the NAND.");
        line(help, "Back up readable files first.");
    } else {
        if (autorun_name) line(help, "Check the autorun override.");
        if (!dsystem) line(help, "Check the DSYSTEM folder.");
        line(help, "Try booting from System Menu.");
        line(help, "See Boot tables if it hangs.");
    }
    write_report(root, pages);
    return show_report(pages, root != NULL, true, root);
}
