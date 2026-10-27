# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""Make the firmware's generated sources in build/fw/gen/: every build (the device's, the emulator's,
the host tests') runs this first. Each step is a tool of its own; a step whose tool is not there yet
is skipped with a note.

    python3 -I fw/tools/gen.py [DIR]      (from ptfm/; DIR defaults to build/fw/gen)

A build or a test that runs while others do may pass a directory of its own.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    gen = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "build", "fw", "gen")
    steps = [
        # (tool, its arguments): run from ptfm/ with -I
        ("tools/pack.py", ["--builtin", gen]),      # the built-in instruments (builtin.c)
        ("tools/gen_fonts.py", ["--fw", gen]),      # the fonts T, S, B, L (fonts.c, fonts.h)
        ("fw/tools/gen_tables.py", [gen]),          # the engines' and effects' tables
        ("fw/tools/gen_texts.py", [gen]),           # the fixed texts and the help, packed
    ]
    os.makedirs(gen, exist_ok=True)
    for tool, args in steps:
        path = os.path.join(ROOT, tool)
        if not os.path.exists(path):
            print("gen: skipped, not there yet:", tool)
            continue
        r = subprocess.run([sys.executable, "-I", path] + args, cwd=ROOT)
        if r.returncode:
            sys.exit("gen: %s failed" % tool)


if __name__ == "__main__":
    main()
