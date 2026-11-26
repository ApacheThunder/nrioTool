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

**NAND and early boot** reports whether the main ARM9 marker is supported, already patched, unknown, or unreadable. The report can also flag a known spare-area overlap on pages 2 and 3. Neither result means a repair has been staged. Check Cart recognizes some archived DS Linker, DS Fire Link, Ultra FlashPass EX, and stock uDisk versions from two sectors of the mapped ROM. That identification alone does not authorize writes or add support for a new NAND geometry.

## Make a recovery backup

1. Open **Utility Menu → Advanced NAND tools → Dump block 0**.
2. Keep `main.bin`, `spare.bin`, and `backup.info` together in their new `nrioFiles/block0-NNN` folder. Label your off-device copy with the exact cart and date. Even identically branded carts can have different layouts.
3. In **Check Cart**, press **X** if the backup option is shown, choose the folder, and inspect the result. nrioTool reopens new backups to check sizes and CRCs before calling them saved. It can also compare saved anchors against the current cart.

A block-0 backup holds the first physical erase block and its spare records. It does **not** back up all hidden firmware or any files outside that block. Copy files separately and dump any available safe-mode/uDisk SRLs before changing their boot routes. SRL dumps are useful references, not substitutes for the matching main-and-spare backup.

## Check NAND health

Press **Y** in Check Cart when the scan is available. It does not erase or program NAND. On independent storage it can save `NAND-HEALTH.TXT`. Marked blocks or repeat read errors help narrow down the issue. A clean read-only scan does not prove that a block will accept writes, and the scan does not remap anything.

**SELECT** starts the experimental read-only 4 GiB address probe. It does not enable 4 GiB mounts or writes.
