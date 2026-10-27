// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
// The debug page's side (fw/web/figures.js, specs/debug.md) on two FIGURES answers the debug build gave on
// the PC (run_ui3.sh catches them): decoded as the device wrote them, gathered into two sections, the
// statistics and the report; a bench run's records, a window on DJ, core 2's 37s (joined to their windows), and
// 36 RECORDS written by hand (only the FM-1 makes one).
//   node fw/tests/figures_test.js FIGURES1.syx FIGURES2.syx [BENCH.syx [FIGURES3.syx [F1-37.syx F3-37.syx]]]
"use strict";
const fs = require("fs"), path = require("path");
const FG = require(path.join(__dirname, "../web/figures.js"));
let fails = 0, checks = 0;
const ok = (c, what) => { checks++; if (!c) { fails++; console.log("FAIL " + what); } };

/* a message as the FM-1 sends one: F0 7D 50 54 cmd, the bytes packed 7 in 8, F7 */
const pack = (cmd, r) => {
    const p = [];
    for (let i = 0; i < r.length; i += 7) {
        const n = Math.min(7, r.length - i);
        let top = 0;
        for (let j = 0; j < n; j++) top |= (r[i + j] >> 7) << j;
        p.push(top);
        for (let j = 0; j < n; j++) p.push(r[i + j] & 0x7F);
    }
    return Uint8Array.from([0xF0, 0x7D, 0x50, 0x54, cmd, ...p, 0xF7]);
};

const [a, b] = process.argv.slice(2, 4).map((p) => FG.decode(new Uint8Array(fs.readFileSync(p))));
ok(a && b, "both answers decode (FIGURES version 11)");
ok(FG.decode(Uint8Array.from([0xF0, 0x7D, 0x50, 0x54, 0x20, 0, 0xF7])) === null, "another command is not FIGURES");
ok(a.win === ((b.win - 1) & 255) && a.c2 === null, "the windows numbered one after another; core 2's not joined yet");
{   /* version 10 (one core, byte 17 spare) is not this page's */
    const raw = FG.unpack7(new Uint8Array(fs.readFileSync(process.argv[2])).subarray(5, -1));
    raw[0] = 10;
    ok(FG.decode(pack(0x31, raw)) === null && FG.decode(pack(0x31, (raw[0] = 11, raw))) !== null,
       "FIGURES: a page reads only its own version (10 refused, 11 read)");
}
ok(FG.ask(FG.FREEZE).join() === [0xF0, 0x7D, 0x50, 0x54, 0x30, 2, 0xF7].join(), "30 FIGURES? with FREEZE");
ok(a.version === "TEST DEBUG" && !a.frozen && a.frozenNow && a.what === 1 && a.playing && a.song === 0,
   "the first: version, frozen from now (not through it), song 00 playing");
ok(b.frozen && !b.frozenNow, "the second: frozen through it, thawed after it");
ok(a.halves > 300 && a.isrSum > 0 && a.stages.length === FG.STAGES.length &&
   a.stages[FG.STAGES.indexOf("VOICES")].name === "VOICES" && a.stages[FG.STAGES.indexOf("DJ LFOS")].sum === 0 &&
   a.stages[FG.STAGES.indexOf("MIX")].sum > 0, "halves, the interrupt, the stages (MIX measured)");
ok(a.stackSize[0] === 24320 && a.frames > 30 && b.frames <= 1, "the stacks; frames drawn, then frozen: none");

const S = FG.session();
S.add(a, 500);                                          /* (not recording: shown, not kept) */
ok(!S.all.length && S.last === a, "before RECORD: nothing kept, the latest shown");
S.record("PLAYING", 900);
S.add(a, 1000);
S.stop(1200);
S.add(b, 1500);                                         /* (stopped: not kept) */
S.record("FROZEN", 2000);
S.add(b, 3000);
S.add(Object.assign({}, b, { playing: false }), 4000);  /* (a stopped window: left out of the figures) */
ok(S.all.length === 3 && S.sections.length === 2 && S.of(0).length === 1 && S.of(1).length === 2 && S.sections[0].end === 1200,
   "RECORD .. STOP: two runs, only what was recorded kept, the first's end");
ok(!S.remove(1), "a run being recorded is not deleted");
ok(FG.stats(S.of(1)).stopped === 1 && FG.stats(S.of(1)).windows === 1, "a section's stopped windows left out, counted");
const s1 = FG.stats(S.of(0)), all = FG.stats(S.all);
ok(s1 && Math.abs(s1.cpu.mean - a.isrSum / (a.halves * a.halfUs) * 100) < 1e-9, "a section's CPU mean is its time over its budget");
ok(all.windows === 2 && all.cpu.worstUs === Math.max(a.isrMax, b.isrMax), "the session: two windows, the worst half");
const shares = all.stages.filter((s) => !s.inside).reduce((t, s) => t + s.share, 0) + all.other.share;
ok(Math.abs(shares - all.cpu.mean) < 1e-6 && all.cpuBoth === all.cpu.mean && all.cpu2 === null,
   "the stages' shares and the interrupt's own add up to the CPU (no core 2's figures: CPU 1 alone)");
ok(all.engines.some((e) => e.blocks > 0 && e.usBlock > 0), "a voice's cost by engine");
ok(FG.STAGES.indexOf("BLOCKS") === FG.STAGES.indexOf("CLOCK") + 1 && !FG.INSIDE.BLOCKS && a.usb.unsent === 0,
   "BLOCKS a stage of its own after CLOCK; every answer sent so far");
{   /* the voices' mean from the voice blocks the windows made (a look a window can miss voices sounding half a row) */
    const blocks = a.blocks.reduce((t, x) => t + x, 0), want = blocks * (64 / 44100 * 1e6) / (a.ms * 1000);
    ok(blocks > 0 && Math.abs(s1.voices.mean - want) < 1e-9 && s1.voices.mean > 0,
       "voices' mean: the voice blocks a section made, over its time");
}
{   /* the answers the FM-1 could not send: since power-on mod 256, counted between a list's first and last windows */
    const w = (n) => Object.assign({}, a, { usb: Object.assign({}, a.usb, { unsent: n }) });
    ok(FG.unsentIn([w(254), w(255), w(1)]) === 3 && FG.unsentIn([w(7)]) === 0 && FG.unsentIn([]) === 0,
       "the answers the FM-1 could not send between the first window and the last (past 255 too)");
    const ul = FG.sectionLines("LOST", FG.stats([w(5), w(7)]));
    ok(ul.includes("FIGURES: 2 windows the FM-1 could not send whole (USB: cut short or not sent)") &&
       !FG.sectionLines("SENT", FG.stats([w(5), w(5)])).some((l) => l.startsWith("FIGURES:")),
       "a section's report says how many windows the FM-1 could not send, when it lost some");
}
{   /* 37 FIGURES 2 (core 2's window, specs/debug.md, Core 2's bytes), as the FM-1 sends one after a's 31: written by
       hand here (the PC's platform has no core 2) */
    const r = new Uint8Array(56);
    const put = (o, v, n) => { for (let i = 0; i < n; i++) r[o + i] = (v >>> (8 * i)) & 255; };
    r[0] = 2; r[1] = a.win; r[2] = 1; r[3] = 0; r[4] = 1 | 8;   /* ON; SETTINGS' SECOND CORE on, the whole window */
    put(6, 41, 2); put(8, a.halves * 900, 4); put(12, 1300, 2); put(14, a.halves * 4, 2); put(16, 0, 2);
    put(18, a.halves * 2, 4); put(22, 14, 2); put(24, 2, 2); put(26, 1, 2); put(28, 1840, 2); put(30, 6144, 2);
    put(40, a.halves * 300, 4); put(44, a.halves, 2); r[46] = 4; put(48, a.halves * 3, 4); put(52, 21, 2);   /* (its
                                                    * voice jobs: one a half, 4 places at most, core 1's waits) */
    const m = pack(0x37, r), c = FG.decode2(m);
    ok(c && c.win === a.win && c.state === 1 && c.setting && !c.forced && !c.clk && c.whole && c.start10ns === 41 &&
       c.sum === a.halves * 900 && c.max === 1300 && c.blocks === a.halves * 4 && c.waitSum === a.halves * 2 &&
       c.waitMax === 14 && c.late1 === 2 && c.late2 === 1 && c.stackUsed === 1840 && c.stackSize === 6144 && !c.faultVec &&
       c.vUs === a.halves * 300 && c.vJobs === a.halves && c.vMost === 4 && c.vWaitSum === a.halves * 3 && c.vWaitMax === 21,
       "37 FIGURES 2 decoded (version 2: its voice jobs too)");
    ok(FG.decode2(FG.secondAsk(0)) === null && FG.decode(m) === null && FG.decode2(new Uint8Array(fs.readFileSync(process.argv[2]))) === null &&
       FG.decode2(m.slice(0, 20)) === null, "37: another command, a 31 or a short one is none, and the other way round");
    ok(FG.secondAsk(0).join() === [0xF0, 0x7D, 0x50, 0x54, 0x38, 0, 0xF7].join() && FG.secondAsk(1)[5] === 1,
       "38 SECOND CORE: 0 core 2 off, 1 as SETTINGS says");
    const a2 = Object.assign({}, a), b2 = Object.assign({}, b);
    ok(FG.join(a2, c) && a2.c2 === c && !FG.join(b2, c) && b2.c2 === null, "37 joined to its own window only (its number)");
    const s2 = FG.stats([a2]);
    ok(s2.cpu2 && Math.abs(s2.cpu2.mean - 900 / a.halfUs * 100) < 1e-9 && s2.cpu2.worstUs === 1300 &&
       Math.abs(s2.cpu.mean - a.isrSum / (a.halves * a.halfUs) * 100) < 1e-9, "CPU 1 from the interrupt, CPU 2 from 37");
    const sh = s2.stages.filter((x) => !x.inside).reduce((t, x) => t + x.share, 0) + s2.other.share;
    ok(Math.abs(sh - s2.cpuBoth) < 1e-6 && Math.abs(s2.cpuBoth - s2.cpu.mean - s2.cpu2.mean) < 1e-9,
       "the stages (both cores' work) and the rest add up to both cores' time");
    ok(s2.core2.late1 === 2 && s2.core2.late2 === 1 && s2.core2.blocks === a.halves * 4 && s2.core2.whole === 1 &&
       Math.abs(s2.core2.waitMean - 2) < 1e-9 && s2.core2.waitMax === 14, "core 2's section: its lates by core, blocks, waits");
    const L = FG.sectionLines("TWO CORES", s2);
    const has = (re) => L.some((l) => re.test(l));
    ok(has(/^CPU 1 % a half: mean /) && has(/^CPU 2 % a half: mean 15\.5 /) && has(/^core 2 in 1 windows: ON 1, blocks handed over /) &&
       has(/core 1 waited for it 2\.0 us a half, the longest 14 us/) && has(/late halves: core 1's 2, core 2's 1/) &&
       has(/its stack 1840 of 6144 B/) && has(/its start answered in 0\.41 us/) && !has(/FAULT/) &&
       has(/voices on core 2: \d+ voice jobs, 4 places at most, 5\.2 ?% of a half/) &&
       has(/core 1 waited for them 3\.0 us a job, the longest 21 us/),
       "a section's report: CPU 1, CPU 2, core 2's state, core 1's waits, the late halves by core, the voice jobs");
    ok(L.every((l) => l.length < 100 && /^[\x20-\x7E]*$/.test(l)), "core 2's lines: plain ASCII, under 100 characters");
    /* stopped after a fault, then off by 38: each window's state, the fault record, the report says them */
    const stop = Object.assign({}, c, { state: 3, why: 4, faultVec: 9, faultPc: 0x01C71234, whole: false });
    const off = Object.assign({}, c, { state: 2, forced: true, whole: false, sum: 0, blocks: 0 });
    ok(FG.c2Text(stop) === "STOPPED: FAULT" && FG.c2Text(off) === "OFF (38)" && FG.c2Text(c) === "ON",
       "core 2's state in words (DEVICE's)");
    const L2 = FG.sectionLines("FALLBACK", FG.stats([Object.assign({}, a, { c2: c }), Object.assign({}, a, { c2: stop }),
                                                      Object.assign({}, a, { c2: off })]));
    ok(L2.some((l) => /core 2 in 3 windows: ON 1, STOPPED: FAULT 1, OFF \(38\) 1/.test(l)) &&
       L2.some((l) => /ITS FAULT: vector 9, pc 01C71234/.test(l)) && L2.every((l) => l.length < 100),
       "a section's core 2 through a fallback: its states in order, its fault record");
    console.log(L.concat(L2).filter((l) => /^(CPU|core 2|  )/.test(l)).join("\n"));
}
const rep = S.report(Date.UTC(2026, 9, 12, 12, 0));
const lines = rep.trimEnd().split("\n");
ok(/^PT\/FM FIGURES  firmware TEST DEBUG  2026-10-12 12:00 UTC  3 windows$/.test(lines[0]), "the report's first line");
ok(rep.includes("== PLAYING") && rep.includes("== FROZEN") && rep.includes("1 stopped left out"), "a run each");
ok(!rep.includes("== ALL") && rep.includes("  MIX") && rep.includes("  J-CHORUS") === (all.stages[FG.STAGES.indexOf("J-CHORUS")].mean > 0),
   "no summary of the runs; VOICES' parts");
{
    const r0 = S.runReport(0, Date.UTC(2026, 9, 12, 12, 0));
    ok(r0.includes("== PLAYING") && !r0.includes("== FROZEN") && /  1 windows\n/.test(r0), "a run's own report (COPY on it)");
}
ok(all.stages[FG.STAGES.indexOf("CELLS")].mean > 0 && all.stages[FG.STAGES.indexOf("NOTES")].mean > 0 &&
   all.stages[FG.STAGES.indexOf("CELLS")].mean + all.stages[FG.STAGES.indexOf("NOTES")].mean <=
   all.stages[FG.STAGES.indexOf("REPLAY")].mean + 3 &&
   rep.includes("  CELLS") && rep.includes("  NOTES"), "REPLAY's parts: CELLS and NOTES measured, inside it");
ok(lines.every((l) => l.length < 100 && /^[\x20-\x7E]*$/.test(l)), "plain ASCII, lines under 100 characters");
console.log(rep);
S.stop(5000);
ok(S.remove(0) && S.sections.length === 1 && S.sections[0].name === "FROZEN" && S.all.length === 2 && S.of(0).length === 2,
   "DELETE on a run: it and its windows gone, the others kept");
S.reset();
ok(!S.all.length && !S.sections.length && !S.recording, "DELETE ALL drops every run");

/* the bench's records (a third file: every 33 BENCH RECORD of a run, one after another) */
const benchFile = process.argv[4];
if (benchFile) {
    const d = fs.readFileSync(benchFile), msgs = [];
    for (let i = 0, s = 0; i < d.length; i++) if (d[i] === 0xF7) { msgs.push(d.subarray(s, i + 1)); s = i + 1; }
    const recs = msgs.map((m) => FG.decodeBench(m));
    ok(recs.length > 12 && recs.every(Boolean), "bench: every record decodes");
    ok(FG.benchAsk(1).join() === [0xF0, 0x7D, 0x50, 0x54, 0x32, 1, 0xF7].join() && FG.decodeBench(msgs[0].slice(0, 6)) === null &&
       FG.decode(msgs[0]) === null && FG.decodeBench(new Uint8Array(fs.readFileSync(process.argv[2]))) === null,
       "bench: 32 BENCH to run it; a bench record is no FIGURES and the other way round");
    const B = FG.bench();
    recs.forEach((r) => B.add(r));
    const kinds = recs.map((r) => r.kind);
    ok(kinds[0] === "START" && kinds[kinds.length - 1] === "END" && B.end.status === 0 && !B.running() &&
       B.rows.length === B.start.total && B.rows.every((r, i) => r.n === i + 1) && B.rows.every((r) => r.kind === "CORE" || r.kind === "ALONE"),
       "bench: START, every piece numbered (THE CORE and ALONE only), END (done)");
    ok(B.rows.filter((r) => r.kind === "ALONE" && /^(DX7|JUNO|TB-303|SAMPLE) /.test(r.name)).every((r) => r.runs === 8 && r.unit === "block"),
       "bench: the engines alone, 8 warm runs a block each");
    const bl = B.lines();
    ok(bl[0].startsWith("== BENCH  (") && bl.some((l) => l.startsWith("THE CORE")) && bl.some((l) => l.startsWith("ALONE")) &&
       !bl.some((l) => l.startsWith("STEPS")) && bl.every((l) => l.length < 100 && /^[\x20-\x7E]*$/.test(l)),
       "bench: the report's parts (THE CORE, ALONE); ASCII, under 100");
    console.log(bl.slice(0, 40).join("\n"));
}

/* DJ (a fourth answer: a window on DJ, both decks playing, the left's FILTER, the crossfader's LFO on) */
const djFile = process.argv[5];
if (djFile) {
    const c = FG.decode(new Uint8Array(fs.readFileSync(djFile)));
    ok(c && c.what === 4 && c.dj.live && c.dj.decks.every((k) => k.playing && k.name && k.bpm10 > 0) && c.dj.rec.length === 136,
       "DJ: the decks in the player, both playing, by name and tempo; DJ's record");
    ok(!a.dj.live && FG.stats([a]).dj === null, "DJ: a song's window has no DJ part");
    const D = FG.session();
    D.record("DJ 2 DECKS", 0);
    D.add(c, 1000);
    const changed = Object.assign({}, c, { dj: Object.assign({}, c.dj, { rec: c.dj.rec.slice() }) });
    changed.dj.rec[5] = 20;                             /* the crossfader moved by hand */
    D.add(changed, 2000);
    const dl = D.report(Date.UTC(2026, 9, 12, 12, 0)).trimEnd().split("\n");
    const has = (re) => dl.some((l) => re.test(l));
    ok(has(/^DJ  \(its settings/) && has(/CROSSFADER SMOOTH at 20 of 32/) && has(/CROSSFADER LFO ON \(TRIANGLE, MIRROR, 1\/2\)/) &&
       has(/SYNC ON \(THE MAIN'S BPM\)/) && has(/REVERB ON \(SIZE 70, /) && has(/DELAY ON \(TIME /),
       "DJ: the setup, its LFOs and its effects listed");
    ok(has(new RegExp(`^   LEFT ${c.dj.rec[88].toString(16).toUpperCase().padStart(2, "0")} ${c.dj.decks[0].name}: TEMPO `)) &&
       has(/FILTER LP 10, REPEAT PATTERN OFF/) && has(/^   RIGHT [0-9A-F]{2} .*VOLUME/) && has(/^   LEFT  now: PLAYING at [0-9A-F]{2}:[0-9A-F]{2}, /) &&
       has(/^   RIGHT now: PLAYING/), "DJ: each deck's song, its settings (the left's filter) and what it is doing");
    ok(has(/changed during the section: CROSSFADER SMOOTH at \d+ of 32 -> SMOOTH at 20 of 32/),
       "DJ: a setting changed during the section, its values in order");
    ok(dl.every((l) => l.length < 100 && /^[\x20-\x7E]*$/.test(l)), "DJ: plain ASCII, lines under 100 characters");
    ok(c.usb.on && c.usb.r48 && c.usb.pkts === 4000 && c.usb.dry === 3 && c.usb.missed === 2 && c.usb.starts === 1 &&
       c.usb.fill === 130 && c.usb.target === 139 && !a.usb.on && FG.stats([a]).usb === null,
       "USB audio: its window decoded; a window without it has none");
    ok(has(/^USB audio: recording at 48 kHz in 2 of 2 windows; its buffer 130\.\.130 frames$/) &&
       has(/^  at a half's start \(aimed at 139\); packets 8000: short 6, missed 4, stalls 0, restarts 2$/),
       "USB audio: the report's lines");
    const usbAt = FG.STAGES.indexOf("USB AUDIO");
    ok(usbAt === FG.STAGES.indexOf("OUTPUT") + 1 && FG.INSIDE["USB AUDIO"] === "OUTPUT" && c.stages[usbAt].sum > 0 &&
       dl.some((l) => /^  USB AUDIO /.test(l)), "USB audio: its stage, a part of OUTPUT, in the table");
    console.log(dl.filter((l, i) => i < 16).join("\n"));
}

/* core 2's 37s as the debug build sent them (fifth and sixth files: the first window's; the DJ window's, its platform
   figures set by the test as the device's plat_dbg gives them) */
if (process.argv[6]) {
    const c = FG.decode2(new Uint8Array(fs.readFileSync(process.argv[6]))), a3 = Object.assign({}, a);
    ok(c && FG.join(a3, c) && a3.c2 === c && !c.forced && !c.blocks && !c.sum,
       "the first window's 37 from the debug build: joined to its 31, not off by 38, no core 2 (no blocks)");
}
if (process.argv[7] && djFile) {
    const d = FG.decode(new Uint8Array(fs.readFileSync(djFile))), c = FG.decode2(new Uint8Array(fs.readFileSync(process.argv[7])));
    ok(c && FG.join(d, c) && !c.forced && c.clk && c.whole && c.start10ns === 41 &&
       c.sum === 400000 && c.max === 1500 && c.blocks === 2700 && c.misreads === 2 && c.waitSum === 3000 &&
       c.waitMax === 25 && c.late1 === 1 && c.late2 === 3 && c.stackUsed === 2100 && c.stackSize === 6144 &&
       c.faultVec === 9 && c.faultPc === 0x01C71234, "the DJ window's 37 from the debug build: every field, joined");
    const L = FG.sectionLines("DJ, TWO CORES", FG.stats([d]));
    ok(L.some((l) => /^CPU 2 % a half: mean /.test(l)) && L.some((l) => /ITS FAULT: vector 9, pc 01C71234/.test(l)) &&
       L.some((l) => /WITH THE CLOCK-DIVIDER BIT/.test(l)) && L.some((l) => /late halves: core 1's 1, core 2's 3/.test(l)) &&
       L.every((l) => l.length < 100 && /^[\x20-\x7E]*$/.test(l)), "the DJ window's report with core 2's lines");
}

/* RESTARTS: a 36 RECORDS as the FM-1's debug build sends it (device/main.c prove_records; only the device makes
   one, so the bytes here are written by hand), packed 7 in 8 as every answer */
{
    const records = (version, len, c2) => {             /* (version 3: 120 B, the pointers the start read; 2: 108, the
                                                         * crash record's 18th word; 1: 104, 17) */
        const r = new Uint8Array(len);
        const put32 = (o, v) => { for (let i = 0; i < 4; i++) r[o + i] = (v >>> (8 * i)) & 255; };
        r[0] = version; r[1] = 2; r[2] = 4; r[3] = 1; r[4] = 0; r[5] = 0; r[6] = 0x40; r[7] = 0; r[8] = 1;
        r[9] = 1; r[10] = 1; r[11] = 1; r[12] = 1; r[13] = 1; r[14] = 0; r[15] = 0;
        put32(16, 0x20);
        const crash = [0x43525348, 3, 1, 0x02001234, 0x02005678, 8, 0, 0x01C7CC80, 0, 0, 1234, 0, 0, 0, 0, 0, 2];
        if (version >= 2) crash.push(c2);
        crash.forEach((v, i) => put32(20 + 4 * i, v));
        [0x01C78D00, 0x01C7CC00, 0x01C7CD00, 0x01C7FC00].forEach((v, i) => put32(20 + 4 * crash.length + 4 * i, v));
        if (version >= 3) [0x01C7CBF0, 0x01C7FC00, 0x01C7FC00].forEach((v, i) => put32(108 + 4 * i, v));   /* (sp = ssp) */
        return pack(0x36, r);
    };
    const m = records(3, 120, 1), d = FG.decodeRecords(m);
    ok(FG.proveAsk(3).join() === [0xF0, 0x7D, 0x50, 0x54, 0x35, 3, 0xF7].join() && FG.proveAsk(6)[5] === 6,
       "35 PROVE: the interrupts' stack; core 2 stuck (6)");
    ok(d && d.windows === 2 && d.step === 4 && d.bandChecked && d.bandChanged === 0 && d.pending === 1 &&
       d.first && d.first.crashKept && d.first.watchdog && d.first.band === 0 && d.crash && d.crash.count === 3 &&
       d.crash.stack === 2 && d.crash.c2 === 1 && d.stacks.mainLo === 0x01C78D00 && d.stacks.irqTop === 0x01C7FC00 &&
       d.read.usp === 0x01C7CBF0 && d.read.ssp === 0x01C7FC00 && d.read.sp === 0x01C7FC00,
       "36 RECORDS (version 3, 120 B) decoded: the crash record's 18th word, core 2 running at it, the pointers read");
    ok(FG.decodeRecords(m.slice(0, 20)) === null && FG.decode(m) === null && FG.decodeRecords(FG.proveAsk(0)) === null &&
       FG.decodeRecords(records(2, 108, 0)) === null, "36 RECORDS: short, another command or an older version: none");
    const rl = FG.recordsLines(d);
    ok(rl.some((l) => /one for each stack/.test(l)) && rl.some((l) => /STACK: THE INTERRUPTS'/.test(l)) &&
       rl.some((l) => /kept: the restart left it alone/.test(l)) && rl.some((l) => /core 2 at the crash: RUNNING/.test(l)) &&
       rl.some((l) => /usp 01C7CBF0, ssp 01C7FC00, sp 01C7FC00 \(sp is ssp\)/.test(l)) &&
       rl.every((l) => /^[\x20-\x7E]*$/.test(l)),
       "RECORDS' lines: the windows, the pointers read, the overflowed stack, the band, core 2 at the crash; plain ASCII");
    ok(FG.recordsLines(FG.decodeRecords(records(3, 120, 0))).some((l) => /core 2 at the crash: not running/.test(l)),
       "RECORDS' lines: a crash with core 2 not running");
}
console.log(fails ? `figures: ${fails} of ${checks} checks FAILED` : `figures: ${checks} checks passed`);
process.exit(fails ? 1 : 0);
