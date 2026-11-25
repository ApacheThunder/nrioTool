#ifndef NRIO_UDISK_NATIVE_FORMAT_H
#define NRIO_UDISK_NATIVE_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    UDISK_NATIVE_PAGE = 2048,
    UDISK_NATIVE_SPARE = 64,
    UDISK_NATIVE_RECORD = UDISK_NATIVE_PAGE + UDISK_NATIVE_SPARE,
};

enum udisk_native_format_stage {
    UDISK_FORMAT_CHECK,
    UDISK_FORMAT_BITMAP,
    UDISK_FORMAT_ERASE,
    UDISK_FORMAT_FAT,
    UDISK_FORMAT_MAP,
    UDISK_FORMAT_COMPLETE,
    UDISK_FORMAT_TEST,
    UDISK_FORMAT_SAVE_BITMAP,
};

struct udisk_native_format_io {
    void *context;
    bool (*read)(void *context, uint32_t page, uint8_t record[UDISK_NATIVE_RECORD]);
    bool (*erase)(void *context, uint32_t first_page);
    bool (*program)(void *context, uint32_t page, const uint8_t record[UDISK_NATIVE_RECORD]);
    void (*progress)(void *context, enum udisk_native_format_stage stage, uint32_t done, uint32_t total);
};

struct udisk_native_format_report {
    enum udisk_native_format_stage stage;
    uint32_t page, bad_blocks, sectors, map_address;
    uint32_t known_bad_blocks, tested_blocks, bitmap_address;
};

/* Scratch is allocated before any write. Page addresses are always physical.
   The frontend is responsible for probing cart identity and user confirmation. */
bool udisk_native_quick_format(const struct udisk_native_format_io *io, uint32_t chip_sectors,
                                uint32_t erase_pages, struct udisk_native_format_report *report);

bool udisk_native_full_format(const struct udisk_native_format_io *io, uint32_t chip_sectors,
                              uint32_t erase_pages, struct udisk_native_format_report *report);

#endif
