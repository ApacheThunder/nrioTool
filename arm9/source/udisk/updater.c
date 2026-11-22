#include "updater.h"

/* ARM9 0x0200042C. The upper bound uses the detected erase-block size. */
bool udisk_update_address_allowed(uint32_t address, uint32_t erase_block_bytes)
{
    return address >= 0x40000 && address <= UINT32_C(0xA00000) - erase_block_bytes;
}

/* ARM9 0x020011A0. The original sends each word most significant byte first
   and uses a zero second word. This reconstruction returns the words for inspection. */
size_t udisk_erase_frames(uint32_t address, uint32_t erase_block_bytes, bool send_bank,
                         uint32_t frames[UDISK_MAX_ERASE_FRAMES])
{
    if (!udisk_update_address_allowed(address, erase_block_bytes))
        return 0;

    uint32_t row = (address >> 11) & 0xffff;
    size_t count = 0;
    frames[count++] = UINT32_C(0xD1616000);
    frames[count++] = UINT32_C(0xD1520000) | ((row & 0xff) << 8) | (row >> 8);
    if (send_bank)
        frames[count++] = UINT32_C(0xD1510000) | ((address >> 27) << 8);
    frames[count++] = UINT32_C(0xD161D000);
    return count;
}

/* ARM9 0x020007B8–0x020007E8. The stock allocator initializes unused words
   to all ones. The expansion applies the same unsigned arithmetic to those words. */
void udisk_expand_block_map(const uint32_t allocations[UDISK_ALLOCATION_COUNT],
                            uint8_t map[UDISK_MAP_BYTES])
{
    for (size_t slot = 0; slot < UDISK_ALLOCATION_COUNT; ++slot) {
        uint32_t address = allocations[slot];
        for (uint32_t within_allocation = 0; within_allocation < UDISK_ALLOCATION_BYTES;
             within_allocation += 0x4000) {
            uint32_t entry = address + within_allocation;
            for (unsigned byte = 0; byte < sizeof(entry); ++byte)
                *map++ = (uint8_t)(entry >> (8 * byte));
        }
    }
}

/* ARM9 0x020003D8. The updater calls this on 128 KiB program and readback buffers. */
uint32_t udisk_byte_sum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0;
    for (size_t offset = 0; offset < length; ++offset)
        sum += data[offset];
    return sum;
}

/* ARM9 0x02000614 and the program/read/compare loops in 0x020006DC. */
static bool write_and_check(const struct udisk_update_io *io, uint32_t address,
                            const uint8_t *data, uint32_t erase_block_bytes)
{
    uint32_t expected = udisk_byte_sum(data, UDISK_ALLOCATION_BYTES);
    bool erase_first = address % erase_block_bytes == 0;
    for (unsigned attempt = 0; attempt < 7; ++attempt) {
        io->program(io->context, address, data, erase_first);
        if (io->read_byte_sum(io->context, address) == expected)
            return true;
    }
    return false;
}

/* ARM9 0x020006DC. The caller supplies an already unpacked payload padded
   to whole allocations. The callbacks represent the hardware operations. */
enum udisk_update_result udisk_install_payload(const uint8_t *payload, size_t payload_size,
                                               uint32_t erase_block_bytes,
                                               uint8_t map_buffer[UDISK_ALLOCATION_BYTES],
                                               const struct udisk_update_io *io)
{
    if (!payload || !map_buffer || !io || !io->program || !io->read_byte_sum ||
        !payload_size || payload_size % UDISK_ALLOCATION_BYTES ||
        payload_size / UDISK_ALLOCATION_BYTES > UDISK_ALLOCATION_COUNT ||
        (erase_block_bytes != 0x20000 && erase_block_bytes != 0x40000))
        return UDISK_UPDATE_INVALID_INPUT;

    uint32_t allocations[UDISK_ALLOCATION_COUNT];
    for (size_t i = 0; i < UDISK_ALLOCATION_COUNT; ++i)
        allocations[i] = UINT32_MAX;

    size_t offset = 0;
    uint32_t address = UDISK_PAYLOAD_NAND_OFFSET;
    while (offset < payload_size) {
        if (!udisk_update_address_allowed(address, erase_block_bytes))
            return UDISK_UPDATE_PAYLOAD_RANGE_EXHAUSTED;
        if (write_and_check(io, address, payload + offset, erase_block_bytes)) {
            allocations[offset / UDISK_ALLOCATION_BYTES] = address;
            offset += UDISK_ALLOCATION_BYTES;
        }
        address += UDISK_ALLOCATION_BYTES;
    }

    udisk_build_secondary_chunk(allocations, map_buffer);

    return write_and_check(io, UDISK_SECONDARY_MAP_NAND_OFFSET, map_buffer, erase_block_bytes)
        ? UDISK_UPDATE_OK : UDISK_UPDATE_MAP_VERIFY_FAILED;
}

void udisk_build_secondary_chunk(const uint32_t allocations[UDISK_ALLOCATION_COUNT],
                                  uint8_t map_buffer[UDISK_ALLOCATION_BYTES])
{
    udisk_expand_block_map(allocations, map_buffer);
    for (size_t i = UDISK_MAP_BYTES; i < UDISK_ALLOCATION_BYTES; ++i)
        map_buffer[i] = 0xff;

    /* The stock secondary-table chunk ends with two little-endian words. */
    const uint8_t map_footer[] = {3, 0, 0, 0, 'g', 'a', 'o', 0};
    for (size_t i = 0; i < sizeof(map_footer); ++i)
        map_buffer[UDISK_ALLOCATION_BYTES - sizeof(map_footer) + i] = map_footer[i];
}
