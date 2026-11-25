#ifndef NRIO_SECTOR_ZERO_H
#define NRIO_SECTOR_ZERO_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { NRIO_SECTOR0_BYTES = 512 };

enum nrio_sector0_result {
    NRIO_SECTOR0_OK, NRIO_SECTOR0_SAME, NRIO_SECTOR0_BAD_IMAGE,
    NRIO_SECTOR0_READ_FAILED, NRIO_SECTOR0_BACKUP_FAILED, NRIO_SECTOR0_CANCELLED,
    NRIO_SECTOR0_CHANGED, NRIO_SECTOR0_WRITE_FAILED, NRIO_SECTOR0_VERIFY_FAILED,
};

struct nrio_sector0_io {
    void *context;
    bool (*read)(void *, uint8_t *);
    bool (*write)(void *, const uint8_t *);
    bool (*restart)(void *);
    bool (*backup)(void *, const uint8_t *);
    bool (*confirm)(void *);
};

bool nrio_sector0_load(FILE *file, uint8_t *sector);
enum nrio_sector0_result nrio_sector0_restore(const struct nrio_sector0_io *io,
                                            const uint8_t *image);
#ifdef __cplusplus
extern "C" {
#endif
void nrioTool_sector0_action(const char *root, bool restore);
#ifdef __cplusplus
}
#endif
#endif
