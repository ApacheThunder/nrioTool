#include "nand-capacity-probe.h"

#include <string.h>

enum { SAMPLE_BYTES = 512 };

/* Uniform empty or open-bus reads are not evidence for either outcome. */
static bool informative(const uint8_t data[SAMPLE_BYTES])
{
    for (unsigned i = 1; i < SAMPLE_BYTES; ++i)
        if (data[i] != data[0]) return true;
    return false;
}

bool nrio_probe_4gib(const struct nrio_capacity_probe_io *io, unsigned page_bytes,
                     struct nrio_capacity_report *report)
{
    static const uint32_t offsets[] = {
        0, 0x8000, 0x20000, 0x40000, 0x80000, 0x100000, 0x1000000,
        0x10000000, 0x40000000, 0x7ff00000, 0x7fff0000, 0x7fffc000,
    };
    if (!io || !io->read || !report ||
        (page_bytes != 2048 && page_bytes != 4096 && page_bytes != 8192 && page_bytes != 16384))
        return false;
    *report = (struct nrio_capacity_report){0};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i) {
        const uint32_t low = offsets[i], high = low | UINT32_C(0x80000000);
        uint8_t a[SAMPLE_BYTES], b[SAMPLE_BYTES], repeat[SAMPLE_BYTES];
        enum nrio_probe_observation result;
        if (!io->read(io->context, low / page_bytes, a) ||
            !io->read(io->context, high / page_bytes, b) ||
            !io->read(io->context, low / page_bytes, repeat)) {
            result = NRIO_PROBE_READ_ERROR;
        } else {
            bool low_stable = !memcmp(a, repeat, SAMPLE_BYTES);
            if (!io->read(io->context, high / page_bytes, repeat))
                result = NRIO_PROBE_READ_ERROR;
            else if (!low_stable || memcmp(b, repeat, SAMPLE_BYTES))
                result = NRIO_PROBE_UNSTABLE;
            else if (!informative(b))
                result = NRIO_PROBE_BLANK;
            else
                result = memcmp(a, b, SAMPLE_BYTES) ? NRIO_PROBE_DISTINCT : NRIO_PROBE_IDENTICAL;
        }
        ++report->count[result];
        ++report->sampled;
        if (io->result) io->result(io->context, low, high, result);
    }
    return true;
}
