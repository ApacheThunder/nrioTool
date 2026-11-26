# Patch, replace, or bypass safe mode

[Back to the nrioTool README](../README.md)

Start with [Check Cart and a block-0 backup](diagnose-and-back-up.md). A
typical layout uses the primary boot table at `0x8000` for safe mode and
the secondary table at `0x40000` for uDisk or XuluMenu. A customized cart
may point straight at that firmware instead. Check what the tables
actually load before picking a repair.

## Fix a stock safe-mode ROM for DSi

1. If the cart white-screens only when launched by another homebrew app, try launching the cart directly from the DSi System Menu first. These are different boot paths.
2. In Check Cart, look at **Boot tables**. If the primary shows **Stock safe mode**, choose **Utility Menu → Advanced NAND tools → Patch safe mode for DSi**. The tool can prepare the patch from the cart’s own recognized ROM. You don’t need to supply a file.
3. If you supply an optional `nrioFiles/safe-mode.nds`, it must match the live ROM outside the header and ARM7 sectors and carry a recognized relocation patch. The targeted patch requires the header and ARM7 sector to be in block 0.
4. After the backup and preflight, hold **L + R** and press **A**. Wait for verification, then test direct boot from the DSi System Menu.

**Patched safe mode** means the tool recognizes the ARM7 patch, not that
the rest of the boot works. If it still white-screens, check the secondary
firmware and menu files before applying the same patch again.

## Replace or reinstall safe mode

1. Prefer a ROM known to work on this exact cart revision. Use **Dump Safe Mode SRL** to keep a copy if the current ROM is readable. Put the intended replacement at `nrioFiles/safe-replacement.nds` on independent storage.
2. Select **Advanced NAND tools → Install/replace safe-mode ROM**. The tool uses the ROM on the cart as its reference where one is present. A stale `safe-original.nds` on the SD is not required and does not decide what matches the cart.
3. Review the plan before confirming with **L + R + A**. On a direct-boot cart, the tool can install missing safe mode if it can verify a suitable area and boot route. **Secondary firmware is kept** means the existing secondary route is retained. If it warns that there is no secondary route, normal boot may not work until you set up uDisk. Cancel if you don’t have a plan for that.
4. Wait for the verification screen, rerun Check Cart, then test normal boot through to uDisk or XuluMenu.

A successful write does not make a newer ROM compatible with older
hardware. Apache Thunder verified reinstalling safe mode and booting
through to uDisk/XuluMenu on an FPGA DS Linker with the ROM that matched
that cart. A newer DS Linker ROM could show a safe-mode screen on the
FPGA cart and still fail to start uDisk.

## Bypass safe mode or undo a bypass

**Advanced NAND tools → Bypass safe mode** redirects boot to a verified
existing stock uDisk 1.45 or hidden XuluMenu layout. It does not erase the
safe-mode ROM and does not install or repair uDisk. Back up block 0
before bypassing. To undo it, use a matching pre-bypass block-0 backup
if it still describes the cart’s layout, or reinstall the compatible
safe-mode ROM on a supported layout. [Block-0 restore instructions](recover-a-bricked-cart.md#restore-block-0-from-a-backup)

## If the write check stops

- **Safe area contains other data** means the tool could not verify a usable layout for the requested ROM. Don’t force it or intentionally brick the cart. Keep the report, exact message, and backup.
- A narrow page-`0xE` exception may be offered during replacement if the spare data and main-page pattern match otherwise erased padding with a few cleared bits. It is not a general “ignore read errors” option. The backup retains the original read. Use **B** if you’re unsure. Accepting the exception still requires the normal write confirmation.
- Replacement diagnostics go to `nrioFiles/ADVANCED.TXT`. Staged writes keep `region-NNN/blocks.bin` and `region-NNN/SESSION.TXT` with the original main and spare bytes. Keep those if verification fails. The region extractor can verify the snapshot and export its individual blocks. [Region extractor](https://github.com/nextcode4u/DLDI-REA-Decomp/blob/main/n-card/nrioTool/unpack_region.py)
