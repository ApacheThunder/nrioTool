#include <nds.h>
#include <nds/arm9/dldi.h>
#include <fat.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include "bundle-assets.h"
#include "cart-dldi.h"
#include "slot1-watch.h"
#include "udisk-setup.h"

static DLDI_INTERFACE *driver;
static bool mounted;

/* libfat and direct sector users share these guarded entry points.
   No old mount can issue I/O after eject, even if the next cart is present. */
static DISC_INTERFACE guarded;
static bool guarded_startup(void)
{
    return driver && nrio_slot1_allowed() && driver->ioInterface.startup();
}
static bool guarded_inserted(void)
{
    return driver && nrio_slot1_allowed() && driver->ioInterface.isInserted();
}
static bool guarded_read(sec_t sector, sec_t count, void *data)
{
    return driver && nrio_slot1_allowed() && driver->ioInterface.readSectors(sector, count, data);
}
static bool guarded_write(sec_t sector, sec_t count, const void *data)
{
    return driver && nrio_slot1_allowed() && driver->ioInterface.writeSectors(sector, count, data);
}
static bool guarded_clear(void)
{
    return driver && nrio_slot1_allowed() && driver->ioInterface.clearStatus();
}
static bool guarded_shutdown(void)
{
    return !driver || !nrio_slot1_allowed() || driver->ioInterface.shutdown();
}

static bool reject_write(sec_t sector, sec_t count, const void *data)
{
    (void)sector;
    (void)count;
    (void)data;
    return false;
}

static bool load_driver(bool read_only)
{
    if (driver || !nrio_slot1_allowed()) return false;
    enum { DRIVER_BYTES = 0x8000 };
    const unsigned asset = NRIO_ASSET_DLDI;
    size_t length;
    uint32_t crc;
    if (!nrio_asset_info(asset, &length, &crc) || length < 0x80 || length > DRIVER_BYTES)
        return false;
    void *buffer = memalign(32, DRIVER_BYTES);
    if (!buffer) return false;
    memset(buffer, 0, DRIVER_BYTES);
    if (!nrio_asset_expand(asset, buffer, length)) {
        free(buffer);
        return false;
    }

    driver = buffer;
    if (!dldiIsValid(driver)) {
        nrioTool_unmount_setup_cart();
        return false;
    }
    driver->allocatedSize = 15;
    /* The legacy relocator uses dldiEnd to bound GOT values, but NRIO's
       GOT also points at its BSS past dldiEnd. Include that range. */
    size_t initialized = (uintptr_t)driver->dldiEnd - (uintptr_t)driver->dldiStart;
    driver->dldiEnd = driver->bssEnd;
    dldiFixDriverAddresses(driver);
    driver->dldiEnd = (uint8_t *)driver + initialized;
    /* Startup and read callbacks never program or erase in either mode. */
    if (read_only) {
        driver->ioInterface.features &= ~FEATURE_MEDIUM_CANWRITE;
        driver->ioInterface.writeSectors = reject_write;
    }
    guarded = driver->ioInterface;
    guarded.startup = guarded_startup;
    guarded.isInserted = guarded_inserted;
    guarded.readSectors = guarded_read;
    guarded.writeSectors = guarded_write;
    guarded.clearStatus = guarded_clear;
    guarded.shutdown = guarded_shutdown;
    DC_FlushRange(driver, DRIVER_BYTES);
    IC_InvalidateAll();
    sysSetCardOwner(BUS_OWNER_ARM9);
    return true;
}

static bool mount_cart(bool read_only)
{
    if (!load_driver(read_only)) return false;
    mounted = fatMountSimple("cart", &guarded);
    if (mounted) return true;
    nrioTool_unmount_setup_cart();
    return false;
}

const DISC_INTERFACE *nrioTool_open_cart_sectors(bool writable)
{
    if (!load_driver(!writable)) return NULL;
    const DISC_INTERFACE *io = &guarded;
    if (io->startup()) return io;
    nrioTool_unmount_setup_cart();
    return NULL;
}

const DISC_INTERFACE *nrioTool_mounted_cart_sectors(void)
{
    return mounted && driver ? &guarded : NULL;
}

void nrioTool_close_cart_sectors(void)
{
    if (driver && !mounted) {
        guarded_shutdown();
        nrioTool_unmount_setup_cart();
    }
}

bool nrioTool_mount_setup_cart(void) { return mount_cart(false); }
bool nrioTool_mount_report_cart(void) { return mount_cart(true); }

void nrioTool_unmount_setup_cart(void)
{
    if (!driver) return;
    if (mounted) fatUnmount("cart:");
    mounted = false;
    free(driver);
    driver = NULL;
}
