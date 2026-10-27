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
            c = R.unpack_sampler(payload)
            d[key] = {"name": c["name"],
                      "slots": [{"name": s["name"], "level": s["level"]} if s else None for s in c["instruments"]],
                      "samples": [{"name": e["name"], "length": e["length"], "rate": e["rate"], "coding": e["coding"]}
                                  if e else None for e in c["samples"]]}
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
    Path(out).write_text(json.dumps(d))
    print(f"py_dump: {len(d)} entries from {disk}")


if __name__ == "__main__":
    main()
