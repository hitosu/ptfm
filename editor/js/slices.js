/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Cutting sounds (specs/editor.md, Recording and cutting sounds): the naming rule, the silence level, TRIM SILENCE,
 * slices found from a sound's level and from starts placed by hand, parts joined with a crossfade, a point carried
 * over a join, and a kit's records. No DOM: Node runs it as it is (tests/editor/slices_test.js).
 *   ED.slices.nameFor(name, taken) -> a new sample's name (the naming rule)
 *   mono(ch, a, b) -> Float32Array (the channels mixed); silence(x, a, b) -> its level (linear)
 *   trimSilence(x, rate) -> [start, end]
 *   detect(x, rate, a, b, sens) -> [{ a, b, silent }] covering a..b; fromStarts(x, rate, a, b, starts) -> the same
 *   join(ch, rate, spans) -> { ch, map(frame) } (each join a 5 ms crossfade; map: a frame of the sound to the joined)
 *   kitPattern(n) -> a FUSION pattern's cells for n slices; MAX_FRAMES, the longest a sample can be */
(function (ED) {
"use strict";
const WIN_S = 0.005, PRE_S = 0.005, QUIET_S = 0.1, MERGE_S = 0.03, FADE_S = 0.005, SILENCE_DB = 40;
const MAX_FRAMES = (1 << 20) - 1;                   /* a sample's length: 20 bits (specs/files/instruments.md) */

/* the naming rule: the name's digits at its end read off, then two digits (three past 99), the first not taken; a
 * name too long gives up its last letters to them; no name counts as SAMPLE */
function nameFor(name, taken) {
    const has = new Set([...(taken || [])].map(s => String(s).toUpperCase()));
    let base = String(name || "").toUpperCase().slice(0, 10).replace(/[0-9]+$/, "");
    if (!base.trim()) base = "SAMPLE";
    for (let n = 0; ; n++) {
        const d = String(n).padStart(2, "0"), nm = base.slice(0, 10 - d.length) + d;
        if (!has.has(nm)) return nm;
    }
}

function mono(ch, a, b) {
    a = a || 0; b = b == null ? ch[0].length : b;
    const out = new Float32Array(Math.max(0, b - a)), k = 1 / ch.length;
    for (const c of ch) for (let i = a; i < b; i++) out[i - a] += c[i] * k;
    return out;
}
/* the silence level: 40 dB under the highest peak of x[a..b) (linear) */
function silence(x, a, b) {
    let pk = 0;
    for (let i = a; i < b; i++) { const v = Math.abs(x[i]); if (v > pk) pk = v; }
    return pk * Math.pow(10, -SILENCE_DB / 20);
}
/* the level of each 5 ms window from a (its peak, linear) */
function windows(x, rate, a, b) {
    const w = Math.max(1, Math.round(rate * WIN_S)), n = Math.ceil((b - a) / w), lv = new Float32Array(n);
    for (let k = 0; k < n; k++) {
        let pk = 0;
        for (let i = a + k * w, e = Math.min(b, i + w); i < e; i++) { const v = Math.abs(x[i]); if (v > pk) pk = v; }
        lv[k] = pk;
    }
    return { w, lv };
}
const db = v => v > 0 ? 20 * Math.log10(v) : -300;

/* TRIM SILENCE: from 5 ms before the first sound over the silence level to 5 ms after the last */
function trimSilence(x, rate) {
    const n = x.length, sil = silence(x, 0, n), pre = Math.round(rate * PRE_S);
    if (!n || sil <= 0) return [0, n];
    let a = 0, b = n;
    while (a < n && Math.abs(x[a]) <= sil) a++;
    while (b > a && Math.abs(x[b - 1]) <= sil) b--;
    if (a >= b) return [0, n];
    return [Math.max(0, a - pre), Math.min(n, b + pre)];
}

/* the slices from their starts: each ends where its level drops under the silence level and stays under it for
 * 100 ms (at the drop), else at the next start; what lies between is silent, as is the part before the first start
 * when it stays under the silence level */
function fromStarts(x, rate, a, b, starts) {
    const sil = silence(x, a, b), { w, lv } = windows(x, rate, a, b), quiet = Math.ceil(QUIET_S / WIN_S);
    const s = [...new Set(starts.map(v => Math.max(a, Math.min(b - 1, Math.round(v)))))].sort((p, q) => p - q);
    const out = [];
    const quietFrom = (from, to) => {                   /* the first window from `from` that stays quiet 100 ms */
        const k0 = Math.floor((from - a) / w), k1 = Math.ceil((to - a) / w);
        for (let k = k0 + 1; k < k1; k++) {
            if (lv[k] > sil) continue;
            let j = k;
            while (j < k1 && j < k + quiet && lv[j] <= sil) j++;
            if (j >= k + quiet) return a + k * w;       /* (quiet only till the next start: it ends there) */
            k = j;
        }
        return to;
    };
    const allQuiet = (p, q) => { for (let i = p; i < q; i++) if (Math.abs(x[i]) > sil) return false; return true; };
    if (!s.length || s[0] > a) {
        const end = s.length ? s[0] : b;
        if (end > a) out.push({ a, b: end, silent: allQuiet(a, end) });
    }
    for (let i = 0; i < s.length; i++) {
        const next = i + 1 < s.length ? s[i + 1] : b, end = Math.min(next, quietFrom(s[i], next));
        out.push({ a: s[i], b: end, silent: false });
        if (end < next) out.push({ a: end, b: next, silent: true });
    }
    return out.filter(z => z.b > z.a);
}
/* DETECT SLICES: a slice starts 5 ms before the level jumps up by SENSITIVITY's jump (24 dB at 1, 3 dB at 10) over
 * the window before (or the one before that) and is over the silence level; starts closer than 30 ms are one */
function detect(x, rate, a, b, sens) {
    sens = Math.max(1, Math.min(10, sens || 5));
    const jump = 24 - (sens - 1) * 21 / 9, sil = silence(x, a, b), { w, lv } = windows(x, rate, a, b);
    const pre = Math.round(rate * PRE_S), merge = Math.round(rate * MERGE_S), starts = [];
    for (let k = 0; k < lv.length; k++) {
        if (lv[k] <= sil) continue;
        const before = Math.min(k > 0 ? lv[k - 1] : 0, k > 1 ? lv[k - 2] : k > 0 ? lv[k - 1] : 0);
        if (db(lv[k]) - db(before) < jump) continue;
        const at = Math.max(a, a + k * w - pre);
        if (!starts.length || at - starts[starts.length - 1] >= merge) starts.push(at);
    }
    return fromStarts(x, rate, a, b, starts);
}

/* the spans kept, joined end to start: each join a 5 ms crossfade (shorter where a part is), the channels kept */
function join(ch, rate, spans) {
    const F = Math.max(1, Math.round(rate * FADE_S)), parts = spans.filter(s => s[1] > s[0]);
    const fades = [];
    let len = 0;
    parts.forEach((s, i) => {
        const f = i ? Math.min(F, s[1] - s[0], parts[i - 1][1] - parts[i - 1][0]) : 0;
        fades.push(f);
        len += s[1] - s[0] - f;
    });
    const out = ch.map(() => new Float32Array(Math.max(0, len)));
    const at = [];                                      /* where each part starts in the joined sound */
    let o = 0;
    parts.forEach((s, i) => {
        const f = fades[i];
        o -= f;
        at.push(o);
        for (let c = 0; c < ch.length; c++) {
            const src = ch[c], dst = out[c];
            for (let j = 0; j < s[1] - s[0]; j++) {
                const v = src[s[0] + j];
                if (j < f) { const g = (j + 0.5) / f; dst[o + j] = dst[o + j] * (1 - g) + v * g; }
                else dst[o + j] = v;
            }
        }
        o += s[1] - s[0];
    });
    /* a frame of the sound to the joined one: inside a part, where it went; in a removed part, the next join */
    const map = v => {
        for (let i = 0; i < parts.length; i++) {
            const s = parts[i];
            if (v < s[0]) return Math.max(0, at[i] + fades[i]);
            if (v < s[1]) return at[i] + (v - s[0]);
        }
        return len;
    };
    return { ch: out, length: len, map };
}

/* a FUSION kit's pattern: 128 rows, one track; slice n on row 3C + n - 1, its instrument at C-4 with the sampler's
 * ENV R4 at 0, so each hit rings out after its key is let go (specs/editor.md, MAKE A KIT) */
function kitPattern(n, cart) {
    const R = ED.rec, cells = new Map();
    for (let i = 0; i < n; i++) cells.set(R.cellKey(0x3C + i, 0), { note: 60, ins: ((cart || 0) << 8) | i, vol: -1, cmds: [[0x4B, 0]] });
    return { tracks: 1, rows: 128, step: 0, cells };
}

ED.slices = { nameFor, mono, silence, trimSilence, detect, fromStarts, join, kitPattern, MAX_FRAMES, FADE_S, WIN_S };
})(globalThis.ED = globalThis.ED || {});
