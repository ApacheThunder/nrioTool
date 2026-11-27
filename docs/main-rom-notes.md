# N-Card first-stage ROM notes

These observations come from Apache Thunder's paired logical ROM and raw
block-0 dumps from one DS Linker and two ExpressCards. All three logical
ROM CRCs and all six raw main/spare CRCs agree with their respective logs
or manifests.

| Logical ROM range | Observed source in these three captures |
| --- | --- |
| `0x0000–0x03FF` | Raw main NAND `0x0400–0x07FF` |
| `0x0400–0x043F` | All 64 bytes of spare page 0, with seven more copies at `0x80`-byte intervals through `0x0780` |
| `0x0C00–0x0FFF` | Raw main NAND `0x0000–0x03FF`, all 1,024 bytes |
| `0x8800–0x883F` | The 64-byte ARM7 payload, matching spare pages 1, 4, and 5 |

The first-page spare record is populated in all three captures. This
confirms that the tested cart's ROM header read exposes that record. It
does not establish what the record does or that the same mapping works
on every board or with a different dumping path. The purpose of the
raw-main prefix at `0x0C00` remains unknown.

## First pass over the boot code

The ROM header declares a 16 KiB ARM9 load at `0x4000`. The first
`0x800` bytes include the code and data, and the rest of that load is
zero-filled in these captures. The ARM7 load is exactly `0x40` bytes.

In the stock ExpressCard image, the ARM9 code at `0x020005EC` sets up
the cartridge ASIC, including the three `C1` calls. It uses a block
reader at `0x02000488` to fetch the next header, then reads the ARM9
and ARM7 ranges listed in that header. That reader sends `B7` in
512-byte transfers. The ARM9 handoff routine writes `0x1918027E` to
shared memory, and the ARM7 stub waits for that word before jumping
through the next entry point.

This identifies the boot handoff, not a software implementation of the
ASIC's ROM map. It does not yet identify where a direct-boot game cart
gets its save configuration. A custom launcher also needs a way to
translate the selected file's filesystem allocation into the addresses
the ASIC expects, which this first stage does not demonstrate.

## Secure-area comparison

The modified DS Linker dump differs from the stock ExpressCard secure
area only at logical `0x4008–0x400F`. The two ExpressCard secure areas
match each other exactly. The stock pair's stored/decoded CRCs are
`450D/9F6A` and the DS Linker's are `3682/BF15`. These are values for
the header and the decoded dump, not evidence by themselves of bad NAND.
