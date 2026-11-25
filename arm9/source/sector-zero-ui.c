#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "cart-dldi.h"
#include "region-recovery.h"
#include "sector-zero.h"
#include "udisk-setup-io.h"
#include "udisk-setup.h"

struct cart_sector0 {
    const char *root;
    const DISC_INTERFACE *disc;
};

static bool read_sector0(void *context, uint8_t *sector)
{
    struct cart_sector0 *cart = context;
    return cart->disc && cart->disc->readSectors(0, 1, sector);
}

static bool write_sector0(void *context, const uint8_t *sector)
{
    struct cart_sector0 *cart = context;
    return cart->disc && cart->disc->writeSectors(0, 1, sector);
}

static bool restart_driver(void *context)
{
    struct cart_sector0 *cart = context;
    nrioTool_close_cart_sectors();
    cart->disc = nrioTool_open_cart_sectors(true);
    return cart->disc != NULL;
}

static bool stream_sector0(void *sector, FILE *out)
{
    return fwrite(sector, 1, NRIO_SECTOR0_BYTES, out) == NRIO_SECTOR0_BYTES;
}

static enum nrio_file_result save_sector0(const char *root, const char *name, const uint8_t *sector)
{
    char path[80], partial[80];
    if (snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path) ||
        snprintf(partial, sizeof(partial), "%s/sector0.part", root) >= (int)sizeof(partial))
        return NRIO_FILE_ERROR;
    struct nrio_file_id id = {
        NRIO_SECTOR0_BYTES, nrio_update_crc32(UINT32_MAX, sector, NRIO_SECTOR0_BYTES) ^ UINT32_MAX
    };
    return nrio_write_new_file(path, partial, id, stream_sector0, (void *)sector);
}

static bool backup_sector0(void *context, const uint8_t *sector)
{
    struct cart_sector0 *cart = context;
    for (unsigned i = 0; i < 1000; ++i) {
        char name[36];
        snprintf(name, sizeof(name), "fat0-before-%03u.bin", i);
        enum nrio_file_result result = save_sector0(cart->root, name, sector);
        if (result == NRIO_FILE_EXISTS) continue;
        if (result != NRIO_FILE_OK) return false;
        printf("Saved current FAT sector 0.\n%s\n", name);
        return true;
    }
    return false;
}

static bool confirm_sector0(void *unused)
{
    (void)unused;
    puts("\nUse a backup from this cart.\n"
         "Writes logical FAT sector 0.\n"
         "L+R+A Restore   B Back");
    return nrio_udisk_confirm();
}

void nrioTool_sector0_action(const char *root, bool restore)
{
    static const char name[] = "nrio_fatsector0.bin";
    consoleClear();
    puts(restore ? "Restore FAT sector 0\n" : "Dump FAT sector 0\n");
    if (!root) {
        puts("Independent storage required.");
        goto done;
    }

    uint8_t sector[NRIO_SECTOR0_BYTES] __attribute__((aligned(4)));
    if (restore) {
        char path[80];
        if (snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path)) goto done;
        FILE *in = fopen(path, "rb");
        bool ok = in && nrio_sector0_load(in, sector);
        if (in && fclose(in)) ok = false;
        if (!ok) {
            puts("Need a 512-byte input file.\nnrio_fatsector0.bin\nNo changes made.");
            goto done;
        }
    }

    struct cart_sector0 cart = {root, nrioTool_open_cart_sectors(restore)};
    if (!cart.disc) {
        puts("Could not open N-Card sectors.\nCheck the cart and FAT map.");
        goto done;
    }
    if (!restore) {
        if (!read_sector0(&cart, sector)) puts("Read failed. Nothing saved.");
        else {
            enum nrio_file_result result = save_sector0(root, name, sector);
            if (result == NRIO_FILE_OK) printf("Saved %s.\n", name);
            else if (result == NRIO_FILE_EXISTS) puts("Backup exists. Move it first.");
            else puts("Could not save a verified dump.");
        }
    } else {
        static const char *const results[] = {
            "Sector 0 restored and verified.", "Sector 0 already matches.",
            "Not a recognized FAT or MBR.\nNo changes made.",
            "Read failed. No changes made.", "Backup failed. No changes made.",
            "Cancelled. No changes made.", "Cart changed or reread failed.\nNo changes made.",
            "Write failed. Check Cart.", "Readback failed. Check Cart.",
        };
        const struct nrio_sector0_io io = {&cart, read_sector0, write_sector0,
                                          restart_driver, backup_sector0, confirm_sector0};
        enum nrio_sector0_result result = nrio_sector0_restore(&io, sector);
        consoleClear();
        puts(results[result]);
    }
    nrioTool_close_cart_sectors();
done:
    nrio_udisk_wait_back();
}
