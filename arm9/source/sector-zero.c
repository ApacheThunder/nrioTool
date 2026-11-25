#include "sector-zero.h"
#include <string.h>

bool nrio_sector0_load(FILE *file, uint8_t *sector)
{
    return file && sector && fread(sector, 1, NRIO_SECTOR0_BYTES, file) == NRIO_SECTOR0_BYTES &&
           fgetc(file) == EOF && !ferror(file);
}

static bool valid_image(const uint8_t *data)
{
    if (!data || data[510] != 0x55 || data[511] != 0xaa) return false;
    /* Accept a FAT boot sector or an MBR that points to a FAT partition. */
    if ((data[0] == 0xe9 || data[0] == 0xeb) && data[11] == 0 && data[12] == 2 &&
        data[13] && data[13] <= 128 && !(data[13] & (data[13] - 1)) &&
        (data[14] || data[15]) && (data[16] == 1 || data[16] == 2)) return true;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = data + 446 + 16 * i;
        if ((p[0] != 0 && p[0] != 0x80) || !(p[8] | p[9] | p[10] | p[11]) ||
            !(p[12] | p[13] | p[14] | p[15])) continue;
        switch (p[4]) {
        case 1: case 4: case 6: case 0xb: case 0xc: case 0xe: return true;
        }
    }
    return false;
}

enum nrio_sector0_result nrio_sector0_restore(const struct nrio_sector0_io *io,
                                             const uint8_t *image)
{
    if (!valid_image(image)) return NRIO_SECTOR0_BAD_IMAGE;
    if (!io || !io->read || !io->write || !io->restart || !io->backup || !io->confirm)
        return NRIO_SECTOR0_READ_FAILED;
    uint32_t old_word[NRIO_SECTOR0_BYTES / 4], check_word[NRIO_SECTOR0_BYTES / 4];
    uint8_t *old = (uint8_t *)old_word, *check = (uint8_t *)check_word;
    void *ctx = io->context;

    if (!io->read(ctx, old)) return NRIO_SECTOR0_READ_FAILED;
    if (!memcmp(old, image, NRIO_SECTOR0_BYTES)) return NRIO_SECTOR0_SAME;
    if (!io->backup(ctx, old)) return NRIO_SECTOR0_BACKUP_FAILED;
    if (!io->confirm(ctx)) return NRIO_SECTOR0_CANCELLED;
    if (!io->restart(ctx) || !io->read(ctx, check) || memcmp(old, check, NRIO_SECTOR0_BYTES))
        return NRIO_SECTOR0_CHANGED;
    if (!io->write(ctx, image)) return NRIO_SECTOR0_WRITE_FAILED;
    if (!io->restart(ctx) || !io->read(ctx, check) || memcmp(image, check, NRIO_SECTOR0_BYTES))
        return NRIO_SECTOR0_VERIFY_FAILED;
    return NRIO_SECTOR0_OK;
}
