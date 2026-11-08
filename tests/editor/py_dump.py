# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""A disk's records as tools/ptfm_records.py reads them, as JSON, for tests/editor/records_test.js to compare the
editor's own reading against (the same fields, keyed "kind.number.sub"; a pattern "32.number", all its parts).

    python3 -I tests/editor/py_dump.py DISK.ptfmdsk OUT.json
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import ptfm_records as R  # noqa: E402


ENV = {"r1": 99, "r2": 90, "r3": 80, "r4": 70, "l1": 99, "l2": 90, "l3": 80, "l4": 0}


def instrument(sample, name, **kw):
    s = {"sample": sample, "start": 0, "end": 100, "loop_start": 0, "loop_end": 0, "loop": 0, "base_note": 60,
         "tune": 128, "level": 0, "volume": 64, "env": dict(ENV), "name": name}
    s.update(kw)
    return s


def sample(tag, name, rate=22050, coding=R.PTR_8):
    """an old table entry and its audio, made from `tag` (distinct tags: distinct audio)"""
    xs = [(tag * 7 + i * (tag % 5 + 1)) % 100 - 50 for i in range(40 + tag % 17)] + [tag & 127, tag >> 7]
    return {"length": len(xs), "rate": rate, "coding": coding, "name": name}, R.ptr_encode(xs, 8)


def old_set(samplers, audio_extra=()):
    """samplers {number: (instruments, [(entry, audio) | None])} -> records of the old form"""
    recs = []
    for num, (insts, table) in sorted(samplers.items()):
        recs.append({"kind": R.KIND_SAMPLER, "num": num, "sub": 0, "ver": 1,
                     "payload": R.pack_sampler_v1(f"S{num}", insts, [t and t[0] for t in table])})
        for i, t in enumerate(table):
            if t:
                recs.append({"kind": R.KIND_AUDIO, "num": num, "sub": i, "ver": 1, "payload": t[1]})
    for num, sub, audio in audio_extra:
        recs.append({"kind": R.KIND_AUDIO, "num": num, "sub": sub, "ver": 1, "payload": audio})
    return recs


def conversion_cases():
    """the converter's cases: tools/ptfm_records.py's answers, which records_test.js compares byte for byte with the
    page's (fw/web/seed.js)"""
    kick, snare, hat, clap, dup = sample(1, "KICK"), sample(2, "SNARE"), sample(3, "HAT"), sample(4, "CLAP"), sample(2, "SNARE")
    s2 = ([instrument(0, "KICK"), instrument(1, "SNARE"), instrument(2, "HAT"), instrument(5, "NOWHERE"), None],
          [kick, snare, hat])
    s5 = ([instrument(1, "CLAP"), instrument(0, "SNARE 2")], [dup, clap])
    cases = {}
    # two samplers, a shared sample, audio with no sampler (09) and past a table's end (02, sub 7)
    cases["basic"] = old_set({2: s2, 5: s5}, [(9, 3, sample(8, "LOST")[1]), (2, 7, sample(9, "PAST")[1])])
    # a version 2 sampler already names 00 and a sample record holds 02
    v2 = {"kind": R.KIND_SAMPLER, "num": 7, "sub": 0, "ver": 2,
          "payload": R.pack_sampler("V2", [instrument(0, "OLD ONE"), None, instrument(2, "OTHER")])}
    x = sample(30, "HELD")
    held = {"kind": R.KIND_SAMPLE, "num": 2, "sub": 0, "ver": 1, "payload": R.pack_sample(*x)}
    cases["named"] = old_set({2: s2, 5: s5}) + [v2, held]
    # an existing sample the same as an old one: merged into it
    cases["existing"] = old_set({2: s2}) + [{"kind": R.KIND_SAMPLE, "num": 9, "sub": 0, "ver": 1,
                                              "payload": R.pack_sample(*snare)}]
    # an emptied entry, the same audio under two names
    twin_a, twin_b = sample(40, "AAA"), (sample(40, "BBB")[0], sample(40, "AAA")[1])
    cases["emptied"] = old_set({3: ([instrument(0, "GONE"), instrument(1, "HERE")], [None, sample(5, "FIVE")]),
                                4: ([instrument(0, "A"), instrument(1, "B")], [twin_a, twin_b])})
    # 257 samples: four samplers of 64, then one of 1 stays old; and a sampler that does not fit stops the rest
    full = {n: ([instrument(i, f"I{i}") for i in range(64)], [sample(n * 64 + i, f"S{i}") for i in range(64)])
            for n in range(1, 5)}
    full[5] = ([instrument(0, "LAST")], [sample(999, "LAST")])
    cases["257"] = old_set(full)
    part = {1: ([instrument(0, "A")], [sample(500 + i, f"P{i}") for i in range(60)]),
            2: ([instrument(0, "B")], [sample(600 + i, f"Q{i}") for i in range(64)]),
            3: ([instrument(0, "C")], [sample(700 + i, f"R{i}") for i in range(64)]),
            4: ([instrument(0, "D")], [sample(800 + i, f"T{i}") for i in range(64)]),
            5: ([instrument(0, "E")], [sample(900 + i, f"U{i}") for i in range(10)]),
            6: ([instrument(0, "F")], [sample(950, "V0")])}
    cases["stops"] = old_set(part)
    # an old sampler record too short to hold its counts (under 11 bytes): unreadable, kept as it is with its audio
    cases["short"] = old_set({2: s2}) + [{"kind": R.KIND_SAMPLER, "num": 6, "sub": 0, "ver": 1, "payload": bytes([1, 2, 3, 4, 5])},
                                         {"kind": R.KIND_AUDIO, "num": 6, "sub": 0, "ver": 1, "payload": sample(77, "X")[1]}]
    out = {}
    for name, recs in cases.items():
        rep = R.convert_samplers_report(recs)
        out[name] = {"in": [[r["kind"], r["num"], r["sub"], r["ver"], r["payload"].hex()] for r in recs],
                     "out": [[r["kind"], r["num"], r["sub"], r["ver"], r["payload"].hex()] for r in rep["records"]],
                     "converted": rep["converted"], "older": rep["older"], "samples": rep["samples"],
                     "dropped": rep["dropped"], "bad": rep["bad"]}
    return out


def v2_forms():
    """a version 2 sampler and a sample record as this file packs them, with the fields they were made from"""
    insts = [instrument(0xC3, "HIGH NUM", loop=2, tune=3), None, instrument(7, "SEVEN", level=-6, volume=1)]
    kick = sample(1, "KICK")
    return {"instruments": insts, "sampler": R.pack_sampler("V2 SAMPLER", insts).hex(),
            "entry": kick[0], "audio": kick[1].hex(), "sample": R.pack_sample(*kick).hex()}


def main():
    disk, out = sys.argv[1], sys.argv[2]
    files = R.unpack_disk(Path(disk).read_bytes())
    d, parts = {}, {}
    for kind, num, sub, ver, payload in files:
        key = f"{kind}.{num}.{sub}"
        if kind in (R.KIND_DX7, R.KIND_JUNO, R.KIND_TB303, R.KIND_FUSION):
            c = R.unpack_cartridge(kind, payload)
            d[key] = {"name": c["name"], "slots": [{"name": s["name"], "level": s["level"]} if s else None for s in c["slots"]]}
        elif kind == R.KIND_SAMPLER:
            c = R.unpack_sampler(payload)                   # (version 2: its instruments; the old form: its table too)
            d[key] = {"name": c["name"],
                      "slots": [{"name": s["name"], "level": s["level"]} if s else None for s in c["instruments"]]}
            if "samples" in c:
                d[key]["samples"] = [{"name": e["name"], "length": e["length"], "rate": e["rate"], "coding": e["coding"]}
                                     if e else None for e in c["samples"]]
        elif kind == R.KIND_SAMPLE:                         # a sample: its entry, and its audio as the old audio's
            s = R.unpack_sample(payload)
            bits, xs = R.ptr_decode(s["audio"])
            h = 0
            for x in xs:
                h = (h * 31 + (x & 0xFFFF)) & 0xFFFFFFFF
            e = s["entry"]
            d[key] = {"entry": e and {"name": e["name"], "length": e["length"], "rate": e["rate"], "coding": e["coding"]},
                      "bits": bits, "length": len(xs), "hash": h}
        elif kind == R.KIND_SONG:
            s = R.unpack_song(payload)
            d[key] = {"name": s["name"], "tempo": s["tempo"], "ticks": s["ticks"], "rpb": s["rpb"], "swing": s["swing"],
                      "positions": s["positions"], "mixer": [[m[f] for f in R.MIX_FIELDS] for m in s["mixer"]]}
        elif kind == R.KIND_AUDIO:
            bits, xs = R.ptr_decode(payload)
            h = 0
            for x in xs:
                h = (h * 31 + (x & 0xFFFF)) & 0xFFFFFFFF
            d[key] = {"bits": bits, "length": len(xs), "hash": h}
        elif kind == R.KIND_PATTERN:
            parts.setdefault(num, {})[sub] = payload
    for num, p in parts.items():
        pat = R.unpack_pattern([p[s] for s in sorted(p)])
        cells = [[f"{r},{t}", c[0], c[1], c[2], [list(x) for x in c[3]]] for (r, t), c in sorted(pat["cells"].items())]
        d[f"32.{num}"] = {"tracks": pat["tracks"], "rows": pat["rows"], "step": pat["edit_step"], "cells": cells}
    d["conv"] = conversion_cases()
    d["v2"] = v2_forms()
    Path(out).write_text(json.dumps(d))
    print(f"py_dump: {len(d)} entries from {disk}")


if __name__ == "__main__":
    main()
