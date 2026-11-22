#include "raw_nand.h"
#include "banner_plan.h"
#include "recovery.h"

static struct nand_geometry geometry;
static enum nrio_raw_mode active_mode;
static bool mode_locked;
static u8 *workspace;
static struct nrio_raw_write_error write_error;

static bool spare_view(void)
{
    return active_mode == NRIO_RAW_SPARE_READ || active_mode == NRIO_RAW_SPARE_WRITE;
}

static u32 visible_sectors(void)
{
    return spare_view() ? geometry.sectors / (SECTORS_PER_PAGE * SPARES_PER_SECTOR) : geometry.sectors;
}

static bool writable_mode(void)
{
    if (active_mode == NRIO_RAW_BANNER_WRITE) return banner_is_armed();
    return active_mode >= NRIO_RAW_MAIN_PROTECTED;
}

bool nrio_recovery_startup(void)
{
    // The ROM owns Slot-1 directly, including after a RAM-based launch.
    sysSetCardOwner(BUS_OWNER_ARM9);
    discard_live_safe_mode();
    active_mode = NRIO_RAW_MAIN_READ;
    mode_locked = false;
    workspace = NULL;
    write_error.stage = NRIO_RAW_WRITE_NONE;
    write_error.page = 0;
    write_error.detail = 0;
    return nand_probe(&geometry);
}

void nrio_recovery_shutdown(void)
{
    geometry.sectors = 0;
    workspace = NULL;
    discard_live_safe_mode();
}

bool nrio_recovery_select_mode(enum nrio_raw_mode mode)
{
    if (!geometry.sectors || mode_locked ||
        ((unsigned)mode > NRIO_RAW_SAFE_MODE_INSTALL && mode != NRIO_RAW_SD_BANNER_INSTALL) ||
        (mode == NRIO_RAW_BANNER_WRITE && !banner_is_armed())) return false;
    active_mode = mode;
    mode_locked = true;
    return true;
}

bool nrio_recovery_get_info(struct nrio_raw_info *info)
{
    if (!geometry.sectors || !info) return false;
    info->sectors = visible_sectors();
    info->workspace_bytes = writable_mode() ? geometry.pages_per_block * PAGE_RECORD_SIZE : 0;
    info->nand_id = geometry.id;
    return true;
}

bool nrio_recovery_bind_workspace(void *buffer, uint32_t size)
{
    if (!geometry.sectors || !writable_mode() || !buffer || (uintptr_t)buffer % 4 ||
        size < geometry.pages_per_block * PAGE_RECORD_SIZE) return false;
    workspace = buffer;
    return true;
}

void nrio_recovery_get_write_error(struct nrio_raw_write_error *error)
{
    *error = write_error;
}

bool nrio_recovery_prepare_safe_mode(uint32_t sectors[2])
{
    if (active_mode != NRIO_RAW_SAFE_MODE_INSTALL || !workspace || !sectors ||
        nrio_recovery_safe_mode_ready() || write_error.stage != NRIO_RAW_WRITE_NONE) return false;
    return prepare_live_safe_mode(geometry.pages_per_block, workspace, &write_error, sectors);
}

bool nrio_recovery_install_safe_mode(void)
{
    if (active_mode != NRIO_RAW_SAFE_MODE_INSTALL || !workspace ||
        write_error.stage != NRIO_RAW_WRITE_NONE) return false;
    return restore_safe_mode(geometry.pages_per_block, workspace, &write_error);
}

bool nrio_recovery_prepare_sd_banner(const uint8_t banner[2048])
{
    if (active_mode != NRIO_RAW_SD_BANNER_INSTALL || !workspace || !banner ||
        write_error.stage != NRIO_RAW_WRITE_NONE) return false;
    return prepare_sd_banner(geometry.pages_per_block, workspace, banner, &write_error);
}

bool nrio_recovery_prepare_banner_spare_restore(void)
{
    if (active_mode != NRIO_RAW_SD_BANNER_INSTALL || !workspace ||
        write_error.stage != NRIO_RAW_WRITE_NONE) return false;
    return prepare_banner_spare_restore(geometry.pages_per_block, workspace, &write_error);
}

bool nrio_recovery_install_sd_banner(void)
{
    if (active_mode != NRIO_RAW_SD_BANNER_INSTALL || !workspace ||
        write_error.stage != NRIO_RAW_WRITE_NONE) return false;
    return install_sd_banner(geometry.pages_per_block, workspace, &write_error);
}

bool nrio_recovery_repair_table(const struct nrio_raw_table_plan *plan)
{
    if (active_mode != NRIO_RAW_TABLE_REPAIR || !plan || !workspace ||
        write_error.stage != NRIO_RAW_WRITE_NONE) return false;
    return restore_boot_table(plan, geometry.pages_per_block, workspace, &write_error);
}

static bool valid_range(u32 sector, u32 count)
{
    u32 limit = visible_sectors();
    return sector < limit && count <= limit - sector;
}

bool nrio_recovery_read(uint32_t sector, uint32_t count, void *buffer)
{
    if (!buffer || !valid_range(sector, count)) return false;
    mode_locked = true;
    if (!spare_view()) return nand_read_main(sector, count, buffer);

    u8 *output = buffer;
    u32 page = sector * SPARES_PER_SECTOR;
    for (u32 i = 0; i < count * SPARES_PER_SECTOR; ++i) {
        if (!nand_load_page(page + i, true) || !nand_read_spare(output)) return false;
        output += SPARE_SIZE;
    }
    return true;
}

bool nrio_recovery_write(uint32_t sector, uint32_t count, const void *buffer)
{
    if (!workspace || !writable_mode() || write_error.stage != NRIO_RAW_WRITE_NONE ||
        !buffer || !valid_range(sector, count) || active_mode >= NRIO_RAW_TABLE_REPAIR) return false;
    // Erasing to update any page of block zero would also erase the boot pages.
    if (active_mode == NRIO_RAW_MAIN_PROTECTED && count && sector < geometry.pages_per_block * SECTORS_PER_PAGE)
        return false;
    if (active_mode == NRIO_RAW_BANNER_WRITE && (sector != BANNER_LBA || count != SECTORS_PER_PAGE))
        return false;
    return restore_sectors(sector, count, buffer, geometry.pages_per_block, workspace, &write_error, active_mode);
}
