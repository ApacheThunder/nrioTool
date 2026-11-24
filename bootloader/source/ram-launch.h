#ifndef NRIO_RAM_LAUNCH_H
#define NRIO_RAM_LAUNCH_H

#include <stdbool.h>
#include <stdint.h>

#define NRIO_RAM_IMAGE UINT32_C(0x02280000)
#define NRIO_RAM_IMAGE_LIMIT UINT32_C(0x40000)

static inline bool nrio_ram_segment_valid(uint32_t file_size, uint32_t offset, uint32_t length,
                                          uint32_t destination, uint32_t entry,
                                          uint32_t first, uint32_t limit)
{
    entry &= ~1u;
    return offset >= 0x200 && offset <= file_size && length && length <= file_size - offset &&
           !((offset | length | destination) & 3) && destination >= first && destination < limit &&
           length <= limit - destination && entry >= destination && entry - destination < length;
}

static inline bool nrio_ram_image_valid(const uint32_t *header, uint32_t length)
{
    if (length < 0x200 || length > NRIO_RAM_IMAGE_LIMIT || length & 3) return false;
    return nrio_ram_segment_valid(length, header[8], header[11], header[10], header[9],
                                 0x02000000, NRIO_RAM_IMAGE) &&
           (nrio_ram_segment_valid(length, header[12], header[15], header[14], header[13],
                                  0x037f8000, 0x03810000) ||
            nrio_ram_segment_valid(length, header[12], header[15], header[14], header[13],
                                  0x02380000, 0x023fc000));
}

#endif
