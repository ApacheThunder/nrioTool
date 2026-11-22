#ifndef NRIO_RAW_RECOVERY_H
#define NRIO_RAW_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

#define NRIO_RAW_WORKSPACE_BYTES (128u * (2048u + 64u))
#define NRIO_RAW_PRIMARY_TABLE UINT32_C(0x8000)
#define NRIO_RAW_SECONDARY_TABLE UINT32_C(0x40000)
#define NRIO_RAW_TABLE_BYTES UINT32_C(0x4000)
#define NRIO_RAW_SHARED_SPARE_BYTES UINT32_C(512)

struct nrio_raw_info {
    uint32_t sectors;
    uint32_t workspace_bytes;
    uint32_t nand_id;
};

enum nrio_raw_write_stage {
    NRIO_RAW_WRITE_NONE,
    NRIO_RAW_WRITE_READ_BLOCK,
    NRIO_RAW_WRITE_MARKER,
    NRIO_RAW_WRITE_ERASE,
    NRIO_RAW_WRITE_PROGRAM,
    NRIO_RAW_WRITE_VERIFY_READ,
    NRIO_RAW_WRITE_VERIFY_DATA,
    NRIO_RAW_WRITE_TABLE_SOURCE,
    NRIO_RAW_WRITE_SNAPSHOT,
    NRIO_RAW_WRITE_SAFE_MODE_PLAN,
};

/* Select the view once, before processing sector I/O. */
enum nrio_raw_mode {
    NRIO_RAW_MAIN_READ,
    NRIO_RAW_SPARE_READ,
    NRIO_RAW_MAIN_PROTECTED,
    NRIO_RAW_MAIN_WRITE,
    NRIO_RAW_SPARE_WRITE,
    NRIO_RAW_BANNER_WRITE,
    NRIO_RAW_TABLE_REPAIR,
    NRIO_RAW_SAFE_MODE_INSTALL,
    NRIO_RAW_SD_BANNER_INSTALL = 0x100,
    NRIO_RAW_SD_BLOCK0_RESTORE,
    NRIO_RAW_SD_REGION_INSTALL,
};

struct nrio_sector_patch {
    uint32_t lba;
    const uint8_t *data;
    const uint8_t *expected;
};

struct nrio_raw_table_plan {
    uint8_t table[NRIO_RAW_TABLE_BYTES];
    uint8_t expected_spare[NRIO_RAW_SHARED_SPARE_BYTES];
};

struct nrio_raw_write_error {
    uint32_t stage;
    uint32_t page;
    uint32_t detail;
};

bool nrio_recovery_startup(void);
void nrio_recovery_shutdown(void);
bool nrio_recovery_select_mode(enum nrio_raw_mode mode);
bool nrio_recovery_get_info(struct nrio_raw_info *info);
bool nrio_recovery_bind_workspace(void *buffer, uint32_t size);
void nrio_recovery_get_write_error(struct nrio_raw_write_error *error);
bool nrio_recovery_safe_mode_ready(void);
bool nrio_recovery_prepare_safe_mode(uint32_t sectors[2]);
uint16_t nrio_safe_mode_crc16(const uint8_t *data, unsigned size);
uint32_t nrio_safe_mode_handoff(const uint8_t *payload);
void discard_live_safe_mode(void);
bool prepare_sd_banner(uint32_t pages, uint8_t *records, const uint8_t *banner, struct nrio_raw_write_error *error);
bool prepare_banner_spare_restore(uint32_t pages, uint8_t *records, struct nrio_raw_write_error *error);
bool prepare_block0_restore(uint32_t pages, uint8_t *records, const uint8_t *backup,
                            struct nrio_raw_write_error *error);
bool install_block0_restore(uint32_t pages, uint8_t *records, struct nrio_raw_write_error *error);
bool install_sd_banner(uint32_t pages, uint8_t *records, struct nrio_raw_write_error *error);
bool prepare_live_safe_mode(uint32_t pages, uint8_t *records, struct nrio_raw_write_error *error,
                            uint32_t sectors[2]);
bool nrio_recovery_install_safe_mode(void);
bool nrio_recovery_check_safe_mode_header(const uint8_t sector[512]);
bool nrio_recovery_select_safe_mode_payload(const uint8_t sector[512]);
bool nrio_recovery_prepare_sd_banner(const uint8_t banner[2048]);
bool nrio_recovery_prepare_banner_spare_restore(void);
bool nrio_recovery_install_sd_banner(void);
bool nrio_recovery_prepare_block0_restore(const void *backup, uint32_t bytes);
bool nrio_recovery_install_block0_restore(void);
uint32_t nrio_recovery_backup_pages(void);
bool nrio_recovery_copy_backup_page(uint32_t page, void *main, void *spare);
bool nrio_recovery_repair_table(const struct nrio_raw_table_plan *plan);
bool nrio_recovery_read(uint32_t sector, uint32_t count, void *buffer);
uint32_t nrio_recovery_pages_per_block(void);
bool nrio_recovery_read_snapshot(uint32_t lba, void *snapshot, uint32_t bytes);
bool nrio_recovery_patch_snapshot(uint32_t lba, const void *snapshot,
                                  const struct nrio_sector_patch *patches, uint32_t count,
                                  bool install);
bool read_block_snapshot(uint32_t page, uint32_t pages, uint8_t *records,
                         struct nrio_raw_write_error *error);
bool patch_block_snapshot(uint32_t page, uint32_t pages, uint8_t *records, const uint8_t *snapshot,
                          const struct nrio_sector_patch *patches, uint32_t count, bool install,
                          struct nrio_raw_write_error *error);

bool nrio_recovery_write(uint32_t sector, uint32_t count, const void *buffer);

bool restore_sectors(uint32_t sector, uint32_t count, const uint8_t *buffer, uint32_t pages_per_block,
                     uint8_t *workspace, struct nrio_raw_write_error *error, enum nrio_raw_mode mode);
bool restore_safe_mode(uint32_t pages_per_block, uint8_t *workspace, struct nrio_raw_write_error *error);
bool restore_boot_table(const struct nrio_raw_table_plan *plan, uint32_t pages_per_block, uint8_t *workspace,
                        struct nrio_raw_write_error *error);

#endif
