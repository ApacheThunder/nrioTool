#include "updater.h"

typedef uint32_t u32;
typedef uint8_t u8;
#include "../source/card/nand_ecc.h"

/* ARM9 0x020033B8, with the aligned 512-byte input used by the updater. */
void udisk_payload_ecc(const uint8_t sector[UDISK_SECTOR_BYTES], uint8_t ecc[3])
{
    uint32_t all = 0, word_bits[3] = {0}, row = 0;
    bool odd = false;
    for (unsigned group = 0; group < 16; ++group) {
        uint32_t parity = 0;
        for (unsigned word_index = 0; word_index < 8; ++word_index) {
            uint32_t word = (uint32_t)sector[0] | ((uint32_t)sector[1] << 8) |
                            ((uint32_t)sector[2] << 16) | ((uint32_t)sector[3] << 24);
            sector += 4;
            parity ^= word;
            for (unsigned bit = 0; bit < 3; ++bit)
                if (word_index & (1u << bit)) word_bits[bit] ^= word;
        }
        all ^= parity;
        if (odd_parity(parity)) {
            row ^= group;
            odd = !odd;
        }
    }
    finish_ecc(ecc, all, word_bits[0], word_bits[1], word_bits[2], row, odd);
}

/* ARM9 0x0200362C. The firmware passes both correction outputs and applies
   the changed byte after this routine returns. */
enum udisk_ecc_result udisk_check_payload_ecc(const uint8_t sector[UDISK_SECTOR_BYTES],
                                             const uint8_t calculated[3],
                                             const uint8_t stored[3],
                                             unsigned *offset, uint8_t *corrected)
{
    uint32_t syndrome = (uint32_t)(calculated[0] ^ stored[0]) |
                        ((uint32_t)(calculated[1] ^ stored[1]) << 8) |
                        ((uint32_t)(calculated[2] ^ stored[2]) << 16);
    unsigned bits = 0;
    for (uint32_t value = syndrome; value; value &= value - 1)
        ++bits;
    if (!bits)
        return UDISK_ECC_MATCH;
    if (bits == 1)
        return UDISK_ECC_TAG_ERROR;
    if (bits != 12 || (((syndrome >> 1) ^ syndrome) & 1) == 0)
        return UDISK_ECC_UNCORRECTABLE;

    unsigned byte_index = 0, bit_index = 0;
    for (unsigned bit = 0; bit < 9; ++bit)
        byte_index |= ((syndrome >> (bit * 2 + 1)) & 1) << bit;
    for (unsigned bit = 0; bit < 3; ++bit)
        bit_index |= ((syndrome >> (bit * 2 + 19)) & 1) << bit;
    if (offset) *offset = byte_index;
    if (corrected) *corrected = sector[byte_index] ^ (1u << bit_index);
    return UDISK_ECC_DATA_CORRECTED;
}

/* ARM9 0x02000460 strips one tag after each sector. Input and output can
   share the same starting address, as in the stock updater. */
bool udisk_unpack_payload(const uint8_t *packed, size_t packed_size, uint8_t *output,
                          size_t output_capacity)
{
    if (!packed_size || packed_size % UDISK_PACKED_SECTOR_BYTES)
        return false;
    size_t sectors = packed_size / UDISK_PACKED_SECTOR_BYTES;
    if (sectors > output_capacity / UDISK_SECTOR_BYTES)
        return false;

    for (size_t i = 0; i < sectors; ++i) {
        uint8_t calculated[3], corrected;
        unsigned offset;
        const uint8_t *tag = packed + UDISK_SECTOR_BYTES;
        udisk_payload_ecc(packed, calculated);
        enum udisk_ecc_result result = udisk_check_payload_ecc(packed, calculated, tag,
                                                              &offset, &corrected);
        bool terminal = i == sectors - 1 && calculated[0] == 0xff &&
                        calculated[1] == 0xff && calculated[2] == 0xff &&
                        tag[0] == 0xff && tag[1] == 0xff && tag[2] == 0;
        if (result > UDISK_ECC_DATA_CORRECTED && !terminal)
            return false;

        /* Destination is either separate storage or behind the source. */
        for (size_t byte = 0; byte < UDISK_SECTOR_BYTES; ++byte)
            output[byte] = packed[byte];
        if (result == UDISK_ECC_DATA_CORRECTED)
            output[offset] = corrected;
        packed += UDISK_PACKED_SECTOR_BYTES;
        output += UDISK_SECTOR_BYTES;
    }
    return true;
}
