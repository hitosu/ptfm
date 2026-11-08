/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The samplers' conversion (specs/files/disk.md#the-samplers-conversion) as the pages make it: a disk in the old form
 * (samplers of version 1 holding their samples' table, the audio of kind 0x10 under them) converted by fw/web/seed.js's
 * convertSamplers, the converter the install page, the emulator and the EDITOR page share, holds the demo disk's
 * records, record for record: the same files, each of the same version and payload, byte for byte; its notes the same.
 * convert_check.py is the same check through tools/ptfm_records.py.
 *   node convert_check.js [--if-old] OLD.ptfmdsk NEW.ptfmdsk
 * --if-old: OLD not in the old form (no sampler of version 1, no old audio) is passed over, not a failure. */
"use strict";
const fs = require("fs");
const S = require("../web/seed.js");

const args = process.argv.slice(2), ifOld = args.includes("--if-old"), [oldPath, newPath] = args.filter(a => a !== "--if-old");
if (!oldPath || !newPath) { console.log("node convert_check.js [--if-old] OLD.ptfmdsk NEW.ptfmdsk"); process.exit(2); }
const read = p => {
    const d = S.parseData(new Uint8Array(fs.readFileSync(p)));
    if (d.kind !== "disk" || d.problems.length) { console.log("FAIL " + p + ": " + d.problems.join("; ")); process.exit(1); }
    return d;
};
const old = read(oldPath), now = read(newPath);
if (!old.files.some(f => (f.kind === 0x04 && f.ver === 1) || f.kind === 0x10)) {
    console.log("convert_check.js: " + oldPath + ": not in the old form" + (ifOld ? ", passed over" : ""));
    process.exit(ifOld ? 0 : 1);
}
const conv = S.convertSamplers(old.files);
const key = f => (f.kind << 16 | f.num << 8 | f.sub) >>> 0, tag = k => [k >> 16, k >> 8 & 255].map(x =>
    (x < 16 ? "0" : "") + x.toString(16).toUpperCase()).join(" ") + " " + (k & 255);
const got = new Map(conv.recs.map(f => [key(f), f])), want = new Map(now.files.map(f => [key(f), f]));
let fails = 0;
const fail = m => { fails++; if (fails <= 20) console.log("FAIL " + m); };
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
for (const k of [...new Set([...got.keys(), ...want.keys()])].sort((a, b) => a - b)) {
    const g = got.get(k), w = want.get(k);
    if (!g) fail(tag(k) + ": on the new disk, not made by the conversion");
    else if (!w) fail(tag(k) + ": made by the conversion, not on the new disk");
    else if (g.ver !== w.ver) fail(tag(k) + ": version " + g.ver + ", the new disk's " + w.ver);
    else if (!same(g.payload, w.payload)) fail(tag(k) + ": its payload differs (" + g.payload.length + " B, the new disk's " +
                                                w.payload.length + " B)");
}
if (old.notes !== now.notes) fail("the notes differ");
if (conv.older.length || conv.bad.length || conv.dropped)
    fail("the conversion left samplers old " + conv.older + ", unreadable " + conv.bad + ", dropped " + conv.dropped + " audio");
console.log("convert_check.js: " + oldPath + " converted (" + conv.converted.length + " samplers, " + conv.samples +
            " samples) against " + newPath + ": " + want.size + " records, " + (fails ? fails + " FAILED" : "every one the same"));
process.exit(fails ? 1 : 0);
