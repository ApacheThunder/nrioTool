#ifndef NRIO_UDISK_RUNTIME_H
#define NRIO_UDISK_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum udisk_usb_command {
    UDISK_USB_BAD_WRAPPER,
    UDISK_USB_UNSUPPORTED,
    UDISK_USB_TEST_UNIT_READY,
    UDISK_USB_REQUEST_SENSE,
    UDISK_USB_INQUIRY,
    UDISK_USB_MODE_SENSE_6,
    UDISK_USB_READ_FORMAT_CAPACITIES,
    UDISK_USB_READ_CAPACITY,
    UDISK_USB_READ_10,
    UDISK_USB_WRITE_10,
    UDISK_USB_VERIFY_10,
    UDISK_USB_MODE_SENSE_10,
};

struct udisk_usb_wrapper {
    uint32_t tag, transfer_bytes;
    uint8_t flags;
};

enum udisk_usb_command udisk_usb_decode_wrapper(const uint8_t *packet, size_t length,
                                                struct udisk_usb_wrapper *wrapper);
bool udisk_usb_rw10_range(const uint8_t packet[31], uint32_t *byte_offset, uint32_t *byte_count);
bool udisk_build_volume_boot_sector(uint32_t sectors, uint8_t boot_sector[512]);

enum { UDISK_FORMAT_WORKSPACE_BYTES = 0x10000 };

struct udisk_format_io {
    void *context;
    bool (*write_sectors)(void *context, uint32_t lba, uint32_t count, const uint8_t *data);
};

/* Format a logical volume through the supplied sector writer. */
bool udisk_format_volume(const struct udisk_format_io *io, uint32_t sectors,
                         uint8_t workspace[UDISK_FORMAT_WORKSPACE_BYTES]);

enum {
    UDISK_FTL_TABLE_BYTES = 0x10000,
    UDISK_FTL_BAD_BITMAP_BYTES = 0x1000,
};

struct udisk_ftl_metadata {
    uint32_t bad_block_begin, physical_table_begin, data_begin;
};

/* The stock searches each range in erase-block steps. Both ranges end
   immediately before the beginning of the next region. */
bool udisk_ftl_metadata_ranges(uint32_t data_base, uint32_t erase_bytes,
                               struct udisk_ftl_metadata *metadata);

/* Slot assignments replace the corresponding entries of an identity map.
   Stock serializes all 32,768 entries, including the unused tail. */
bool udisk_encode_physical_table(uint32_t data_base, uint32_t erase_bytes,
                                 const uint16_t *blocks, size_t count,
                                 uint8_t table[UDISK_FTL_TABLE_BYTES]);

/* Reassign bad slots to good blocks from the end of the data region.
   The bitmap is indexed relative to data_base and uses LSB-first bits. */
bool udisk_prepare_physical_table(uint32_t data_base, uint32_t erase_bytes,
                                  uint16_t physical_blocks,
                                  const uint8_t bad[UDISK_FTL_BAD_BITMAP_BYTES],
                                  uint8_t table[UDISK_FTL_TABLE_BYTES],
                                  uint32_t *logical_sectors);

enum udisk_metadata_write_result {
    UDISK_METADATA_VERIFIED,
    UDISK_METADATA_RETRY,
    UDISK_METADATA_STOP,
};

struct udisk_metadata_io {
    void *context;
    /* Retry is allowed only if the failed candidate contains no visible
       metadata marker, so stock startup cannot pick it ahead of a later copy. */
    enum udisk_metadata_write_result (*replace_block)(void *context, uint32_t address,
                                                        uint8_t marker, const uint8_t *data,
                                                        size_t bytes);
};

/* Publish the bitmap before the physical table, which is the mapper's entry point. */
bool udisk_write_ftl_metadata(const struct udisk_metadata_io *io,
                               uint32_t data_base, uint32_t erase_bytes,
                               const uint8_t bad[UDISK_FTL_BAD_BITMAP_BYTES],
                               const uint8_t table[UDISK_FTL_TABLE_BYTES]);

struct udisk_launch_io {
    void *context;
    bool (*find_file)(void *context, const char *path, uint32_t *first_cluster);
    uint32_t (*next_cluster)(void *context, uint32_t cluster);
    uint32_t (*physical_address)(void *context, uint32_t logical_byte_offset);
};

enum udisk_launch_result {
    UDISK_LAUNCH_READY,
    UDISK_LAUNCH_INVALID_INPUT,
    UDISK_LAUNCH_FILE_MISSING,
    UDISK_LAUNCH_UNALIGNED,
    UDISK_LAUNCH_MAP_FULL,
};

enum udisk_launch_result udisk_build_launch_map(const struct udisk_launch_io *io,
                                                uint32_t sectors_per_cluster,
                                                uint32_t root_sector, uint32_t data_sector,
                                                const char **selected_path,
                                                uint32_t *map, size_t map_capacity,
                                                size_t *map_entries);

#endif
