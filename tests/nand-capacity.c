#include "../arm9/source/nand-capacity-probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum behavior { REAL_UPPER, MIRROR, BLANK, UNSTABLE, NO_UPPER_READ, STALE_STRUCTURED };
struct test { uint32_t delta, half, upper_reads; unsigned samples, mode; };

static bool read(void *opaque, uint32_t row, uint8_t data[512])
{
    struct test *t = opaque;
    bool upper = row >= t->half;
    assert(row < 2 * t->half);
    if (upper && t->mode == NO_UPPER_READ) return false;
    if (upper && t->mode == BLANK) { memset(data, 0xff, 512); return true; }
    memset(data, 0, 512);
    uint32_t encoded = t->mode == MIRROR && upper ? row - t->half : row;
    if (upper && t->mode == STALE_STRUCTURED) encoded = 0xdeadbeef;
    memcpy(data, &encoded, sizeof(encoded));
    data[20] = 0xab;
    if (t->mode == UNSTABLE && upper) data[30] = ++t->upper_reads;
    return true;
}

static void sample(void *opaque, uint32_t low, uint32_t high, enum nrio_probe_observation result)
{
    struct test *t = opaque;
    assert(high - low == t->delta);
    assert(low % (t->delta / t->half) == 0);
    assert(result < NRIO_PROBE_OBSERVATIONS);
    ++t->samples;
}

int main(void)
{
    static const enum nrio_probe_observation expected[] = {
        NRIO_PROBE_DISTINCT, NRIO_PROBE_IDENTICAL, NRIO_PROBE_BLANK,
        NRIO_PROBE_UNSTABLE, NRIO_PROBE_READ_ERROR, NRIO_PROBE_DISTINCT,
    };
    for (unsigned bytes = 2048; bytes <= 16384; bytes *= 2) {
        for (unsigned mode = REAL_UPPER; mode <= STALE_STRUCTURED; ++mode) {
            struct test t = {.delta = UINT32_C(0x80000000), .half = UINT32_C(0x80000000) / bytes, .mode = mode};
            struct nrio_capacity_probe_io io = {&t, read, sample};
            struct nrio_capacity_report r;
            assert(nrio_probe_4gib(&io, bytes, &r));
            assert(r.sampled == 12 && t.samples == 12 && r.count[expected[mode]] == 12);
            assert(r.varying_distinct_upper == (mode == REAL_UPPER));
            assert(r.varying_matched_upper == (mode == MIRROR));
            assert(!nrio_probe_4gib(&io, 512, &r));
            for (unsigned mib = 128; mib <= 256; mib *= 2) {
                t.delta = (uint32_t)mib << 20;
                t.half = t.delta / bytes;
                t.samples = t.upper_reads = 0;
                assert(nrio_probe_alias(&io, bytes, mib, &r));
                assert(r.sampled == 7 && t.samples == 7 && r.count[expected[mode]] == 7);
                assert(r.varying_distinct_upper == (mode == REAL_UPPER));
                assert(r.varying_matched_upper == (mode == MIRROR));
            }
            assert(!nrio_probe_alias(&io, bytes, 192, &r));
        }
    }
    puts("128 MiB, 256 MiB and 4 GiB probes tested with distinct, mirrored, blank, unstable, stale and failing upper reads.");
}
