# Recover a cart that won’t boot

[Back to the nrioTool README](../README.md)

Start nrioTool from the DSi SD card or writable Slot-2 storage. Hold **L** if the target prevents normal startup. Run [Check Cart](diagnose-and-back-up.md) and keep the latest report. Before choosing a repair, [export a recovery bundle](diagnose-and-back-up.md#export-a-recovery-bundle) to save the report and back up whatever block-0 data is still readable. The ROM dump is optional. Skip it if the cart can’t start a ROM session.

## Pick the repair that matches the damage

- **The cart white-screened just after changing its banner.** Try [Restore banner ARM7](#recover-after-a-banner-write) if the report or history points to the old banner writer.
- **Block 0 was damaged and you have a pre-fault backup from this cart.** Use [Restore block 0](#restore-block-0-from-a-backup).
- **Safe mode starts but its next boot step fails, or the same cart boots on DS Lite but not on DSi.** Check the [safe-mode and uDisk routes](safe-mode.md) before restoring block 0 or formatting.
- **The filesystem mounts but the menu is missing.** [Set up the menu files](update-and-format.md#fix-missing-menu-files). A boot-block restore won’t install them.

## Restore block 0 from a backup

1. Put the original `block0-NNN` folder, with `main.bin`, `spare.bin`, and `backup.info`, under `nrioFiles` on your separate storage. Backups from older builds without `backup.info` are not selectable here. Don’t substitute a dump from a different cart.
2. Choose **Utility Menu → Advanced NAND tools → Restore block 0**. Use **Left/Right** to pick the backup and **A** to inspect it. The app checks sizes, CRCs, NAND geometry, and the saved anchor sectors at `0x40000` and `0x80000`. Both anchors must match the live cart. A rejection is a reason to recheck the backup and report, not to force a write.
3. The app saves the current block in a separate folder. To proceed, hold **L + R** and press **A**. Use **B** to cancel before the write. Keep power on until the tool finishes.
4. Wait for **Block-0 restore verified**, then rerun Check Cart and test a normal boot. This restores block 0 only. Damage in hidden uDisk or in the filesystem needs its own repair.

If a program or verification step fails after a write begins, keep the console powered until it reports the result. Keep the snapshot and exact error. Don’t keep retrying writes to the same block. This complete restore path still needs physical DSi verification.

## Recover after a banner write

**Advanced NAND tools → Restore banner ARM7** checks for one specific failure from the old banner writer. If the intact ARM7 spare copies from pages 1 and 5 agree and page 4 matches the old ECC damage pattern, the tool can reconstruct page 4’s spare record without a donor cart or a prior backup. It saves the current block before asking for confirmation and preserves the visible banner. Apache Thunder confirmed that this repair restored boot on an ExpressCard.

For a new banner, use **Install custom banner** with `nrioFiles/banner.bin`. Supply a v1 banner of `0x800` bytes, or `0x840` bytes with a zero-filled final `0x40` bytes. The current installer updates both copies and preserves recognized ARM7 boot records. It regenerates ECC only where it recognizes the original spare record as ECC.

## If no pre-fault backup exists

Fully erased block 0 cannot be reconstructed by this tool without verified original boot data. The targeted banner repair is an exception only if the other copies survive. Keep the current block dump and report for diagnosis. Formatting does not recreate boot data, and matching the cart label or capacity is not enough to pick a donor image.

**Patch main ARM9 marker** is not a general unbrick option. It reproduces a specific duplicate-marker edit on a verified ExpressCard layout, saves block 0 first, and preserves all spare records. Check Cart can identify whether the live pattern is supported without staging a write.
