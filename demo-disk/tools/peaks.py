#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""What sounds when a song is at its most voices (voices.py): for thinning an arrangement out.

  peaks.py [song folder ...]        (default: every song of catalog.py's SONGS)
"""
import sys

import voices
from catalog import FUSION_PATH, SONGS
from common import DISK, read_json


def fusion():
    """the FUSION cartridge's slots: slot -> its pattern"""
    fp = {p["number"]: p for p in read_json((DISK / FUSION_PATH).parent / "patterns.json")["patterns"]}
    return {i: fp[s["pattern"]] for i, s in enumerate(read_json(DISK / FUSION_PATH)["slots"])}


def main(names):
    fus = fusion()
    names = names or [folder for folder, _ in SONGS]
    for name in names:
        song = read_json(DISK / "songs" / name / "song.json")
        pats = {p["number"]: p for p in read_json(DISK / "songs" / name / "patterns.json")["patterns"]}
        peak = []
        most, at, where = voices.count(song, pats, fus, peak)
        print(f"{name}: {most} voices at {at:.1f} s, position {where[0]}, row {where[1]:02X}")
        print("    " + "; ".join(peak))


if __name__ == "__main__":
    main(sys.argv[1:])
