#!/usr/bin/env python3
"""Stage NitroFS with a compressed GodMode9Nrio launch payload."""

import argparse
from pathlib import Path
import shutil
from struct import pack
import zlib


def pack_gm9(raw):
    if not 512 <= len(raw) < 0x200000:
        raise ValueError("Unexpected GodMode9Nrio size.")
    return b"NRIOGM01" + pack("<2I", len(raw), zlib.crc32(raw)) + zlib.compress(raw, 9)


def stage(root):
    source, output = root / "nrioFiles", root / "build" / "nitrofs"
    if output.is_symlink() or output.parent.is_symlink():
        raise ValueError("NitroFS staging must be a local build directory.")
    payload = (source / "GM9Nrio.nds").read_bytes()
    packed = pack_gm9(payload)
    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(source, output, ignore=lambda folder, names:
                    {"GM9Nrio.nds"} if Path(folder) == source else set())
    (output / "GM9Nrio.z").write_bytes(packed)
    print(f"GodMode9Nrio: {len(payload):,} → {len(packed):,} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    stage(parser.parse_args().checkout.resolve())
