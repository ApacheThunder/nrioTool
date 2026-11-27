# Diagnose and back up a cart

[Back to the nrioTool README](../README.md)

## Start with Check Cart

1. Launch nrioTool from the DSi SD card in DSi mode, or use writable Slot-2 storage on a DS or DS Lite if you need backups or recovery writes. A Slot-2 N-Card USB writer is not the same thing as writable Slot-2 launch storage.
2. If normal startup hangs or stops at cart initialization, relaunch from that separate storage while holding **L**. This opens recovery before normal Slot-1 initialization. The target still has to answer the NAND probe.
3. Open **Utility Menu → Check Cart**. Use **Left/Right** to change pages, **Up/Down** to scroll, **L** for help, and **R** to retry. With independent storage, the app appends each report to `nrioFiles/CHECK-CART.TXT`. Use the latest report for the cart you just tested.

| What the report shows | What to try |
| --- | --- |
| NAND probe failed or geometry could not be determined | Reseat the cart and try **R** or the **L** startup path. If it still fails, save the exact error. A failed probe alone doesn’t prove the NAND is dead, and a restore can’t run without a working probe. |
| Primary safe mode is stock or the ARM7 patch is recognized | This identifies the ROM, not whether it will boot on your console. For a DSi white screen, see [Safe mode](safe-mode.md). |
| Primary table maps to a ROM at `0x40000` | This can be a direct-boot ROM or firmware rather than a separate secondary table. Don’t treat an absent secondary table as damage just because another cart has one. |
| FAT mounts, but `xmenu.dat` or `DSYSTEM` is missing | [Set up menu files](update-and-format.md#fix-missing-menu-files) before trying a format or firmware install. |
| Read errors, unstable pages, or FAT won’t mount | Copy any accessible files, then run a [read-only health scan](#check-nand-health). Don’t format a cart whose files you still want to recover. |
| Early boot or block 0 looks erased or corrupt | See [Recover a cart that won’t boot](recover-a-bricked-cart.md). |

**NAND and early boot** separates the last logical ROM initialization from
the raw NAND result. The last ROM result is historical. In the L-start
recovery path it may not have been attempted. Check Cart also reports whether the main ARM9 marker is supported, already patched, unknown, or unreadable. Neither this check nor the boot spare check stages a repair. Check Cart recognizes some archived DS Linker, DS Fire Link, Ultra FlashPass EX, and stock uDisk versions from two sectors of the mapped ROM. That identification alone does not authorize writes or add support for a new NAND geometry.

If the direct NAND ID probe gets no usable reply, the read-only recovery
path tries the existing cart setup sequence once without launching the
cart ROM. Two reads of the first sector must agree before it uses that
retry for diagnostics or backups. The report tells you whether the retry
worked, returned no ID, or returned an ID but could not read reliably.
This does not bypass any restore checks, and the retry still needs a
test on physically bricked hardware.

## Compare the final C1 command

Use **Utility Menu → Compare C1 NAND reads** on a working cart from DSi
SD or separate writable Slot-2 storage. Keep the same cart in all three
passes. On DS, reinsert it when asked. A recognized DSGB ROM supplies its
actual setup word. This test does not guess a word for an unreadable ROM.

Choose a known-good block-0 backup from that same cart if you have one.
The test checks the backup's CRCs, geometry and anchors before comparing
sampled pages. The test runs full init, resets and omits the last C1, then
resets and repeats full init. It samples six main and spare pages twice
per pass and records NAND ID, read stability, CRCs, and exact comparisons
in `nrioFiles/C1-READ-TEST.TXT`. If its full baseline does not verify, it
will not run the omitted-command pass. Without a separate reference, a
stable full baseline is only a control, not proof the bytes are correct.
If normal setup cannot be confirmed afterward, nrioTool exits rather
than allowing another operation in an uncertain state.

This tests Apache Thunder's observation that skipping the final C1 may
break reads even if the NAND ID still comes back. It does not program or
erase NAND or test whether writes would be affected.

## Check the boot spare records

On the **Boot spare check** page, nrioTool reads each of the first six
spare records twice. These are 64-byte records, separate from each page's
main data. The same results are saved in `CHECK-CART.TXT`.

- **Page 0** is reported as populated or all `FF`. Both are seen on working carts.
- **ARM7 p1, p4 and p5** show whether each copy matches a known stock fingerprint and whether the three copies agree. Apache Thunder's eight bootleg dumps match the `1918027E` variant. An older 8 Gbit DS Linker capture matches `1118027E`.
- **Table p2 and p3** show stock fingerprint matches and whether the overlapping 56 bytes agree. Agreement alone does not verify the unique eight bytes in either record.
- **Read failed** or **unstable** means nrioTool could not verify that record. It is not treated as erased or as evidence of a stock match.

This is useful even if the main boot header is missing, as long as the
NAND probe works. A match identifies known spare data, not a compatible
recovery image or a guarantee the cart boots. This check does not write
the NAND or reconstruct an erased block 0.

## Make a recovery backup

1. Open **Utility Menu → Advanced NAND tools → Dump block 0**.
2. Keep `main.bin`, `spare.bin`, and `backup.info` together in their new `nrioFiles/block0-NNN` folder. Label your off-device copy with the exact cart and date. Even identically branded carts can have different layouts.
3. In **Check Cart**, press **X** if the backup option is shown, choose the folder, and inspect the result. nrioTool reopens new backups to check sizes and CRCs before calling them saved. It can also compare saved anchors against the current cart.

A block-0 backup holds the first physical erase block and its spare records. It does **not** back up all hidden firmware or any files outside that block. Copy files separately and dump any available safe-mode/uDisk SRLs before changing their boot routes. SRL dumps are useful references, not substitutes for the matching main-and-spare backup.

## Export a recovery bundle

1. Launch from DSi SD or separate writable Slot-2 storage. If normal startup fails, hold **L** when launching.
2. Open **Utility Menu → Export recovery bundle**. Press **A** to save Check Cart and, if readable, the block-0 backup in a new `nrioFiles/capture-NNN` folder. You can use **B** to skip the ROM and still keep those files.
3. To include the ROM, choose **Add ROM dump**. If the DS prompts you to reinsert the cart, use the same one. A failed or cancelled ROM read does not delete the earlier raw backup or report.

The folder contains `CAPTURE.TXT` with the outcome of each step.
If the early FireLine table sectors can be read twice, the capture includes
`OS-MAP.BIN` and `DIR-MAP.BIN`. It also includes the first sectors at valid
mapped destinations as `OS-HEAD.BIN` and `DIR-HEAD.BIN`. The report
records which files were saved and which signatures matched.
`block0-000` inside it is the usual restore backup. Keep its three files
together. To make it selectable by **Restore block 0**, copy that whole
folder under `nrioFiles`, with a free `block0-NNN` name.

The optional `cart-rom.nds` is the logical ROM that the cart presents
to Slot-1, not a raw NAND image and not the hidden uDisk dump. The dumper
accepts DS-mode ROMs and reads the first 4 KiB twice without replacing
the header padding. It zero-fills `0x1000–0x3FFF` and uses the usual
decoded form for the secure area. The log shows both the stored
secure-area CRC and the CRC of the exported bytes. A difference alone
doesn't establish damage, because they may describe different forms.
The dumper saves the plausible used size when
present, or the capacity from the header otherwise.

When the raw backup and live checks agree, the log compares the 64
bytes at ROM offset `0x400` with spare page 0. The log also compares ROM
`0xC00–0xFFF` with raw main NAND `0x000–0x3FF`, and counts consecutive
copies of the ROM record at `0x80`-byte intervals. Apache Thunder's
DS Linker and both ExpressCard captures matched both windows exactly.
The log also checks the masked spare copies in ROM `0x800–0xBFF` and
compares a 64-byte ROM ARM7 payload with the saved spare copies. It tries
direct `B7` reads at the two early offsets separately from the header
read. A failed early `B7` probe does not invalidate the saved ROM.
These are comparisons, not a full logical-to-physical map. [First-stage ROM notes](main-rom-notes.md) A match does not make the
dump a safe source to program into NAND. The new ROM path still needs
physical-cart testing.

For a recognized main header, **NAND and early boot** shows the ASIC
setup word. It is not used to infer the save type or erase geometry.

### Preview ARM7 spare recovery

A complete bundle checks the logical ROM's 64-byte ARM7 against packed
spare pages 1, 4, and 5. The read-only preview can name one candidate
page if the ROM header matches the raw header, all three spare reads
are stable and agree with the saved capture, and two copies match a
known ARM7 variant byte for byte. An alternate known variant is a
conflict, not an instruction to overwrite it. A preview never stages a
write or touches spare page 0.

## Check a ROM file before launching elsewhere

Select a `.nds` file in **File Browser**, then **X → Check ROM launch**.
It checks the DS header, file length, the 16,384-entry limit of the stock
ROM table, and readability in 16 KiB chunks. For a supported FAT16 or
FAT32 N-Card volume it also checks the file's logical FAT chain and
reports contiguous runs. The mounted DLDI interface does not expose
physical NAND addresses. This is not a physical map preview, a
compatibility guarantee, or a launcher.

## Check NAND health

Press **Y** in Check Cart when the scan is available. It does not erase or program NAND. On independent storage it can save `NAND-HEALTH.TXT`. Marked blocks or repeat read errors help narrow down the issue. A clean read-only scan does not prove that a block will accept writes, and the scan does not remap anything.

**SELECT** opens the read-only address probe. Select the NAND's main page
size, then press **A** for the 4 GiB probe or **X** to compare raw reads
128 and 256 MiB apart. The alias check saves `NAND-ALIAS.TXT` on independent
storage. Uniform, unstable, or failed upper reads are inconclusive. Matching
structured reads suggest a possible alias, which can also occur when the chip
is smaller than the tested address range. Neither option tests USB writes.

## Identify and dump the early FireLine OS

**Check Cart → Boot Tables** checks the older FireLine OS and directory
maps at `0x80000` and `0x84000`. A match uses the archived `os.nds`
fingerprint, the directory signature, and a valid OS block map.
With independent storage available, **Dump FireLine OS** in the main
menu saves the mapped image with its padding as `nrioFiles/fireline-os.nds`.
This check does not enable legacy formatting or installation.

## Swap carts on DSi

When nrioTool is running in DSi mode with access to the Slot-1 status
register, it detects removal and insertion without rebooting. Swap carts
while idle. The menus drop the old cart's state, and nrioTool probes the
new one without remounting DSi SD. Reopen File Browser to mount the new
cart. DS mode and launchers that lock the status register do not use
automatic detection.

Removing a cart during a confirmation cancels the pending write. Never
intentionally remove it while formatting or writing. If a write is
interrupted, the old cart may still need repair. nrioTool does not resume
an interrupted cart operation on the replacement.
