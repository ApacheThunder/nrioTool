#ifndef NRIO_CARD_H
#define NRIO_CARD_H

#include <stdbool.h>
#include <nds/ndstypes.h>

#ifdef __cplusplus
extern "C" {
#endif

bool InitCartNandReadMode(u32 card_type);

#ifdef __cplusplus
}
#endif

#endif
