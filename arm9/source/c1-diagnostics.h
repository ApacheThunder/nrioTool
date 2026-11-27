#ifndef NRIO_C1_DIAGNOSTICS_H
#define NRIO_C1_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>
#include "recovery_nand/recovery.h"

enum { NRIO_C1_SAMPLES = 6, NRIO_C1_MAIN = 2048, NRIO_C1_SPARE = 64 };
extern const unsigned nrio_c1_pages[NRIO_C1_SAMPLES];

typedef bool (*nrio_c1_reference)(void *, const struct nrio_raw_info *,
                                 unsigned page, const uint8_t *main, const uint8_t *spare);

bool nrio_c1_diagnostics(const char *root, nrio_c1_reference reference, void *context);

#endif
