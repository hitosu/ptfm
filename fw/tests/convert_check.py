# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The samplers' conversion (specs/files/disk.md#the-samplers-conversion) against the demo disk: a disk in the old
form (samplers of version 1 holding their samples' table, the audio of kind 0x10 under them) converted by
tools/ptfm_records.py's convert_samplers holds the demo disk's records, record for record: the same files, each of the
same version and payload, byte for byte. So an FM-1 converted from the old demo disk holds what the new one uploads.

    python3 -I fw/tests/convert_check.py [--if-old] OLD.ptfmdsk NEW.ptfmdsk    (from ptfm/)

--if-old: OLD not in the old form (no sampler of version 1, no old audio) is passed over, not a failure.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import ptfm_records as R  # noqa: E402


def records(path):
    return [{"kind": k, "num": n, "sub": s, "ver": v, "payload": bytes(p)}
            for k, n, s, v, p in R.unpack_disk(open(path, "rb").read())]


def main():
    args = sys.argv[1:]
    if_old = "--if-old" in args
    args = [a for a in args if a != "--if-old"]
    if len(args) != 2:
        raise SystemExit(__doc__)
    old, new = records(args[0]), records(args[1])
    olds = [r for r in old if (r["kind"] == R.KIND_SAMPLER and r["ver"] == 1) or r["kind"] == R.KIND_AUDIO]
    if not olds:
        print(f"convert_check: {args[0]}: not in the old form" + (", passed over" if if_old else ""))
        return 0 if if_old else 1
    rep = R.convert_samplers_report(old)
    got = {(r["kind"], r["num"], r["sub"]): r for r in rep["records"]}
    want = {(r["kind"], r["num"], r["sub"]): r for r in new}
    fails = 0

    def fail(what):
        nonlocal fails
        fails += 1
        if fails <= 20:
            print("FAIL " + what)

    for k in sorted(set(got) | set(want)):
        tag = "%02X %02X %d" % k
        if k not in got:
            fail(f"{tag}: on the new disk, not made by the conversion")
        elif k not in want:
            fail(f"{tag}: made by the conversion, not on the new disk")
        elif got[k]["ver"] != want[k]["ver"]:
            fail(f"{tag}: version {got[k]['ver']}, the new disk's {want[k]['ver']}")
        elif got[k]["payload"] != want[k]["payload"]:
            fail(f"{tag}: its payload differs ({len(got[k]['payload'])} B, the new disk's {len(want[k]['payload'])} B)")
    if rep["older"] or rep["bad"] or rep["dropped"]:
        fail(f"the conversion left samplers old {rep['older']}, unreadable {rep['bad']}, dropped {rep['dropped']} audio")
    print(f"convert_check: {args[0]} converted ({len(rep['converted'])} samplers, {rep['samples']} samples) "
          f"against {args[1]}: {len(want)} records, " + (f"{fails} FAILED" if fails else "every one the same"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
