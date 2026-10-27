#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The demo disk's index and credits, from what the other tools made (catalog.py: the cartridges and songs).

  index.py        -> index.json, CREDITS.md

The credits name every work by its licence and its author: a cartridge's own voices, patches and presets
under its licence; one carrying a licence of its own under that one, with its name in its source; every
sample; every song.
"""
from catalog import CARTRIDGES, SONGS
from common import DISK, fmt, read_json, write_json

LICENSE_NAMES = {"CC0-1.0": "CC0 1.0 Universal (public domain dedication)", "MIT": "MIT License",
                 "CC-PDM-1.0": "Public Domain Mark 1.0 (no known copyright)",
                 "CC-BY-3.0": "Creative Commons Attribution 3.0", "CC-BY-4.0": "Creative Commons Attribution 4.0",
                 "CC-BY-SA-4.0": "Creative Commons Attribution-ShareAlike 4.0",
                 "BSD-2-Clause": "BSD 2-Clause License", "GPL-3.0-only": "GNU General Public License 3.0 only",
                 "GPL-3.0-or-later": "GNU GPL 3.0 or later", "Unlicense": "The Unlicense"}
LICENSE_TEXTS = {"CC0-1.0": "https://creativecommons.org/publicdomain/zero/1.0/",
                 "CC-BY-4.0": "https://creativecommons.org/licenses/by/4.0/",
                 "CC-BY-SA-4.0": "https://creativecommons.org/licenses/by-sa/4.0/",
                 "GPL-3.0-only": "https://www.gnu.org/licenses/gpl-3.0.html",
                 "BSD-2-Clause": "https://opensource.org/license/bsd-2-clause"}
MIT_TEXT = """Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE."""
BSD2_TEXT = """Redistribution and use in source and binary forms, with or without modification, are permitted
provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE."""
NOTICES = {"MIT": MIT_TEXT, "BSD-2-Clause": BSD2_TEXT}


def items_of(d):
    return d.get("voices") or d.get("patches") or d.get("instruments") or d.get("slots") or []


def works():
    """(licence, where it is: a cartridge, a song, ..., the work) for everything on the disk"""
    out = []
    for number, kind, path, name in CARTRIDGES:
        d = read_json(DISK / path)
        if kind == "sampler":
            for s in d["samples"]:
                out.append((s["license"], name, s["name"]))
            continue
        items = items_of(d)
        own = [it for it in items if "license" not in it]
        if len(own) == len(items):
            out.append((d["license"], name, f"all {len(items)}"))
        else:
            out.append((d["license"], name, "the choice and names of its slots"))
            out += [(d["license"], name, it["name"].strip()) for it in own]
        for it in items:
            if "license" in it:
                there = it.get("source", {}).get("voice") or it.get("source", {}).get("preset") or \
                    it.get("source", {}).get("file", "")
                there = there.rsplit(".", 1)[0] if there.endswith(".dmp") else there
                out.append((it["license"], name, f"{it['name'].strip()} ({there})"))
    for folder, name in SONGS:
        out.append((read_json(DISK / "songs" / folder / "song.json")["license"], "songs", name))
    return out


def credits():
    groups = {}
    for lic, where, what in works():
        by = groups.setdefault(lic["id"], {}).setdefault((lic["credit"], lic["url"]), {})
        by.setdefault(where, []).append(what)
    lines = ["# The demo disk: credits", "",
             "What the demo disk holds, under which licence, and from whom: each cartridge, its voices, patches",
             "and presets (with their names in their sources where they come from elsewhere), the samples, and",
             "the songs.", ""]
    for lid in sorted(groups, key=lambda k: (k != "CC0-1.0", k)):
        text = LICENSE_TEXTS.get(lid)
        lines += [f"## {LICENSE_NAMES.get(lid, lid)} ({lid})", ""]
        if text:
            lines += [f"The licence: {text}", ""]
        for (credit, url), wheres in sorted(groups[lid].items()):
            lines.append(f"- **{credit}** ({url}):")
            for where, whats in wheres.items():
                lines.append(f"  - {where}: {', '.join(whats)}")
        if lid in NOTICES:
            lines += [""] + ["  " + ln if ln else "  " for ln in NOTICES[lid].split("\n")]
        lines.append("")
    (DISK / "CREDITS.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    write_json(DISK / "index.json", {
        "format": fmt("index"), "version": 2, "name": "DEMO",
        "cartridges": [{"number": n, "type": kind, "path": path, "name": name} for n, kind, path, name in CARTRIDGES],
        "songs": [{"number": i, "path": f"songs/{folder}/song.json", "name": name} for i, (folder, name) in enumerate(SONGS)]})
    credits()
    print(f"index: {len(CARTRIDGES)} cartridges, {len(SONGS)} songs; CREDITS.md")


if __name__ == "__main__":
    main()
