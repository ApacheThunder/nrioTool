#ifndef NRIO_UDISK_UPGRADE_CHECK_H
#define NRIO_UDISK_UPGRADE_CHECK_H

#include <stdbool.h>
#include <stdint.h>

typedef bool (*nrio_read_nand_sector)(void *context, uint32_t lba, uint8_t sector[512]);

enum nrio_udisk_layout {
    NRIO_UDISK_LAYOUT_READ_ERROR,
    NRIO_UDISK_LAYOUT_UNKNOWN,
    NRIO_UDISK_LAYOUT_LEGACY_123,
    NRIO_UDISK_LAYOUT_STOCK_145,
};

/* Recognize only the stock 1.45 header/map footer or the tested legacy map.
   This does not verify a payload or grant permission to write a boot region. */
enum nrio_udisk_layout nrio_udisk_classify(nrio_read_nand_sector read, void *context,
                                          uint32_t physical_sectors);

enum nrio_boot_fingerprint_status {
    NRIO_BOOT_HASH_UNAVAILABLE, NRIO_BOOT_HASH_READ_ERROR, NRIO_BOOT_HASH_UNSTABLE,
    NRIO_BOOT_HASH_ERASED, NRIO_BOOT_HASH_ZERO, NRIO_BOOT_HASH_OK,
};

/* Compare two reads of each sector. This is diagnostic, not a restore gate. */
enum nrio_boot_fingerprint_status nrio_boot_fingerprint(
    nrio_read_nand_sector read, void *context, uint32_t sectors,
    uint32_t address, uint32_t bytes, uint32_t *crc);

enum nrio_udisk_capture {
    NRIO_UDISK_CAPTURE_UNKNOWN, NRIO_UDISK_CAPTURE_FIRE8, NRIO_UDISK_CAPTURE_FIRE16,
};

/* Read-only identification of full secondary maps from hardware captures. */
enum nrio_udisk_capture nrio_identify_udisk_capture(nrio_read_nand_sector read, void *context,
                                                    uint32_t sectors);

struct nrio_udisk_archive {
    const char *name;
    uint32_t bytes;
    bool wrapped;
    uint32_t image_crc[6];
};

/* Diagnostic fingerprint only. Never use it to authorize a write. */
const struct nrio_udisk_archive *nrio_identify_archived_udisk(
    nrio_read_nand_sector read, void *context, uint32_t physical_sectors, uint32_t table);

#endif
