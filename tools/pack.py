#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The packer: the demo disk's sources (demo-disk/, notation version 2) as PT/FM's records
(specs/files/; the bytes: tools/ptfm_records.py).

  python3 -I tools/pack.py [--src DIR] [--out FILE.ptfmdsk]   -> dist/firmware/demo.ptfmdsk: the demo disk (needs ffmpeg)
  python3 -I tools/pack.py --builtin DIR                      -> DIR/builtin.c: the built-in instruments
  python3 -I tools/pack.py --check FILE.ptfmdsk [--src DIR]   -> reads a disk back and compares it, and the
                                                                 built-in instruments, with playground/content.js
  python3 -I tools/pack.py --dump FILE.ptfmdsk DIR            -> every record decoded, for the firmware's tests
  python3 -I tools/pack.py --selftest                         -> the codecs on made-up data

The demo disk (specs/files/disk.md#the-demo-disk) holds the sampler cartridges and their samples' audio
(PTR), FUSION and its patterns, the songs, their patterns, and its notes (demo-disk/CREDITS.md). The
built-in instruments (the synth cartridges, numbered F0-FF: specs/files/instruments.md) go into the
firmware instead, as builtin.c (fw/core/store/builtin.h).

--check decodes every record with ptfm_records.py and compares it with the playground's content, as
tools/gen_playground.py made it from the same sources: names, LEVELs, every slot's values, the samples'
8-bit audio, every cell, the songs' timing, order, mixer and FX setup. It also packs every decoded record
again and compares the bytes. Exit status 1 on a difference."""
import base64
import json
import os
import struct
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import ptfm_records as R  # noqa: E402
import gen_playground as G  # noqa: E402  (the demo disk's readers: names, cells, FLAC)

BUILTIN_FIRST = 0xF0                                     # cartridges and patterns F0-FF are the firmware's
ENGINE_KIND = {"dx7": R.KIND_DX7, "juno106": R.KIND_JUNO, "tb303": R.KIND_TB303, "sampler": R.KIND_SAMPLER,
               "fusion": R.KIND_FUSION, "chip": R.KIND_CHIP}


# ----------------------------------------------------------------------------- reading the sources ---
def cells_of(rows):
    """a pattern's rows as the disk writes them -> {(row, track): (note, ins, vol, cmds)}"""
    out = {}
    for c in G.pattern_cells(rows):
        r, t, note, ins, vol, rest = c[0], c[1], c[2], c[3], c[4], c[5:]
        out[(r, t)] = (note, ins, vol, [(rest[i], rest[i + 1]) for i in range(0, len(rest), 2)])
    return out


def patterns_of(path):
    """a patterns.json -> {number: (payloads, source)}"""
    out = {}
    for p in G.read_json(path)["patterns"]:
        out[p["number"]] = R.pack_pattern(p["tracks"], len(p["rows"]), p["edit_step"], cells_of(p["rows"]))
    return out


def slot_values(kind, src):
    """a slot as the demo disk writes it -> the fields ptfm_records packs"""
    if kind == R.KIND_DX7:
        ops, voice = R.dx7_values(src)
        out = {"ops": ops, "voice": voice}
    elif kind == R.KIND_JUNO:
        out = R.juno_values(src)
    elif kind == R.KIND_TB303:
        out = R.tb303_values(src)
    elif kind == R.KIND_CHIP:
        out = R.chip_values(src)
    else:
        out = {"pattern": src["pattern"]}
    out["name"] = G.clean(src["name"], 10)
    out["level"] = R.level_byte(src.get("level", 0))
    return out


def sample_audio(path, bits, opt=None):
    """a FLAC sample -> signed samples at the depth the disk keeps (8 bits: rounded as the playground does); opt:
    its `optimize` (8-bit samples only)"""
    if bits == 8:
        return [v - 256 if v > 127 else v for v in G.flac_read(path, opt)]
    if opt:
        raise SystemExit(f"pack: {path.name}: `optimize` is for 8-bit samples")
    raw = __import__("subprocess").run(["ffmpeg", "-v", "error", "-i", str(path), "-f", "s16le", "-ac", "1",
                                        "pipe:1"], capture_output=True, check=True).stdout
    return list(struct.unpack(f"<{len(raw) // 2}h", raw))


def sampler_of(cart, path):
    """a samples.json -> (the cartridge's payload, [(sub, PTR payload)])"""
    src = G.read_json(path)
    ids = [s["id"] for s in src["samples"]]
    entries, audio = [], []
    for i, s in enumerate(src["samples"]):
        bits = s.get("bits", 8)
        values = sample_audio(path.parent / s["file"], bits, s.get("optimize"))
        if len(values) != s["length"]:
            raise SystemExit(f"pack: {s['id']}: {len(values)} samples, the disk says {s['length']}")
        audio.append((i, R.ptr_encode(values, bits)))
        entries.append({"length": len(values), "rate": s["rate"], "coding": R.PTR_8 if bits == 8 else R.PTR_16,
                        "name": G.clean(s["name"], 10)})
    instruments = []
    for i in src["instruments"]:
        instruments.append({"sample": ids.index(i["sample"]), "start": i["start"], "end": i["end"],
                            "loop_start": i["loop_start"], "loop_end": i["loop_end"],
                            "loop": R.LOOPS.index(i["loop"]), "base_note": G.midi(i["base_note"]),
                            "tune": i["tune"] + 0x80, "level": R.level_byte(G.made_up(src, i)),
                            "volume": i["volume"],
                            "env": dict(zip(["r1", "r2", "r3", "r4", "l1", "l2", "l3", "l4"],
                                            i["envelope"]["rates"] + i["envelope"]["levels"])),
                            "name": G.clean(i["name"], 10)})
    return R.pack_sampler(G.clean(cart["name"], 12), instruments, entries), audio


def song_of(path):
    s = G.read_json(path)
    return {"name": G.clean(s["name"], 16), "tempo": s["tempo"], "ticks": s["ticks_a_row"], "rpb": s["rows_a_beat"],
            "swing": s["swing"], "at_end": s["at_the_end"], "loop_from": s["loop_from"], "slides": s["slides"],
            "fast_volume_slides": s["fast_volume_slides"], "global_volume": s["global_volume"],
            "positions": s["positions"], "mixer": s["mixer"], "fx": s["fx"]}, s


def read_sources(src, disk_too=True):
    """the demo disk's sources -> (disk files [(kind, number, sub, payload)], built-in records [same],
    the licences the built-in instruments carry); disk_too False: the built-in records only"""
    index = G.read_json(src / "index.json")
    disk, builtin, lic = [], [], set()
    patterns = {}
    for c in index["cartridges"]:
        kind, path = ENGINE_KIND[c["type"]], src / c["path"]
        num = c["number"]
        if kind == R.KIND_SAMPLER:
            if num >= BUILTIN_FIRST:
                raise SystemExit(f"pack: sampler cartridge {num:02X} in the built-in range")
            if not disk_too:
                continue
            payload, audio = sampler_of(c, path)
            disk.append((kind, num, 0, payload))
            disk += [(R.KIND_AUDIO, num, sub, a) for sub, a in audio]
            continue
        on_disk = num < BUILTIN_FIRST           # (FUSION: the disk's; the synth cartridges: built in)
        if on_disk and kind != R.KIND_FUSION:
            raise SystemExit(f"pack: {c['name']}: a synth cartridge outside the built-in range F0-FF")
        if on_disk and not disk_too:
            continue
        js = G.read_json(path)
        if not on_disk:
            G.licences(js, lic)
        slots = js["voices"] if kind == R.KIND_DX7 else js["slots"] if kind == R.KIND_FUSION else \
            [dict(i, level=lv) for i, lv in zip(js["instruments"], js["levels"])] if kind == R.KIND_CHIP else js["patches"]
        to = disk if on_disk else builtin
        to.append((kind, num, 0, R.pack_cartridge(kind, G.clean(c["name"], 12), [slot_values(kind, s) for s in slots])))
        if kind == R.KIND_FUSION:
            for n, parts in patterns_of(path.parent / js["patterns"]).items():
                if (n < BUILTIN_FIRST) != on_disk:
                    raise SystemExit(f"pack: FUSION pattern {n:02X} outside its cartridge's range")
                to += [(R.KIND_PATTERN, n, sub, p) for sub, p in enumerate(parts)]
                patterns[n] = "FUSION"
    for s in index["songs"]:
        path = src / s["path"]
        if not disk_too:
            continue
        song, js = song_of(path)
        disk.append((R.KIND_SONG, s["number"], 0, R.pack_song(song)))
        for n, parts in patterns_of(path.parent / js["patterns"]).items():
            if n in patterns:
                raise SystemExit(f"pack: pattern {n:02X} twice ({patterns[n]}, {song['name']})")
            if n >= BUILTIN_FIRST:
                raise SystemExit(f"pack: {song['name']}: pattern {n:02X} in the built-in range")
            patterns[n] = song["name"]
            disk += [(R.KIND_PATTERN, n, sub, p) for sub, p in enumerate(parts)]
    if disk_too:
        credits = (src / "CREDITS.md").read_bytes()
        credits.decode("utf-8")
        disk.append((R.KIND_NOTES, 0, 0, credits))
    return disk, builtin, lic


# --------------------------------------------------------------------------------------- builtin.c ---
def write_builtin(out_dir, builtin, lic):
    lines = ["/* SPDX-License-Identifier: " + " AND ".join(sorted(lic)),
             " * PT/FM's built-in instruments (specs/files/instruments.md#built-in-instruments): the demo disk's synth",
             " * cartridges, numbered F0-FF, each payload byte for byte as a record holds it. Generated by",
             " * tools/pack.py --builtin from demo-disk/: do not edit. Every work's licence, author",
             " * and source: demo-disk/CREDITS.md. */",
             "#include \"store/builtin.h\"", ""]
    table = []
    for kind, num, sub, payload in sorted(builtin, key=lambda f: (f[0], f[1], f[2])):
        name = f"bi_{kind:02x}_{num:02x}_{sub:x}"
        lines.append(f"static const uint8_t {name}[{len(payload)}] __attribute__((aligned(4))) = {{")
        for i in range(0, len(payload), 16):
            lines.append("    " + ", ".join(f"0x{x:02x}" for x in payload[i:i + 16]) + ",")
        lines.append("};")
        what = {R.KIND_DX7: "DX7", R.KIND_JUNO: "Juno-106", R.KIND_TB303: "TB-303", R.KIND_FUSION: "FUSION",
                R.KIND_CHIP: "CHIP", R.KIND_PATTERN: "pattern"}[kind]
        table.append(f"    {{0x{kind:02x}, 0x{num:02x}, {sub}, {R.VERSION[kind]}, {len(payload)}, {name}}},"
                     f"  /* {what} {num:02X}{'' if kind != R.KIND_PATTERN else ' part %d' % sub} */")
    lines += ["", "const builtin_rec_t builtin_recs[] = {"] + table + ["};",
              f"const int builtin_count = {len(table)};", ""]
    out = Path(out_dir) / "builtin.c"
    out.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines)
    if not out.exists() or out.read_text(encoding="ascii") != text:
        out.write_text(text, encoding="ascii")
    size = sum(len(p) for *_, p in builtin)
    print(f"pack: {out}: {len(table)} built-in records, {size:,} B")


# ------------------------------------------------------------------------------------------- check ---
def first_diff(a, b, path=""):
    """where two structures first differ: (path, a's value, b's value), or None"""
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b), key=str):
            if k not in a or k not in b:
                return f"{path}.{k}", a.get(k, "(none)"), b.get(k, "(none)")
            d = first_diff(a[k], b[k], f"{path}.{k}")
            if d:
                return d
        return None
    if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        if len(a) != len(b):
            return f"{path} length", len(a), len(b)
        for i, (x, y) in enumerate(zip(a, b)):
            d = first_diff(x, y, f"{path}[{i}]")
            if d:
                return d
        return None
    return None if a == b else (path, a, b)


class Diff:
    def __init__(self):
        self.n = 0

    def __call__(self, what, got, want):
        d = first_diff(got, want)
        if d:
            self.n += 1
            if self.n <= 40:
                print(f"check: {what}{d[0]}: {str(d[1])[:200]} != {str(d[2])[:200]}")


def only(want, like):
    """want, keeping only what a record stores (the keys of like, at every level): a slot keeps no licence,
    source or notes, and no bits the DX7 ignores (`ignored_bits`)"""
    if isinstance(want, dict) and isinstance(like, dict):
        return {k: only(want[k], like[k]) if k in want else "(none)" for k in like}
    if isinstance(want, list) and isinstance(like, list) and len(want) == len(like):
        return [only(w, x) for w, x in zip(want, like)]
    return want


def check_cartridge(diff, kind, num, payload, want, audio):
    tag = f"cartridge {num:02X}"
    if kind == R.KIND_SAMPLER:
        c = R.unpack_sampler(payload)
        diff(tag + " repacked", R.pack_sampler(c["name"], c["instruments"], c["samples"]), payload)
        diff(tag + " name", c["name"], want["name"])
        diff(tag + " samples", len(c["samples"]), len(want["samples"]))
        for i, (e, w) in enumerate(zip(c["samples"], want["samples"])):
            bits, values = R.ptr_decode(audio[(num, i)])
            diff(f"{tag} sample {i:02X} name", e["name"], w["name"])
            diff(f"{tag} sample {i:02X} rate", e["rate"], w["rate"])
            diff(f"{tag} sample {i:02X} length", (e["length"], e["coding"]), (len(values), R.PTR_8 if bits == 8 else R.PTR_16))
            diff(f"{tag} sample {i:02X} audio", base64.b64encode(bytes(v & 0xFF for v in values)).decode("ascii"),
                 w["audio"])
        slots = c["instruments"]
        for i, (s, w) in enumerate(zip(slots, want["slots"])):
            x = w["sampler"]
            got = {"sample": s["sample"], "start": s["start"], "end": s["end"], "loop": R.LOOPS[s["loop"]].upper(),
                   "loopStart": s["loop_start"], "loopEnd": s["loop_end"], "base": s["base_note"],
                   "tune": s["tune"] - 0x80, "volume": s["volume"],
                   "env": {"rates": [s["env"][k] for k in ("r1", "r2", "r3", "r4")],
                           "levels": [s["env"][k] for k in ("l1", "l2", "l3", "l4")]}}
            diff(f"{tag} slot {i:02X}", (s["name"], s["level"] / 2, got), (w["name"], w["level"], x))
    else:
        c = R.unpack_cartridge(kind, payload)
        diff(tag + " repacked", R.pack_cartridge(kind, c["name"], c["slots"]), payload)
        diff(tag + " name", c["name"], want["name"])
        slots = c["slots"]
        for i, (s, w) in enumerate(zip(slots, want["slots"])):
            if kind == R.KIND_DX7:
                got, x = R.dx7_voice(s["ops"], s["voice"]), w["dx7"]
            elif kind == R.KIND_JUNO:
                got, x = R.juno_patch(s), w["juno"]
            elif kind == R.KIND_TB303:
                got, x = R.tb303_patch(s), w["tb303"]
            elif kind == R.KIND_CHIP:
                got, x = R.chip_numeric(s), w["chip"]
            else:
                got, x = {"pattern": s["pattern"]}, w["fusion"]
            diff(f"{tag} slot {i:02X}", (s["name"], s["level"] / 2, got), (w["name"], w["level"], only(x, got)))
    diff(tag + " slots", len(slots), len(want["slots"]))


def check_pattern(diff, num, parts, want):
    p = R.unpack_pattern(parts)
    diff(f"pattern {num:02X} repacked", R.pack_pattern(p["tracks"], p["rows"], p["edit_step"], p["cells"]), parts)
    cells = []
    for (r, t), (note, ins, vol, cmds) in sorted(p["cells"].items()):
        cells.append([r, t, note, ins, vol] + [x for c in cmds for x in c])
    diff(f"pattern {num:02X}", (p["tracks"], p["rows"], p["edit_step"], cells),
         (want["tracks"], want["rows"], want["step"], want["cells"]))


def check_song(diff, num, payload, want):
    s = R.unpack_song(payload)
    diff(f"song {num:02X} repacked", R.pack_song(s), payload)
    got = [s["name"], s["tempo"], s["ticks"], s["rpb"], s["swing"], s["at_end"].upper(), s["loop_from"],
           s["slides"], s["fast_volume_slides"], s["global_volume"], s["positions"], s["mixer"]]
    exp = [want["name"], want["tempo"], want["ticks"], want["rpb"], want["swing"], want["atEnd"], want["loopFrom"],
           want["slides"], want["fastVol"], want["gvol"], want["order"], want["mixer"]]
    diff(f"song {num:02X}", got, exp)
    fx, wfx = s["fx"], json.loads(json.dumps(want["fx"]))
    wfx["reverb"]["pre_delay"] = min(100, wfx["reverb"]["pre_delay"])   # the knob's range: 0-100 ms
    wfx["filter"] = dict(R.FILTER_ENV_DEFAULT, **wfx["filter"])          # (FILTER's envelope: a new song's if none)
    diff(f"song {num:02X} FX setup", fx, wfx)


def check(path, src):
    files = R.unpack_disk(Path(path).read_bytes())
    content = (ROOT / "playground" / "content.js").read_text(encoding="ascii")
    c = json.loads(content[content.index("PTFM.CONTENT = ") + 15:content.rindex(";")])
    want_carts = {x["id"]: x for x in c["cartridges"]}
    want_pats = {x["n"]: x for x in c["patterns"]}
    want_songs = {x["n"]: x for x in c["songs"]}
    diff = Diff()
    for kind, num, sub, ver, _ in files:
        diff(f"file {kind:02X} {num:02X} {sub} version", ver, R.VERSION.get(kind))
    _, builtin, _ = read_sources(src)
    records = [(k, n, s, p) for k, n, s, _, p in files] + builtin
    audio = {(n, s): p for k, n, s, p in records if k == R.KIND_AUDIO}
    pats = {}
    for k, n, s, p in records:
        if k == R.KIND_PATTERN:
            pats.setdefault(n, []).append((s, p))
    seen_carts, seen_songs = set(), set()
    for k, n, s, p in records:
        if k in ENGINE_KIND.values():
            seen_carts.add(n)
            if n not in want_carts:
                diff(f"cartridge {n:02X}", "on the disk", "not in the playground")
            else:
                check_cartridge(diff, k, n, p, want_carts[n], audio)
        elif k == R.KIND_SONG:
            seen_songs.add(n)
            if n in want_songs:
                check_song(diff, n, p, want_songs[n])
    cells = 0
    for n, parts in pats.items():
        subs = [s for s, _ in sorted(parts)]
        diff(f"pattern {n:02X} parts", subs, list(range(len(subs))))
        if n in want_pats:
            check_pattern(diff, n, [p for _, p in sorted(parts)], want_pats[n])
            cells += len(want_pats[n]["cells"])
    diff("cartridges", sorted(seen_carts), sorted(want_carts))
    diff("songs", sorted(seen_songs), sorted(want_songs))
    diff("patterns", sorted(pats), sorted(want_pats))
    if any(n >= BUILTIN_FIRST for k, n, s, _, p in files if k in ENGINE_KIND.values() or k == R.KIND_PATTERN):
        diff("the disk", "holds a cartridge or pattern F0-FF", "none")
    disk_bytes = Path(path).stat().st_size
    audio_bytes = sum(len(p) for k, n, s, _, p in files if k == R.KIND_AUDIO)
    print(f"check: {path}: {len(files)} files, {disk_bytes:,} B (audio {audio_bytes:,} B in {len(audio)} samples); "
          f"built-in {len(builtin)} records, {sum(len(p) for *_, p in builtin):,} B; compared {len(seen_carts)} "
          f"cartridges, {len(pats)} patterns ({cells} cells), {len(seen_songs)} songs: "
          f"{'all equal' if not diff.n else f'{diff.n} differences'}")
    return diff.n == 0


def dump(path, out_dir, src):
    """every record of a disk, and the built-in instruments', decoded for the firmware's tests to compare
    with: DIR/records.json (cartridges, patterns with their cells as [row, track, note, instrument, volume,
    code, value, ...], songs) and each sample's audio as raw signed values (DIR/audio_CC_SS.s8 or .s16,
    little-endian)"""
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    files = R.unpack_disk(Path(path).read_bytes())
    files += [(k, n, s, R.VERSION[k], p) for k, n, s, p in read_sources(src, disk_too=False)[1]]
    recs = {"cartridges": {}, "patterns": {}, "songs": {}}
    pats = {}
    for kind, num, sub, ver, p in files:
        if kind == R.KIND_SAMPLER:
            recs["cartridges"][f"{num:02X}"] = R.unpack_sampler(p)
        elif kind in ENGINE_KIND.values():
            recs["cartridges"][f"{num:02X}"] = R.unpack_cartridge(kind, p)
        elif kind == R.KIND_SONG:
            recs["songs"][f"{num:02X}"] = R.unpack_song(p)
        elif kind == R.KIND_PATTERN:
            pats.setdefault(num, []).append((sub, p))
        elif kind == R.KIND_AUDIO:
            bits, values = R.ptr_decode(p)
            fmt = "b" if bits == 8 else "h"
            (out / f"audio_{num:02X}_{sub:02X}.s{bits}").write_bytes(struct.pack(f"<{len(values)}{fmt}", *values))
    for num, parts in sorted(pats.items()):
        p = R.unpack_pattern([x for _, x in sorted(parts)])
        p["cells"] = [[r, t, note, ins, vol] + [x for c in cmds for x in c]
                      for (r, t), (note, ins, vol, cmds) in sorted(p["cells"].items())]
        recs["patterns"][f"{num:02X}"] = p
    (out / "records.json").write_text(json.dumps(recs, indent=1), encoding="ascii")
    print(f"pack: {out}: records.json and {sum(1 for f in files if f[0] == R.KIND_AUDIO)} samples' audio")


def selftest():
    """ptfm_records' codecs on made-up data, where the demo disk does not reach: 16-bit PTR and its
    extremes, every block length, long skips, command lists, every instrument form, patterns in parts"""
    import random
    rnd, n = random.Random(1), 0
    for bits in (8, 16):
        lo, hi = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
        for length in (1, 2, 3, 4, 63, 64, 65, 255, 256, 257, 511, 4096 + 17, 16 * 256 + 1):
            for shape in ("noise", "sine", "edges", "zero"):
                if shape == "noise":
                    s = [rnd.randint(lo, hi) for _ in range(length)]
                elif shape == "sine":
                    s = [round(hi * 0.9 * __import__("math").sin(i * 0.05)) for i in range(length)]
                elif shape == "edges":
                    s = [(lo, hi)[(i // 3) % 2] for i in range(length)]
                else:
                    s = [0] * length
                rec = R.ptr_encode(s, bits)                            # checks itself
                if R.ptr_decode(rec) != (bits, s):
                    raise SystemExit(f"selftest: PTR {bits}-bit {shape} {length}")
                n += 1
    cells = {(0, 0): (60, 0x0105, 0x7F, [(0x0C, 0x40)]),                        # full instrument, volume, a command
             (1, 0): (62, 0x0105, -1, []),                                       # the same instrument (0 B)
             (2, 0): (64, 0x0106, -1, [(0x0A, 0x04), (0xC0, 0), (0x94, 10), (0xD1, 8)]),  # another slot, a list of 4
             (9, 0): (R.NOTE_OFF, -1, -1, []),                                   # a skip of 6, a note off
             (17, 0): (0, -1, 0x20, []),                                         # a skip of 7: its byte
             (255, 0): (127, 0x0201, -1, [(0x0F, 0x20), (0x08, 0x80)]),          # a skip of 237, another cartridge
             (255, 15): (12, -1, -1, [])}                                        # track 15, row 255 from row 0
    parts = R.pack_pattern(16, 256, 3, cells)
    p = R.unpack_pattern(parts)
    if (p["tracks"], p["rows"], p["edit_step"], p["cells"]) != (16, 256, 3, cells):
        raise SystemExit("selftest: a pattern")
    big = {(r, t): (24 + (r * 7 + t) % 90, (t << 8) | r % 64, r % 128, [(0x0A, r), (0x04, t)])
           for r in range(256) for t in range(16)}
    parts = R.pack_pattern(16, 256, 1, big)
    if len(parts) < 2 or any(len(x) > R.RECORD_MAX for x in parts) or R.unpack_pattern(parts)["cells"] != big:
        raise SystemExit("selftest: a pattern in parts")
    if R.unpack_pattern(R.pack_pattern(4, 64, 1, {}))["cells"] != {}:
        raise SystemExit("selftest: an empty pattern")
    name = R.NAME_CHARS[1:13]
    if R.unpack_name(R.pack_name(name, 12), 12) != name:
        raise SystemExit("selftest: a name")
    slots = [{"pattern": 0xF0, "name": "A", "level": -128}, None, {"pattern": 1, "name": "%@$#*", "level": 127}]
    c = R.unpack_cartridge(R.KIND_FUSION, R.pack_cartridge(R.KIND_FUSION, "X", slots))
    if c["slots"] != slots:
        raise SystemExit("selftest: a cartridge with an empty slot")
    print(f"selftest: {n} PTR round trips, patterns, names, cartridges: all good")


# -------------------------------------------------------------------------------------------- main ---
def main():
    args = sys.argv[1:]
    opt = {}
    while args:
        a = args.pop(0)
        if a in ("--src", "--out", "--builtin", "--check") and args:
            opt[a] = args.pop(0)
        elif a == "--selftest":
            selftest()
            return
        elif a == "--dump" and len(args) >= 2:
            dump(args[0], args[1], ROOT / "demo-disk")
            return
        else:
            raise SystemExit(__doc__)
    src = Path(opt.get("--src", ROOT / "demo-disk")).resolve()
    if "--check" in opt:
        sys.exit(0 if check(opt["--check"], src) else 1)
    if "--builtin" in opt:
        _, builtin, lic = read_sources(src, disk_too=False)
        write_builtin(opt["--builtin"], builtin, lic)
        return
    disk, builtin, lic = read_sources(src)
    out = Path(opt.get("--out", ROOT / "dist" / "firmware" / "demo.ptfmdsk"))
    data = R.pack_disk(disk)
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_bytes() != data:
        out.write_bytes(data)
    audio = sum(len(p) for k, n, s, p in disk if k == R.KIND_AUDIO)
    print(f"pack: {out}: {len(disk)} files, {len(data):,} B (audio {audio:,} B)")


if __name__ == "__main__":
    os.umask(0o022)
    main()
