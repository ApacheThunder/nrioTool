#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "button-legend.h"
#include "nand-health.h"
#include "recovery_nand/raw_nand.h"
#include "recovery_nand/recovery.h"

enum { SCREEN_EVENTS = 256, EVENT_ROWS = 10 };
struct health_event { uint32_t address; enum nrio_health_area area; unsigned flags; };
struct scan_screen {
    FILE *log;
    const char *title;
    unsigned count, shown;
    struct health_event events[SCREEN_EVENTS];
};

static const char *area_name(enum nrio_health_area area)
{
    static const char *const areas[] = {"boot", "reserved", "metadata", "data"};
    return areas[area];
}

static void print_flags(FILE *out, unsigned flags)
{
    if (!flags) fputs("no issues found", out);
    if (flags & NRIO_HEALTH_LISTED) fputs(" bitmap-listed", out);
    if (flags & NRIO_HEALTH_SPARE) fputs(" unusual-spare", out);
    if (flags & NRIO_HEALTH_ECC) fputs(" ECC-mismatch", out);
    if (flags & NRIO_HEALTH_IO) fputs(" read-failure", out);
    if (flags & NRIO_HEALTH_RETRIED) fputs(" read-retried", out);
    fputc('\n', out);
}

static bool read_spare(void *ctx, uint32_t page, uint8_t spare[SPARE_SIZE])
{
    (void)ctx;
    return nand_load_page(page, true) && nand_read_spare(spare);
}

static bool read_main(void *ctx, uint32_t page, uint8_t main[PAGE_SIZE])
{
    (void)ctx;
    return nand_read_main(page * SECTORS_PER_PAGE, SECTORS_PER_PAGE, main);
}

static uint32_t input(void)
{
    do { swiWaitForVBlank(); scanKeys(); } while (!keysDown());
    return keysDown();
}

static void event(void *ctx, uint32_t address, enum nrio_health_area area, unsigned flags)
{
    struct scan_screen *s = ctx;
    ++s->count;
    if (s->shown < SCREEN_EVENTS)
        s->events[s->shown++] = (struct health_event){address, area, flags};
    if (s->log) {
        fprintf(s->log, "%08lX %s:", (unsigned long)address, area_name(area));
        print_flags(s->log, flags);
    }
}

static bool progress(void *ctx, uint32_t done, uint32_t total)
{
    struct scan_screen *s = ctx;
    if (!(done & 63)) {
        consoleClear();
        printf("%s\n\n%lu of %lu\n\nRead only.\n\n", s->title,
               (unsigned long)done, (unsigned long)total);
        nrio_button_legend("B Stop", NULL);
    }
    scanKeys();
    return !(keysHeld() & KEY_B);
}

static void show_event(const struct nrio_health_io *io, const struct nand_geometry *geometry,
                       const struct health_event *e)
{
    unsigned flags = e->flags;
    bool rechecked = false, stopped = false;
    for (;;) {
        consoleClear();
        printf("Block 0x%08lX (%s)\n\n", (unsigned long)e->address, area_name(e->area));
        if (stopped) puts("Recheck stopped.");
        else if (rechecked) puts("Full block read recheck:");
        else puts("Quick scan:");
        if (!flags) puts("No read or ECC issue found.");
        if (flags & NRIO_HEALTH_LISTED) puts("Listed in bad-block bitmap.");
        if (flags & NRIO_HEALTH_SPARE) puts("Unusual spare bytes.");
        if (flags & NRIO_HEALTH_ECC) puts("Stored ECC differs.");
        if (flags & NRIO_HEALTH_IO) puts("Read failed after retries.");
        if (flags & NRIO_HEALTH_RETRIED) puts("Read worked after a retry.");
        puts("\nNo writes performed.\n");
        nrio_button_legend(flags & NRIO_HEALTH_LISTED ? "A/B Back" : "X Recheck block  A/B Back", NULL);
        unsigned key = input();
        if (key & (KEY_A | KEY_B)) return;
        if ((key & KEY_X) && !(flags & NRIO_HEALTH_LISTED)) {
            struct scan_screen *s = io->context;
            s->title = "Rechecking block pages";
            rechecked = nrio_recheck_nand_block(io, geometry->sectors, geometry->pages_per_block,
                                                e->address, e->area, &flags);
            stopped = !rechecked;
            if (s->log && rechecked) {
                fprintf(s->log, "Recheck %08lX:", (unsigned long)e->address);
                print_flags(s->log, flags);
            }
        }
    }
}

static void show_results(const struct nrio_health_io *io, const struct nand_geometry *geometry,
                         const struct nrio_health_report *r)
{
    struct scan_screen *s = io->context;
    bool list = false;
    unsigned selected = 0;
    for (;;) {
        consoleClear();
        if (list) {
            printf("Block offsets  %u/%u\n\n", selected + 1, s->count);
            unsigned first = selected >= EVENT_ROWS ? selected - EVENT_ROWS + 1 : 0;
            unsigned stop = first + EVENT_ROWS < s->shown ? first + EVENT_ROWS : s->shown;
            for (unsigned i = first; i < stop; ++i) {
                const struct health_event *e = &s->events[i];
                printf("%c %08lX %s\n", i == selected ? '>' : ' ',
                       (unsigned long)e->address, area_name(e->area));
            }
            if (s->shown < s->count) puts(s->log ? "See log for remaining blocks." : "Only first 256 shown.");
            putchar('\n');
            nrio_button_legend("Up/Down Pick  A Details", "B Summary");
        } else {
            printf("NAND scan %s\n\n", r->cancelled ? "stopped" : "done");
            printf("%lu/%lu blocks sampled\n", (unsigned long)r->scanned, (unsigned long)r->total);
            if (r->bitmap == NRIO_BITMAP_FOUND) printf("Bitmap-listed: %lu\n", (unsigned long)r->listed);
            else puts(r->bitmap == NRIO_BITMAP_READ_ERROR ? "Bad bitmap unreadable." : "No bad-block bitmap found.");
            printf("Boot issues: %lu\nData issues: %lu\n", (unsigned long)r->boot_issues, (unsigned long)r->data_issues);
            printf("Read failures: %lu\nRetried blocks: %lu\n", (unsigned long)r->io, (unsigned long)r->retried);
            printf("ECC mismatches: %lu\nUnusual spare: %lu\n\n", (unsigned long)r->ecc, (unsigned long)r->spare);
            puts("Samples are not a write test.\n");
            nrio_button_legend(s->shown ? "X View offsets  A/B Back" : "A/B Back", NULL);
        }
        unsigned key = input();
        if (!list) {
            if (key & (KEY_A | KEY_B)) return;
            if (s->shown && (key & KEY_X)) list = true;
        } else if (key & KEY_B) list = false;
        else if (key & KEY_UP) selected = selected ? selected - 1 : s->shown - 1;
        else if (key & KEY_DOWN) selected = (selected + 1) % s->shown;
        else if (key & KEY_A) show_event(io, geometry, &s->events[selected]);
    }
}

void nrio_nand_health_ui(const char *root)
{
    static struct scan_screen screen;
    memset(&screen, 0, sizeof(screen));
    nrio_recovery_shutdown();
    sysSetCardOwner(BUS_OWNER_ARM9);
    struct nand_geometry geometry;
    consoleClear();
    if (!nand_probe(&geometry)) {
        puts("NAND did not respond.\n");
        nrio_button_legend("A/B Back", NULL);
        while (!(input() & (KEY_A | KEY_B))) {}
        return;
    }
    puts("Quick NAND health scan\n\n"
         "Reads all pages in block 0\n"
         "and samples other blocks.\n"
         "Does not write or format.\n");
    nrio_button_legend("A Scan  B Cancel", NULL);
    uint32_t key;
    do { key = input(); } while (!(key & (KEY_A | KEY_B)));
    if (key & KEY_B) return;

    if (root) {
        char path[64];
        int n = snprintf(path, sizeof(path), "%s/NAND-HEALTH.TXT", root);
        if (n >= 0 && (size_t)n < sizeof(path)) screen.log = fopen(path, "a");
    }
    if (screen.log)
        fprintf(screen.log, "\nNAND %08lX. %lu pages/block. Read-only quick scan.\n",
                (unsigned long)geometry.id, (unsigned long)geometry.pages_per_block);
    screen.title = "Scanning NAND";
    struct nrio_health_io io = {&screen, read_spare, read_main, event, progress};
    struct nrio_health_report report;
    if (nrio_scan_nand_health(&io, geometry.sectors, geometry.pages_per_block, &report)) {
        if (screen.log)
            fprintf(screen.log, "Scan %s: %lu/%lu blocks, bitmap status %u at %08lX.\n"
                    "Listed %lu, unusual spare %lu, ECC %lu, read failures %lu, retried %lu.\n",
                    report.cancelled ? "stopped" : "complete",
                    (unsigned long)report.scanned, (unsigned long)report.total, report.bitmap,
                    (unsigned long)report.bitmap_address, (unsigned long)report.listed,
                    (unsigned long)report.spare, (unsigned long)report.ecc,
                    (unsigned long)report.io, (unsigned long)report.retried);
        show_results(&io, &geometry, &report);
    } else {
        consoleClear();
        puts("Unknown NAND geometry.\nNo scan was run.\n");
        nrio_button_legend("A/B Back", NULL);
        while (!(input() & (KEY_A | KEY_B))) {}
    }
    if (screen.log) fclose(screen.log);
    screen.log = NULL;
}
