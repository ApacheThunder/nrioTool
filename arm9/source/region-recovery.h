#ifndef NRIO_TOOL_REGION_RECOVERY_H
#define NRIO_TOOL_REGION_RECOVERY_H

#include <stddef.h>

#include "recovery_nand/recovery.h"

enum nrio_boot_kind {
    NRIO_BOOT_READ_ERROR,
    NRIO_BOOT_SKIPPED,
    NRIO_BOOT_UNKNOWN,
    NRIO_BOOT_STOCK,
    NRIO_BOOT_PATCHED,
    NRIO_BOOT_UDISK,
    NRIO_BOOT_COMPACT,
    NRIO_BOOT_ERASED,
    NRIO_BOOT_XULU,
    NRIO_BOOT_DIRECT_DS,
};

struct nrio_boot_rom {
    enum nrio_boot_kind kind;
    uint32_t address, arm7_address, handoff, size;
};

struct nrio_boot_rom nrio_inspect_boot(uint32_t table, uint32_t sectors);
struct nrio_boot_rom nrio_inspect_primary_route(uint32_t sectors);
bool nrio_safe_mode_arm9_crc(uint32_t sectors, const struct nrio_boot_rom *rom, uint32_t *crc);
const char *nrio_boot_kind_name(enum nrio_boot_kind kind);
void nrio_install_region(const char *root, const struct nrio_raw_info *info,
                         void *snapshot, bool udisk, bool bios);
void nrio_install_custom_udisk(const char *root, const struct nrio_raw_info *info, uint8_t *snapshot);
void nrio_install_hidden_xulu(const char *root, const struct nrio_raw_info *info, uint8_t *snapshot);
typedef bool (*nrio_block0_saver)(const char *root, const struct nrio_raw_info *info,
                                 const uint8_t *records);
void nrio_bypass_safe_mode(const char *root, const struct nrio_raw_info *info,
                           uint8_t *snapshot, nrio_block0_saver save);
void nrio_region_screen(const char *heading);
bool nrio_confirm_write(void);
bool nrio_confirm_padding_erase(void);
void nrio_show_write_error(void);
void nrio_format_write_error(char *message, size_t size, const struct nrio_raw_write_error *error);
uint32_t nrio_update_crc32(uint32_t crc, const uint8_t *data, size_t size);

#endif
