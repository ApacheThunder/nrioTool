"""Build the RAM-resident nrioTool assets without requiring NitroFS images."""
from hashlib import sha256
from pathlib import Path
from struct import pack, unpack_from
import sys
from zipfile import ZipFile
import zlib

STARTER_SHA = "1f4b2d7ac49a9c888b9e1984128e59bb1f30df5dfa3f61b1fefd1ccf8e2e0d55"
RUNTIME_SHA = "9c83cfa1bbfb9b928ccabc4403144480ba1428a80d069908f76fd3d56a49c686"
DLDI_SHA = "a3deda54cdbb3e65ad69d7f060a2a14f08d57550249408db9636eea0c3fc533e"


STARTER_FILES = (
    "xmenu.dat", "DSYSTEM/C_FILE.BMP", "DSYSTEM/DESKTOP.BMP",
    "DSYSTEM/GLOBAL.INI", "DSYSTEM/SAVETYPE.SDB",
)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: embed_bundle.py /path/to/nrioTool-checkout")
    root = Path(sys.argv[1])
    starter = root / "assets/starter.zip"
    driver = (root / "nrio-unified.dldi").read_bytes()
    if sha256(starter.read_bytes()).hexdigest() != STARTER_SHA:
        raise SystemExit("Unexpected compressed starter assets.")
    if len(driver) < 0x80 or sha256(driver).hexdigest() != DLDI_SHA:
        raise SystemExit("Unexpected unified NRIO DLDI build.")
    with ZipFile(starter) as archive:
        runtime = archive.read("udisk-1.45.bin")
        if len(runtime) != 0xc0000 or sha256(runtime).hexdigest() != RUNTIME_SHA:
            raise SystemExit("Unexpected uDisk payload.")
        chunks = [runtime[i:i + 0x20000] for i in range(0, len(runtime), 0x20000)]
        chunks += [archive.read(name) for name in STARTER_FILES]
    xulu = (root / "XuluMenu/xBootStrap.nds").read_bytes()
    if not 0x8000 <= len(xulu) <= 0x80000 or b"NAND FLASH CARD LIBFATNRIO" not in xulu:
        raise SystemExit("Expected the file-based XuluMenu build with the NRIO DLDI.")
    for field in (0x20, 0x30):
        offset, _, _, size = unpack_from("<4I", xulu, field)
        if offset < 0x200 or not size or offset + size > len(xulu):
            raise SystemExit("Invalid XuluMenu ARM executable section.")
    chunks += [driver, xulu]
    index, compressed = bytearray(), bytearray()
    header = b"NRIOSD01" + pack("<I", len(chunks))
    for source in chunks:
        packed = zlib.compress(source, 9)
        index += pack("<4I", len(header) + len(chunks) * 16 + len(compressed),
                      len(packed), len(source), zlib.crc32(source))
        compressed += packed
    output = root / "data/nrio_bundle.bin"
    output.parent.mkdir(exist_ok=True)
    data = header + index + compressed
    if not output.exists() or output.read_bytes() != data:
        output.write_bytes(data)
    # Previous builds placed an uncompressed copy here.
    (root / "data/udisk_145.bin").unlink(missing_ok=True)
    print(f"Packed {len(chunks)} resident assets into {len(data):,} bytes.")


if __name__ == "__main__":
    main()
