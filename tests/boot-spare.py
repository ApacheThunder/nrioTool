#!/usr/bin/env python3
"""Host tests. Optionally pass private capture ZIPs or packed spare.bin files."""

import ctypes as c
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from zipfile import ZipFile, is_zipfile

UNREADABLE, UNSTABLE, ERASED, OTHER, ARM7_19, ARM7_11, TABLE = range(7)
NOT_COMPARED, AGREE, DIFFER = range(3)
RECORD = 64
ARM7_PAGES = (1, 4, 5)

class Report(c.Structure):
    _fields_ = [("page", c.c_int * 6), ("arm7_copies", c.c_int), ("table_overlap", c.c_int)]

READER = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.c_uint, c.POINTER(c.c_uint8))


def check(lib, data, *, fail=-1, unstable=-1):
    reads = [0] * 6

    @READER
    def read(_, page, output):
        reads[page] += 1
        if page == fail:
            return False
        copy = bytearray(data[page * RECORD:(page + 1) * RECORD])
        if page == unstable and reads[page] == 2:
            copy[0] ^= 1
        c.memmove(output, bytes(copy), RECORD)
        return True

    report = Report()
    lib.nrio_check_boot_spare(read, None, c.byref(report))
    assert reads == [1 if p == fail else 2 for p in range(6)]
    return list(report.page), report.arm7_copies, report.table_overlap


def mutations(lib, intact, expected):
    for page in range(6):
        for offset in (0, 8, 56, 63):
            image = bytearray(intact)
            image[page * RECORD + offset] ^= 1
            states, arm7, table = check(lib, image)
            changed = expected.copy()
            changed[page] = OTHER
            assert states == changed, (page, offset, states, changed)
            assert arm7 == (DIFFER if page in ARM7_PAGES else AGREE)
            # Page 2's trailing 8 bytes and page 3's leading 8 are unique.
            in_overlap = (page == 2 and offset < 56) or (page == 3 and offset >= 8)
            assert table == (DIFFER if in_overlap else AGREE)
        for state, error in ((UNREADABLE, {"fail": page}), (UNSTABLE, {"unstable": page})):
            states, arm7, table = check(lib, intact, **error)
            changed = expected.copy()
            changed[page] = state
            assert states == changed
            assert arm7 == (NOT_COMPARED if page in ARM7_PAGES else AGREE)
            assert table == (NOT_COMPARED if page in (2, 3) else AGREE)
        if page:
            image = bytearray(intact)
            image[page * RECORD:(page + 1) * RECORD] = b"\xff" * RECORD
            states, arm7, table = check(lib, image)
            changed = expected.copy()
            changed[page] = ERASED
            assert states == changed
            assert arm7 == (DIFFER if page in ARM7_PAGES else AGREE)
            assert table == (DIFFER if page in (2, 3) else AGREE)


def capture_files(paths):
    for path in map(Path, paths):
        if is_zipfile(path):
            with ZipFile(path) as archive:
                for name in sorted(archive.namelist()):
                    if Path(name).name == "spare.bin":
                        yield archive.read(name)
        else:
            yield path.read_bytes()


with tempfile.TemporaryDirectory() as directory:
    src = Path(__file__).resolve().parents[1] / "arm9" / "source" / "boot-spare.c"
    library = Path(directory) / "boot-spare.so"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-shared", "-fPIC", str(src), "-lz", "-o", str(library)], check=True)
    lib = c.CDLL(str(library))
    lib.nrio_check_boot_spare.argtypes = [READER, c.c_void_p, c.POINTER(Report)]
    lib.nrio_check_boot_spare.restype = None

    assert check(lib, b"\xff" * 384) == ([ERASED] * 6, NOT_COMPARED, NOT_COMPARED)
    assert check(lib, b"\0" * 384) == ([OTHER] * 6, AGREE, AGREE)
    # Unknown data may have the expected shape, but is not called stock.
    synthetic = bytes(range(64)) + bytes(range(1, 65)) * 5
    assert check(lib, synthetic) == ([OTHER] * 6, AGREE, DIFFER)
    synthetic_match = b"\xff" * RECORD + b"\x20" * (RECORD * 5)
    mutations(lib, synthetic_match, [ERASED, OTHER, OTHER, OTHER, OTHER, OTHER])
    variants = {}
    count = 0
    for capture in capture_files(sys.argv[1:]):
        assert len(capture) in (4096, 8192)
        arm7 = {0x11: ARM7_11, 0x19: ARM7_19}[capture[0x77]]
        page0 = ERASED if capture[:RECORD] == b"\xff" * RECORD else OTHER
        expected = [page0, arm7, TABLE, TABLE, arm7, arm7]
        assert check(lib, capture) == (expected, AGREE, AGREE)
        mutations(lib, capture, expected)
        variants[arm7] = capture
        count += 1
    if len(variants) == 2:
        mixed = bytearray(variants[ARM7_19])
        mixed[5 * RECORD:6 * RECORD] = variants[ARM7_11][5 * RECORD:6 * RECORD]
        states, arm7, table = check(lib, mixed)
        assert states[1] == states[4] == ARM7_19 and states[5] == ARM7_11
        assert (arm7, table) == (DIFFER, AGREE)
    print(f"Boot spare tests passed ({count} real captures plus synthetic controls)")
