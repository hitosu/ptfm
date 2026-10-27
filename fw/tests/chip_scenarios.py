# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The CHIP engine's test scenarios (fw/tests/run_chip.sh): every instrument of the demo cartridge
(demo-disk/patches/chip/chiptune.json) packed as its slot's bytes (tools/ptfm_records.py), at a low, a middle and
two high notes, held 1 s and let go; one of them bent and one with a parameter command, the commands taken in turn;
the engine's reference sound (specs/files/instruments.md); then random ones (a fixed seed): random settings, any
note, parked a while (skipped, unheard), up to four more commands of any code and value. One scenario a line, for
fw/tests/chip_test.c and fw/tests/chip_twin.js alike:

    SLOT(56 hex) NOTE BLOCKS OFF BEND_AT BEND64 PARAM_AT CODE VALUE PARK_FROM PARK_TO N [AT CODE VALUE]x N
                                                                                    (-1: none)
    python3 -I fw/tests/chip_scenarios.py [--quick]
"""
import json
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.dont_write_bytecode = True
import ptfm_records as R  # noqa: E402

SR, N = 44100, 64
BLOCKS = 1.4 * SR // N                   # 1.4 s
OFF = SR // N                            # let go after 1 s
# a parameter command for each scenario in turn: (code, value)
PARAMS = [(0x40, 2), (0x41, 1), (0x4C, 6), (0x4D, 2), (0x4E, 1), (0x50, 2), (0x54, 7), (0x55, 3), (0x58, 14),
          (0x60, 6), (0x61, 6), (0x62, 8), (0x63, 2), (0x64, 6), (0x68, 3), (0x6F, 30), (0x5B, 5), (0x48, 1),
          (0x4A, 0), (0x57, 5), (0x43, 1), (0x44, 1), (0x49, 1), (0x4B, 1), (0x4F, 1), (0x51, 4), (0x52, 3),
          (0x56, 2), (0x59, 3), (0x5A, 77), (0x6D, 10), (0x6E, 3), (0x40, 4), (0x40, 3), (0x4C, 0), (0x4C, 1)]
REFERENCE = {"name": "REFERENCE", "osc": "PULSE", "duty": "50%", "clock": "EXACT", "curve": "BRK", "filter": "NONE",
             "frames": 60, "volume": {"vol": 15, "env": "NONE", "step": 0, "release": 0}, "level": 0}


def slot_hex(ins):
    v = R.chip_values(ins)
    v["name"] = ins["name"]
    v["level"] = R.level_byte(ins.get("level", 0))
    return R.pack_slot(R.KIND_CHIP, v).hex()


def main():
    quick = "--quick" in sys.argv
    with open(os.path.join(ROOT, "demo-disk", "patches", "chip", "chiptune.json")) as f:
        js = json.load(f)
    cart = [dict(ins, level=lv) for ins, lv in zip(js["instruments"], js["levels"])]
    if "--levels" in sys.argv:           # each instrument as AUTO LEVEL measures it: C-4, 826 blocks, let go at 689
        print("\n".join(f"{slot_hex(ins)} 60 826 689 -1 0 -1 0 0 -1 -1 0" for ins in cart))
        return
    if "--check-levels" in sys.argv:     # chip_test -l's LEVELs (stdin) against the cartridge's
        got = [float(line.split()[0]) for line in sys.stdin.read().split("\n") if line.strip()]
        bad = [f"{i['name']} {i['level']} (measured {g})" for i, g in zip(cart, got) if g != i["level"]]
        if bad or len(got) != len(cart):
            raise SystemExit("chip: LEVELs not as measured: " + ", ".join(bad[:8]))
        print(f"chip: the {len(cart)} LEVELs as measured")
        return
    lines = [f"{slot_hex(REFERENCE)} 60 {int(BLOCKS)} -1 -1 0 -1 0 0 -1 -1 0"]
    k = 0
    for ins in cart if not quick else cart[::8]:
        h = slot_hex(ins)
        for note in (36, 60, 84, 100):
            lines.append(f"{h} {note} {int(BLOCKS)} {OFF} -1 0 -1 0 0 -1 -1 0")
        code, value = PARAMS[k % len(PARAMS)]
        lines.append(f"{h} 60 {int(BLOCKS)} {OFF} 120 {150 - 300 * (k & 1)} 200 {code} {value} -1 -1 0")
        k += 1
    # random: the fields' 121 bits at random (the rest, name and LEVEL, empty), any note, a park, more commands
    rnd = random.Random(2610251)
    codes = sorted(set(c for c, _ in PARAMS) | set(range(0x40, 0x45)) | set(range(0x48, 0x53)) |
                   set(range(0x54, 0x5C)) | set(range(0x60, 0x65)) | set(range(0x68, 0x70)) | {0x45, 0x53, 0x70, 0x7F})
    for _ in range(40 if quick else 400):
        b = bytearray(rnd.getrandbits(8) for _ in range(16)) + bytearray(12)
        b[15] &= 1                       # (bits 120-127: bit 120 the vibrato's depth, the name's from 121)
        blocks = rnd.randrange(80, 500)
        off = rnd.randrange(1, blocks) if rnd.random() < .8 else -1
        park = sorted(rnd.sample(range(1, blocks), 2)) if rnd.random() < .5 else [-1, -1]
        cmds = [(rnd.randrange(0, blocks), rnd.choice(codes), rnd.choice((rnd.randrange(256), rnd.randrange(16))))
                for _ in range(rnd.randrange(0, 5))]
        bend = rnd.randrange(-2000, 2000)
        lines.append(f"{bytes(b).hex()} {rnd.randrange(0, 128)} {blocks} {off} {rnd.randrange(-1, blocks)} {bend} -1 0 0 "
                     f"{park[0]} {park[1]} {len(cmds)}" + "".join(f" {a} {c} {v}" for a, c, v in cmds))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
