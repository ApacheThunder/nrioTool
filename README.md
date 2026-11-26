# nrioTool
_A tool for dumping hidden SRL ROM sections from N-Card and its clones._

(F-Card, Fire Card, DS-Linker and NeoFlash MK5 are known clones of N-Card). Some recovery options are also available for DLDI section of the cart.

## List of Known Carts
<p align="center">
  <img src="https://github.com/ApacheThunder/nrioTool/raw/main/carts.jpg" alt="NinjaPass Junior 512M, DS Fire Link, Sunny Flash, F-Card, ExpressCard, DS-Linker, N-Card.">
</p>

Tested:
- DS Fire Link (8 Gbit and 16 Gbit).
- DS-Linker (8 Gbit and 16 Gbit).
- ExpressCard (16 Gbit).

## Usage

* The older 1.7 release includes the 3DS CIA build that SylverReZ helped test. Build the current source for the features documented below. It produces `nrioTool.nds`. [1.7 release](https://github.com/ApacheThunder/nrioTool/releases/tag/1.7)
* On DS or DS Lite, launch `nrioTool.nds` from your homebrew launcher. Normal dumps use `fat:/nrioFiles`. For NAND recovery, launch with a writable Slot-2 DLDI storage device and put `nrioFiles` on that device.
* On DSi, put the `nrioFiles` folder at the root of the SD card and launch with SD access. Stock setup is built into the ROM, so no external firmware or starter files are needed.
* HiyaCFW users can copy the `title` folder from the older 1.9 release to the root of the SD card to install that version on the System Menu. You can also launch `nrioTool.nds` directly from Unlaunch. [1.9 release](https://github.com/ApacheThunder/nrioTool/releases/tag/1.9)

## Features

* **Main Menu** dumps the main, safe-mode, bootleg, or uDisk SRL as available, exports the banner, and runs a 3,300-sector raw NAND test dump.
* **Utility Menu** opens **Advanced NAND tools**, checks the cart, builds update files, and mounts USB with a detected Slot-2 writer. In DSi mode it also provides **DLDI Menu**, **Boot Cart**, and **Boot GodMode9Nrio**.
* **File Browser** switches between available volumes and supports batch copy and cut, paste previews, filename conflict choices, rename, delete, and a touch keyboard. GodMode9Nrio remains available.
* **DLDI Menu** is available in DSi mode. It keeps the FAT-image dump and can dump or restore logical FAT sector 0 without mounting the cart filesystem. Restore requires an exact 512-byte FAT boot sector or FAT MBR, saves the existing sector, and verifies the write after restarting the driver. Just looking at the menu no longer forces a reboot.

## Format and set up the cart

Open **Utility Menu → Advanced NAND tools → uDisk setup**. **Quick Format** rebuilds FAT and skips marked or failed blocks. **Full Format** also write-tests usable data blocks and records newly failed ones. Both erase cart files, preserve boot firmware, and require a mounted internal SD in DSi mode or an independent Slot-2 mount in DS mode. Installed DSi System Menu titles do not need launch arguments. Explicit Slot-1 launches are blocked in DS mode. They replace **Write default FAT image** and its custom-image override.

After formatting, run **Set up stock XMenu** before rebooting. That option also replaces the exact old file-based XuluMenu wrapper, saving it as `xulu-old.nds`. Custom XMenu files are left alone. **XuluMenu NAND (recommended)** is the recommended firmware option for supported carts. It replaces hidden stock uDisk, not `xmenu.dat`. It requires the normal [XuluMenu 1.4.1 payload](https://github.com/ApacheThunder/XuluMenu/blob/7ceabcbc9331f84a8c4fcde38b6f60193405ee8a/XulumenuUpdator/NitroFS/xulumenu.bin) under `nrioFiles` on independent storage, a supported boot layout and a NAND backup before writing. Menu setup streams the compressed starter files without relying on NitroFS. **Install stock uDisk 1.45** updates hidden firmware without formatting. Exact archive matches for DS Linker, DS Fire Link, Ultra FlashPass EX, and older stock uDisk releases can qualify on verified maps with independent backup storage. Fire Link 1.23o detection is read-only. **Delete old uDisk installer** only removes a recognized leftover `autorun1.nds`.

 **Install custom uDisk** accepts `nrioFiles/udisk-replacement.nds` (compact, unpacked NRIO ROM, at most 1 MiB). The confirmation shows whether it will publish at secondary `0x40000` or fall back to primary `0x8000` on a verified direct-boot layout. **Install/replace safe-mode ROM** can also provision an erased, non-overlapping safe-mode area on a direct-boot cart. It warns if there is no secondary uDisk route. Both save main and spare snapshots before writing.


Quick Format has been tested on physical carts. Full Format and the latest driver changes have host regression coverage and still need hardware verification.

## Dump SRLs

The Main Menu probes Slot-1 with the read-only NAND backend and lists the SRL dumps available for the detected layout. **Dump main SRL** saves the original 22-sector main area as `stage1.bin`. **Dump Safe Mode SRL**, **Dump bootleg SRL**, and **Dump uDisk SRL** follow their NAND block tables into `safe-original.nds`, `bootleg.nds`, or `udisk.nds`. The table reader recognizes a primary table redirected to uDisk and skips tables marked `QAFE`. Mapped dumps use 16 KiB blocks and retain the last block's padding. A credible ROM size limits the dump. Otherwise, the reader uses the valid prefix of the block table.

**Dump banner** saves the first 2 KiB banner at NAND offset `0x2000` as `banner.bin`. It appends `0x40` zero bytes to produce a standard `0x840`-byte v1 banner that **Install custom banner** also accepts.

The SRL and banner dumps save to the current storage's `nrioFiles` directory. They publish the finished file after a successful read and close. Press **B** during a dump to cancel. Safe-mode dumps get numbered filenames when a previous dump exists. Move other existing outputs before repeating a dump.

**Test NAND dump** uses the raw NAND reader to save the first 3,300 sectors as `nrio_data.bin`. Starting it replaces any existing file of that name. Cancelling with **B** keeps the partial file.

## Menu controls

Use **Up/Down** to highlight a row, **A** to select it, and **B** to go back or exit. Available options depend on the console and mounted storage.

## Targeted block-0 fixes

The advanced menu includes these targeted fixes.

- **Patch safe mode for DSi.** Prepare the patch from the cart's own stock safe-mode ROM. An optional `safe-mode.nds` in the mounted storage's `nrioFiles` directory must match the target ROM outside the header and ARM7 sectors and carry a recognized relocation patch. Erased `FF` padding after the declared ROM length is accepted up to the next power-of-two size (at least 128 KiB). The patch still requires the live header and ARM7 sector to be in block 0.
- **Install custom banner.** Read `banner.bin` from the mounted storage's `nrioFiles` directory and update both v1 banner copies at NAND offsets `0x2000` and `0x2800` in one block-0 rewrite. The input can be `0x800` bytes or a full `0x840`-byte banner with a zero-filled final `0x40` bytes. The installer checks each page’s spare format independently. It preserves recognized ARM7 boot records and regenerates ECC only for pages already using the ECC format.
- **Restore banner ARM7.** Repair page 4's spare record if the two intact ARM7 copies from pages 1 and 5 agree and the live page-4 damage matches the old banner writer's ECC pattern. The current banner and all other records are preserved. The tool saves a block-0 backup before confirmation.

The targeted NAND write paths save the target's block-0 backup to the mounted storage before confirmation. Keep a pre-fault backup when you have one. Hold **L + R** and press **A** to write. The tool verifies the live block against the backup before erase and reads back the entire block afterward. Apache confirmed SD backup export and banner display on hardware with the initial build. This was a different cart from the verified DS Linker. It white-screened afterward and had no pre-write boot test. Apache suspected an existing uDisk problem. The revised banner writer preserves the ARM7 spare record in emulation and still needs hardware testing. The ExpressCard's raw before-and-after dumps matched the old writer's output byte for byte. It changed 472 banner bytes and 34 ARM7 spare bytes, all on NAND page 4. The restore passed emulation on the actual post-write main and spare dumps and preserved the replacement banner. Apache confirmed on hardware that the ExpressCard booted again after the targeted ARM7 restore.

## Build

Set `DEVKITPRO` and `DEVKITARM` for devkitARM and add the devkitPro tools to `PATH`. The asset packer uses Python 3 and the ARM9 build needs `nds-zlib`. The tested build uses libnds 1.8.3 and libfat-nds 1.1.5 in a shared legacy SDK directory. Run this from the repository root.

```sh
make LIBNDS=/path/to/legacy-sdk/opt/devkitpro/libnds
```

The Makefile builds `nrioTool.nds`, embeds the starter bundle and patches in the unified C NRIO DLDI. The same driver handles the private cart mounts and the bundled USB app. Menu setup does not depend on a NitroFS FAT image. Keep the external `nrioFiles` directory for optional inputs, logs and backups. [Integration notes and host tests](https://github.com/nextcode4u/DLDI-REA-Decomp/tree/main/n-card/nrioTool)

## DS and DS Lite recovery with Slot-2 storage

Launch `nrioTool.nds` from a Slot-2 storage device with its writable DLDI driver patched into the ROM. Put `nrioFiles` at the root of that device. The Utility Menu uses `fat:/nrioFiles` for backups and optional input files on this setup. In DSi mode it continues to use `sd:/nrioFiles`.

Hold **L** at launch to jump straight to recovery before the regular Slot-1 initialization. **Dump block 0** in **Advanced NAND tools** saves a block-0 backup from the N-Card in Slot-1. Recovery is available in DS mode when nrioTool mounts a read/write Slot-2 DLDI device. This avoids storing its backups on the Slot-1 cart being repaired. The Slot-2 path has been checked in software and still needs a physical DS or DS Lite test.

## Mount the N-Card over USB

Use a DS or DS Lite with the N-Card in Slot-1 and an N-Card USB writer in Slot-2. With the USB-enabled build of nrioTool and a detected USB writer, hold **X** at startup or select **Mount USB drive** in the DS Utility Menu. Connect the writer to your computer. The cart's existing FAT volume appears as a removable drive, ready for copying ROMs and other files in either direction.

Eject the drive on the computer before pressing **START** on the DS to leave USB mode. The app requires a readable FAT volume and keeps raw NAND and recovery controls separate. Apache Thunder confirmed USB mounting and copying a file off the cart on hardware. USB writes have been tested in emulation.

The standard build includes the checked-in `data/usb_mount.bin` payload. The companion build instructions show how to regenerate it from the pinned `nrio-usb-disk` source. The earlier fork release also includes the payload. [Fork release](https://github.com/nextcode4u/nrioTool/releases/tag/recovery-1.0) · [USB mount build instructions](https://github.com/nextcode4u/DLDI-REA-Decomp/tree/main/n-card/nrioTool#build) · [Pinned USB app source](https://github.com/ApacheThunder/nrio-usb-disk/tree/b9b781bf5deec5096cbc517432878430ac0e58c6) · [Slot-2 detection adapted from Asie’s code](https://github.com/ApacheThunder/R4iSDHC_Bootstrap/blob/main/arm9/include/common/nrio_detect.c) · [USB payload notices](USB-MOUNT-LICENSES.txt)

## Check the cart

Select **Check Cart** in the Utility Menu. The read-only report checks NAND geometry, boot routes, FAT mapping, and missing menu files. Press **R** to retry, **L** for next steps, and **Y** for a NAND health scan. **SELECT** tests upper address reads for possible 4 GiB NAND without enabling 4 GiB mounts or writes. With independent storage, the report is saved to `nrioFiles/CHECK-CART.TXT` and **X** checks a saved block-0 backup.

## Advanced NAND tools

Select **Advanced NAND tools** in the Utility Menu. The NAND installers validate the target, save the affected erase blocks before confirmation, and verify each rewritten block. **Restore block 0** requires a matching source backup. **Build Update File** in the Utility Menu is a file conversion action. It reads `nrioFiles/update.bin` and produces `nrioFiles/update_converted.bin` with the updater's `MKR` sector markers.

- **Replace safe-mode ROM.** Put `safe-replacement.nds` in `nrioFiles`. The tool uses the live ROM as its baseline. A local `safe-original.nds` is a backup only. The replacement must fit within the verified existing map and pass header, load address, and spare checks.
- **Repair stock uDisk.** Restore the bundled stock 1.45 payload and secondary map on supported layouts. The tool verifies the original data and readback, publishes the secondary map last, and leaves block 0 alone.
- **Bypass safe mode.** Redirect boot to a recognized existing stock uDisk 1.45 or hidden XuluMenu layout. This does not delete the installed safe-mode ROM.

If **Replace safe-mode ROM** repeatedly misreads page `0xE` (`0x7000–0x77FF`) in the first erase block, it can offer to leave that page erased. The offer depends on the data and applies across supported cart models. Both reads must show all-`FF` spare bytes, an all-`FF` first half, and only sparse cleared bits in the second half. Press **A** to allow the exception or **B** to cancel. The backup retains the original read, and the usual **L + R + A** write confirmation still applies. The tool checks the erased-page pattern on readback and compares every other page exactly. Apache Thunder tested the page-`0xE` workaround on a DS Linker. The post-write backup matched the replacement ROM byte for byte. Page `0xE` read back entirely as `FF`, and the only other main and spare changes were the two patched sectors and their ECC. DSi boot for this cart remains unconfirmed.

**Replace safe-mode ROM** and **Repair stock uDisk** append diagnostics to `ADVANCED.TXT`. When they stage a write, they save `region-NNN/blocks.bin` and `region-NNN/SESSION.TXT` on the mounted storage. If a snapshot reread differs from the saved page, the tool checks up to four more times and proceeds only after two consecutive exact matches. It publishes `blocks.bin` only after the full backup passes preflight and closes successfully. A persistent mismatch reports the NAND page, whether it was in main or spare data, the byte offset, and both values. Each block snapshot includes every main and spare byte, its physical LBA, and a CRC. Use the integration project's extractor to verify the backup or extract each block's main and packed spare files. [Download `unpack_region.py`](https://github.com/nextcode4u/DLDI-REA-Decomp/blob/main/n-card/nrioTool/unpack_region.py)

```sh
python3 unpack_region.py region-000/blocks.bin --out extracted-blocks
```

After confirmation, the tool shows the current block and displays a separate completion or failure screen. A program or verification failure stops the operation and leaves the saved snapshots available. Stock uDisk repair still needs a physical-cart test.

## Recover block 0 from a backup

While the cart is working, select **Dump block 0** under **Utility Menu → Advanced NAND tools**. Keep `main.bin`, packed `spare.bin`, and `backup.info` together in the resulting `nrioFiles/block0-NNN` folder. Backups from earlier builds lack the metadata used by this menu.

If the cart stops booting, launch nrioTool from the DSi SD card or Slot-2 storage while holding **L** to enter recovery before the regular cart initialization. The NAND bridge must still answer the raw probe. Choose **Advanced NAND tools**, then **Restore block 0**. Use **Left/Right** to choose the backup from that same cart, then press **A**. The app checks the file CRCs, NAND geometry, and saved data at `0x40000` and `0x80000`. Those data checks help catch a mismatched backup. Always use the original cart's backup.

After the app saves the current block to a separate backup folder, hold **L + R** and press **A** to restore. It checks the live snapshot before erase and verifies every main and spare byte after writing. This restores block 0 only. If block 0 is erased and there is no prior backup, the narrower ARM7 repair can help only when its required copies survive. The new startup shortcut and full restore still need a physical DSi test.

**GodMode9Nrio:** The built-in copy is compressed in NitroFS and unpacked
to `sd:/nrioFiles/GM9Nrio.nds` only when launched without an existing SD copy.
The existing SD copy is left alone.

**FPGA DS Linker:** Apache Thunder confirmed on hardware that he could
restore safe mode on a bypassed cart, replace it with the ROM that matches
that cart, and still boot through to uDisk/XuluMenu. A successful write does
not make the newer safe-mode ROM compatible with the FPGA revision.

## Developer note

The recovery features were developed with assistance from AI language models. Some source code was generated or refined using AI, then reviewed, modified and integrated where appropriate.

## Credits
lifehackerhansol: Improvements to file read logic.

SylverReZ: Troubleshooting bugs.

Check Cart reports when the primary table points to a direct-boot DS ROM at `0x40000` and when the known spare overlap repeats on pages 2 and 3. These are read-only observations.

**Patch main ARM9 marker** reproduces the duplicate-marker edit on the verified ExpressCard layout. It saves a block-0 backup on independent storage and preserves all spare records. It is not a general main-ROM installer.

**Check Cart → NAND and early boot** reports whether the main ARM9 marker is supported, already patched, unknown, or unreadable. It never stages a write. Block-0 backups are reopened and checked for exact sizes and CRCs before nrioTool reports them as saved.

**Older firmware recognition.** Check Cart identifies known DS Linker, DS Fire Link, Ultra FlashPass EX, and older stock uDisk images using two sectors from the mapped ROM. The SRL dumper uses the recognized wrapper length and retains the last mapped block padding. Archive recognition alone does not authorize writes or add support for new NAND geometry.
