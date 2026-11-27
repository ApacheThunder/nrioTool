#ifndef NRIO_CARD_H
#define NRIO_CARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool InitCartNandReadMode(uint32_t card_type);

#ifdef __cplusplus
}
#endif

#endif
