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

#ifndef DSX_DLDI_H
#define DSX_DLDI_H

#ifdef __cplusplus
extern "C" {
#endif

void PrintProgramName(void);
void dsxWaitMs(unsigned int requestTime);
void dsxSendCommand(unsigned int command[2], unsigned int pageSize, unsigned int latency, unsigned char *buf);
void dsxPoll(void);
void dsxZoneSwitch(unsigned int lba);
bool dsxStartup(void);
bool dsxIsInserted (void);
bool dsxClearStatus (void);
bool dsxReadSectors (u32 sector, u32 numSectors, void* buffer);
bool dsxWriteSectors (u32 sector, u32 numSectors, void* buffer);
bool dsx2ReadSectors (u32 sector, u32 numSectors, void* buffer);
bool dsx2WriteSectors (u32 sector, u32 numSectors, void* buffer);
void bannerWrite(int sectorStart, int writeSize);
bool dsxShutdown(void);

#ifdef __cplusplus
}
#endif

#endif // DSX_DLDI_H

