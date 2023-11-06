# XuluMenu

N-Card (and clones) Universal Loader

[URL TO BE ADDED LATER]

This is a custom xmenu.dat replacement for the N-Card and it's clones.

This will simply launch `BOOT.NDS` homebrew on the root of the NAND. Only homebrew files are supported (not that the DS-Xtreme really supports any games anyway.)
A fall back file browsing UI is added if boot.nds is missing.

### Flashing instructions

1. Find and connect a DS/DS Lite with GBA slot2 writer device for your N-card/clone to PC. (or use GM9i once DLDI support is added)
2. Rename the existing xmenu.dat to xmenu.nds on your N-Card/clone's filesystem (if you want to still be able to launch it later to boot retail games).
3. Copy 'xmenu.dat' to your N-Card/clone.
4. After the process is complete, reboot and see your changes live!

### License

Major portions of this code are licensed under GPL-2.0-or-later (particularly, nds-bootloader and nds_loader_arm9.)
```
	Copyright (C) 2005 - 2010
		Michael "Chishm" Chisholm
		Dave "WinterMute" Murphy

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
```

All other source code files are licensed under the 0BSD license.
