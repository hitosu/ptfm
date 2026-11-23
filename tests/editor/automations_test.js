/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* AUTOMATIONS in the editor (specs/editor.md; the coding specs/files/automations.md): the slots the firmware's own test
 * records (fw/tests/auto_vectors.json) decoded to their events and coded back byte for byte; every time class at its
 * ends, a knob's clicks either way, presses and releases of every control, by hand to the bit; the parts split as the
 * firmware splits them (whole events, at most 4,080 bytes a part, checked against a coder of the test's own), a slot of
 * 16 full parts and the event past them refused, an event its fields cannot hold refused; input that does not read
 * whole; the words; what one pass of the FM-1's cancels; the item (one of each on the FM-1 panel, any number on the
 * TABLE, an empty one there its default, slots counted by their part 0, a disk and back, its pending changes); an
 * AUTOMATIONS file of several slots and parts written to the virtual FM-1 and read back, a slot gone not named as
 * removed, a BACKUP of a slot changed on the FM-1 taken whole.
 *
 *   node tests/editor/automations_test.js [WASM] */
"use strict";
const fs = require("fs"), path = require("path");
const { ED, S, ROOT, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND, M = ED.model, F = ED.fm1;
const eq = (a, b) => a.length === b.length && Array.prototype.every.call(a, (v, i) => v === b[i]);
const hex = b => Buffer.from(b).toString("hex");
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const press = (t, id) => ({ t, k: "press", id }), release = (t, id) => ({ t, k: "release", id }), knob = (t, id, c) => ({ t, k: "knob", id, c });
const slotOf = (screen, events) => R.unpackAutoSlot(R.packAutoSlot(screen, events));

/* ---- the firmware's vectors: decoded to their events, coded back to their bytes ---- */
const VEC = path.join(ROOT, "fw", "tests", "auto_vectors.json");
if (fs.existsSync(VEC)) {
    const vs = JSON.parse(fs.readFileSync(VEC, "utf8")).vectors || [];
    check(vs.length > 0, "the firmware's vectors: " + vs.length);
    vs.forEach((v, n) => {
        /* (a vector gives its events, or their count: a slot of several parts, its bytes the firmware's split) */
        const want = v.events ? v.events.map(e => e.k === "knob" ? knob(e.t, e.id, e.c) : { t: e.t, k: e.k, id: e.id }) : null;
        const parts = (v.parts || [v.hex]).map(x => new Uint8Array(Buffer.from(x, "hex")));
        const got = R.unpackAutoSlot(parts), count = want ? want.length : v.events_n;
        check(!got.bad && got.screen === v.screen && got.events.length === count && (!want || same(got.events, want)) && got.parts === parts.length,
            "vector " + n + ": its " + count + " events decoded" + (want ? "" : " (the count given)") + ", read whole across " + parts.length + " part" +
            (parts.length > 1 ? "s" : "") + " (" + parts.map(p => p.length).join(", ") + " bytes), screen " + v.screen + (got.bad ? " (" + got.bad + ")" : ""));
        const back = R.packAutoSlot(v.screen, want || got.events);
        check(back.length === parts.length && back.every((p, i) => eq(p, parts[i])),
            "vector " + n + ": coded back to its " + parts.length + " part" + (parts.length > 1 ? "s" : "") + " byte for byte, split where the firmware split them" +
            (parts.length === 1 ? " (" + hex(back[0]) + ")" : ""));
        /* (as the file's records: slot 5's) */
        const file = R.unpackAuto(parts.map((payload, k) => ({ sub: 5 * 16 + k, ver: 1, payload })));
        check(file.filter(Boolean).length === 1 && file[5] && file[5].events.length === count && !file[5].bad, "vector " + n + ": read as the file's slot 05");
    });
    check(vs.some(v => (v.parts || []).length > 1), "the firmware's vectors hold a slot of several parts: " + vs.map(v => (v.parts || [v.hex]).length).join(", ") + " parts");
} else console.log("(fw/tests/auto_vectors.json not there: the firmware's vectors not checked)");

/* ---- by hand, to the bit ---- */
{
    /* KEY A#3 (key 5) DOWN at 0: 0 (0-31 ms) + 5 bits 0, then 0 (pressed) + 6 bits 5 from bit 7: bits 7 and 9 set, so
     * 0x80 0x02 after the screen and the count */
    const p = R.packAutoSlot(3, [press(0, 5)]);
    check(p.length === 1 && hex(p[0]) === "0301008002", "KEY A#3 DOWN at 0 on INSTRUMENTS: 03 0100 80 02 (" + hex(p[0]) + ")");
    /* the most a gap holds, a knob's -1: 1111 + 24 ones, 11, 010, 1111 = 37 bits */
    const q = R.packAutoSlot(0, [knob(R.AUTO_GAP_MAX, 2, -1)]);
    check(hex(q[0]) === "000100ffffffbf1e", "K1 -1 after " + R.autoTime(R.AUTO_GAP_MAX) + ": 1111 + 24 bits, 11 010 1111 (" + hex(q[0]) + ")");
    check(R.AUTO_GAP_MAX === 16847135 && R.autoTime(R.AUTO_GAP_MAX) === "280:47.135", "the longest gap: 16,847,135 ms, 280:47.135");
    /* SEL UP (a release: 10 + 6 bits 30) at 40 ms: 10 + 8 bits 8, then 10 + 011110 */
    const r = R.packAutoSlot(1, [press(0, 30), release(40, 30)]);
    const b = new R.BitReader(r[0].subarray(3));
    const bits = [b.get(1), b.get(5), b.get(1), b.get(6), b.get(2), b.get(8), b.get(2), b.get(6)];
    check(same(bits, [0, 0, 0, 30, 1, 8, 1, 30]) && r[0].length === 3 + Math.ceil((13 + 18) / 8), "SEL DOWN at 0, SEL UP 40 ms after: their bits as specs/files/automations.md (" + bits.join(",") + ")");
}

/* ---- every time class at its ends, knobs both ways, every control ---- */
{
    const gaps = [0, 1, 31, 32, 33, 287, 288, 4383, 4384, 69919, 69920, 69921, 1e6, R.AUTO_GAP_MAX];
    const bitsOf = d => d < 32 ? 6 : d < 288 ? 10 : d < 4384 ? 15 : d < 69920 ? 20 : 28;
    let t = 0;
    const ev = gaps.map((d, i) => { t += d; return i % 3 === 0 ? press(t, i) : i % 3 === 1 ? release(t, i) : knob(t, i % 7, (i % 15) - 8 || 7); });
    const p = R.packAutoSlot(9, ev), back = R.unpackAutoSlot(p);
    const want = 3 + Math.ceil(ev.reduce((n, e, i) => n + bitsOf(gaps[i]) + (e.k === "press" ? 7 : e.k === "release" ? 8 : 9), 0) / 8);
    check(p.length === 1 && p[0].length === want && same(back.events, ev) && back.screen === 9 && !back.bad,
        "every time class at its ends (0-31 6 bits, -287 10, -4,383 15, -69,919 20, on 28): " + ev.length + " events round trip, " + p[0].length + " bytes");
    const clicks = [-8, -7, -6, -5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 6, 7], kn = [];
    for (let id = 0; id < 7; id++) for (const c of clicks) kn.push(knob(kn.length * 3, id, c));
    check(same(slotOf(6, kn).events, kn), "each knob's clicks -8 to +7 (never 0), both ways: " + kn.length + " turns round trip");
    const all = [];
    for (let id = 0; id < 41; id++) all.push(press(all.length * 50, id), release(all.length * 50 + 25, id));
    check(same(slotOf(2, all).events, all), "every key (0-26) and button (27-40) pressed and let go: round trip");
    /* a stream across bytes: an odd number of bits in each event, the next one from the bit after */
    const odd = [press(0, 1), knob(5, 3, -8), release(300, 1), knob(5000, 6, 7), press(80000, 40)];
    check(same(slotOf(4, odd).events, odd), "events across byte boundaries in every class read back");
}

/* ---- the parts: whole events, at most 4,080 bytes, split as the firmware splits them ---- */
{
    /* the test's own coder: a part is full when an event of the most bits (37) might not fit (the firmware's rule) */
    const tbits = d => d < 32 ? [1, 0, 5, d] : d < 288 ? [2, 1, 8, d - 32] : d < 4384 ? [3, 3, 12, d - 288] : d < 69920 ? [4, 7, 16, d - 4384] : [4, 15, 24, d - 69920];
    const mine = (screen, ev) => {
        const parts = [];
        let w = null, bits = 0, n = 0, last = 0;
        const open = () => { w = new R.BitWriter(new Uint8Array(4080)); bits = 0; n = 0; };
        const close = () => { const hb = parts.length ? 2 : 3, nb = Math.ceil(bits / 8), o = new Uint8Array(hb + nb);
            if (!parts.length) o[0] = screen; o[hb - 2] = n & 255; o[hb - 1] = n >> 8; o.set(w.b.subarray(0, nb), hb); parts.push(o); };
        open();
        for (const e of ev) {
            if ((parts.length ? 2 : 3) + Math.floor((bits + 44) / 8) > 4080) { close(); open(); }
            const [pb, pv, vb, v] = tbits(e.t - last);
            last = e.t;
            w.put(pv, pb); w.put(v, vb); bits += pb + vb;
            if (e.k === "knob") { w.put(3, 2); w.put(e.id, 3); w.put(e.c & 15, 4); bits += 9; }
            else if (e.k === "press") { w.put(0, 1); w.put(e.id, 6); bits += 7; }
            else { w.put(1, 2); w.put(e.id, 6); bits += 8; }
            n++;
        }
        if (n) close();
        return parts;
    };
    let t = 0;
    const ev = [];
    for (let i = 0; i < 9000; i++) {
        const d = [0, 12, 40, 300, 5000][i % 5] + (i % 7);
        t += d;
        ev.push(i % 4 === 3 ? knob(t, i % 7, i % 2 ? 3 : -5) : i % 2 ? release(t, i % 41) : press(t, i % 41));
    }
    const p = R.packAutoSlot(7, ev), q = mine(7, ev);
    const counts = p.map((x, k) => x[k ? 0 : 1] | x[k ? 1 : 2] << 8);
    check(p.length > 2 && p.every(x => x.length <= 4080) && counts.reduce((a, b) => a + b, 0) === ev.length,
        "9,000 events in " + p.length + " parts of at most 4,080 bytes (" + p.map(x => x.length).join(", ") + "), each its count (" + counts.join(", ") + ")");
    check(p.length === q.length && p.every((x, i) => eq(x, q[i])), "... byte for byte as the test's own coder splits and codes them");
    const back = R.unpackAuto(p.map((payload, k) => ({ sub: 2 * 16 + k, ver: 1, payload })));
    check(back[2] && same(back[2].events, ev) && back[2].screen === 7 && back[2].parts === p.length && !back[2].bad, "... read back across the parts, the times counting on");
    /* 16 full parts: the most a slot holds; the next event refused */
    const fill = [];
    for (;;) {
        fill.push(knob(0, 2, 1));
        if (mine(0, fill).length > 16) { fill.pop(); break; }
    }
    const full = R.packAutoSlot(0, fill);
    let refused = null;
    try { R.packAutoSlot(0, fill.concat([knob(0, 2, 1)])); } catch (e) { refused = e.message; }
    check(full.length === 16 && full.every(x => x.length <= 4080) && same(R.unpackAutoSlot(full).events, fill),
        "a slot of 16 full parts: " + fill.length + " turns, the parts " + full[0].length + " / " + full[15].length + " bytes");
    check(refused === R.AUTO_FULL, "one event more: refused, " + refused);
    let order = null, far = null;
    try { R.packAutoSlot(0, [press(100, 0), release(50, 0)]); } catch (e) { order = e.message; }
    try { R.packAutoSlot(0, [press(R.AUTO_GAP_MAX + 1, 0)]); } catch (e) { far = e.message; }
    check(/ORDER/.test(order || "") && /AT MOST 280:47.135/.test(far || ""), "times out of order and a gap past the most refused (" + order + "; " + far + ")");
    const refuse = ev => { try { R.packAutoSlot(0, ev); return false; } catch (e) { return /CANNOT HOLD/.test(e.message); } };
    check([knob(0, 2, 8), knob(0, 2, -9), knob(0, 8, 1), knob(0, 2, 1.5), press(0, 64), release(0, -1), press(0.5, 3), { t: 0, k: "turn", id: 1 }, { t: 0, id: 1 }].every(e => refuse([e])) &&
        !refuse([knob(0, 7, 0), press(0, 63)]), "an event its fields cannot hold refused, nothing cut to fit (clicks past -8..+7, knob 8, id 64, a fraction of a ms, an unknown kind); knob 7 and 0 clicks as read kept");
    check(R.packAutoSlot(0, []).length === 0 && R.autoWithSlot([{ sub: 0x31, ver: 1, payload: new Uint8Array(3) }], 3, []).length === 0 &&
        R.packAuto([null, { screen: 1, events: [press(0, 0)] }]).map(r => r.sub).join() === "16", "no event: no part (an empty slot has none); a slot's records replaced alone");
}

/* ---- input that does not read whole ---- */
{
    const ok = R.packAutoSlot(1, [press(0, 0), release(100, 0), knob(200, 2, 3)])[0];
    const cut = ok.slice(0, ok.length - 1);
    const a = R.unpackAutoSlot([cut]);
    check(a.bad && /CUT SHORT: 2 OF ITS 3 EVENTS READ/.test(a.bad) && a.events.length === 2 && a.screen === 1, "a part cut short: the events that read (" + a.bad + ")");
    const many = ok.slice(); many[1] = 200;
    const b = R.unpackAutoSlot([many]);
    check(b.bad && b.events.length >= 3 && b.events.length < 200, "a count past the part's bytes: what reads, and why (" + b.events.length + " read; " + b.bad + ")");
    const c = R.unpackAutoSlot([new Uint8Array([4, 1])]);
    check(c.bad && c.screen === -1 && !c.events.length, "part 0 of 2 bytes: the FM-1 reads the slot as empty (" + c.bad + ")");
    const p2 = R.packAutoSlot(0, Array.from({ length: 3000 }, (_, i) => knob(i, 3, 1)));
    const d = R.unpackAutoSlot([p2[0], undefined, p2[1]]);
    check(d.bad && /PART 1 IS MISSING/.test(d.bad) && d.events.length === (p2[0][1] | p2[0][2] << 8), "a part missing before another: the FM-1 stops before it (" + d.bad + ")");
    const e = R.unpackAuto([{ sub: 0x41, ver: 1, payload: p2[1] }]);
    check(e[4] && /PART 0 IS MISSING/.test(e[4].bad) && !e[4].events.length && e.filter(Boolean).length === 1, "part 0 missing: the slot read as empty, its records kept (" + e[4].bad + ")");
    /* a part of no event between two: the firmware reads on into the next (as here) */
    const x0 = R.packAutoSlot(2, [press(0, 3)])[0], x2 = R.packAutoSlot(0, [press(0, 0)])[0].slice(1);   /* (a part after the first: no screen byte) */
    const z = R.unpackAutoSlot([x0, new Uint8Array([0, 0]), x2]);
    check(!z.bad && z.events.length === 2 && z.parts === 3, "a part of no event between two: read on into the next");
    /* numbers kept for later: read, kept byte for byte, skipped as the FM-1 skips them */
    const later = [press(0, 41), release(10, 63), knob(20, 7, 5), press(30, 3)];
    const lp = R.packAutoSlot(5, later), lb = R.unpackAutoSlot(lp);
    check(same(lb.events, later) && eq(R.packAutoSlot(5, lb.events)[0], lp[0]), "numbers kept for later (a press of 41, a release of 63, knob 7) read and coded back as they are");
    const sc = R.autoScan(later);
    check(same(sc.why.map(w => !!w), [true, true, true, false]) && same(sc.held, [3]), "... the FM-1 skips them (" + sc.why.filter(Boolean)[0] + "); KEY G#3 still down at the end");
    /* random bytes never throw, every event within its fields */
    let seed = 12345, threw = 0, wild = 0;
    const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 16;
    for (let n = 0; n < 2000; n++) {
        const len = rnd() % 64, b2 = new Uint8Array(len);
        for (let i = 0; i < len; i++) b2[i] = rnd() & 255;
        try {
            const s2 = R.unpackAutoSlot([b2, rnd() & 1 ? b2.slice(1) : undefined]);
            if (s2.events.some(e2 => e2.id > 63 || (e2.k === "knob" && (e2.id > 7 || e2.c < -8 || e2.c > 7)) || !(e2.t >= 0))) wild++;
        } catch (e2) { threw++; }
    }
    check(!threw && !wild, "2,000 parts of random bytes: none throws, every event within its fields (" + threw + " threw, " + wild + " wild)");
}

/* ---- the words ---- */
{
    const w = [press(0, 0), release(0, 26), press(0, 27), release(0, 40), press(0, 30), knob(0, 0, -3), knob(0, 2, 2), knob(0, 6, -8), knob(0, 1, 7)].map(R.autoEventText);
    check(same(w, ["KEY F-3 DOWN", "KEY G-5 UP", "OCT- DOWN", "REC UP", "SEL DOWN", "SELECT -3", "K1 +2", "PRESETS -8", "ALGORITHM +7"]), "the events in words: " + w.join(", "));
    check(R.autoEventText(press(0, 41)) === "KEPT FOR LATER 41 DOWN" && R.autoEventText(knob(0, 7, 2)) === "KEPT FOR LATER KNOB 7 +2", "numbers kept for later in words");
    check(R.autoTime(0) === "0:00.000" && R.autoTime(12345) === "0:12.345" && R.autoTime(3723004) === "62:03.004" && R.autoLength(42999) === "0:42" && R.autoLength(65000) === "1:05",
        "times: 0:00.000, 0:12.345, 62:03.004; lengths as the FM-1's grid: 0:42, 1:05");
    const pt = ["0:12.345", "12.345", "12", "1:05", "1:05.5", " 2:00.000 ", "1:75", "abc", "1:2:3", "-1", ""].map(R.autoParseTime);
    check(same(pt, [12345, 12345, 12000, 65000, 65500, 120000, null, null, null, null, null]), "times typed: M:SS.MMM or seconds (" + JSON.stringify(pt) + ")");
    const sc = R.autoScan([press(0, 5), press(1, 5), release(2, 6), release(3, 5), press(4, 30), knob(5, 2, 0)]);
    check(same(sc.why, [null, "DOWN ALREADY: NOTHING CHANGES", "NOT DOWN: THE FM-1 SKIPS IT", null, null, "NO CLICKS: NOTHING TURNS"]) && same(sc.held, [30]),
        "what the FM-1 skips, and what is still down at the end (SEL)");
    /* one pass of the FM-1's: a row and those after it at +0 come together, only where each control ends up passed on */
    const ps = R.autoScan([press(0, 5), release(0, 5), press(100, 6), knob(100, 2, 3), knob(100, 2, -3), release(200, 6), press(200, 6),
        press(300, 7), release(300, 7), press(300, 7), knob(400, 3, 7), knob(400, 3, 2), press(500, 8), release(600, 8), press(600, 30), release(600, 30)]);
    const C = R.AUTO_CANCELLED, wantPs = [C, C, null, C, C, C, C, C, C, null, null, null, null, null, C, C];
    check(same(ps.why, wantPs) && same(ps.held, [6, 7]),
        "one pass at +0: a press and its release, turns adding up to nothing, a release and a press: " + C + "; of three changes the last stands; " +
        "+7 and +2 of one turn stand; changes of different controls stand (" + ps.why.map(w => w ? "X" : ".").join("") + ")");
    check(same(R.AUTO_SCREENS, ["STATUS", "SONGS", "EDITOR", "INSTRUMENTS", "PLAYBACK", "VISUAL", "FX", "LOOPS", "DJ", "SETTINGS"]) && R.AUTO_WHERE === 0xFF,
        "the screens: HOME's round's places 0-9; another number, WHERE IT IS");
}

/* ---- the item: a system item, one of each on the FM-1 panel, any number on the TABLE; a disk and back ---- */
const fileRecs = (seedT, slots) => R.packAuto(Array.from({ length: 16 }, (_, i) => slots.includes(i) ? { screen: i % 10,
    events: Array.from({ length: 3 + i * 3 }, (_, k) => k % 3 === 2 ? knob(seedT + k * 90, k % 7, (k % 5) - 2 || 1) : k % 2 ? release(seedT + k * 90, i) : press(seedT + k * 90, i)) } : null))
    .map(r => Object.assign({ kind: K.AUTO, num: 0 }, r));
{
    check(K.AUTO === 0x32 && R.VERSION[K.AUTO] === 1 && M.SYSTEM.includes(K.AUTO), "kind 0x32, version 1, a system item");
    const a = M.fromRecords(fileRecs(0, [0, 3, 7]), { side: "L", src: "A" }).items, b = M.fromRecords(fileRecs(500, [1]), { side: "L", src: "B" }).items;
    check(a.length === 1 && a[0].recs.length === 3 && M.nameOf(a[0], a) === "AUTOMATIONS" && M.groupOf(a[0]) === "SYSTEM" && M.kindText(a[0], a) === "3 OF 16 SLOTS" &&
        !M.refs(a[0]).length && !ED.player.canPlay(a[0]), "one item of its records: AUTOMATIONS, SYSTEM, 3 OF 16 SLOTS, naming nothing, no preview (" + M.kindText(a[0], a) + ")");
    const st0 = { L: a.concat(b), R: [], base: [], fm1: { from: "empty" } };
    const mv = M.transfer(st0, "L", [a[0].id, b[0].id], {});
    const onR = mv.st.R.filter(i => i.kind === K.AUTO);
    check(mv.ok && onR.length === 1 && onR[0].num === 0 && eq(onR[0].recs[0].payload, a[0].recs[0].payload) && /KEEPS ONE OF EACH/.test(mv.msg),
        "two moved onto the FM-1 panel: it keeps one, the first (" + mv.msg + ")");
    const mv2 = M.transfer(mv.st, "L", [b[0].id], {});
    check(mv2.ok && mv2.replaced.length === 1 && mv2.st.R.filter(i => i.kind === K.AUTO).length === 1 && mv2.st.R.find(i => i.kind === K.AUTO).recs.length === 1,
        "another moved over it: it replaces the panel's, asked first (" + mv2.replaced.length + " to ask about)");
    const back = M.transfer(mv2.st, "R", [mv2.st.R.find(i => i.kind === K.AUTO).id], {});
    check(back.ok && back.st.L.filter(i => i.kind === K.AUTO).length === 2, "moved back to the TABLE (any number there: the two it had stay; the same one found there by hash, nothing copied: " + back.msg + ")");
    /* a disk: the records byte for byte */
    const disk = S.makeDisk(M.toRecords(mv.st.R));
    const read = M.fromRecords(S.parseData(disk).files, { side: "R" }).items;
    check(read.length === 1 && read[0].kind === K.AUTO && read[0].recs.length === 3 && read[0].recs.every((r, i) => eq(r.payload, onR[0].recs[i].payload) && r.sub === onR[0].recs[i].sub),
        "on a disk and back: its records byte for byte (subs " + read[0].recs.map(r => H(r.sub)).join(", ") + ")");
    /* the changes not written: an empty one is nothing the FM-1 holds (as its default); emptying the panel's removes it */
    const empty = { id: M.newId(), side: "R", kind: K.AUTO, num: 0, orig: 0, link: {}, recs: [] };
    check(M.pending({ R: [empty], base: [] }).count === 0, "an empty AUTOMATIONS on the FM-1 panel: no change to write");
    const pd = M.pending({ R: [Object.assign({}, empty, { id: onR[0].id })], base: onR });
    check(pd.count === 1 && pd.removed.length === 1, "the FM-1's emptied: one change, its records removed by WRITE");
    check(M.size(empty) === 0 && M.kindText(empty, [empty]) === "0 OF 16 SLOTS", "an empty one: no bytes, 0 OF 16 SLOTS");
    /* a slot without its part 0 is empty on the FM-1: not counted */
    const orphan = Object.assign({}, empty, { recs: [{ sub: 0x41, ver: 1, payload: new Uint8Array([1, 0, 0, 0]) }, { sub: 0x50, ver: 1, payload: R.packAutoSlot(2, [press(0, 1)])[0] }] });
    check(M.kindText(orphan, [orphan]) === "1 OF 16 SLOTS", "slots counted by their part 0, as the FM-1 counts them: " + M.kindText(orphan, [orphan]));
    /* an empty one moved onto the FM-1 panel: it replaces the panel's (asked), and the panel's is its default again */
    const e2 = Object.assign({}, empty, { id: M.newId(), side: "L", num: null, src: "NEW" });
    const mvE = M.transfer(Object.assign({}, mv.st, { L: mv.st.L.concat([e2]) }), "L", [e2.id], {});
    check(mvE.ok && mvE.replaced.length === 1 && !mvE.st.R.some(i => i.kind === K.AUTO), "an empty one moved over the panel's: asked, then the panel's DEFAULT (no item)");
}
function H(v) { return R.hex2(v); }

/* ---- READ and WRITE against the virtual FM-1: a file of several slots and parts written and read back ---- */
(async () => {
    const wasm = await WebAssembly.compile(fs.readFileSync(process.argv[2] || path.join(ROOT, "build", "editor", "wasm", "ptfm.wasm")));
    const fw = await new ED.Firmware(wasm).start(null);
    M.setBuiltins(fw.builtins());
    const ses = S.session(fw.transport(), { timeout: 20000, blockTimeout: 20000, endTimeout: 30000 });
    const big = [];
    for (let i = 0; i < 5000; i++) big.push(i % 2 ? release(i * 37, i % 27) : press(i * 37, i % 27));
    const parts = R.packAutoSlot(4, big);
    const recs = fileRecs(0, [0, 9]).filter(r => r.sub >> 4 !== 2).concat(parts.map((payload, k) => ({ kind: K.AUTO, num: 0, sub: 2 * 16 + k, ver: 1, payload })));
    const it = M.fromRecords(recs, { side: "L", src: "TEST" }).items[0];
    const mv = M.transfer({ L: [it], R: [], base: [], fm1: { from: "empty", lastSave: null } }, "L", [it.id], {});
    const lay = await ses.hello();
    check(mv.ok && parts.length >= 3 && lay.accepts.some(a => a.kind === K.AUTO && a.ver === 1),
        "a file of 3 slots, slot 02 in " + parts.length + " parts, on the FM-1 panel; the FM-1 takes kind 32 version 1");
    const w = await F.writeTo(ses, Object.assign({}, mv.st, { fm1: { from: "empty", lastSave: null } }), {});
    const rd = await F.readFrom(ses, { L: [], R: [], base: [], fm1: { from: "empty", lastSave: null } });
    const got = rd.items.find(i => i.kind === K.AUTO), want = M.toRecords(mv.st.R);
    const key = r => r.kind + "." + r.num + "." + r.sub;
    const gm = new Map(M.toRecords(rd.items).map(r => [key(r), r]));
    check(w.bad === 0 && got && got.recs.length === want.length && want.every(r => gm.has(key(r)) && eq(gm.get(key(r)).payload, r.payload)),
        "written and READ back byte for byte: " + (got ? got.recs.length : 0) + " records, " + (got ? M.kindText(got, rd.items) : ""));
    const slots = got ? R.unpackAuto(got.recs) : [];
    check(slots[2] && same(slots[2].events, big) && slots[2].screen === 4, "... slot 02's 5,000 events as they were");
    /* WRITE's list of removals: a slot gone is the item written with fewer records, not removed; the whole item gone, removed */
    const withRecs = (items, recs2) => items.map(i => i.kind === K.AUTO ? Object.assign({}, i, { recs: recs2 }) : i);
    const read = { L: [], R: rd.items, base: rd.items, fm1: rd.fm1 };
    const less = Object.assign({}, read, { R: withRecs(rd.items, R.autoWithSlot(got.recs, 9, [])) });
    const none = Object.assign({}, read, { R: rd.items.filter(i => i.kind !== K.AUTO) });
    const wl = F.planWrite(await ses.hello(), less), wg = F.planWrite(await ses.hello(), none);
    check(wl.plan.del.length === 1 && !F.removedNames(wl.plan, less).includes("AUTOMATIONS") && F.removedNames(wg.plan, none).includes("AUTOMATIONS"),
        "WRITE without slot 09: AUTOMATIONS written, not named as removed (" + JSON.stringify(F.removedNames(wl.plan, less)) + "); without the item: removed");
    /* BACKUP of a slot changed on the FM-1 since the READ (its part 1 alone), the panel no longer holding it: the slot backed
     * up whole, its parts 0 and 2 from what the READ saw, never part 1 alone */
    const n0 = parts[0][1] | parts[0][2] << 8, big2 = big.map((e, i) => i === n0 + 10 ? Object.assign({}, e, { id: (e.id + 1) % 27 }) : e);
    const parts2 = R.packAutoSlot(4, big2);
    check(parts2.length === parts.length && eq(parts2[0], parts[0]) && !eq(parts2[1], parts[1]) && eq(parts2[2], parts[2]), "(on the FM-1: slot 02's part 1 changed alone)");
    const changed = Object.assign({}, read, { R: withRecs(rd.items, R.autoWithSlot(got.recs, 2, parts2)) });
    const wc = await F.writeTo(ses, changed, {});
    let taken = null;
    const stale = Object.assign({}, read, { R: withRecs(rd.items, R.autoWithSlot(got.recs, 2, [])), fm1: Object.assign({}, rd.fm1) });
    const wb = await F.writeTo(ses, stale, { backup: async b => { taken = b; } });
    const bAuto = taken && taken.items.find(i => i.kind === K.AUTO && taken.ids.includes(i.id)), bs = bAuto ? R.unpackAuto(bAuto.recs) : [];
    check(wc.bad === 0 && wb.bad === 0 && bs[2] && !bs[2].bad && bs[2].parts === 3 && same(bs[2].events, big2) && bs[0] && bs[9] && !bs[0].bad,
        "BACKUP: the changed slot whole as the FM-1 held it (" + (bs[2] ? bs[2].parts + " parts, " + bs[2].events.length + " events" + (bs[2].bad ? ", " + bs[2].bad : "") : "none") + "), the other slots beside it");
    ses.close();
    done("automations");
})().catch(e => { console.log("FAIL: " + (e.stack || e)); process.exit(1); });
