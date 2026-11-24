#ifndef NRIO_UDISK_UPDATER_H
#define NRIO_UDISK_UPDATER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    UDISK_SECTOR_BYTES = 512,
    UDISK_PACKED_SECTOR_BYTES = 515,
    UDISK_ALLOCATION_BYTES = 0x20000,
    UDISK_ALLOCATION_COUNT = 0x800,
    UDISK_MAP_BYTES = 0x10000,
    UDISK_MAX_ERASE_FRAMES = 4,
    UDISK_PAYLOAD_NAND_OFFSET = 0x80000,
    UDISK_SECONDARY_MAP_NAND_OFFSET = 0x40000,
};

bool udisk_update_address_allowed(uint32_t address, uint32_t erase_block_bytes);
size_t udisk_erase_frames(uint32_t address, uint32_t erase_block_bytes, bool send_bank,
                         uint32_t frames[UDISK_MAX_ERASE_FRAMES]);
void udisk_expand_block_map(const uint32_t allocations[UDISK_ALLOCATION_COUNT],
                            uint8_t map[UDISK_MAP_BYTES]);
void udisk_build_secondary_chunk(const uint32_t allocations[UDISK_ALLOCATION_COUNT],
                                  uint8_t map_buffer[UDISK_ALLOCATION_BYTES]);
uint32_t udisk_byte_sum(const uint8_t *data, size_t length);

enum udisk_ecc_result {
    UDISK_ECC_MATCH,
    UDISK_ECC_DATA_CORRECTED,
    UDISK_ECC_TAG_ERROR,
    UDISK_ECC_UNCORRECTABLE,
};

/* Each operation completes synchronously and addresses a 128 KiB allocation. */
struct udisk_update_io {
    void *context;
    void (*program)(void *context, uint32_t address, const uint8_t *data, bool erase_first);
    uint32_t (*read_byte_sum)(void *context, uint32_t address);
};

enum udisk_update_result {
    UDISK_UPDATE_OK,
    UDISK_UPDATE_INVALID_INPUT,
    UDISK_UPDATE_PAYLOAD_RANGE_EXHAUSTED,
    UDISK_UPDATE_MAP_VERIFY_FAILED,
};

void udisk_payload_ecc(const uint8_t sector[UDISK_SECTOR_BYTES], uint8_t ecc[3]);
enum udisk_ecc_result udisk_check_payload_ecc(const uint8_t sector[UDISK_SECTOR_BYTES],
                                             const uint8_t calculated[3],
                                             const uint8_t stored[3],
                                             unsigned *offset, uint8_t *corrected);
/* Buffers must be separate or start at the same address. */
bool udisk_unpack_payload(const uint8_t *packed, size_t packed_size, uint8_t *output,
                          size_t output_capacity);
enum udisk_update_result udisk_install_payload(const uint8_t *payload, size_t payload_size,
                                               uint32_t erase_block_bytes,
                                               uint8_t map_buffer[UDISK_ALLOCATION_BYTES],
                                               const struct udisk_update_io *io);

#endif
