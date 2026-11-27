# nrioTool

Inspect, back up, and repair N-Card and compatible clones on a Nintendo DS,
DS Lite, or DSi. You can also dump hidden SRLs, manage files, and mount a
working cart filesystem over USB with a Slot-2 writer.

## List of Known Carts

<p align="center">
  <img src="https://github.com/ApacheThunder/nrioTool/raw/main/carts.jpg" alt="NinjaPass Junior 512M, DS Fire Link, Sunny Flash, F-Card, ExpressCard, DS-Linker, N-Card.">
</p>

Tested carts include

- DS Fire Link (8 Gbit and 16 Gbit).
- DS-Linker (8 Gbit and 16 Gbit).
- ExpressCard (16 Gbit).

A cart being listed or identified by Check Cart does not mean every write
operation supports its hardware or current boot layout.

## Start here

If you’re not sure what went wrong, open **Utility Menu → Check Cart** first.
It checks the NAND, boot spare records, boot tables, and filesystem without writing to the cart.

| What you want to do | Guide |
| --- | --- |
| Find out why the cart won’t boot, check NAND health, or make a backup | [Diagnose and back up a cart](docs/diagnose-and-back-up.md) |
| Save a ROM dump, block-0 backup, and diagnostics together | [Export a recovery bundle](docs/diagnose-and-back-up.md#export-a-recovery-bundle) |
| Recover a white-screening or bricked cart, with or without a backup | [Recover a cart that won’t boot](docs/recover-a-bricked-cart.md) |
| Fix safe mode for DSi, replace a safe-mode ROM, or undo a bypass | [Patch, replace, or bypass safe mode](docs/safe-mode.md) |
| Update stock uDisk, install XuluMenu, or rebuild the filesystem | [Update, format, and set up a cart](docs/update-and-format.md) |

## Usage

1. On DSi, put `nrioTool.nds` and a folder named `nrioFiles` on the SD card. Launch with DSi SD access, for example from Unlaunch. Recovery inputs and backups go in `sd:/nrioFiles`.
2. On DS or DS Lite, launch `nrioTool.nds` from a homebrew launcher. For advanced recovery or formatting, use separate writable Slot-2 DLDI storage and put `nrioFiles` at its root. Standard dumps use `fat:/nrioFiles`.
3. Use **Up/Down** to highlight a row, **A** to select, and **B** to go back. Options depend on the cart, console mode, and available storage. If normal startup is blocked by a bad cart, hold **L** while starting nrioTool from separate storage to go straight to recovery.

The older releases do not have all the features in these guides. The 1.7
release includes the CIA build SylverReZ helped test. The 1.9 release
includes a `title` folder for a HiyaCFW System Menu install.
[1.7 release](https://github.com/ApacheThunder/nrioTool/releases/tag/1.7) ·
[1.9 release](https://github.com/ApacheThunder/nrioTool/releases/tag/1.9)

## Features

* **Main Menu** dumps the main, safe-mode, bootleg, or uDisk SRL as available, exports the banner, and runs a 3,300-sector raw NAND test dump.
* **Utility Menu** opens **Advanced NAND tools**, checks the cart, builds update files, and mounts USB with a detected Slot-2 writer. In DSi mode it also provides **DLDI Menu**, **Boot Cart**, and **Boot GodMode9Nrio**.
* **File Browser** switches between available volumes and supports batch copy and cut, paste previews, filename conflict choices, rename, delete, and a touch keyboard. GodMode9Nrio remains available.
* **DLDI Menu** is available in DSi mode. It keeps the FAT-image dump and can dump or restore logical FAT sector 0 without mounting the cart filesystem. Restore requires an exact 512-byte FAT boot sector or FAT MBR, saves the existing sector, and verifies the write after restarting the driver. Just looking at the menu no longer forces a reboot.

**Build Update File** only converts `nrioFiles/update.bin` to
`nrioFiles/update_converted.bin` with `MKR` sector markers. It does not
install firmware. In DSi mode, **Boot GodMode9Nrio** uses an existing
`sd:/nrioFiles/GM9Nrio.nds` or extracts the bundled copy if needed.

## Dump the cartridge ROM

**Main Menu → Dump cartridge ROM** saves the Slot-1 ROM view to a new
`nrioFiles/cart-rom-NNN.nds` on DSi SD or separate writable Slot-2
storage. This is different from the 22-sector raw `stage1.bin` below.

To collect both views, use **Utility Menu → Export recovery bundle**.
It can save a block-0 backup and Check Cart report even if the ROM
cannot be read. [Recovery bundle instructions](docs/diagnose-and-back-up.md#export-a-recovery-bundle)

## Dump SRLs

The Main Menu probes Slot-1 with the read-only NAND backend and lists the SRL dumps available for the detected layout. **Dump main SRL** saves the original 22-sector main area as `stage1.bin`. **Dump Safe Mode SRL**, **Dump bootleg SRL**, and **Dump uDisk SRL** follow their NAND block tables into `safe-original.nds`, `bootleg.nds`, or `udisk.nds`. The table reader recognizes a primary table redirected to uDisk and skips tables marked `QAFE`. Mapped dumps use 16 KiB blocks and retain the last block's padding. A credible ROM size limits the dump. Otherwise, the reader uses the valid prefix of the block table.

**Dump banner** saves the first 2 KiB banner at NAND offset `0x2000` as `banner.bin`. It appends `0x40` zero bytes to produce a standard `0x840`-byte v1 banner that **Install custom banner** also accepts.

The SRL and banner dumps save to the current storage's `nrioFiles` directory. They publish the finished file after a successful read and close. Press **B** during a dump to cancel. Safe-mode dumps get numbered filenames when a previous dump exists. Move other existing outputs before repeating a dump.

**Test NAND dump** uses the raw NAND reader to save the first 3,300 sectors as `nrio_data.bin`. Starting it replaces any existing file of that name. Cancelling with **B** keeps the partial file.

## Mount the N-Card over USB

Use a DS or DS Lite with the N-Card in Slot-1 and an N-Card USB writer in Slot-2. With the USB-enabled build of nrioTool and a detected USB writer, hold **X** at startup or select **Mount USB drive** in the DS Utility Menu. Connect the writer to your computer. The cart's existing FAT volume appears as a removable drive, ready for copying ROMs and other files in either direction.

Eject the drive on the computer before pressing **START** on the DS to leave USB mode. The app requires a readable FAT volume and keeps raw NAND and recovery controls separate. Apache Thunder confirmed USB mounting and copying a file off the cart on hardware. USB writes have been tested in emulation.

The standard build includes the checked-in `data/usb_mount.bin` payload. The companion build instructions show how to regenerate it from the pinned `nrio-usb-disk` source. The earlier fork release also includes the payload. [Fork release](https://github.com/nextcode4u/nrioTool/releases/tag/recovery-1.0) · [USB mount build instructions](https://github.com/nextcode4u/DLDI-REA-Decomp/tree/main/n-card/nrioTool#build) · [Pinned USB app source](https://github.com/ApacheThunder/nrio-usb-disk/tree/b9b781bf5deec5096cbc517432878430ac0e58c6) · [Slot-2 detection adapted from Asie’s code](https://github.com/ApacheThunder/R4iSDHC_Bootstrap/blob/main/arm9/include/common/nrio_detect.c) · [USB payload notices](USB-MOUNT-LICENSES.txt)

## Build

Set `DEVKITPRO` and `DEVKITARM` for devkitARM and add the devkitPro tools to `PATH`. The asset packer uses Python 3 and the ARM9 build needs `nds-zlib`. The tested build uses libnds 1.8.3 and libfat-nds 1.1.5 in a shared legacy SDK directory. Run this from the repository root.

```sh
make LIBNDS=/path/to/legacy-sdk/opt/devkitpro/libnds
```

The Makefile builds `nrioTool.nds`, embeds the starter bundle and patches in the unified C NRIO DLDI. The same driver handles the private cart mounts and the bundled USB app. Menu setup does not depend on a NitroFS FAT image. Keep the external `nrioFiles` directory for optional inputs, logs and backups. [Integration notes and host tests](https://github.com/nextcode4u/DLDI-REA-Decomp/tree/main/n-card/nrioTool)

## Developer note

The recovery features were developed with assistance from AI language models. Some source code was generated or refined using AI, then reviewed, modified and integrated where appropriate.

## Credits

- lifehackerhansol for improvements to file read logic.
- SylverReZ for troubleshooting bugs.
