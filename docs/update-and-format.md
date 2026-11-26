# Update, format, and set up a cart

[Back to the nrioTool README](../README.md)

Start with [Check Cart](diagnose-and-back-up.md). Back up block 0 and copy
any files you want to keep to separate storage. Hidden uDisk or XuluMenu
firmware and the `xmenu.dat` menu file are different things. If the
firmware works and only the files are missing, skip the firmware install.

## Update or repair hidden firmware

Open **Utility Menu → Advanced NAND tools → uDisk setup**.

| Option | When to use it |
| --- | --- |
| **Install stock uDisk 1.45** | Upgrade a supported older version or return to stock on a supported layout. The installer can report **No upgrade needed** if it already recognizes 1.45. It does not format the cart. |
| **XuluMenu NAND (recommended)** | Use XuluMenu as the hidden uDisk replacement on a supported layout. Put the normal `xulumenu.bin` in `nrioFiles` on independent storage. Don’t rename it to `xmenu.dat`. [XuluMenu 1.4.1 payload](https://github.com/ApacheThunder/XuluMenu/blob/7ceabcbc9331f84a8c4fcde38b6f60193405ee8a/XulumenuUpdator/NitroFS/xulumenu.bin) |
| **Install custom uDisk** | Advanced option for a compatible compact, unpacked NRIO ROM up to 1 MiB. Put it at `nrioFiles/udisk-replacement.nds`. Read the confirmation to see whether it will use the secondary table or change the primary boot route on a verified direct-boot cart. |

Stock install needs a recognized safe-mode route. If it says the cart
boots firmware directly, [restore the compatible safe-mode route](safe-mode.md)
first. On DSi, the stock updater also requires the recognized DSi
safe-mode patch before it upgrades. An archived version appearing in
Check Cart does not guarantee it can be upgraded. For example, DS Fire
Link 1.23o recognition is read-only.

**Advanced NAND tools → Repair stock uDisk** is separate from the install
menu. Use it for supported layouts with damaged stock firmware, not as
a first step if only menu files are missing. It restores the bundled
1.45 payload and its secondary map, and leaves block 0 alone. This repair
still needs physical-cart verification.

Review the backup and target shown on screen before holding **L + R**
and pressing **A** for a NAND write. Wait for verification, run Check
Cart again, and test a normal boot. Don’t format just because you updated
if the cart boots and the filesystem works.

## Fix missing menu files

If FAT mounts, choose **uDisk setup → Set up stock XMenu**. nrioTool
contains the stock `xmenu.dat` and `DSYSTEM` starter files. It streams
them to the cart without needing a separate download or NitroFS access.
It leaves a custom menu alone except for the recognized old file-based
XuluMenu wrapper, which it backs up as `xulu-old.nds` before replacement.
Use **Copy stock/custom XMenu** when supplying your own menu files.

**Delete old uDisk installer** only removes a recognized leftover
`autorun1.nds` from the cart’s filesystem. It does not uninstall uDisk
or change the boot tables.

## Restore a backed-up FAT sector 0

If you know the logical FAT boot sector or MBR was damaged and have its
exact 512-byte backup from the same cart, use **Utility Menu → DLDI Menu →
Restore FAT sector 0** in DSi mode. Put the backup at
`sd:/nrioFiles/nrio_fatsector0.bin`. The tool can restore it without
mounting the filesystem, saves the current sector before confirmation,
and verifies the write. This restores only that sector, not a whole FAT
image or physical NAND block 0.

## Rebuild the filesystem

Both format options **erase the cart’s files** and preserve the boot
firmware. They do not fix an erased block 0. Copy readable files off the
cart first if you want to keep them.

1. Launch with mounted internal SD in DSi mode. An installed DSi System Menu title works with mounted SD even without a launcher path. On DS or DS Lite, use separate writable Slot-2 DLDI storage. Formatting from a DS-mode Slot-1 launch is blocked.
2. Open **uDisk setup**. Choose **Quick Format N-Card** to rebuild FAT and skip known bad blocks or blocks it cannot erase. Choose **Full Format N-Card** if you also want to write-test usable data blocks and record new failures. Full Format takes longer. For diagnosis without erasing files, run the [read-only NAND health scan](diagnose-and-back-up.md#check-nand-health) instead.
3. Read the prompt. Hold **L + R** and press **A** only if you’re ready to erase the files, and leave the console powered until it finishes. If it refuses the layout, keep the Check Cart report. Do not treat format refusal as proof that the cart cannot mount.
4. Run **Set up stock XMenu** after formatting, then boot the cart normally. If setup or boot fails, save a fresh Check Cart report and `nrioFiles/UDISK-FORMAT.TXT` if present.

Quick Format has been tested on physical carts. Full Format has host
regression coverage and still needs hardware verification.
