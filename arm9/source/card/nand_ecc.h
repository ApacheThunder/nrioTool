#ifndef NRIO_NAND_ECC_H
#define NRIO_NAND_ECC_H

static inline bool odd_parity(u32 word)
{
    word ^= word >> 16;
    word ^= word >> 8;
    word ^= word >> 4;
    word ^= word >> 2;
    word &= 3;
    return word == 1 || word == 2;
}

static inline void finish_ecc(u8 *ecc, u32 all, u32 word_bit0, u32 word_bit1, u32 word_bit2,
                              u32 row, bool odd)
{
    word_bit2 ^= word_bit2 >> 16;
    word_bit1 ^= word_bit1 >> 16;
    word_bit0 ^= word_bit0 >> 16;
    word_bit2 ^= word_bit2 << 8;
    word_bit1 ^= word_bit1 >> 8;
    word_bit2 ^= word_bit2 >> 4;
    word_bit1 ^= word_bit1 << 4;
    word_bit0 ^= word_bit0 >> 8;
    word_bit2 ^= word_bit2 >> 2;
    word_bit1 ^= word_bit1 << 2;
    word_bit0 ^= word_bit0 << 4;
    u32 high_nibble = all & 0xf0f0f0f0;
    high_nibble ^= high_nibble << 16;
    word_bit0 ^= word_bit0 >> 2;
    high_nibble ^= high_nibble >> 8;
    u32 odd_bits = all & 0xaaaaaaaa;
    odd_bits ^= odd_bits << 16;
    u32 odd_bytes = (all & 0xff00ff00) ^ ((all & 0xff00ffff) >> 16);
    high_nibble ^= high_nibble << 2;
    odd_bits ^= odd_bits >> 8;
    u32 high_bytes = (all >> 24) ^ (all >> 16);
    u32 high_pairs = all & 0xcccccccc;
    high_pairs ^= high_pairs << 16;
    odd_bits ^= odd_bits >> 4;
    odd_bytes = (odd_bytes >> 12) ^ (odd_bytes >> 8);
    high_bytes ^= high_bytes >> 4;
    high_pairs ^= high_pairs >> 8;
    high_bytes ^= high_bytes << 2;
    odd_bytes ^= odd_bytes >> 2;
    high_pairs = (high_pairs ^ (high_pairs << 4)) >> 2;

    /* Pack the row and column parities before adding their complements. */
    u32 packed = ((high_bytes ^ (high_bytes << 1)) & 0x8) | ((odd_bytes ^ (odd_bytes << 1)) & 0x2) |
                 ((high_pairs ^ (high_pairs << 1)) & 0x200000) | ((row & 1) << 11) |
                 ((row & 4) << 13) | ((word_bit2 ^ (word_bit2 << 1)) & 0x200) |
                 ((word_bit1 ^ (word_bit1 << 1)) & 0x80) | ((word_bit0 ^ (word_bit0 << 1)) & 0x20) |
                 ((high_nibble ^ (high_nibble << 1)) & 0x800000) |
                 ((odd_bits ^ (odd_bits << 2)) & 0x80000) | ((row & 2) << 12) | ((row & 8) << 14);
    u32 complements = odd ? packed ^ 0xaaaaaa : packed;
    u32 code = ~(packed | (complements >> 1));
    ecc[0] = (u8)code;
    ecc[2] = (u8)(code >> 16);
    ecc[1] = (u8)(code >> 8);
}

#endif
