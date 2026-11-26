#ifndef NRIO_NAND_CAPACITY_PROBE_H
#define NRIO_NAND_CAPACITY_PROBE_H

#include <stdbool.h>
#include <stdint.h>

enum nrio_probe_observation {
    NRIO_PROBE_IDENTICAL, NRIO_PROBE_DISTINCT, NRIO_PROBE_BLANK,
    NRIO_PROBE_UNSTABLE, NRIO_PROBE_READ_ERROR, NRIO_PROBE_OBSERVATIONS,
};

struct nrio_capacity_report {
    unsigned count[NRIO_PROBE_OBSERVATIONS], sampled;
    bool varying_distinct_upper;
};

struct nrio_capacity_probe_io {
    void *context;
    bool (*read)(void *, uint32_t row, uint8_t data[512]);
    void (*result)(void *, uint32_t low, uint32_t high, enum nrio_probe_observation observation);
};

bool nrio_probe_4gib(const struct nrio_capacity_probe_io *io, unsigned page_bytes,
                     struct nrio_capacity_report *report);
void nrio_capacity_probe_ui(const char *root);
#endif
