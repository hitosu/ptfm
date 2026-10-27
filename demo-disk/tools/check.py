#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""Checks the demo disk as the tools made it: every reference, limit, command, level and licence (a bank's,
and each voice's, patch's or sample's of its own, with its source), every sample's audio against its entry.
Prints the most voices each song sounds at once (voices.py: every release counted until it is 60 dB down);
more than the largest pool, 16, is a problem, and for a DJ loop more than 4, or a track of more than one
instrument (LOOPS loads a track as a loop of one sound) (catalog.DJ_LOOPS: a pattern
that loops, after an intro if it has one).

  check.py        -> a line for each problem; exit 1 if there is one
"""
import sys

import voices
from catalog import BUILT_IN, CARTRIDGES, CAT, DJ_LOOPS, DJ_VOICES, FUSION_CART, FUSION_FIRST, FUSION_PATH
from common import DISK, LICENSES, OWN_LICENSE, flac_read, read_json
from compose import CHAIN, FX_DEFAULTS, MIX_DEFAULT, cmd_key, midi

MAX_VOICES = 16                         # the largest pool of voices: past it, notes would be lost
# the parameter codes each engine has (specs/files/commands.md)
PARAMS = {"tb303": set(range(0x40, 0x46)), "juno106": set(range(0x40, 0x58)),
          "dx7": set(range(0x40, 0x70)) | set(range(0x70, 0x7B)),
          "sampler": set(range(0x40, 0x46)) | set(range(0x48, 0x50)), "fusion": set(),
          "chip": set(range(0x40, 0x45)) | set(range(0x48, 0x53)) | set(range(0x54, 0x5C)) | set(range(0x60, 0x65)) |
          set(range(0x68, 0x70))}
NAME_CHARS = set(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-?|/\\*#$@%")
problems = []


def bad(what):
    problems.append(what)


def name_ok(name, width, what):
    n = name.strip().upper()
    if len(n) > width or set(n) - NAME_CHARS:
        bad(f"{what}: the name {name!r} (at most {width} of PT/FM's characters)")


def license_ok(lic, what):
    if not lic or lic.get("id") not in LICENSES or not lic.get("credit") or not lic.get("url"):
        bad(f"{what}: licence {lic}")


def level_ok(item, what):
    lv = item.get("level", 0)
    if not isinstance(lv, (int, float)) or lv * 2 != int(lv * 2) or not -64 <= lv <= 63.5:
        bad(f"{what}: LEVEL {lv} (dB, 0.5 dB steps, -64..63.5)")


def item_ok(item, what):
    """a voice, patch or instrument: its LEVEL; its own licence, if it has one, with its source unless ours"""
    level_ok(item, what)
    if "license" in item:
        license_ok(item["license"], what)
        if item["license"] != OWN_LICENSE and not item.get("source", {}).get("file"):
            bad(f"{what}: a licence of its own and no source")


def slots_of(number):
    kind = CAT.kind((number, 0))
    d = CAT.data(number)
    return {"dx7": d.get("voices"), "juno106": d.get("patches"), "tb303": d.get("patches"),
            "sampler": d.get("instruments"), "fusion": d.get("slots"), "chip": d.get("instruments")}[kind]


def check_pattern(p, what, fusion=False):
    rows = voices.rows_of(p)
    if not 1 <= p["tracks"] <= 16 or not 1 <= len(rows) <= 256:
        bad(f"{what}: {p['tracks']} tracks, {len(rows)} rows")
    current = {}
    for r, row in enumerate(rows):
        if len(row) != p["tracks"]:
            bad(f"{what} row {r:02X}: {len(row)} cells, not {p['tracks']}")
        for t, (note, ins, vol, cmds) in enumerate(row, 1):
            where = f"{what} row {r:02X} track {t}"
            if ins is not None:
                if ins[0] not in [c[0] for c in CARTRIDGES] or ins[1] >= len(slots_of(ins[0])):
                    bad(f"{where}: no instrument {ins[0]:02X}:{ins[1]:02X}")
                    continue
                if fusion and ins[0] == FUSION_CART:
                    bad(f"{where}: a FUSION row naming a FUSION instrument (silent)")
                current[t] = ins
            if isinstance(note, int) and t not in current:
                bad(f"{where}: a note with no instrument")
            if len(cmds) > 4 or len({cmd_key(c, v) for c, v in cmds}) != len(cmds):
                bad(f"{where}: commands {cmds}")
            for code, value in cmds:
                if 0x40 <= code < 0x80:
                    kind = CAT.kind(current[t]) if t in current else None
                    if kind is None or code not in PARAMS[kind]:
                        bad(f"{where}: parameter {code:02X} on a {kind} instrument")
                if fusion and (code in (0x0B, 0x0D, 0x0F, 0x10, 0x11) or 0x90 <= code < 0xB0 or
                               code == 0x0E and value >> 4 == 0xE):
                    bad(f"{where}: a global command in a FUSION row (ignored)")


def check_fx(fx, what):
    if fx.get("routing") not in ("global", "sends") or sorted(fx.get("chain", [])) != sorted(CHAIN):
        bad(f"{what}: routing {fx.get('routing')}, chain {fx.get('chain')}")
    for name, knobs in FX_DEFAULTS.items():
        if set(fx.get(name, {})) != set(knobs):
            bad(f"{what}: the FX {name}'s knobs {sorted(fx.get(name, {}))}")


def check_mixer(mixer, n, what):
    if len(mixer) > n:
        bad(f"{what}: a mixer of {len(mixer)} tracks")
    for m in mixer:
        if set(m) != set(MIX_DEFAULT) or not 0 <= m["level"] <= 127 or not 0 <= m["pan"] <= 255:
            bad(f"{what}: mixer {m}")


def main():
    index = read_json(DISK / "index.json")
    if [(c["number"], c["type"], c["path"], c["name"]) for c in index["cartridges"]] != [tuple(c) for c in CARTRIDGES]:
        bad("index.json: its cartridges are not catalog.py's (run index.py)")
    pool = {}
    fp = {}
    for c in index["cartridges"]:
        d = read_json(DISK / c["path"])
        name_ok(d["name"], 12, c["path"])
        if (c["number"] >= BUILT_IN) != (c["type"] not in ("sampler", "fusion")):
            bad(f"{c['name']}: number {c['number']:02X}: the built-in instruments (the synth cartridges) "
                f"take {BUILT_IN:02X}-FF, the uploaded disk's cartridges (the samplers, FUSION) 00-{BUILT_IN - 1:02X}")
        if c["type"] != "sampler":
            license_ok(d.get("license"), c["path"])
        slots = slots_of(c["number"])
        if len(slots) > {"dx7": 32, "juno106": 128, "tb303": 128, "sampler": 64, "fusion": 64, "chip": 128}[c["type"]]:
            bad(f"{c['name']}: {len(slots)} slots")
        for i, s in enumerate(slots):
            name_ok(s["name"], 10, f"{c['name']} slot {i:02X}")
            item_ok(s, f"{c['name']} slot {i:02X} {s['name'].strip()}")
        if c["type"] == "sampler":
            if len(d["samples"]) > 64:
                bad(f"{c['name']}: {len(d['samples'])} samples")
            folder = (DISK / c["path"]).parent
            for s in d["samples"]:
                license_ok(s.get("license"), f"{c['name']} {s['id']}")
                audio = flac_read(folder / s["file"])
                if len(audio) != s["length"] or s.get("bits", 8) not in (8, 16) or not 1000 <= s["rate"] < 1 << 18:
                    bad(f"{c['name']} {s['id']}: {len(audio)} samples in {s['file']}, length {s['length']}, "
                        f"bits {s.get('bits')}, rate {s['rate']}")
                opt = s.get("optimize")
                if opt is not None and (s.get("bits", 8) != 8 or set(opt) - {"soften", "gain"}
                                        or not 0 < opt.get("soften", 0.25) < 0.5 or not -24 <= opt.get("gain", 0) <= 0):
                    bad(f"{c['name']} {s['id']}: optimize {opt} (8-bit samples; soften 0-0.5, gain -24-0)")
            ids = {s["id"]: s for s in d["samples"]}
            for i, it in enumerate(d["instruments"]):
                smp = ids.get(it["sample"])
                if smp is None or not 0 <= it["start"] < it["end"] <= smp["length"]:
                    bad(f"{c['name']} slot {i:02X}: sample {it['sample']}, {it['start']}-{it['end']}")
                if it["loop"] != "off" and not it["start"] <= it["loop_start"] < it["loop_end"] <= it["end"]:
                    bad(f"{c['name']} slot {i:02X}: loop {it['loop_start']}-{it['loop_end']}")
                if not -128 <= it["tune"] <= 127 or not 0 <= it["volume"] <= 64:
                    bad(f"{c['name']} slot {i:02X}: TUNE {it['tune']}, VOLUME {it['volume']}")
                midi(it["base_note"])
        if c["type"] == "fusion":
            ps = {p["number"]: p for p in read_json(DISK / c["path"].replace("fusion.json", "patterns.json"))["patterns"]}
            for s in d["slots"]:
                if s["pattern"] not in ps:
                    bad(f"FUSION {s['name']}: no pattern {s['pattern']:02X}")
                elif len(ps[s["pattern"]]["rows"]) != 128:
                    bad(f"FUSION {s['name']}: its pattern has {len(ps[s['pattern']]['rows'])} rows, not 128")
            for n, p in ps.items():
                if n in pool:
                    bad(f"pattern {n:02X}: twice ({pool[n]}, FUSION)")
                if not FUSION_FIRST <= n < BUILT_IN:
                    bad(f"FUSION pattern {n:02X}: FUSION's patterns are numbered {FUSION_FIRST:02X}-{BUILT_IN - 1:02X}")
                pool[n] = "FUSION"
                check_pattern(p, f"FUSION pattern {n:02X}", fusion=True)
            if c["path"] == FUSION_PATH:
                fp = {i: ps[s["pattern"]] for i, s in enumerate(d["slots"]) if s["pattern"] in ps}
    fusion = fp

    for entry in index["songs"]:
        song = read_json(DISK / entry["path"])
        what = song["name"]
        name_ok(what, 16, entry["path"])
        license_ok(song.get("license"), what)
        pats = {p["number"]: p for p in read_json(DISK / entry["path"].replace("song.json", "patterns.json"))["patterns"]}
        for n, p in pats.items():
            if n in pool:
                bad(f"pattern {n:02X}: twice ({pool[n]}, {what})")
            if n >= FUSION_FIRST:
                bad(f"{what} pattern {n:02X}: {FUSION_FIRST:02X}-FF are FUSION's and the built-in instruments' numbers")
            pool[n] = what
            check_pattern(p, f"{what} pattern {n:02X}")
        for n in song["positions"]:
            if n not in pats:
                bad(f"{what}: position of pattern {n:02X}, which is not the song's")
        check_mixer(song["mixer"], 16, what)
        check_fx(song["fx"], what)
        dj = entry["path"].split("/")[1] in DJ_LOOPS
        if dj and (song["at_the_end"] != "loop" or song["loop_from"] not in (0, 1) or
                   not 1 <= len(song["positions"]) - song["loop_from"] <= 2):
            bad(f"{what}: a DJ loop of {len(song['positions'])} positions, AT THE END {song['at_the_end']} from "
                f"{song['loop_from']} (one or two patterns that loop, after an intro if it has one)")
        if dj:                                   # LOOPS loads a track as a loop of one sound
            sounds = {}
            for p in pats.values():
                for row in voices.rows_of(p):
                    for t, (note, ins, _, _) in enumerate(row):
                        if isinstance(note, int) and ins is not None:
                            sounds.setdefault(t + 1, set()).add(ins)
            for t, ins in sorted(sounds.items()):
                if len(ins) > 1:
                    bad(f"{what}: track {t} plays {len(ins)} instruments (a DJ loop's track has one, for LOOPS)")
        limit = DJ_VOICES if dj else MAX_VOICES
        most, at, where = voices.count(song, pats, fusion)
        flag = "" if most <= limit else "   <- over"
        print(f"check: {what:14s} voices at most {most:2d} (at {at:6.1f} s, position {where[0]}, row {where[1]:02X}){flag}")
        if most > limit:
            bad(f"{what}: {most} voices at once at {at:.1f} s (position {where[0]}, row {where[1]:02X}; "
                f"{'a DJ loop has' if dj else 'the largest pool is'} {limit})")

    raw = sum(s["length"] * s.get("bits", 8) // 8 for c in index["cartridges"] if c["type"] == "sampler"
              for s in read_json(DISK / c["path"])["samples"])
    print(f"check: {len(index['cartridges'])} cartridges, {len(pool)} patterns in the pool, {raw:,} B of audio "
          f"as the samples are kept (before coding)")
    for pr in problems:
        print("PROBLEM:", pr)
    print(f"check: {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
