#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "recovery_nand/raw_nand.h"
#include "region-recovery.h"
#include "srl-dump.h"
#include "udisk-setup.h"
#include "udisk-upgrade-check.h"
#include "udisk/format-native.h"
#include "udisk/runtime.h"

static const char *const stages[] = {
    [UDISK_FORMAT_CHECK] = "Preflight",
    [UDISK_FORMAT_BITMAP] = "Reading bad-block bitmap",
    [UDISK_FORMAT_ERASE] = "Erasing filesystem blocks",
    [UDISK_FORMAT_FAT] = "Writing FAT and root directory",
    [UDISK_FORMAT_MAP] = "Publishing NAND map",
    [UDISK_FORMAT_COMPLETE] = "Format complete",
    [UDISK_FORMAT_TEST] = "Testing NAND data blocks",
    [UDISK_FORMAT_SAVE_BITMAP] = "Saving bad-block bitmap",
};

static bool check_sector(void *unused, uint32_t lba, uint8_t sector[512])
{
    (void)unused;
    return nrio_recovery_read(lba, 1, sector);
}

static bool read_page(void *unused, uint32_t row, uint8_t record[UDISK_NATIVE_RECORD])
{
    (void)unused;
    if (!nand_load_page(row, false)) return false;
    for (unsigned i = 0; i < SECTORS_PER_PAGE; ++i)
        if (!nand_read_sector(i, record + i * SECTOR_SIZE)) return false;
    return nand_load_page(row, true) && nand_read_spare(record + PAGE_SIZE);
}

static bool erase_block(void *unused, uint32_t row)
{
    (void)unused;
    return nand_erase_block(row);
}

static bool program_page(void *unused, uint32_t row, const uint8_t record[UDISK_NATIVE_RECORD])
{
    (void)unused;
    return nand_program_page(row, record);
}

static void format_progress(void *context, enum udisk_native_format_stage stage,
                            uint32_t done, uint32_t total)
{
    FILE *log = context;
    consoleClear();
    puts(stages[stage]);
    if (total) printf("Block %lu of %lu\n", (unsigned long)done, (unsigned long)total);
    if (stage != UDISK_FORMAT_COMPLETE) puts("\nDo not turn off the console.");
    if (log) {
        fprintf(log, "%s %lu/%lu\n", stages[stage], (unsigned long)done, (unsigned long)total);
        fflush(log);
    }
}

struct format_plan {
    uint32_t pages, route[2];
    enum nrio_boot_kind kind[2];
};

static bool direct_boot(enum nrio_boot_kind kind)
{
    return kind == NRIO_BOOT_UDISK || kind == NRIO_BOOT_XULU || kind == NRIO_BOOT_COMPACT;
}

static bool format_plan(const struct nrio_raw_info *info, struct format_plan *plan)
{
    *plan = (struct format_plan){.pages = nrio_recovery_pages_per_block()};
    if (plan->pages != 64 && plan->pages != 128) {
        puts("Unsupported erase geometry.");
        return false;
    }

    const uint32_t data_base = plan->pages == 128 ? 0x1000000 : 0x800000;
    struct udisk_ftl_metadata meta;
    if (!udisk_ftl_metadata_ranges(data_base, plan->pages * PAGE_SIZE, &meta)) return false;

    const uint32_t tables[] = {NRIO_RAW_PRIMARY_TABLE, NRIO_RAW_SECONDARY_TABLE};
    uint8_t header[SECTOR_SIZE];
    if (!nrio_recovery_read(2, 1, header) || memcmp(header, "DSGB", 4) ||
        memcmp(header + 0xc, "DSGB", 4)) {
        puts("No recognized N-Card header.");
        return false;
    }

    bool safe_mode = false;
    for (unsigned i = 0; i < 2; ++i) {
        struct nrio_boot_rom rom = nrio_inspect_boot(tables[i], info->sectors);
        plan->kind[i] = rom.kind;
        bool safe = rom.kind == NRIO_BOOT_STOCK || rom.kind == NRIO_BOOT_PATCHED;
        if (!i) safe_mode = safe;
        if (i && !safe_mode && (rom.kind == NRIO_BOOT_ERASED || rom.kind == NRIO_BOOT_SKIPPED))
            continue;
        if (i && !direct_boot(rom.kind)) {
            enum nrio_udisk_layout stock = nrio_udisk_classify(check_sector, NULL, info->sectors);
            if (stock == NRIO_UDISK_LAYOUT_LEGACY_123) {
                /* The legacy wrapper has a separately fingerprinted map. */
                rom.address = 0x80000;
                rom.size = 0xc0000;
            } else if (stock == NRIO_UDISK_LAYOUT_STOCK_145 && rom.address) {
                rom.size = 0xc0000;
            } else {
                printf("Secondary firmware unverified.\n%s\n", nrio_boot_kind_name(rom.kind));
                return false;
            }
        } else if (!i && !safe && !direct_boot(rom.kind)) {
            printf("Primary firmware unverified.\n%s\n", nrio_boot_kind_name(rom.kind));
            return false;
        }
        if (rom.kind == NRIO_BOOT_UDISK) rom.size = 0xc0000;
        if (!nrio_srl_format_check(tables[i], rom.address, rom.size, meta.bad_block_begin,
                                   &plan->route[i])) {
            printf("%s boot map unsafe\nor unreadable.\n", i ? "Secondary" : "Primary");
            return false;
        }
    }

    return true;
}

static void run_format(const char *root, bool full)
{
    struct nrio_raw_info info;
    consoleClear();
    puts("Checking N-Card for format...");
    if (!nrio_srl_open(&info)) {
        puts("Could not probe Slot-1 NAND.");
        goto done;
    }
    struct format_plan plan;
    if (!format_plan(&info, &plan)) {
        puts("No changes made.");
        goto done;
    }

    consoleClear();
    puts(full ? "Full Format N-Card" : "Quick Format N-Card");
    puts("\nThis erases all cart files.\n"
         "Back up anything you need.\n"
         "Boot firmware is not updated.");
    if (full) puts("\nTests every usable block.\nThis can take a long time.\nKeep the console powered.");
    puts("\nL+R+A Format   B Back");
    if (!nrio_udisk_confirm()) {
        nrio_recovery_shutdown();
        return;
    }
    /* Re-probe after confirmation to catch a removed or changed cart. */
    struct nrio_raw_info after;
    struct format_plan second;
    if (!nrio_srl_open(&after) || after.nand_id != info.nand_id || after.sectors != info.sectors ||
        !format_plan(&after, &second) || second.pages != plan.pages ||
        second.route[0] != plan.route[0] || second.route[1] != plan.route[1] ||
        second.kind[0] != plan.kind[0] || second.kind[1] != plan.kind[1]) {
        puts("Cart changed or read failed.\nNo changes made.");
        goto done;
    }
    FILE *log = NULL;
    if (root) {
        char path[64];
        mkdir(root, 0777);
        snprintf(path, sizeof(path), "%s/UDISK-FORMAT.TXT", root);
        log = fopen(path, "a");
    }
    if (log) fprintf(log, "\nNative %s Format. NAND %08lX\n", full ? "Full" : "Quick",
                     (unsigned long)info.nand_id);
    const struct udisk_native_format_io io = {log, read_page, erase_block, program_page, format_progress};
    struct udisk_native_format_report result;
    bool ok = full ? udisk_native_full_format(&io, info.sectors, plan.pages, &result)
                   : udisk_native_quick_format(&io, info.sectors, plan.pages, &result);
    if (ok) {
        printf("FAT sectors %lu\nSkipped blocks %lu\n", (unsigned long)result.sectors,
               (unsigned long)result.bad_blocks);
        if (full) printf("Tested blocks %lu\nNew failures %lu\n",
            (unsigned long)result.tested_blocks,
            (unsigned long)(result.bad_blocks - result.known_bad_blocks));
        puts("Set up XuluMenu or copy\nXMenu files next.");
    } else {
        consoleClear();
        printf("Format stopped.\n%s\nPage %08lX\n", stages[result.stage], (unsigned long)result.page);
        if (result.stage == UDISK_FORMAT_CHECK) puts("No NAND changes made.");
    }
    if (log) {
        fprintf(log, "Stage %u, page %08lX, map %08lX, %lu skipped blocks\n", (unsigned)result.stage,
                (unsigned long)result.page, (unsigned long)result.map_address, (unsigned long)result.bad_blocks);
        if (full) fprintf(log, "Tested %lu, previously marked %lu, bitmap %08lX\n",
                          (unsigned long)result.tested_blocks, (unsigned long)result.known_bad_blocks,
                          (unsigned long)result.bitmap_address);
        fclose(log);
    }
done:
    nrio_recovery_shutdown();
    nrio_udisk_wait_back();
}


void nrioTool_quick_format(const char *root) { run_format(root, false); }
void nrioTool_full_format(const char *root) { run_format(root, true); }
