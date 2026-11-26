#ifndef NRIO_BLOCK0_BACKUP_CHECK_H
#define NRIO_BLOCK0_BACKUP_CHECK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t (*nrio_backup_crc_fn)(uint32_t, const uint8_t *, size_t);

static inline bool nrio_backup_check_file(const char *path, size_t bytes, uint32_t expected,
                                          nrio_backup_crc_fn update)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t data[512];
    uint32_t crc = UINT32_MAX;
    bool ok = true;
    while (bytes && ok) {
        size_t chunk = bytes < sizeof(data) ? bytes : sizeof(data);
        ok = fread(data, 1, chunk, file) == chunk;
        if (ok) crc = update(crc, data, chunk);
        bytes -= chunk;
    }
    ok = ok && fgetc(file) == EOF && !ferror(file) && crc == expected;
    return fclose(file) == 0 && ok;
}

#endif
