/* Pass the archived os.nds as argv[1], or test rejection cases without it. */
#include "../arm9/source/fireline-layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { COUNT = 8192, SECTOR = 512, OS = 0x100000, DIR = 0x280000 };
static uint8_t disk[COUNT][SECTOR];
static uint32_t unstable_lba = UINT32_MAX;
static unsigned retry;

static bool read_sector(void *context, uint32_t lba, uint8_t *out)
{
    (void)context;
    assert(lba < COUNT);
    memcpy(out, disk[lba], SECTOR);
    if (lba == unstable_lba) out[10] ^= retry++;
    return true;
}

static void put(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}

int main(int argc, char **argv)
{
    struct nrio_fireline_layout r;
    memset(disk, 0xff, sizeof(disk));
    nrio_fireline_inspect(read_sector, NULL, COUNT, &r);
    assert(r.present == 3 && !r.os_address && !r.dir_address && !r.dumpable);

    uint8_t *map = disk[NRIO_FIRELINE_OS_MAP / SECTOR];
    for (unsigned i = 0; i < NRIO_FIRELINE_OS_BLOCKS; ++i)
        put(map + 4 * i, OS + i * 0x4000);
    put(disk[NRIO_FIRELINE_DIR_MAP / SECTOR], DIR);
    memcpy(disk[DIR / SECTOR], "\0SIC", 4);
    nrio_fireline_inspect(read_sector, NULL, COUNT, &r);
    assert(r.present == 15 && r.known_dir && !r.known_os && !r.dumpable);
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        assert(f && fread(disk[OS / SECTOR], 1, 1024, f) == 1024);
        assert(fclose(f) == 0);
        nrio_fireline_inspect(read_sector, NULL, COUNT, &r);
        assert(r.dumpable && r.known_os && r.known_dir && r.os_address == OS);
        assert(nrio_fireline_block(&r, 47) == OS + 47 * 0x4000);
        assert(!nrio_fireline_block(&r, 48));
        disk[OS / SECTOR + 1][10] ^= 1;
        nrio_fireline_inspect(read_sector, NULL, COUNT, &r);
        assert(!r.known_os && !r.dumpable);
        disk[OS / SECTOR + 1][10] ^= 1;
        put(map + 47 * 4, DIR);
        nrio_fireline_inspect(read_sector, NULL, COUNT, &r);
        assert(r.known_os && r.known_dir && !r.dumpable);
    }

    unstable_lba = NRIO_FIRELINE_OS_MAP / SECTOR;
    nrio_fireline_inspect(read_sector, NULL, COUNT, &r);
    assert(r.present == 10 && !r.os_address && !r.dumpable);
    unstable_lba = UINT32_MAX;
    put(map, COUNT * SECTOR);
    nrio_fireline_inspect(read_sector, NULL, COUNT, &r);
    assert(r.present == 11 && !r.os_address && !r.dumpable);
    puts("FireLine maps, signatures and bounds passed.");
}
