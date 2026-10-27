#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The UI playground's content, from the demo disk's sources (demo-disk/, notation version 2), as the
playground's model reads it.

  python3 tools/gen_playground.py [DIR]      -> playground/content.js     (needs ffmpeg, for the FLAC files)

DIR: a copy of the demo disk's sources to read instead of demo-disk/.

Run it again when demo-disk/ changes; the playground itself has no build. Everything the demo disk
holds goes in, with the disk's numbers: the cartridges (patches with their LEVEL, sampler cartridges
with their samples' audio and their instruments, FUSION), the pattern pool, the songs (timing,
positions, mixer, FX setup). Nothing is made up here; the playground starts with no loops. The header
names every licence the disk's files carry.

What changes on the way:
- names in capitals, from the characters PT/FM's names allow (instruments and samples 10, cartridges
  12, songs 16);
- a sample's audio as signed 8-bit, base64, rounded from the 16-bit master as the disk keeps it; an
  instrument's sample as its index in its cartridge;
- notes as MIDI numbers, instruments as cartridge << 8 | slot;
- a cell as a list: [row, track, note, instrument, volume, code, value, code, value, ...], with note
  0 for none and 1 for a note off, instrument and volume -1 for none, then up to four commands as
  their codes and values (specs/files/commands.md);
- the songs' FX setups as the disk writes them (the playground's model reads them)."""
import base64
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ptfm_records as R  # noqa: E402  (a CHIP slot's settings)

DEMO = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "demo-disk"
OUT = ROOT / "playground" / "content.js"
NOTES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
NAME_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 :.-?|/\\*#$@%")
TYPES = {"dx7": ("DX7", 32), "juno106": ("JUNO", 128), "tb303": ("TB303", 128), "sampler": ("SAMPLER", 64),
         "fusion": ("FUSION", 64), "chip": ("CHIP", 128)}
VOL_COLUMN = {"-": 0x24, "+": 0x25, "D": 0x26, "U": 0x27, "S": 0x28, "V": 0x29, "P": 0x2A, "<": 0x2B, ">": 0x2C,
              "M": 0x2D}
OWN = {"ACC": 0xC0, "SLD": 0xC1, "A+S": 0xC2, "LEG": 0xD3}
GROUPS = {"*": (0x40, 0x7F), "=": (0x80, 0x84), "#": (0x90, 0xAF), "!": (0xC3, 0xD2)}
MIX_DEFAULT = {"level": 127, "pan": 128, "duck": 0, "chorus": 0, "delay": 0, "reverb": 0}


def read_json(path):
    return json.loads(Path(path).read_text(encoding="ascii"))


def optimized(values, opt):
    """a sample's `optimize` (demo-disk/README.md): a softer top (a 2-pole low-pass, RBJ, Q 0.707, at `soften` of
    the rate) and a lower level (`gain`, dB), which PTR codes in fewer bits; its instruments' `level` makes the
    gain up. The editor's OPTIMIZE does the same (editor/js/conv_sound.js). 16-bit values in and out."""
    x = [v / 32768 for v in values]
    if opt.get("soften"):
        w0 = 2 * math.pi * opt["soften"]
        c, al = math.cos(w0), math.sin(w0) / (2 * math.sqrt(0.5))
        b0, b1, a1, a2 = (1 - c) / 2 / (1 + al), (1 - c) / (1 + al), -2 * c / (1 + al), (1 - al) / (1 + al)
        x1 = x2 = y1 = y2 = 0.0
        out = []
        for v in x:
            y = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2
            x2, x1, y2, y1 = x1, v, y1, y
            out.append(y)
        x = out
    if opt.get("gain"):
        k = 10 ** (opt["gain"] / 20)
        x = [v * k for v in x]
    return [max(-32768, min(32767, round(v * 32768))) for v in x]


def made_up(src, inst):
    """a sample instrument's level (dB) with what its sample's `optimize` lowered made up: samples.json keeps the
    level measured on the master"""
    s = next(x for x in src["samples"] if x["id"] == inst["sample"])
    return inst.get("level", 0) - (s.get("optimize") or {}).get("gain", 0)


def flac_read(path, opt=None):
    """a FLAC sample (the 16-bit master) -> signed 8-bit bytes, each value rounded to the nearest 256th as
    the disk keeps it (demo-disk/README.md): (v + 128) >> 8, at most 127; opt: its `optimize` first"""
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-f", "s16le", "-ac", "1", "pipe:1"],
                         capture_output=True, check=True).stdout
    values = struct.unpack(f"<{len(raw) // 2}h", raw)
    if opt:
        values = optimized(values, opt)
    return bytes(min(127, (v + 128) >> 8) & 0xFF for v in values)


def licences(node, found):
    """every licence the disk's files name (`license`: {"id": an SPDX identifier, ...}), anywhere in them"""
    if isinstance(node, dict):
        lic = node.get("license")
        if isinstance(lic, dict) and "id" in lic:
            found.add(lic["id"])
        for v in node.values():
            licences(v, found)
    elif isinstance(node, list):
        for v in node:
            licences(v, found)
    return found


def clean(s, width):
    s = "".join(c if c in NAME_CHARS else " " for c in s.strip().upper()).strip()
    if len(s) > width:
        raise SystemExit(f"gen_playground: name past {width} characters: {s!r}")
    return s


def midi(text):
    """C-4 -> 60"""
    return NOTES.index(text[0:2]) + 12 * (int(text[2]) + 1)


def ref(text):
    """0B:05 -> 0x0B05; ..:.. -> -1"""
    return -1 if text == "..:.." else int(text[0:2], 16) << 8 | int(text[3:5], 16)


def command(tok):
    """a command as the disk writes it -> (code, value)"""
    if tok in OWN:
        return OWN[tok], 0
    if tok[0] in GROUPS:
        code, value = int(tok[1:3], 16), int(tok[4:6], 16)
        lo, hi = GROUPS[tok[0]]
        if len(tok) != 6 or tok[3] != ":" or not lo <= code <= hi:
            raise SystemExit(f"gen_playground: not a command: {tok!r}")
        return code, value
    if tok[0] == "~":
        if len(tok) != 3 or tok[1] not in VOL_COLUMN:
            raise SystemExit(f"gen_playground: not a command: {tok!r}")
        return VOL_COLUMN[tok[1]], int(tok[2], 16)
    if len(tok) != 3:
        raise SystemExit(f"gen_playground: not a command: {tok!r}")
    return int(tok[0], 36), int(tok[1:], 16)


def cell(r, t, text):
    """`NNN CC:SS VV` and up to four commands -> the playground's list, or None for an empty cell"""
    parts = text.split()
    note, ins, vol, cmds = parts[0], parts[1], parts[2], parts[3:]
    if len(cmds) > 4:
        raise SystemExit(f"gen_playground: more than four commands: {text!r}")
    n = 0 if note == "---" else 1 if note == "===" else midi(note)
    out = [r, t, n, ref(ins), -1 if vol == ".." else int(vol, 16)]
    for tok in cmds:
        out += command(tok)
    return None if out[2:] == [0, -1, -1] else out


def pattern_cells(rows):
    cells = []
    for r, text in enumerate(rows):
        for t, c in enumerate(text.split(" | ")):
            x = cell(r, t, c)
            if x:
                cells.append(x)
    return cells


def mixer(rows, n):
    rows = [dict(MIX_DEFAULT, **r) for r in rows]
    return rows + [dict(MIX_DEFAULT) for _ in range(n - len(rows))]


def main():
    index = read_json(DEMO / "index.json")
    carts, patterns, found = [], [], set()

    def add_patterns(path):
        for p in read_json(path)["patterns"]:
            patterns.append({"n": p["number"], "tracks": p["tracks"], "rows": len(p["rows"]), "step": p["edit_step"],
                             "what": p.get("what", ""), "cells": pattern_cells(p["rows"])})

    for c in index["cartridges"]:
        path = DEMO / c["path"]
        kind, size = TYPES[c["type"]]
        cart = {"id": c["number"], "name": clean(c["name"], 12), "type": kind, "size": size, "slots": []}
        src = read_json(path)
        licences(src, found)
        if c["type"] == "dx7":
            for v in src["voices"]:
                v = dict(v)
                cart["slots"].append({"name": clean(v.pop("name"), 10), "level": v.pop("level", 0), "dx7": v})
        elif c["type"] in ("juno106", "tb303"):
            key = "juno" if c["type"] == "juno106" else "tb303"
            for p in src["patches"]:
                p = dict(p)
                p.pop("label")
                cart["slots"].append({"name": clean(p.pop("name"), 10), "level": p.pop("level", 0), key: p})
        elif c["type"] == "chip":                       # (the engine's own settings: tools/ptfm_records.py)
            for i, lv in zip(src["instruments"], src["levels"]):
                cart["slots"].append({"name": clean(i["name"], 10), "level": lv,
                                      "chip": R.chip_numeric(R.chip_values(i))})
        elif c["type"] == "sampler":
            ids = [s["id"] for s in src["samples"]]
            cart["samples"] = [{"name": clean(s["name"], 10), "kind": s.get("kind"), "rate": s["rate"],
                                "audio": base64.b64encode(flac_read(path.parent / s["file"], s.get("optimize"))).decode("ascii")}
                               for s in src["samples"]]
            for i in src["instruments"]:
                cart["slots"].append({"name": clean(i["name"], 10), "level": made_up(src, i), "sampler": {
                    "sample": ids.index(i["sample"]), "start": i["start"], "end": i["end"],
                    "loop": i["loop"].upper(), "loopStart": i["loop_start"], "loopEnd": i["loop_end"],
                    "base": midi(i["base_note"]), "tune": i["tune"], "volume": i["volume"],
                    "env": {"rates": i["envelope"]["rates"], "levels": i["envelope"]["levels"]}}})
        else:
            for s in src["slots"]:
                cart["slots"].append({"name": clean(s["name"], 10), "level": s.get("level", 0),
                                      "fusion": {"pattern": s["pattern"]}})
            add_patterns(path.parent / src["patterns"])
        if len(cart["slots"]) > size:
            raise SystemExit(f"gen_playground: {cart['name']}: more than {size} slots")
        carts.append(cart)

    songs = []
    for s in index["songs"]:
        path = DEMO / s["path"]
        song = read_json(path)
        licences(song, found)
        add_patterns(path.parent / song["patterns"])
        songs.append({"n": s["number"], "name": clean(song["name"], 16), "what": song.get("what", ""),
                      "tempo": song["tempo"], "ticks": song["ticks_a_row"], "rpb": song["rows_a_beat"],
                      "swing": song["swing"], "atEnd": song["at_the_end"].upper(), "loopFrom": song["loop_from"],
                      "slides": song["slides"], "fastVol": song["fast_volume_slides"], "gvol": song["global_volume"],
                      "order": song["positions"], "mixer": mixer(song["mixer"], 16), "fx": song["fx"]})

    numbers = [p["n"] for p in patterns]
    if len(numbers) != len(set(numbers)):
        raise SystemExit("gen_playground: a pattern number used twice")
    patterns.sort(key=lambda p: p["n"])

    if not found:
        raise SystemExit("gen_playground: the disk names no licence")
    head = ["/* SPDX-License-Identifier: " + " AND ".join(sorted(found)),
            " * PT/FM UI playground: its content, the demo disk (demo-disk/). Generated by tools/gen_playground.py:",
            " * do not edit. Every work's licence, author and source, and the licences' texts: demo-disk/CREDITS.md. */",
            "window.PTFM = window.PTFM || {};"]
    body = json.dumps({"cartridges": carts, "patterns": patterns, "songs": songs}, separators=(",", ":"))
    OUT.write_text("\n".join(head) + "\nPTFM.CONTENT = " + body + ";\n", encoding="ascii")
    print(f"{OUT.relative_to(ROOT)}: {len(carts)} cartridges, {len(patterns)} patterns, {len(songs)} songs, "
          f"{OUT.stat().st_size:,} bytes")


if __name__ == "__main__":
    main()
