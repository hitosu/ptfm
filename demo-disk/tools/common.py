# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""Shared by the demo disk's tools: the folders, the licences, the JSON layout, FLAC through ffmpeg, the
cell notation (../README.md). Nothing here runs anything from the downloads."""
import hashlib
import json
import subprocess
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
DISK = TOOLS.parent                      # demo-disk/
WIDTH = 120                              # JSON: a value fits on one line up to this width

NOTE_NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]

# the licences the demo disk may hold: they let anyone pass the work on, changed or not, commercially
# or not (SPDX identifiers)
LICENSES = {"CC0-1.0", "CC-PDM-1.0", "CC-BY-3.0", "CC-BY-4.0", "CC-BY-SA-3.0", "CC-BY-SA-4.0", "MIT",
            "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "GPL-2.0-only", "GPL-2.0-or-later", "GPL-3.0-only",
            "GPL-3.0-or-later"}
# our own work: the patches, the synthesized sounds, the songs
OWN_LICENSE = {"id": "CC0-1.0", "credit": "Vasily Kazantsev (PT/FM)",
               "url": "https://creativecommons.org/publicdomain/zero/1.0/"}


def fmt(kind):
    """a JSON file's "format": demo-disk/<kind>"""
    return f"demo-disk/{kind}"


def sha_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# ------------------------------------------------------------------ JSON ---
def dumps(obj, indent=0, lead=0):
    """JSON that puts a value on one line when the line stays within WIDTH, else one item a line.
    indent: the value's nesting; lead: the characters before it on its line (indent, key)"""
    flat = json.dumps(obj)
    if not isinstance(obj, (dict, list)) or not obj or lead + len(flat) < WIDTH:
        return flat
    pad = " " * (indent + 2)
    if isinstance(obj, list) and all(isinstance(v, int) or isinstance(v, str) and len(v) <= 3 for v in obj):
        lines, cur = [], []              # numbers (and short marks): as many a line as fit
        for v in map(json.dumps, obj):
            if cur and len(pad) + len(", ".join(cur + [v])) + 1 >= WIDTH:
                lines.append(", ".join(cur))
                cur = []
            cur.append(v)
        lines.append(", ".join(cur))
        return "[\n" + ",\n".join(pad + ln for ln in lines) + "\n" + " " * indent + "]"
    if isinstance(obj, dict):
        items = []
        for k, v in obj.items():
            key = json.dumps(k) + ": "
            items.append(pad + key + dumps(v, indent + 2, indent + 2 + len(key)))
        return "{\n" + ",\n".join(items) + "\n" + " " * indent + "}"
    return "[\n" + ",\n".join(pad + dumps(v, indent + 2, indent + 2) for v in obj) + "\n" + " " * indent + "]"


def write_json(path, obj):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(dumps(obj) + "\n", encoding="ascii")


def read_json(path):
    return json.loads(Path(path).read_text(encoding="ascii"))


# ------------------------------------------------------------------ FLAC ---
def flac_write(path, values, rate):
    """signed 16-bit mono samples (a list of ints) -> FLAC, losslessly. Bit-exact flags: the same samples give
    the same file."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    raw = b"".join(int(v).to_bytes(2, "little", signed=True) for v in values)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "s16le", "-ar", str(rate), "-ac", "1", "-i", "pipe:0",
                    "-c:a", "flac", "-compression_level", "12", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                    str(path)], input=raw, check=True)


def flac_read(path):
    """a FLAC sample -> its signed 16-bit values"""
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-f", "s16le", "-ac", "1", "pipe:1"],
                         capture_output=True, check=True).stdout
    return [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]


# ------------------------------------------------------------- notation ---
def note_name(midi):
    """MIDI -> a cell's note: C-4 is MIDI 60"""
    return f"{NOTE_NAMES[midi % 12]}{midi // 12 - 1}"
