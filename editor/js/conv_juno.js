/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Juno-106 banks into Juno-106 cartridges (specs/editor.md, Juno-106 banks; Juno tape recordings; Juno-60 patches as
 * Juno-106 ones). A patch here is the Juno-106's own 18 bytes: the 16 sliders in the panel's order, sw1, sw2.
 *   ED.conv.juno.slotOf(p, name) -> a cartridge slot; bytesOf(slot) -> the 18 bytes (exact both ways for what a Juno-106
 *     sends: one RANGE bit, CHORUS I only with CHORUS on, sw1's bit 7 and sw2's bits 5-7 clear; others are read as the
 *     engine reads them and come back so)
 *   kind(bytes) -> "syx" | "106" | "pat" | null (by content); read(bytes, name) -> { patches: [{ p, name } | null],
 *     cartName, notes: [] } (throws when the file is not one this page reads)
 *   cartRecs(patches, base, cartName, names?) -> Juno-106 cartridge records, 128 slots each (a null patch: an empty
 *     slot; names: each cartridge's, else the name numbered past 128)
 *   from60(sliders, sw0, sw1) -> { p, pwmEnv } (a Juno-60 patch converted: juno60_tables.js's tables and rules)
 *   recsOf(bytes, name) -> { recs, what, warnings } (drops.js: a file into records) */
(function (ED) {
"use strict";
const R = ED.rec, K = R.KIND;
ED.conv = ED.conv || {};
const plural = (n, w) => n + " " + w + (n === 1 ? "" : "S");

/* ---- a patch's bytes and a slot (records.js's JUNO fields; the firmware's rec_juno_patch the other way) ---- */
const SLIDERS = ["lfo_rate", "lfo_delay", "dco_lfo", "dco_pwm", "dco_noise", "vcf_freq", "vcf_res", "vcf_env", "vcf_lfo",
    "vcf_kybd", "vca_level", "env_a", "env_d", "env_s", "env_r", "dco_sub"];
function slotOf(p, name) {
    const s = { name: name || " ", level: 0 };
    SLIDERS.forEach((f, i) => { s[f] = p[i] & 127; });
    const sw1 = p[16], sw2 = p[17], r = sw1 & 7;
    s.range = r === 1 ? 0 : r === 4 ? 2 : 1;              /* (16', 8', 4'; none or several set: 8') */
    s.pulse = (sw1 >> 3) & 1;
    s.saw = (sw1 >> 4) & 1;
    s.chorus = sw1 & 0x20 ? 0 : sw1 & 0x40 ? 1 : 2;      /* (off, I, II) */
    s.pwm_source = sw2 & 1;
    s.env_polarity = (sw2 >> 1) & 1;
    s.vca_mode = (sw2 >> 2) & 1;
    s.hpf = 3 - ((sw2 >> 3) & 3);                       /* (sw2's two bits inverted: HPF 0 BASS BOOST ... 3) */
    return s;
}
function bytesOf(s) {
    const p = new Uint8Array(18);
    SLIDERS.forEach((f, i) => { p[i] = s[f] & 127; });
    p[16] = [1, 2, 4][s.range] | (s.pulse & 1) << 3 | (s.saw & 1) << 4 | [0x20, 0x40, 0][s.chorus];
    p[17] = (s.pwm_source & 1) | (s.env_polarity & 1) << 1 | (s.vca_mode & 1) << 2 | (3 - (s.hpf & 3)) << 3;
    return p;
}

/* ---- names ---- */
/* a patch's name: a slot label at its start dropped (A31, A-21: A or B, an optional -, two digits 1-8, a space) */
const patchName = n => R.cleanName(String(n || "").replace(/^\s*[AB]-?[1-8][1-8]\s+/i, ""), 10);
const noName = slot => "PATCH" + R.hex2(slot);

/* ---- .syx: the patch messages F0 41 30|31 0n pp, 16 sliders, sw1, sw2, F7 ---- */
function messages(b) {
    const out = [];
    let i = 0, outside = 0;
    while (i < b.length) {
        if (b[i] !== 0xF0) { outside++; i++; continue; }
        let e = i + 1;
        while (e < b.length && b[e] !== 0xF7 && b[e] !== 0xF0) e++;
        const end = e < b.length && b[e] === 0xF7 ? e + 1 : e;
        out.push(b.subarray(i, end));
        i = end;
    }
    return { msgs: out, outside };
}
const isPatchMsg = m => m.length === 24 && m[1] === 0x41 && (m[2] === 0x30 || m[2] === 0x31) && (m[3] & 0xF0) === 0 && m[23] === 0xF7 &&
    m.subarray(4, 23).every(v => v < 0x80);
/* a Juno-106 .syx: one whole patch message in it at least (three bytes alike turn up in other files' data) */
function hasSyx(b) {
    for (let i = 0; i + 24 <= b.length; i++)
        if (b[i] === 0xF0 && b[i + 1] === 0x41 && (b[i + 2] === 0x30 || b[i + 2] === 0x31) && isPatchMsg(b.subarray(i, i + 24))) return true;
    return false;
}
function readSyx(b, o) {
    const { msgs, outside } = messages(b);
    const pm = msgs.filter(isPatchMsg), key = m => m.subarray(5, 23).join(",");
    const stored = new Set(pm.filter(m => m[2] === 0x30).map(key));
    const patches = [];
    let copies = 0;
    for (const m of pm) {
        if (m[2] === 0x31 && stored.has(key(m))) { copies++; continue; }   /* (the panel's sound, a patch of the file) */
        patches.push({ p: Uint8Array.from(m.subarray(5, 23)), name: "" });
    }
    /* (the DX7's messages are the DX7 converter's: not counted here when it takes them) */
    const other = msgs.filter(m => !isPatchMsg(m) && !(o && o.dx7 && m[1] === 0x43)).length;
    const notes = [];
    if (copies) notes.push(copies + (copies === 1 ? " PANEL COPY" : " PANEL COPIES") + " LEFT OUT");
    if (other) notes.push(plural(other, "OTHER MESSAGE") + " SKIPPED");
    if (outside) notes.push(plural(outside, "BYTE") + " OUTSIDE MESSAGES SKIPPED");
    return { patches, cartName: "", notes };
}

/* ---- .106: the Juno-106 Librarian's library, big-endian; a value stored times K[(i + 9n) mod 16] ---- */
const MAGIC_106 = [0x21, 0x6A, 0x31, 0x30, 0x36, 0x5C];      /* !j106\ */
const K106 = [71, 124, 6, 50, 46, 50, 32, 104, 34, 68, 40, 103, 61, 43, 49, 45];
const is106 = b => b.length >= 6 && MAGIC_106.every((c, i) => b[i] === c);
function read106(b) {
    const bad = () => new Error("NOT A JUNO-106 LIBRARY THIS PAGE READS");
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength), utf8 = new TextDecoder("utf-8");
    let at = 6;
    const need = n => { if (at + n > b.length) throw bad(); const a = at; at += n; return a; };
    const u32 = () => dv.getUint32(need(4), false);
    const str = () => { const n = u32(), a = need(n); return utf8.decode(b.subarray(a, a + n)); };
    str(); u32(); u32();                                        /* "Library", 3, 1 */
    const lib = str();
    u32();
    const count = u32();
    need(8); u32(); u32();                                      /* a time, 0, a check value */
    const patches = [];
    for (let n = 0; n < count; n++) {
        const name = str();
        str(); need(8);                                         /* its id, a time */
        const p = new Uint8Array(18), a = need(40);
        for (let i = 0; i < 18; i++) {
            const v = dv.getUint16(a + 2 * i, false), k = K106[(i + 9 * n) % 16];
            if (v % k || v / k > 127) throw bad();
            p[i] = v / k;
        }
        u32();                                                  /* its check value (not read) */
        patches.push({ p, name });
    }
    return { patches, cartName: lib, notes: [] };
}

/* ---- .pat: a line a patch, 18 two-digit hex numbers, a comma, the name ---- */
const PAT_LINE = /^\s*((?:[0-9A-Fa-f]{2}\s+){17}[0-9A-Fa-f]{2})\s*,(.*)$/;
function patLines(b) {
    if (b.length > (1 << 20)) return null;
    for (const v of b) if (v < 0x09 || (v > 0x0D && v < 0x20)) return null;   /* (text only) */
    if (b[0] === 0xEF && b[1] === 0xBB && b[2] === 0xBF) b = b.subarray(3);   /* (a UTF-8 mark at its start) */
    const lines = new TextDecoder("windows-1252").decode(b).split(/\r?\n|\r/).filter(l => l.trim());
    if (!lines.length) return null;
    const out = [];
    for (const l of lines) {
        const m = PAT_LINE.exec(l);
        if (!m) return null;
        const p = Uint8Array.from(m[1].trim().split(/\s+/).map(h => parseInt(h, 16)));
        if (p.some(v => v > 127)) return null;
        out.push({ p, name: m[2] });
    }
    return out;
}

function kind(b) {
    if (is106(b)) return "106";
    if (hasSyx(b)) return "syx";
    if (patLines(b)) return "pat";
    return null;
}
function read(b, name, o) {
    const k = o && o.kind || kind(b);                   /* (the kind drops.js found, else found here) */
    if (k === "106") return read106(b);
    if (k === "syx") return readSyx(b, o);
    if (k === "pat") return { patches: patLines(b), cartName: "", notes: [] };
    throw new Error("NOT A FILE THIS PAGE READS");
}

/* ---- cartridges: 128 slots each, in order; a nameless patch PATCH and its slot's number ---- */
function cartRecs(patches, base, cartName, names) {
    const name0 = R.cleanName(cartName || "", 12) || R.cleanName(String(base || ""), 12) || "JUNO-106";
    const n = Math.max(1, Math.ceil(patches.length / 128)), recs = [];
    for (let c = 0; c < n; c++) {
        const part = patches.slice(128 * c, 128 * c + 128);
        const slots = part.map((x, i) => x ? slotOf(x.p, patchName(x.name) || noName(i)) : null);
        const nm = names && names[c] ? names[c] : n > 1 ? R.cleanName(name0.slice(0, 9).replace(/ +$/, "") + " " + (c + 1), 12) : name0;
        recs.push({ kind: K.JUNO, num: c, sub: 0, ver: R.VERSION[K.JUNO], payload: R.packCart(K.JUNO, { name: nm, slots }) });
    }
    return recs;
}

/* a file into records, for drops.js */
function recsOf(b, name, o) {
    const r = read(b, name, o), base = String(name || "").replace(/\.[^.]*$/, "");
    if (!r.patches.length) throw new Error("NO JUNO-106 PATCH IN IT");
    return { recs: cartRecs(r.patches, base, r.cartName), warnings: r.notes };
}

/* ---- a Juno-60 patch as the closest Juno-106 one (juno60_tables.js: made by editor/tools/juno60/) ---- */
/* the Juno-60's tape: sliders in its order VCF ENV, VCF LFO, VCF KYBD, VCA LEVEL, ENV A, R, D, S, VCF FREQ, DCO NOISE,
 * DCO SUB, VCF RES, DCO PWM, LFO RATE, DCO LFO, LFO DELAY; sw0 bit 0 ENV POLARITY, bits 1-2 PWM SOURCE (LFO, MAN, ENV),
 * bits 3-4 HPF (122 Hz, FLAT, 269 Hz, 571 Hz), bit 5 VCA MODE (0 GATE), bit 7 LFO TRIGGER; sw1 bit 0 CHORUS I, bit 1
 * PULSE, bit 2 SAW, bit 3 SUB on, bit 4 CHORUS II, bit 5 CHORUS off */
function from60(s, sw0, sw1) {
    const T = ED.juno60;
    if (!T) throw new Error("THE JUNO-60 CONVERSION IS NOT IN THIS PAGE");
    return T.convert({ vcf_env: s[0], vcf_lfo: s[1], vcf_kybd: s[2], vca_level: s[3], env_a: s[4], env_r: s[5], env_d: s[6],
        env_s: s[7], vcf_freq: s[8], dco_noise: s[9], dco_sub: s[10], vcf_res: s[11], dco_pwm: s[12], lfo_rate: s[13], dco_lfo: s[14],
        lfo_delay: s[15], env_polarity: sw0 & 1, pwm_source: (sw0 >> 1) & 3, hpf: (sw0 >> 3) & 3, vca_gate: (sw0 >> 5) & 1 ? 0 : 1,
        lfo_trigger: (sw0 >> 7) & 1, chorus1: sw1 & 1, pulse: (sw1 >> 1) & 1, saw: (sw1 >> 2) & 1, sub_on: (sw1 >> 3) & 1,
        chorus2: (sw1 >> 4) & 1, chorus_off: (sw1 >> 5) & 1 });
}

/* ---- tapes (juno_tape.js's readings) ---- */
/* a Juno-106 tape's 16 bytes: the sliders in the low 7 bits, sw1's bits 0-6 the top bits of bytes 0-6, sw2's 0-4 those
 * of bytes 8-12 (byte 7's top bit has no place in a patch) */
function from106tape(b) {
    const p = new Uint8Array(18);
    for (let i = 0; i < 16; i++) p[i] = b[i] & 127;
    for (let i = 0; i < 7; i++) p[16] |= (b[i] >> 7) << i;
    for (let i = 0; i < 5; i++) p[17] |= (b[8 + i] >> 7) << i;
    return p;
}
const j60Name = k => ((k >> 3) + 1) + "-" + ((k & 7) + 1);      /* (a Juno-60 patch by bank and number: 1-1 ... 7-8) */
/* Juno-60 patches in a list, three or more in a row as a span: 1-6, 2-1 TO 2-8 */
function j60List(ks) {
    const out = [];
    for (let i = 0; i < ks.length;) {
        let j = i;
        while (j + 1 < ks.length && ks[j + 1] === ks[j] + 1) j++;
        if (j - i >= 2) out.push(j60Name(ks[i]) + " TO " + j60Name(ks[j]));
        else for (let x = i; x <= j; x++) out.push(j60Name(ks[x]));
        i = j + 1;
    }
    return out.join(", ");
}
/* what a Juno-60 dump lacks, in words: 3-4 DAMAGED; 5-1 TO 7-8 NOT ON THE TAPE (or the take) */
function j60Lacks(d, where) {
    const w = [];
    if (d.damaged.length) w.push(j60List(d.damaged) + " DAMAGED");
    if (d.missing && d.missing.length) w.push(j60List(d.missing) + " NOT ON THE " + where);
    return w.join("; ");
}
/* what a tape holds, as patches in its order (a Juno-60 dump 56 slots, a damaged patch's left empty), and the notes */
function tapePatches(res, where) {
    const patches = [], notes = [];
    let pwm = 0;
    res.dumps.forEach((d, di) => {
        if (res.machine === 106) { for (const b of d.patches) patches.push(b ? { p: from106tape(b), name: "" } : null); return; }
        for (const q of d.patches) {
            if (!q) { patches.push(null); continue; }
            const c = from60(q.subarray(0, 16), q[16], q[17]);
            if (c.pwmEnv) pwm++;
            patches.push({ p: c.p, name: "" });
        }
        const lacks = j60Lacks(d, where || "TAPE");
        if (lacks) notes.push((res.dumps.length > 1 ? "DUMP " + (di + 1) + ": " : "") + lacks + ": LEFT EMPTY");
    });
    if (pwm) notes.push(pwm + " WITH PWM BY ENV: THE CLOSEST KEPT");
    return { patches, notes };
}
/* (names: each cartridge's own, given by the recorder's naming rule; else the file's, numbered past 128) */
function tapeRecs(res, base, cartName, where, names) {
    const t = tapePatches(res, where);
    return { recs: cartRecs(t.patches, base, cartName, names), warnings: t.notes };
}
/* why a tape did not read (its first dump that did not): a dropped file's words, or a take's */
const WHY = { checksum: "THE TAPE'S CHECKSUM IS WRONG", ended: "THE TAPE ENDS BEFORE ITS DUMP DOES", nopatch: "NO PATCH ON THE TAPE READ WHOLE" };
const WHY_TAKE = { checksum: "THE CHECKSUM IS WRONG", ended: "THE TAKE ENDS BEFORE THE DUMP DOES", nopatch: "NO PATCH READ WHOLE" };
function tapeWhy(res, take) {
    const i = res.dumps.findIndex(d => !d.ok);
    return i < 0 ? "" : (res.dumps.length > 1 ? "DUMP " + (i + 1) + ": " : "") + (take ? WHY_TAKE : WHY)[res.dumps[i].why];
}
const machineName = m => m === 106 ? "JUNO-106" : "JUNO-60";
/* what a tape that read holds, in a line: JUNO-106 TAPE: 64 PATCHES, CHECKSUM RIGHT; JUNO-60 TAPE: 55 OF 56 PATCHES (3-4
 * DAMAGED), CONVERTED TO JUNO-106; JUNO-106 TAPE: 2 DUMPS, 128 PATCHES, CHECKSUMS RIGHT (where: TAPE, or TAKE for the
 * recorder's, in what a Juno-60's lacks) */
function tapeSays(res, where) {
    const n = res.dumps.length, two = n > 1 ? n + " DUMPS, " : "";
    if (res.machine === 106) return "JUNO-106 TAPE: " + two + 64 * n + " PATCHES, CHECKSUM" + (n > 1 ? "S" : "") + " RIGHT";
    const all = 56 * n, good = res.dumps.reduce((a, d) => a + d.patches.filter(Boolean).length, 0);
    const lacks = res.dumps.map((d, i) => { const l = j60Lacks(d, where || "TAPE"); return l ? (n > 1 ? "DUMP " + (i + 1) + ": " : "") + l : ""; }).filter(Boolean);
    return "JUNO-60 TAPE: " + two + (good < all ? good + " OF " + all : all) + " PATCHES" + (lacks.length ? " (" + lacks.join("; ") + ")" : "") + ", CONVERTED TO JUNO-106";
}

ED.conv.juno = { slotOf, bytesOf, kind, read, cartRecs, recsOf, from60, from106tape, tapePatches, tapeRecs, tapeWhy, tapeSays,
    machineName, patchName, K106, MAGIC_106 };
})(globalThis.ED = globalThis.ED || {});
