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

bool _nrio_startUp (void);
bool _nrio_isInserted (void);
bool _nrio_readSectors (u32 sector, u32 numSecs, void* buffer);
bool _nrio_readSectorsTest (u32 sector, u32 numSecs, void* buffer);
bool _nrio_writeSectors (u32 sector, u32 numSectors, void* buffer);
bool _nrio_clearStatus (void);
bool _nrio_shutdown (void);

void readCardB7Mode(void* destination, u32 rom_offset, u32 num_words);
void readSectorB7Mode(void* destination, u32 rom_offset);

#ifdef __cplusplus
}
#endif

#endif // DSX_DLDI_H

