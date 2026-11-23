/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Juno banks and tapes (specs/editor.md, Juno-106 banks; Juno tape recordings), on files and tapes the tests make
 * (juno_synth.js): a patch into a slot and back exactly; .syx (panel copies, 32 messages, gaps, other bytes, a DX7 dump
 * in the same file), .106 (the scrambling, more than 128 patches, a value that does not divide), .pat (and a .pat of
 * another kind); the names, the cartridges' names, the notes; tapes of both machines at several rates, levels and
 * amounts of noise, two dumps, damaged and cut copies, each read back exactly; the drops' question (CARTRIDGE, SAMPLE, a
 * sound that is no tape never asked) and a tape that does not read.
 *
 *   node tests/editor/juno_test.js */
"use strict";
const { ED, check, done } = require("./load.js");
const JS = require("./juno_synth.js");
const R = ED.rec, K = R.KIND, J = ED.conv.juno, T = ED.junoTape;
const same = (a, b) => a.length === b.length && Array.from(a).every((v, i) => v === b[i]);
const cartsOf = items => items.filter(i => i.kind === K.JUNO).map(i => R.unpackCart(K.JUNO, i.recs[0].payload));
const DX7 = items => items.filter(i => i.kind === K.DX7).map(i => R.unpackCart(K.DX7, i.recs[0].payload));

(async () => {
    const P = JS.patches106(200, 7);

    /* ---- a patch, both ways ---- */
    let exact = 0;
    for (const p of P) if (same(J.bytesOf(J.slotOf(p, "X")), p)) exact++;
    check(exact === P.length, "a Juno-106 patch into a slot and back, exactly: " + exact + " of " + P.length);
    const s0 = J.slotOf(Uint8Array.from([...new Array(16).fill(0), 0x07, 0x18]), "X");
    check(s0.range === 1 && s0.hpf === 0 && s0.chorus === 2, "RANGE with several set: 8'; HPF bits 11: HPF 0; CHORUS bits clear: II");
    const s1 = J.slotOf(Uint8Array.from([...new Array(16).fill(0), 0x60, 0x00]), "X");
    check(s1.range === 1 && s1.chorus === 0 && s1.hpf === 3, "RANGE none set: 8'; CHORUS off bit and I bit: off; HPF bits 00: HPF 3");
    const rec = R.packCart(K.JUNO, { name: "T", slots: P.slice(0, 128).map(p => J.slotOf(p, "N")) });
    const back = R.unpackCart(K.JUNO, rec).slots.map(s => J.bytesOf(s));
    check(back.every((p, i) => same(p, P[i])), "128 patches through the cartridge record and back, exactly");

    /* ---- .syx ---- */
    const sy = JS.syx([JS.msg31(P[0]), JS.msg30(P[0], 0), JS.msg30(P[1], 5), [0x12, 0x34], JS.msg32(0x10, 0x40), JS.msg30(P[2], 9),
        JS.msg31(P[3]), JS.msg30(P[4], 120, 3), JS.dx7Voice("DX VOICE"), [0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7]]);
    const d1 = await ED.drops.convert("Basses.syx", sy);
    const c1 = cartsOf(d1.items), x1 = DX7(d1.items);
    check(c1.length === 1 && c1[0].slots.length === 5 && [P[0], P[1], P[2], P[3], P[4]].every((p, i) => same(J.bytesOf(c1[0].slots[i]), p)),
        ".syx: the patches in file order, gaps closed, a panel message that copies none kept in its place, the copy left out");
    check(x1.length === 1 && x1[0].slots[0].name === "DX VOICE", ".syx: its DX7 voice dump a DX7 cartridge of its own");
    check(c1[0].name === "BASSES" && c1[0].slots.map(s => s.name).join() === "PATCH00,PATCH01,PATCH02,PATCH03,PATCH04",
        ".syx: the cartridge named after the file, the patches PATCH00 ...: " + c1[0].name + " " + c1[0].slots.map(s => s.name).join());
    check(d1.what === "2 CARTRIDGES" && d1.warnings.join("; ") === "1 PANEL COPY LEFT OUT; 2 OTHER MESSAGES SKIPPED; 2 BYTES OUTSIDE MESSAGES SKIPPED",
        ".syx: the status line's words: " + d1.what + " (" + d1.warnings.join("; ") + ")");
    const d1b = await ED.drops.convert("x.bin", JS.syx([JS.msg30(P[5], 0)]));
    check(cartsOf(d1b.items).length === 1, ".syx recognised by its content, whatever the extension");
    let err = "";
    try { await ED.drops.convert("empty.syx", JS.syx([JS.msg32(1, 2)])); } catch (e) { err = e.message; }
    check(/^NO DX7 VOICE DUMP .* OR JUNO-106 PATCH IN IT$/.test(err), ".syx with no patch: refused: " + err);
    const many = [];
    for (let i = 0; i < 130; i++) many.push(JS.msg30(P[i], i));
    const d1c = await ED.drops.convert("A Very Long Name.syx", JS.syx(many));
    const c1c = cartsOf(d1c.items);
    check(c1c.length === 2 && c1c[0].slots.length === 128 && c1c[1].slots.length === 2 && c1c[0].name === "A VERY LO 1" && c1c[1].name === "A VERY LO 2"
        && c1c[1].slots[1].name === "PATCH01" && same(J.bytesOf(c1c[1].slots[1]), P[129]),
        ".syx of 130: a second cartridge, both named as the DX7 import names them: " + c1c.map(c => c.name + ":" + c.slots.length).join(" "));

    /* ---- .106 ---- */
    const names = ["A31 Clock Chimes", "A-21 Kraftwerk Reso Bass", "b88 lower", "Plain", "", "!!!", "A99 Not A Label", "Ünïcødé pad"];
    const libList = P.slice(0, 140).map((p, i) => ({ name: names[i % names.length], p }));
    const d2 = await ED.drops.convert("whatever.106", JS.lib106("My Favourites", libList));
    const c2 = cartsOf(d2.items);
    check(c2.length === 2 && c2[0].slots.length === 128 && c2[1].slots.length === 12 && libList.every((x, i) => same(J.bytesOf(c2[i >> 7].slots[i & 127]), x.p)),
        ".106: every value through the scrambling exactly, 140 patches in two cartridges");
    check(c2[0].name === "MY FAVOUR 1" && c2[1].name === "MY FAVOUR 2", ".106: the library's name, cut to 9 and numbered: " + c2.map(c => c.name).join(", "));
    const nm = c2[0].slots.slice(0, 8).map(s => s.name);
    check(nm.join("|") === "CLOCK CHIM|KRAFTWERK|LOWER|PLAIN|PATCH04|PATCH05|A99 NOT A| N C D  PA",
        ".106: names, their slot labels dropped, as the DX7's are made, an empty one PATCH and its slot: " + nm.join("|"));
    const bad = JS.lib106("X", [{ name: "x", p: P[3] }]);
    bad[bad.length - 4 - 4 - 40 + 1] += 1;                   /* (a value that no longer divides) */
    err = "";
    try { await ED.drops.convert("bad.106", bad); } catch (e) { err = e.message; }
    check(err === "NOT A JUNO-106 LIBRARY THIS PAGE READS", ".106 with a value that does not divide: refused: " + err);

    /* ---- .pat ---- */
    const d3 = await ED.drops.convert("list.pat", JS.pat(P.slice(0, 20).map((p, i) => ({ name: i ? "Pat " + i : "A11 Brass", p }))));
    const c3 = cartsOf(d3.items);
    check(c3.length === 1 && c3[0].slots.length === 20 && P.slice(0, 20).every((p, i) => same(J.bytesOf(c3[0].slots[i]), p)) && c3[0].slots[0].name === "BRASS"
        && c3[0].name === "LIST", ".pat: every patch exactly, its names, the cartridge named after the file");
    const bom = new Uint8Array([0xEF, 0xBB, 0xBF, ...JS.pat([{ name: "Bom", p: P[7] }])]);
    const d3b = await ED.drops.convert("bom.pat", bom);
    check(cartsOf(d3b.items).length === 1 && same(J.bytesOf(cartsOf(d3b.items)[0].slots[0]), P[7]), ".pat with a UTF-8 mark at its start: read");
    const dxStray = JS.syx([JS.dx7Voice("ALONE"), [0xF0, 0x41, 0x30, 0x00, 0xF7]]);
    const d3c = await ED.drops.convert("dx.syx", dxStray);
    check(DX7(d3c.items).length === 1 && !cartsOf(d3c.items).length, "a DX7 dump with stray Juno bytes (no whole patch message): the DX7 cartridge alone");
    err = "";
    try { await ED.drops.convert("piano.pat", new TextEncoder().encode("GF1PATCH110\0ID#000002\0")); } catch (e) { err = e.message; }
    check(err === "NOT A FILE THIS PAGE READS", ".pat of another kind: refused as before: " + err);
    err = "";
    try { await ED.drops.convert("mixed.pat", new TextEncoder().encode(Array.from(P[0], v => v.toString(16).padStart(2, "0")).join(" ") + ",ok\nnot a patch line\n")); }
    catch (e) { err = e.message; }
    check(err === "NOT A FILE THIS PAGE READS", ".pat with a line of another form: refused: " + err);

    /* ---- Juno-106 tapes ---- */
    const A = P.slice(0, 64), B = P.slice(64, 128);
    const read = (x, rate, o) => T.read({ rate, ch: [x] }, o);
    const sameDump = (d, list) => d && d.patches.length === 64 && d.patches.every((b, i) => b && same(J.from106tape(b), list[i]));
    for (const [rate, level, noise, hp] of [[22050, 0.5, 0, 300], [44100, 0.9, 0.01, 1000], [48000, 0.02, 0.0005, 300], [96000, 0.5, 0.02, 50], [32000, 0.3, 0.05, 600]]) {
        const res = read(JS.tape106([A], { rate, level, noise, hp, seed: rate }), rate, { window: 20 });
        check(res && res.machine === 106 && res.ok && res.dumps.length === 1 && sameDump(res.dumps[0], A),
            `a Juno-106 tape at ${rate} Hz, level ${level}, noise ${noise}, the recording's high-pass ${hp} Hz: read back exactly`);
    }
    const two = read(JS.tape106([A, B], { rate: 44100, speed: 1.04 }), 44100);
    check(two && two.ok && two.dumps.length === 2 && sameDump(two.dumps[0], A) && sameDump(two.dumps[1], B), "a Juno-106 tape of both bank groups, 4 % fast: two dumps, exactly");
    const twoRecs = J.tapeRecs(two, "BOTH");
    const tc = R.unpackCart(K.JUNO, twoRecs.recs[0].payload);
    check(twoRecs.recs.length === 1 && tc.slots.length === 128 && tc.slots.every((s, i) => same(J.bytesOf(s), P[i])) && tc.slots[127].name === "PATCH7F",
        "both bank groups: one cartridge of 128, PATCH00-PATCH7F");
    check(J.tapeSays(two) === "JUNO-106 TAPE: 2 DUMPS, 128 PATCHES, CHECKSUMS RIGHT" && J.tapeSays(read(JS.tape106([A], {}), 44100)) === "JUNO-106 TAPE: 64 PATCHES, CHECKSUM RIGHT",
        "a Juno-106 tape's line: " + J.tapeSays(two));
    const dmg = read(JS.tape106([A], { damage: [{ dump: 0, byte: 300 }] }), 44100);
    check(dmg && !dmg.ok && dmg.why === "checksum" && J.tapeWhy(dmg) === "THE TAPE'S CHECKSUM IS WRONG", "a Juno-106 tape with a byte changed: its checksum wrong, not read");
    const cat = (...xs) => { const o = new Float32Array(xs.reduce((a, x) => a + x.length, 0)); let at = 0; for (const x of xs) { o.set(x, at); at += x.length; } return o; };
    const pop = new Float32Array(200).fill(0.95);
    const popped = read(cat(pop, JS.tape106([A], { level: 0.09 })), 44100, { window: 20 });
    check(popped && popped.ok && sameDump(popped.dumps[0], A), "a quiet Juno-106 tape after a loud click: found and read (the hysteresis lets the click go)");
    const quieter = read(cat(JS.tape106([A], { level: 0.9 }), JS.tape106([B], { level: 0.09, seed: 9 })), 44100);
    check(quieter && quieter.ok && quieter.dumps.length === 2 && sameDump(quieter.dumps[1], B), "a second dump 20 dB quieter than the first: both read");
    const burst = JS.tape106([A], { level: 0.5 }), mid = Math.floor(burst.length / 2);
    for (let i = 0; i < 132; i++) burst[mid + i] = Math.sin(2 * Math.PI * 3000 * i / 44100);   /* (3 ms of another sound in dump 1) */
    const rw = new T.Reader(44100);
    let wasWrong = false;
    for (const x of [burst, JS.tape106([B], { level: 0.5 })]) for (let a = 0; a < x.length; a += 512) { rw.push(x.subarray(a, a + 512)); if (rw.state().wrong) wasWrong = true; }
    rw.end();
    const rws = rw.state();
    check(wasWrong && !rws.wrong && rws.dumps === 2 && rws.count === 64, "bytes coming in wrong in one dump: said while it comes, not in the next dump: " + JSON.stringify(rws));
    const cut = read(JS.tape106([A], { cut: 0.6 }), 44100);
    check(cut && !cut.ok && cut.why === "ended" && J.tapeWhy(cut, true) === "THE TAKE ENDS BEFORE THE DUMP DOES", "a Juno-106 tape cut short: it ends early, not read");

    /* ---- Juno-60 tapes ---- */
    const Q = JS.patches60(112, 9), QA = Q.slice(0, 56), QB = Q.slice(56);
    const same60 = (q, x) => q && same(q.subarray(0, 16), x.s) && q[16] === x.sw0 && q[17] === x.sw1;
    for (const [rate, level, noise] of [[22050, 0.5, 0], [44100, 0.8, 0.02], [48000, 0.01, 0.0003], [16000, 0.4, 0.05]]) {
        const res = read(JS.tape60([QA], { rate, level, noise, seed: rate + 1 }), rate, { window: 20 });
        check(res && res.machine === 60 && res.ok && res.dumps[0].patches.every((q, i) => same60(q, QA[i])) && !res.dumps[0].damaged.length,
            `a Juno-60 tape at ${rate} Hz, level ${level}, noise ${noise}: every patch read back exactly`);
    }
    const two60 = read(JS.tape60([QA, QB], { rate: 44100, speed: 0.97 }), 44100);
    check(two60 && two60.ok && two60.dumps.length === 2 && two60.dumps[1].patches.every((q, i) => same60(q, QB[i])), "a Juno-60 tape of two dumps, 3 % slow: both, exactly");
    /* (record 5: its sliders; record 57: patches 8-15's switches; in the second dump record 3) */
    const d60 = read(JS.tape60([QA, QB], { damage: [{ dump: 0, byte: 5 * 18 + 4 }, { dump: 0, byte: 57 * 18 + 2 }, { dump: 1, byte: 3 * 18 + 1 }] }), 44100);
    check(d60 && d60.ok && d60.dumps[0].damaged.join() === "5,8,9,10,11,12,13,14,15" && d60.dumps[1].damaged.join() === "3"
        && d60.dumps[0].patches[5] === null && same60(d60.dumps[0].patches[6], QA[6]),
        "a damaged Juno-60 tape: the patches whose records fail left out, the rest in their places: " + d60.dumps.map(d => d.damaged.join(",")).join(" / "));
    check(J.tapeSays(d60) === "JUNO-60 TAPE: 2 DUMPS, 102 OF 112 PATCHES (DUMP 1: 1-6, 2-1 TO 2-8 DAMAGED; DUMP 2: 1-4 DAMAGED), CONVERTED TO JUNO-106",
        "a damaged Juno-60 tape's line: " + J.tapeSays(d60));
    /* (a click late in the leader, read as a byte before the dump's 80: the dump still found) */
    const clicked = JS.tape60([QA], { rate: 22050 }), at = Math.round(2.75 * 22050);
    for (let i = 0; i < 70; i++) clicked[at + i] = 0.5 * Math.sin(2 * Math.PI * 1300 * i / 22050);
    const rc = read(clicked, 22050, { window: 20 });
    check(rc && rc.ok && rc.dumps[0].patches.every((q, i) => same60(q, QA[i])), "a Juno-60 tape with a click late in its leader: found and read whole");
    const none60 = read(JS.tape60([QA], { damage: [56, 57, 58, 59, 60, 61, 62].map(n => ({ dump: 0, byte: n * 18 + 3 })) }), 44100);
    check(none60 && !none60.ok && none60.why === "nopatch", "a Juno-60 tape whose switches' records all fail: no patch whole, not read");
    /* (cut after record 59: banks 1-4's switches in, 5-7's never sent; cut in the sliders: nothing whole, ended) */
    const cut60 = read(JS.tape60([QA], { cut: 60 / 64 }), 44100), short60 = read(JS.tape60([QA], { cut: 50 / 64 }), 44100);
    check(cut60 && cut60.ok && cut60.dumps[0].missing.join() === Array.from({ length: 24 }, (_, i) => 32 + i).join() && !cut60.dumps[0].damaged.length
        && J.tapeSays(cut60, "TAKE") === "JUNO-60 TAPE: 32 OF 56 PATCHES (5-1 TO 7-8 NOT ON THE TAKE), CONVERTED TO JUNO-106"
        && J.tapePatches(cut60).notes[0] === "5-1 TO 7-8 NOT ON THE TAPE: LEFT EMPTY",
        "a Juno-60 tape that ends early: the patches it never held named apart from damaged ones: " + J.tapeSays(cut60, "TAKE"));
    check(short60 && !short60.ok && short60.why === "ended" && J.tapeWhy(short60) === "THE TAPE ENDS BEFORE ITS DUMP DOES",
        "a Juno-60 tape that ends before any switches: not read, it ends early: " + (short60 && short60.why));

    /* ---- not tapes ---- */
    const sine = new Float32Array(44100 * 3).map((v, i) => 0.5 * Math.sin(2 * Math.PI * 440 * i / 44100));
    check(read(sine, 44100, { window: 20 }) === null, "a held sine: no tape");
    const late = new Float32Array(44100 * 25);
    late.set(JS.tape106([A], {}).subarray(0, 44100 * 4), 44100 * 21);
    check(read(late, 44100, { window: 20 }) === null && read(late, 44100) !== null, "a tape starting past the first 20 s: not looked for in a drop (but in a take)");

    /* ---- the drops' question ---- */
    const wavA = JS.wav(JS.tape106([A], { rate: 44100 }), 44100);
    let asked = [];
    const ask = ans => (n, m) => { asked.push(n + ":" + m); return Promise.resolve(ans); };
    const dc = await ED.drops.convert("Bank A.wav", wavA, { ask: ask("CARTRIDGE") });
    const cc = cartsOf(dc.items);
    check(asked.join() === "BANK A.WAV:106" && cc.length === 1 && cc[0].name === "BANK A" && cc[0].slots.every((s, i) => same(J.bytesOf(s), A[i])) && dc.what === "1 CARTRIDGE",
        "a tape dropped, CARTRIDGE: a Juno-106 cartridge named after the file: " + dc.what + " " + asked.join());
    asked = [];
    const ds = await ED.drops.convert("Bank A.wav", wavA, { ask: ask("SAMPLE") });
    check(asked.length === 1 && ds.items.length === 1 && ds.items[0].kind === K.SAMPLE, "a tape dropped, SAMPLE: a sample, as any sound");
    asked = [];
    const sw = JS.wav(sine, 44100);
    const dn = await ED.drops.convert("tone.wav", sw, { ask: ask("CARTRIDGE") });
    check(!asked.length && dn.items[0].kind === K.SAMPLE, "a sound that is no tape: never asked, a sample");
    const holds = sw.slice();
    holds.set(JS.msg30(P[3], 0), 1000);                     /* (a whole Juno-106 patch message in its audio data) */
    const dh = await ED.drops.convert("holds.wav", holds, { ask: ask("CARTRIDGE") });
    check(dh.items.length === 1 && dh.items[0].kind === K.SAMPLE, "a WAV whose data holds a Juno-106 message: a sound, a sample");
    err = null;
    try { await ED.drops.convert("Broken.wav", JS.wav(JS.tape106([A], { damage: [{ dump: 0, byte: 40 }] }), 44100), { ask: ask("CARTRIDGE") }); } catch (e) { err = e; }
    check(err && err.tape && err.message === "NOT READ: THE TAPE'S CHECKSUM IS WRONG", "a tape that does not read, CARTRIDGE: nothing, an error for the dialog: " + (err && err.message));
    const d60w = await ED.drops.convert("Sixty.wav", JS.wav(JS.tape60([QA], { rate: 22050 }), 22050), { ask: ask("SAMPLE") });
    check(d60w.items[0].kind === K.SAMPLE && asked[asked.length - 1] === "SIXTY.WAV:60", "a Juno-60 tape asked about as one");

    /* ---- the reader as a take comes in ---- */
    const live = new T.Reader(44100), x = JS.tape106([A], { rate: 44100 });
    let seen = [];
    for (let a = 0; a < x.length; a += 128) {
        live.push(x.subarray(a, a + 128));
        const s = live.state();
        if (s.machine && (!seen.length || seen[seen.length - 1] !== s.count)) seen.push(s.count);
    }
    check(seen[0] === 0 && seen[seen.length - 1] === 64 && seen.every((v, i) => !i || v >= seen[i - 1]), "a take read as it comes: its count from 0 to 64 (" + seen.length + " steps)");
    /* (record 2's sliders damaged, and record 60's switches: while the sliders come, 1 damaged; once in, 9 patches) */
    const live60 = new T.Reader(22050), y = JS.tape60([QA], { rate: 22050, damage: [{ dump: 0, byte: 2 * 18 + 5 }, { dump: 0, byte: 60 * 18 + 5 }] });
    let last = null, atSliders = null;
    for (let a = 0; a < y.length; a += 256) {
        live60.push(y.subarray(a, a + 256));
        last = live60.state();
        if (last.reading && last.count === 50) atSliders = last;
    }
    check(atSliders && atSliders.damaged === 1 && last.machine === 60 && last.count === 47 && last.damaged === 9,
        "a Juno-60 take as it comes: the sliders' records counted (50, 1 damaged), then the patches (47 of 56, 9 damaged): " + JSON.stringify([atSliders, last]));
    /* (a take of three Juno-106 dumps: two cartridges, each its own name) */
    const three = read(JS.tape106([A, B, A], { rate: 44100, gap: 0.5 }), 44100);
    const tr = J.tapeRecs(three, "TAKE03", "TAKE03", "TAKE", ["TAKE03", "TAKE04"]).recs.map(x => R.unpackCart(K.JUNO, x.payload));
    check(three && three.ok && tr.length === 2 && tr[0].name === "TAKE03" && tr[1].name === "TAKE04" && tr[1].slots.length === 64,
        "a take of three dumps: two cartridges, the names given: " + tr.map(c => c.name + ":" + c.slots.length).join(" "));
    check(T.read({ rate: 44100, ch: [JS.tape106([A], {})] }, { window: 20, detect: true }).machine === 106, "a tape looked for only: its machine, nothing read");
    done("juno");
})().catch(e => { console.log("FAIL: " + (e.stack || e)); process.exit(1); });
