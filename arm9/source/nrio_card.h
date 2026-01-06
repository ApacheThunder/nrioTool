#ifndef NRIO_DLDI_H
#define NRIO_DLDI_H

#ifdef __cplusplus
extern "C" {
#endif

u32 CalcCARD_CR2_D2();
void InitCartNandReadMode(u32 CardType);
void nrio_readSectors(void* destination, u32 rom_offset, u32 num_words);
void nrio_readSector(void* destination, u32 rom_offset);
// void nrio_readSectorB7(void* destination, u32 rom_offset);
// void nrio_writeSector(u32 rom_dest, void* source);
void nrio_writeSectors(u32 dest, u32 src, u32 length);

#ifdef __cplusplus
}
#endif

#endif // DSX_DLDI_H

