#include "c1-diagnostics.h"

#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "nrio_card.h"
#include "read_card.h"
#include "recovery_nand/raw_nand.h"

const unsigned nrio_c1_pages[NRIO_C1_SAMPLES] = {0, 1, 2, 4, 5, 16};
enum sample_state { READ_FAILED, UNSTABLE, VALID };
struct sample {
    uint8_t main[NRIO_C1_MAIN], spare[NRIO_C1_SPARE];
    enum sample_state main_state, spare_state;
};
struct pass {
    uint32_t id;
    unsigned erase_pages;
    bool captured, stable, reference_ok;
    struct sample pages[NRIO_C1_SAMPLES];
};
static struct pass passes[3];
static uint32_t header_window[0x1000 / sizeof(uint32_t)];
static sNDSHeaderExt header __attribute__((aligned(4)));

static unsigned wait_key(void)
{
    unsigned key;
    do { swiWaitForVBlank(); scanKeys(); key = keysDown(); } while (!(key & (KEY_A | KEY_B)));
    return key;
}

static bool prepare_rom(FILE *log, const char *label, uint32_t *type, uint32_t *rom_crc, bool *touched)
{
    consoleClear();
    printf("C1 read comparison\n\n%s\n", label);
    if (!isDSiMode()) puts("Reinsert the SAME cart now.");
    puts("\nA Continue  B Stop");
    if (wait_key() & KEY_B) { fprintf(log, "%s: cancelled before reset.\n", label); return false; }
    *touched = true;
    nrio_recovery_shutdown();
    const uint32_t status = cardInitForRomDump(&header, header_window);
    if (status) { fprintf(log, "%s: ROM init error %02lX.\n", label, (unsigned long)status); return false; }
    const uint8_t *bytes = (const uint8_t *)header_window;
    if (memcmp(bytes, "DSGBA", 5) || memcmp(bytes + 0xc, "DSGB", 4)) {
        fputs("Unsupported main ROM signature. No comparison.\n", log);
        return false;
    }
    *type = bytes[8] | (uint32_t)bytes[9] << 8 | (uint32_t)bytes[10] << 16 | (uint32_t)bytes[11] << 24;
    if (*type != 0x10830000 && *type != 0x10430000) {
        fprintf(log, "ASIC word %08lX not supported by this test.\n", (unsigned long)*type);
        return false;
    }
    *rom_crc = crc32(0, bytes, 0x1000);
    return true;
}

static bool read_spare(unsigned page, uint8_t *data)
{
    return nand_load_page(page, true) && nand_read_spare(data);
}

static const char *show(enum sample_state state)
{
    return state == VALID ? "stable" : state == UNSTABLE ? "changed" : "read failed";
}

static void acquire(FILE *log, struct pass *out, bool omit, uint32_t type,
                    nrio_c1_reference reference, void *ctx)
{
    const bool setup = nrio_cart_setup_diagnostic(type, omit);
    struct nrio_raw_info info;
    /* Never use the normal probe wrapper here: its fallback sends final C1. */
    const bool started = nrio_recovery_startup();
    out->id = nrio_recovery_last_probe_id();
    fprintf(log, "Setup %s, NAND ID %08lX\n", setup ? "completed" : "failed", (unsigned long)out->id);
    if (!setup || !started || !nrio_recovery_select_mode(NRIO_RAW_MAIN_READ) ||
        !nrio_recovery_get_info(&info)) return;
    out->captured = out->stable = out->reference_ok = true;
    out->erase_pages = nrio_recovery_pages_per_block();
    for (unsigned i = 0; i < NRIO_C1_SAMPLES; ++i) {
        struct sample *p = &out->pages[i];
        const unsigned page = nrio_c1_pages[i];
        uint8_t retry[NRIO_C1_MAIN] __attribute__((aligned(4)));
        if (nrio_recovery_read(page * 4, 4, p->main) && nrio_recovery_read(page * 4, 4, retry))
            p->main_state = memcmp(p->main, retry, NRIO_C1_MAIN) ? UNSTABLE : VALID;
        if (read_spare(page, p->spare) && read_spare(page, retry))
            p->spare_state = memcmp(p->spare, retry, NRIO_C1_SPARE) ? UNSTABLE : VALID;
        fprintf(log, "Page %02X main %s [%08lX], spare %s [%08lX]", page,
                show(p->main_state), p->main_state ? (unsigned long)crc32(0, p->main, NRIO_C1_MAIN) : 0ul,
                show(p->spare_state), p->spare_state ? (unsigned long)crc32(0, p->spare, NRIO_C1_SPARE) : 0ul);
        const bool stable = p->main_state == VALID && p->spare_state == VALID;
        out->stable &= stable;
        if (reference && !omit) {
            const bool match = stable && reference(ctx, &info, page, p->main, p->spare);
            out->reference_ok &= match;
            fprintf(log, ", backup %s", match ? "match" : "not verified/mismatch");
        }
        fputc('\n', log);
    }
}

static const char *result(enum sample_state a, enum sample_state b, const uint8_t *x,
                          const uint8_t *y, unsigned bytes)
{
    if (a != VALID || b != VALID) return "unavailable";
    return memcmp(x, y, bytes) ? "different" : "match";
}

static void compare(FILE *log, unsigned b, const char *label)
{
    fprintf(log, "\n%s\n", label);
    if (!passes[0].captured || !passes[b].captured || passes[0].id != passes[b].id ||
        passes[0].erase_pages != passes[b].erase_pages) {
        fputs("Not comparable: probe or geometry differs.\n", log);
        return;
    }
    for (unsigned i = 0; i < NRIO_C1_SAMPLES; ++i) {
        const struct sample *x = &passes[0].pages[i], *y = &passes[b].pages[i];
        fprintf(log, "Page %02X main %s, spare %s\n", nrio_c1_pages[i],
                result(x->main_state, y->main_state, x->main, y->main, NRIO_C1_MAIN),
                result(x->spare_state, y->spare_state, x->spare, y->spare, NRIO_C1_SPARE));
    }
}

bool nrio_c1_diagnostics(const char *root, nrio_c1_reference reference, void *context)
{
    char path[64];
    if (!root || snprintf(path, sizeof(path), "%s/C1-READ-TEST.TXT", root) >= (int)sizeof(path)) return true;
    FILE *log = fopen(path, "a");
    if (!log) {
        puts("Could not open C1-READ-TEST.TXT.");
        wait_key();
        return true;
    }
    memset(passes, 0, sizeof(passes));
    fputs("\nC1 comparison. No NAND program or erase commands.\n", log);
    if (!reference) fputs("No independent backup selected. Compare against a known-good same-cart dump.\n", log);
    const char *labels[] = {"A1: all setup commands", "B: omit final C1", "A2: all setup commands"};
    uint32_t first_type = 0, first_rom = 0;
    bool normal_setup_restored = false, touched = false;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t type, rom_crc;
        normal_setup_restored = false;
        if (!prepare_rom(log, labels[i], &type, &rom_crc, &touched)) break;
        if (i && (type != first_type || rom_crc != first_rom)) {
            fputs("ROM header changed. Test stopped; use the same cart for all passes.\n", log);
            break;
        }
        first_type = type;
        first_rom = rom_crc;
        fprintf(log, "\n%s, header CRC32 %08lX, setup word %08lX\n",
                labels[i], (unsigned long)rom_crc, (unsigned long)type);
        acquire(log, &passes[i], i == 1, type, reference, context);
        normal_setup_restored = i != 1 && passes[i].stable && passes[i].reference_ok;
        if (i == 0 && !normal_setup_restored) {
            fputs("Full setup baseline not verified. Did not run the omitted-C1 pass.\n", log);
            break;
        }
        if (i == 2 && normal_setup_restored) {
            normal_setup_restored = passes[0].id == passes[2].id &&
                                    passes[0].erase_pages == passes[2].erase_pages;
            for (unsigned n = 0; n < NRIO_C1_SAMPLES; ++n)
                normal_setup_restored &= !memcmp(passes[0].pages[n].main, passes[2].pages[n].main, NRIO_C1_MAIN) &&
                                        !memcmp(passes[0].pages[n].spare, passes[2].pages[n].spare, NRIO_C1_SPARE);
        }
    }
    nrio_recovery_shutdown();
    compare(log, 1, "A1 vs omitted final C1");
    compare(log, 2, "A1 vs full setup recheck");
    fprintf(log, "Normal setup %s. Sample agreement does not authorize writes.\n",
            normal_setup_restored ? "verified" : touched ? "not verified; relaunch" : "not attempted");
    const bool ok = !ferror(log);
    const bool saved = fclose(log) == 0 && ok;
    consoleClear();
    puts(saved ? "C1-READ-TEST.TXT saved." : "Could not finish saving log.");
    if (touched && !normal_setup_restored) puts("Relaunch before further tests.");
    puts("\nA/B Back");
    wait_key();
    return normal_setup_restored || !touched;
}
