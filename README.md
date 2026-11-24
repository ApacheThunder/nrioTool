# nrioTool
_Dump hidden SRLs, back up and repair NAND, and access the FAT volume on N-Card and compatible clones._

(F-Card, Fire Card, DS-Linker and NeoFlash MK5 are known clones of N-Card). Some recovery options are also available for DLDI section of the cart.

## List of Known Carts
![NinjaPass Junior 512M, DS Fire Link, Sunny Flash, F-Card, ExpressCard, DS-Linker, N-Card.](carts.jpg)

Tested:
- DS Fire Link (8 Gbit and 16 Gbit).
- DS-Linker (8 Gbit and 16 Gbit).
- ExpressCard (16 Gbit).

## Getting started

Build and launch `nrioTool.nds` from a homebrew launcher. In DSi mode, use a launcher with SD access and put any input files in `sd:/nrioFiles`. In DS mode, normal dumps use `fat:/nrioFiles` on the mounted DLDI device. nrioTool creates the output directory when it starts. Targeted NAND recovery in DS mode requires separate writable Slot-2 storage.

Apache Thunder confirmed that nrioTool launches from XMenu on a DS Lite with a 2 GB N-Card after the Slot-1 init fix. Build the current source for the features documented here. The older 1.9 release includes a HiyaCFW `title` folder. [Upstream 1.9 release](https://github.com/ApacheThunder/nrioTool/releases/tag/1.9)

## Menu controls

Use **Up/Down** to highlight a row, **A** to select it, and **B** to go back or exit. The Main Menu lists the available NAND dumps and opens the Utility Menu for cart checks, block-0 backups, USB mounting, GodMode9Nrio, and Advanced NAND tools. Available options depend on the console and mounted storage.

### DLDI Menu

The DLDI Menu is available in DSi mode. Use it to boot the cart menu, dump FAT sector 0 to `sd:/nrioFiles/nrio_fatsector0.bin`, or dump the FAT image to `sd:/nrioFiles/nrio_fatimage_dump.bin`. Restart nrioTool to return to the Main Menu after entering the DLDI Menu.

- **Write default FAT image** selects one of the two bundled 16 Gbit images by detected FAT capacity. If `sd:/nrioFiles/nrio_customimage.img` exists, the tool uses it instead. This overwrites the cart's FAT contents.
- **Restore FAT sector 0** has a known bug. It reads sector 0 again and incorrectly reports a successful restore.

## Dump SRLs

The Main Menu probes Slot-1 with the read-only NAND backend and lists the SRL dumps available for the detected layout. **Dump main SRL** saves the original 22-sector main area as `stage1.bin`. **Dump Safe Mode SRL**, **Dump bootleg SRL**, and **Dump uDisk SRL** follow their NAND block tables into `safe-original.nds`, `bootleg.nds`, or `udisk.nds`. The table reader recognizes a primary table redirected to uDisk and skips tables marked `QAFE`. Mapped dumps use 16 KiB blocks and retain the last block's padding. A credible ROM size limits the dump. Otherwise, the reader uses the valid prefix of the block table.

**Dump banner** saves the first 2 KiB banner at NAND offset `0x2000` as `banner.bin`. It appends `0x40` zero bytes to produce a standard `0x840`-byte v1 banner that **Install custom banner** also accepts.

The SRL and banner dumps save to the current storage's `nrioFiles` directory. They publish the finished file after a successful read and close. Press **B** during a dump to cancel. Move an existing output file before dumping it again.

**Test NAND dump** uses the raw NAND reader to save the first 3,300 sectors as `nrio_data.bin`. Starting it replaces any existing file of that name. Cancelling with **B** keeps the partial file.

## Targeted block-0 fixes

The advanced menu includes these targeted fixes.

- **Patch safe mode for DSi.** Prepare the patch from the cart's own stock safe-mode ROM. An optional `safe-mode.nds` in the mounted storage's `nrioFiles` directory must match the target ROM outside the header and ARM7 sectors and carry a recognized relocation patch. Erased `FF` padding after the declared ROM length is accepted up to the next power-of-two size (at least 128 KiB). The patch still requires the live header and ARM7 sector to be in block 0.
- **Install custom banner.** Read `banner.bin` from the mounted storage's `nrioFiles` directory and update the first v1 banner copy at NAND offset `0x2000`. The input can be `0x800` bytes or a full `0x840`-byte banner with a zero-filled final `0x40` bytes. The second banner copy and recognized ARM7 boot records in the spare area are preserved. ECC is regenerated only for existing ECC-format banner records.
- **Restore banner ARM7.** Repair page 4's spare record if the two intact ARM7 copies from pages 1 and 5 agree and the live page-4 damage matches the old banner writer's ECC pattern. The current banner and all other records are preserved. The tool saves a block-0 backup before confirmation.

The targeted NAND write paths save the target's block-0 backup to the mounted storage before confirmation. Keep a pre-fault backup when you have one. Hold **L + R** and press **A** to write. The tool verifies the live block against the backup before erase and reads back the entire block afterward. Apache confirmed SD backup export and banner display on hardware with the initial build. This was a different cart from the verified DS Linker. It white-screened afterward and had no pre-write boot test. Apache suspected an existing uDisk problem. The revised banner writer preserves the ARM7 spare record in emulation and still needs hardware testing. The ExpressCard's raw before-and-after dumps matched the old writer's output byte for byte. It changed 472 banner bytes and 34 ARM7 spare bytes, all on NAND page 4. The restore passed emulation on the actual post-write main and spare dumps and preserved the replacement banner. Apache confirmed on hardware that the ExpressCard booted again after the targeted ARM7 restore.

## Build

Set `DEVKITPRO` and `DEVKITARM` for devkitARM and add the devkitPro tools to `PATH`. The tested build uses libnds 1.8.3 and libfat-nds 1.1.5 in a shared legacy SDK directory. Run this from the repository root.

```sh
make LIBNDS=/path/to/legacy-sdk/opt/devkitpro/libnds
```

The Makefile builds `nrioTool.nds`, embeds the files in `nrioFiles` as NitroFS, and patches in `nrio.dldi`. Keep the external `nrioFiles` directory for inputs and outputs. [Integration notes](https://github.com/nextcode4u/DLDI-REA-Decomp/tree/b66838d/n-card/nrioTool)

## DS and DS Lite recovery with Slot-2 storage

Launch `nrioTool.nds` from a Slot-2 storage device with its writable DLDI driver patched into the ROM. Put `nrioFiles` at the root of that device. The Utility Menu uses `fat:/nrioFiles` for backups and optional input files on this setup. In DSi mode it continues to use `sd:/nrioFiles`.

Hold **L** at launch to jump straight to recovery before the regular Slot-1 initialization. **Back up block 0** saves a block-0 backup from the N-Card in Slot-1. Recovery is available in DS mode when nrioTool mounts a read/write Slot-2 DLDI device. This avoids storing its backups on the Slot-1 cart being repaired. The Slot-2 path has been checked in software and still needs a physical DS or DS Lite test.

## Mount the N-Card over USB

Use a DS or DS Lite with the N-Card in Slot-1 and an N-Card USB writer in Slot-2. With the USB-enabled build of nrioTool and a detected USB writer, hold **X** at startup or select **Mount USB drive** in the DS Utility Menu. Connect the writer to your computer. The cart's existing FAT volume appears as a removable drive, ready for copying ROMs and other files in either direction.

Eject the drive on the computer before pressing **START** on the DS to leave USB mode. The app requires a readable FAT volume and keeps raw NAND and recovery controls separate. Apache Thunder confirmed USB mounting and copying a file off the cart on hardware. USB writes have been tested in emulation.

The standard build includes the checked-in `data/usb_mount.bin` payload. The companion build instructions show how to regenerate it from the pinned `nrio-usb-disk` source. The earlier fork release also includes the payload. [Fork release](https://github.com/nextcode4u/nrioTool/releases/tag/recovery-1.0) · [USB mount build instructions](https://github.com/nextcode4u/DLDI-REA-Decomp/tree/main/n-card/nrioTool#build) · [Pinned USB app source](https://github.com/ApacheThunder/nrio-usb-disk/tree/b9b781bf5deec5096cbc517432878430ac0e58c6) · [Slot-2 detection adapted from Asie’s code](https://github.com/ApacheThunder/R4iSDHC_Bootstrap/blob/main/arm9/include/common/nrio_detect.c) · [USB payload notices](USB-MOUNT-LICENSES.txt)

## Check the cart

Select **Check cart** in the Utility Menu. The read-only check reports the NAND ID, detected erase geometry, and the addresses and recognized ROM types selected by the primary and secondary boot tables. The report is appended to `nrioFiles/CHECK-CART.TXT`. Press **X** while viewing the report to select a saved block-0 backup and check its file lengths, CRCs, and cart anchors. A layout match identifies the header and mapped ARM7 pattern. It cannot predict bootability.

## Advanced NAND tools

Select **Advanced NAND tools** in the Utility Menu. The NAND installers validate the target, save the affected erase blocks before confirmation, and verify each rewritten block. **Restore block 0** requires a matching source backup. **Build update file** is a file conversion action. It reads `nrioFiles/update.bin` and produces `nrioFiles/update_converted.bin` with the updater's `MKR` sector markers.

- **Replace safe-mode ROM.** Put `safe-replacement.nds` in `nrioFiles` on the mounted storage. The tool reads the original safe-mode ROM from the cart. If you supply `safe-original.nds`, it must match the mapped ROM byte for byte through its declared length. The replacement must fit in the existing mapping and pass header, ARM load range, and spare checks. Files up to 256 KiB are accepted. The checker keeps padding beyond the replacement's declared size on NAND and can stage changes in more than one erase block.
- **Repair stock uDisk.** Extract the tagged `udisk_eng.nds` from the archived stock BIOS updater and put it in `nrioFiles`. The tool verifies the known package by size, CRC, and sector ECC. It requires an intact recognized early safe-mode route and either the contiguous stock uDisk map or an erased secondary map with blank or already matching payload sectors. The map tail must be blank or already match the stock padding and footer. It writes and verifies the payload first, updates the secondary map last, and refuses to touch block 0. [Stock updater archive](https://github.com/m4x10187/fire-card-archive/tree/ced86434054e47dcca8d4e90efd25cc92f88d688/N-Card/Dumps/recovered)

If **Replace safe-mode ROM** repeatedly misreads page `0xE` (`0x7000–0x77FF`) in the first erase block, it can offer to leave that page erased. The offer depends on the data and applies across supported cart models. Both reads must show all-`FF` spare bytes, an all-`FF` first half, and only sparse cleared bits in the second half. Press **A** to allow the exception or **B** to cancel. The backup retains the original read, and the usual **L + R + A** write confirmation still applies. The tool checks the erased-page pattern on readback and compares every other page exactly. Apache Thunder tested the page-`0xE` workaround on a DS Linker. The post-write backup matched the replacement ROM byte for byte. Page `0xE` read back entirely as `FF`, and the only other main and spare changes were the two patched sectors and their ECC. DSi boot for this cart remains unconfirmed.

The `udisk_eng.nds` data in the reference `update.nds` begins at `0x24200` and is `0xC1200` bytes long. Extract it with a NitroFS tool or run this command with the archived updater.

```sh
dd if=update.nds of=udisk_eng.nds bs=512 skip=289 count=1545
```

**Replace safe-mode ROM** and **Repair stock uDisk** append diagnostics to `ADVANCED.TXT`. When they stage a write, they save `region-NNN/blocks.bin` and `region-NNN/SESSION.TXT` on the mounted storage. If a snapshot reread differs from the saved page, the tool checks up to four more times and proceeds only after two consecutive exact matches. It publishes `blocks.bin` only after the full backup passes preflight and closes successfully. A persistent mismatch reports the NAND page, whether it was in main or spare data, the byte offset, and both values. Each block snapshot includes every main and spare byte, its physical LBA, and a CRC. Use the integration project's extractor to verify the backup or extract each block's main and packed spare files. [Download `unpack_region.py`](https://github.com/nextcode4u/DLDI-REA-Decomp/blob/main/n-card/nrioTool/unpack_region.py)

```sh
python3 unpack_region.py region-000/blocks.bin --out extracted-blocks
```

After confirmation, the tool shows the current block and displays a separate completion or failure screen. A program or verification failure stops the operation and leaves the saved snapshots available. Stock uDisk repair still needs a physical-cart test.

## Recover block 0 from a backup

While the cart is working, select **Back up block 0** in the Utility Menu. Keep `main.bin`, packed `spare.bin`, and `backup.info` together in the resulting `nrioFiles/block0-NNN` folder. Backups from earlier builds lack the metadata used by this menu.

If the cart stops booting, launch nrioTool from the DSi SD card or Slot-2 storage while holding **L** to enter recovery before the regular cart initialization. The NAND bridge must still answer the raw probe. Choose **Advanced NAND tools**, then **Restore block 0**. Use **Left/Right** to choose the backup from that same cart, then press **A**. The app checks the file CRCs, NAND geometry, and saved data at `0x40000` and `0x80000`. Those data checks help catch a mismatched backup. Always use the original cart's backup.

After the app saves the current block to a separate backup folder, hold **L + R** and press **A** to restore. It checks the live snapshot before erase and verifies every main and spare byte after writing. This restores block 0 only. If block 0 is erased and there is no prior backup, the narrower ARM7 repair can help only when its required copies survive. The new startup shortcut and full restore still need a physical DSi test.

## Developer note

The recovery features were developed with assistance from AI language models. Some source code was generated or refined using AI, then reviewed, modified and integrated where appropriate.

## Credits
lifehackerhansol: Improvements to file read logic.

SylverReZ: Troubleshooting bugs.
