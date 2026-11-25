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

#endif
