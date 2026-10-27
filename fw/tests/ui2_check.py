# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""What ui2_test wrote, read by the packer's own code (tools/ptfm_records.py): the edited pattern and
cartridge unpack, pack again byte for byte, and differ from the disk's only where the test edited.

    python3 -I fw/tests/ui2_check.py DISK.ptfmdsk OUTDIR    (from ptfm/)
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import ptfm_records as pr  # noqa: E402


def main():
    disk, out = sys.argv[1], sys.argv[2]
    ed = os.path.join(out, "edited")
    files = pr.unpack_disk(open(disk, "rb").read())
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            fails += 1

    # the pattern
    lines = open(os.path.join(ed, "pattern.txt")).read().split("\n")
    pat = int(lines[0])
    edited = {tuple(int(x) for x in ln.split()) for ln in lines[1:] if ln.strip()}
    orig = pr.unpack_pattern([p for k, n, s, v, p in sorted(files, key=lambda f: f[2]) if k == pr.KIND_PATTERN and n == pat])
    parts, s = [], 0
    while os.path.exists(os.path.join(ed, "pattern_%d.bin" % s)):
        parts.append(open(os.path.join(ed, "pattern_%d.bin" % s), "rb").read())
        s += 1
    saved = pr.unpack_pattern(parts)
    again = pr.pack_pattern(saved["tracks"], saved["rows"], saved["edit_step"], saved["cells"])
    check(again == parts, "the pattern the firmware wrote packs again byte for byte (%d B in %d part)" % (sum(map(len, parts)), len(parts)))
    diff = {k for k in set(orig["cells"]) | set(saved["cells"]) if orig["cells"].get(k) != saved["cells"].get(k)}
    check(diff <= edited and diff, "it differs from the disk's only at the cells edited: %s" % sorted(diff))
    check((saved["tracks"], saved["rows"], saved["edit_step"]) == (orig["tracks"], orig["rows"], orig["edit_step"]), "its size as it was")

    # the cartridge
    cart, level, kind = (int(x) for x in open(os.path.join(ed, "cartridge.txt")).read().split())
    b = open(os.path.join(ed, "cartridge_0.bin"), "rb").read()
    c = pr.unpack_cartridge(kind, b)
    check(pr.pack_cartridge(kind, c["name"], c["slots"]) == b, "the cartridge the firmware wrote packs again byte for byte")
    check(c["slots"][0]["ops"][5]["output_level"] == level, "its slot 00's OP1 LEVEL as edited (%02X)" % level)
    print("ui2_check: %s" % ("%d FAILED" % fails if fails else "all passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
