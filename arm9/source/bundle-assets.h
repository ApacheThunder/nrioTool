#ifndef NRIO_BUNDLE_ASSETS_H
#define NRIO_BUNDLE_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum nrio_asset {
    NRIO_ASSET_UDISK_0,
    NRIO_ASSET_UDISK_CHUNKS = 6,
    NRIO_ASSET_XMENU = NRIO_ASSET_UDISK_CHUNKS,
    NRIO_ASSET_C_FILE,
    NRIO_ASSET_DESKTOP,
    NRIO_ASSET_GLOBAL,
    NRIO_ASSET_SAVETYPE,
    NRIO_ASSET_DLDI,
    NRIO_ASSET_XULU,
    NRIO_ASSET_COUNT,
};

#ifdef __cplusplus
extern "C" {
#endif

bool nrio_asset_info(unsigned id, size_t *bytes, uint32_t *crc);
bool nrio_asset_expand(unsigned id, void *output, size_t length);
bool nrio_asset_stream(unsigned id, FILE *output);

#ifdef __cplusplus
}
#endif
#endif
