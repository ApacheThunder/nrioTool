/*
    NitroHax -- Cheat tool for the Nintendo DS
    Copyright (C) 2008  Michael "Chishm" Chisholm

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NRIO_DLDI_H
#define NRIO_DLDI_H

#ifdef __cplusplus
extern "C" {
#endif

void InitCartNandReadMode(void);
void nrio_readSectors(void* destination, u32 rom_offset, u32 num_words);
void nrio_readSector(void* destination, u32 rom_offset);
void nrio_writeSector(u32 rom_dest, void* source);

#ifdef __cplusplus
}
#endif

#endif // DSX_DLDI_H

