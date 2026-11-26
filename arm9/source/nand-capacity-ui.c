#include <nds.h>
#include <stdio.h>

#include "button-legend.h"
#include "nand-capacity-probe.h"
#include "recovery_nand/raw_nand.h"
#include "recovery_nand/recovery.h"

struct probe_context { FILE *log; unsigned sample; };

static const char *const outcomes[] = {
    "Matching structured data", "Different structured data", "Uniform upper data",
    "Read changed on retry", "Read failed",
};

static bool read_one(void *context, uint32_t row, uint8_t data[512])
{
    (void)context;
    /* Column 0 is valid for every selectable page size. Don't use the
       2 KiB-specific spare addressing or filesystem driver in this tool. */
    return nand_load_page(row, false) && nand_read_sector(0, data);
}

static void show_sample(void *opaque, uint32_t low, uint32_t high, enum nrio_probe_observation outcome)
{
    struct probe_context *ctx = opaque;
    consoleClear();
    printf("4 GiB address probe\n\nPair %u\n%08lX / %08lX\n%s\n\nRead only.\n",
           ++ctx->sample, (unsigned long)low, (unsigned long)high, outcomes[outcome]);
    if (ctx->log) fprintf(ctx->log, "%08lX / %08lX : %s\n",
                         (unsigned long)low, (unsigned long)high, outcomes[outcome]);
}

static unsigned input(void)
{
    do { swiWaitForVBlank(); scanKeys(); } while (!keysDown());
    return keysDown();
}

static bool usable_id(uint32_t id) { return id && id != UINT32_MAX; }

void nrio_capacity_probe_ui(const char *root)
{
    nrio_recovery_shutdown();
    sysSetCardOwner(BUS_OWNER_ARM9);
    uint32_t id;
    if (!nand_read_probe_id(&id) || !usable_id(id)) {
        consoleClear();
        puts("4 GiB address probe\n\nNo usable NAND ID returned.\nNo page reads were attempted.\n");
        nrio_button_legend("A/B Back", NULL);
        while (!(input() & (KEY_A | KEY_B))) {}
        return;
    }
    static const unsigned page_sizes[] = {2048, 4096, 8192, 16384};
    unsigned pick = 0;
    for (;;) {
        consoleClear();
        printf("4 GiB address probe\n\nID %02lX %02lX %02lX %02lX\n",
               (unsigned long)(id & 255), (unsigned long)((id >> 8) & 255),
               (unsigned long)((id >> 16) & 255), (unsigned long)(id >> 24));
        puts("Use your chip's main page size.\nReads only, no capacity change.\n");
        for (unsigned i = 0; i < 4; ++i)
            printf("%c %u KiB page\n", i == pick ? '>' : ' ', page_sizes[i] / 1024);
        putchar('\n');
        nrio_button_legend("Up/Down Pick  A Read", "B Back");
        unsigned keys = input();
        if (keys & KEY_B) return;
        if (keys & KEY_A) break;
        if (keys & KEY_UP) pick = (pick + 3) % 4;
        else if (keys & KEY_DOWN) pick = (pick + 1) % 4;
    }

    struct probe_context ctx = {0};
    if (root) {
        char path[64];
        int n = snprintf(path, sizeof(path), "%s/NAND-4GIB.TXT", root);
        if (n >= 0 && (size_t)n < sizeof(path)) ctx.log = fopen(path, "a");
    }
    if (ctx.log) fprintf(ctx.log, "\nNAND ID %08lX. Assumed main page %u bytes. Read only.\n",
                         (unsigned long)id, page_sizes[pick]);
    struct nrio_capacity_probe_io io = {&ctx, read_one, show_sample};
    struct nrio_capacity_report r;
    nrio_probe_4gib(&io, page_sizes[pick], &r);

    const char *summary = r.varying_distinct_upper ? "Upper addresses differ." :
        r.count[NRIO_PROBE_DISTINCT] ? "Upper reads inconclusive." :
        r.count[NRIO_PROBE_IDENTICAL] >= 2 ? "Upper reads may be mirrored." : "Upper reads inconclusive.";
    consoleClear();
    printf("4 GiB address probe\n\n%s\n\nMatching: %u\nDifferent: %u\nUniform upper: %u\n"
           "Unstable: %u\nRead errors: %u\n\nCheck the chip part number.\nWrite support is unchanged.\n\n",
           summary, r.count[NRIO_PROBE_IDENTICAL], r.count[NRIO_PROBE_DISTINCT],
           r.count[NRIO_PROBE_BLANK], r.count[NRIO_PROBE_UNSTABLE], r.count[NRIO_PROBE_READ_ERROR]);
    if (ctx.log) {
        fprintf(ctx.log, "%s %u pairs. Write support unchanged.\n", summary, r.sampled);
        fclose(ctx.log);
    }
    nrio_button_legend("A/B Back", NULL);
    while (!(input() & (KEY_A | KEY_B))) {}
}
