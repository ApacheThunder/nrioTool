#ifndef NRIO_FIRELINE_LAYOUT_H
#define NRIO_FIRELINE_LAYOUT_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NRIO_FIRELINE_OS_MAP = 0x80000,
    NRIO_FIRELINE_DIR_MAP = 0x84000,
    NRIO_FIRELINE_OS_BLOCKS = 48,
    NRIO_FIRELINE_CAPTURE_SECTORS = 4,
};

struct nrio_fireline_layout {
    uint32_t os_address, dir_address;
    unsigned present;
    bool known_os, known_dir, dumpable;
    uint8_t sectors[NRIO_FIRELINE_CAPTURE_SECTORS][512];
};

/* Sectors are OS map, directory map, OS header and directory header. */
void nrio_fireline_inspect(bool (*read)(void *, uint32_t, uint8_t *), void *context,
                           uint32_t sectors, struct nrio_fireline_layout *layout);
uint32_t nrio_fireline_block(const struct nrio_fireline_layout *layout, unsigned index);

#ifdef __cplusplus
}
#endif
#endif
