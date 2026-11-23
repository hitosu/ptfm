// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
// The debug page's side (fw/web/figures.js, specs/debug.md) on two FIGURES answers the debug build gave on
// the PC (run_ui3.sh catches them): decoded as the device wrote them, gathered into two sections, the
// statistics and the report; a window on DJ, core 2's 37s (joined to their windows), and 36 RECORDS written by
// hand (only the FM-1 makes one).
//   node fw/tests/figures_test.js FIGURES1.syx FIGURES2.syx [FIGURES3.syx [F1-37.syx F3-37.syx [GHOSTS.syx GHOSTS-37.syx
//        [FILLED.syx FILLED-37.syx]]]]   (FILLED: a window whose ghost counts the debug build's test hook set)
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
ok(a.version === "TEST DEBUG" && !a.frozen && a.frozenNow && a.what === 1 && a.playing && a.song === 0xA0,
   "the first: version, frozen from now (not through it), song A0 playing");
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
    const r = new Uint8Array(FG.LEN2);
    const put = (o, v, n) => { for (let i = 0; i < n; i++) r[o + i] = (v >>> (8 * i)) & 255; };
    r[0] = 6; r[1] = a.win; r[2] = 1; r[3] = 0; r[4] = 1 | 8;   /* ON; SETTINGS' SECOND CORE on, the whole window */
    put(6, 41, 2); put(8, a.halves * 900, 4); put(12, 1300, 2); put(14, a.halves * 4, 2); put(16, 0, 2);
    put(18, a.halves * 2, 4); put(22, 14, 2); put(24, 2, 2); put(26, 1, 2); put(28, 1840, 2); put(30, 6144, 2);
    put(366, 8, 2); put(368, 9800, 2);                  /* (the main loop's stack's bottom: 8 B written, 9800 B paint) */
    put(40, a.halves * 300, 4); put(44, a.halves, 2); r[46] = 6; put(48, a.halves * 3, 4); put(52, 21, 2);   /* (its
                                                    * voice jobs: one a half, 6 voices at most in one, core 1's waits) */
    put(374, a.halves * 4 * 9, 4); put(378, a.halves * 4, 2); r[380] = 5; put(382, -24 & 0xFFFF, 2);   /* (the split:
                                                    * after place 9 on average over every block, 5 the lowest; the margin
                                                    * -24) */
    put(384, a.halves * 5, 4); put(388, a.halves >> 1, 2);   /* (5 voices a job; core 1 waited in half of them) */
    r[390] = 2; r[391] = 1; r[392] = 9; r[393] = 87;    /* (the keys: NORMAL, WAIT 1, the last press D-4 at 87, touch 612,
                                                         * driven 455 / 448, landing 455; 34 presses, 2 grazes, 1 late; the
                                                         * tick 4.12 us, the longest 9.30) */
    put(394, 612, 2); put(396, 455, 2); put(398, 448, 2); put(400, 455, 2); put(402, 34, 2); put(404, 2, 2); put(406, 1, 2);
    put(410, 412, 2); put(412, 930, 2);
    r[414] = 1 | 2 | 4;                                 /* (scanned, MASTER both ways, the battery: D-4 at rest 1008, the
                                                         * others 1020; MASTER 512 / 515; the battery 560, 7 readings; 12
                                                         * landings, the newest two D-4 at 87 (2 frames waited) and E-4) */
    for (let n = 0; n < 20; n++) r[415 + n] = n === 9 ? 252 : 255;
    put(435, 512, 2); put(437, 515, 2); put(439, 560, 2); put(441, 7, 2); r[443] = 12;
    r[444] = 9; r[445] = 87; r[446] = 2; put(447, 612, 2); put(449, 455, 2); put(451, 448, 2);
    r[453] = 11; r[454] = 64; r[455] = 15; put(456, 700, 2); put(458, 610, 2); put(460, 0, 2);
    r[462] = 255; r[471] = 255;
    const m = pack(0x37, r), c = FG.decode2(m);
    ok(c && c.win === a.win && c.state === 1 && c.setting && !c.fixed && !c.clk && c.whole && c.start10ns === 41 &&
       c.sum === a.halves * 900 && c.max === 1300 && c.blocks === a.halves * 4 && c.waitSum === a.halves * 2 &&
       c.waitMax === 14 && c.late1 === 2 && c.late2 === 1 && c.stackUsed === 1840 && c.stackSize === 6144 && !c.faultVec &&
       c.vUs === a.halves * 300 && c.vJobs === a.halves && c.vMost === 6 && c.vWaitSum === a.halves * 3 && c.vWaitMax === 21 &&
       c.kSum === a.halves * 36 && c.kBlocks === a.halves * 4 && c.kLow === 5 && c.margin === -24 && c.vSum === a.halves * 5 &&
       c.vWaited === a.halves >> 1 && c.keys.curve === 2 && c.keys.wait === 1 && c.keys.key === 9 && c.keys.vel === 87 &&
       c.keys.touch === 612 && c.keys.d1 === 455 && c.keys.d2 === 448 && c.keys.land === 455 && c.keys.presses === 34 &&
       c.keys.grazes === 2 && c.keys.lates === 1 && c.keys.lost === 0 && c.keys.tick10ns === 412 && c.keys.tickMost10ns === 930 &&
       c.keys.scanned && c.keys.masterBoth && c.keys.battRead && c.keys.rest[9] === 1008 && c.keys.rest[0] === 1020 &&
       c.keys.mSlot === 512 && c.keys.mDirect === 515 && c.keys.batt === 560 && c.keys.battN === 7 && c.keys.landed === 12 &&
       c.keys.ring[0].key === 9 && c.keys.ring[0].vel === 87 && c.keys.ring[0].wait === 2 && c.keys.ring[0].d2 === 448 &&
       c.keys.ring[1].key === 11 && c.keys.ring[1].wait === 15 && c.keys.ring[2].key === 255,
       "37 FIGURES 2 decoded (version 6: its voice jobs, the voices' split and the keys' readings, at rest and the last " +
       "landings, too)");
    {   /* the keys' readings in a run's report */
        const L = FG.sectionLines("KEYS", FG.stats([Object.assign({}, a, { c2: c })]));
        const has = (re) => L.some((l) => re.test(l));
        ok(has(/keys' velocity: NORMAL, WAIT 1/) && has(/the last press D-4: velocity 87, touch 612, driven 455 \/ 448, landing 455/) &&
           has(/presses 34, grazes 2, late 1, conversions lost 0/) && has(/the scan's tick 4\.12 us, the longest 9\.30 us/) &&
           FG.keysLines({ curve: 0, wait: 1, key: 255 }).join(" ").includes("keys' velocity: CONSTANT") &&
           FG.keysLines({ curve: 0, wait: 1, key: 255 }).join(" ").includes("no press read yet") &&
           has(/20 of 20 read, the lowest 1008 \(D-4\), all open/) && has(/MASTER from the slot 512, read directly 515; the battery 560/) &&
           has(/^presses read \(2\):$/) && has(/^  E-4: touch 700, driven 610, velocity 64, waited too long/) &&
           has(/^  D-4: touch 612, driven 455 \/ 448, velocity 87, waited 2\.2 ms$/),
           "the keys' readings in the report: KEYS VELOCITY and WAIT, the last press's readings, the counts, the scan's " +
           "tick, the keys at rest, MASTER, the battery, the presses read (the oldest first)");
    }
    {   /* the split's lines in a run's report; the fixed split (the debug switch, byte 4 bit 1) says so */
        const r2 = r.slice();
        r2[4] |= 2;
        const L = FG.sectionLines("SPLIT", FG.stats([Object.assign({}, a, { c2: c })]));
        const F = FG.sectionLines("FIXED", FG.stats([Object.assign({}, a, { c2: FG.decode2(pack(0x37, r2)) })]));
        const has = (x, re) => x.some((l) => re.test(l));
        ok(has(L, /the voices' split after place 9\.0 on average, 5 at the lowest/) &&
           has(L, /5\.0 voices a job, 6 at most/) && has(L, /at the join in (4\d|50)\.\d % of them/) &&
           has(L, /the split's margin at the end -24 units a block/) && !has(L, /FIXED/) &&
           has(F, /THE SPLIT FIXED AS BEFORE \(the debug switch\) in 1 windows/) &&
           L.every((l) => l.length < 100 && /^[\x20-\x7E]*$/.test(l)),
           "the voices' split in the report: where it stood, core 2's voices a job, core 1's waits at the join, the " +
           "margin; the fixed split named");
    }
    {   /* version 5 (the keys' 24 bytes, 414 B), 4 (no keys' bytes, 390 B), 3 (no split's bytes, 374 B) and 2 (no ghosts'
         * bytes, 56 B) are not this page's */
        const old = r.slice(0, 56), old3 = r.slice(0, 374), old4 = r.slice(0, 390), old5 = r.slice(0, 414);
        old[0] = 2;
        old3[0] = 3;
        old4[0] = 4;
        old5[0] = 5;
        ok(FG.decode2(pack(0x37, old)) === null && FG.decode2(pack(0x37, old3)) === null && FG.decode2(pack(0x37, old4)) === null &&
           FG.decode2(pack(0x37, old5)) === null && FG.LEN2 === 480 && c.gh &&
           !c.gh.stageSum && !FG.ghostStats([]) && !FG.ghostLines(FG.ghostStats([Object.assign({}, a, { c2: c })])).length,
           "37: a page reads only its own version (2-5 refused); version 6's ghosts, none here: no ghosts' lines");
    }
    ok(FG.decode2(FG.ask(0)) === null && FG.decode(m) === null && FG.decode2(new Uint8Array(fs.readFileSync(process.argv[2]))) === null &&
       FG.decode2(m.slice(0, 20)) === null, "37: another command, a 31 or a short one is none, and the other way round");
    ok(FG.secondAsk === undefined && FG.benchAsk === undefined && FG.bench === undefined,
       "no 38 SECOND CORE, no 32 BENCH: the debug build has neither");
    const a2 = Object.assign({}, a), b2 = Object.assign({}, b);
    ok(FG.join(a2, c) && a2.c2 === c && !FG.join(b2, c) && b2.c2 === null, "37 joined to its own window only (its number)");
    const s2 = FG.stats([a2]);
    {   /* a stack read as used whole: its bottom's words, then the paint above, in the report */
        const a4 = Object.assign({}, a2, { stackUsed: [a.stackSize[0], a.stackUsed[1]] });
        const l4 = FG.sectionLines("X", FG.stats([a4])), l2 = FG.sectionLines("X", s2);
        ok(c.stackIsland[0] === 8 && c.stackPaint[0] === 9800 && !c.stackIsland[1] &&
           l4.includes(`  the main loop's stack: 8 B written at its bottom, then 9800 B untouched: ${a.stackSize[0] - 9808} B deep above`) &&
           !l2.some((x) => x.includes("stack: ")) && l4.every((x) => x.length < 100),
           "37's stacks' bottoms: a main loop's stack read as used whole says what its bottom holds; one not whole says nothing");
    }
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
       has(/voices on core 2: \d+ voice jobs, 5\.0 voices a job, 6 at most, 5\.2 ?% of a half/) &&
       has(/core 1 waited for them 3\.0 us a job, the longest 21 us/),
       "a section's report: CPU 1, CPU 2, core 2's state, core 1's waits, the late halves by core, the voice jobs");
    ok(L.every((l) => l.length < 100 && /^[\x20-\x7E]*$/.test(l)), "core 2's lines: plain ASCII, under 100 characters");
    /* stopped after a fault, then off (SETTINGS' SECOND CORE OFF): each window's state, the fault record, the report says them */
    const stop = Object.assign({}, c, { state: 3, why: 4, faultVec: 9, faultPc: 0x01C71234, whole: false });
    const off = Object.assign({}, c, { state: 2, setting: false, whole: false, sum: 0, blocks: 0 });
    ok(FG.c2Text(stop) === "STOPPED: FAULT" && FG.c2Text(off) === "OFF" && FG.c2Text(c) === "ON",
       "core 2's state in words (DEVICE's)");
    const L2 = FG.sectionLines("FALLBACK", FG.stats([Object.assign({}, a, { c2: c }), Object.assign({}, a, { c2: stop }),
                                                      Object.assign({}, a, { c2: off })]));
    ok(L2.some((l) => /core 2 in 3 windows: ON 1, STOPPED: FAULT 1, OFF 1/.test(l)) &&
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

/* DJ (a third answer: a window on DJ, both decks playing, the left's FILTER, the crossfader's LFO on) */
const djFile = process.argv[4];
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

/* core 2's 37s as the debug build sent them (fourth and fifth files: the first window's; the DJ window's, its platform
   figures set by the test as the device's plat_dbg gives them) */
if (process.argv[5]) {
    const c = FG.decode2(new Uint8Array(fs.readFileSync(process.argv[5]))), a3 = Object.assign({}, a);
    ok(c && FG.join(a3, c) && a3.c2 === c && !c.blocks && !c.sum,
       "the first window's 37 from the debug build: joined to its 31, no core 2 (no blocks)");
}
if (process.argv[6] && djFile) {
    const d = FG.decode(new Uint8Array(fs.readFileSync(djFile))), c = FG.decode2(new Uint8Array(fs.readFileSync(process.argv[6])));
    ok(c && FG.join(d, c) && c.clk && c.whole && c.start10ns === 41 &&
       c.sum === 400000 && c.max === 1500 && c.blocks === 2700 && c.misreads === 2 && c.waitSum === 3000 &&
       c.waitMax === 25 && c.late1 === 1 && c.late2 === 3 && c.stackUsed === 2100 && c.stackSize === 6144 &&
       c.faultVec === 9 && c.faultPc === 0x01C71234, "the DJ window's 37 from the debug build: every field, joined");
    const L = FG.sectionLines("DJ, TWO CORES", FG.stats([d]));
    ok(L.some((l) => /^CPU 2 % a half: mean /.test(l)) && L.some((l) => /ITS FAULT: vector 9, pc 01C71234/.test(l)) &&
       L.some((l) => /WITH THE CLOCK-DIVIDER BIT/.test(l)) && L.some((l) => /late halves: core 1's 1, core 2's 3/.test(l)) &&
       L.every((l) => l.length < 100 && /^[\x20-\x7E]*$/.test(l)), "the DJ window's report with core 2's lines");
}

/* the ghosts (FIGURES 2's ghosts): a window of the debug build's, VOICES 8, twelve keys held on a JUNO-106 pad, the 4
   newest let go (seventh and eighth files): the 4 oldest made ghosts by takes, back into the release tails' engines */
if (process.argv[8]) {
    const d = FG.decode(new Uint8Array(fs.readFileSync(process.argv[7]))), c = FG.decode2(new Uint8Array(fs.readFileSync(process.argv[8])));
    ok(d && c && FG.join(d, c) && c.gh, "the ghosts' window: its 31 and its 37 (version 5) joined, its ghosts decoded");
    const g = c.gh, sumOf = (x) => x.reduce((t, v) => t + v, 0);
    ok(g.made[0] === 4 && sumOf(g.made) === 4 && !g.madeTb && g.backTail === 4 && !g.backFree && !g.tailLost &&
       !sumOf(g.gone.map((x) => x[0] + x[1])) && !g.deferred && !g.backQuiet && !sumOf(g.gave),
       "the ghosts' window: 4 made by takes, 4 back into tails' engines, none gone or given up");
    ok(!g.now.ghosts && g.now.keysOn === 8 && !g.now.songsOn && g.most === 4 && g.backMost >= 1 && g.backMost <= g.returns &&
       g.fwd[1].n >= 4 && g.starts[1].n === 4 && !g.starts[0].n && g.units > 0 && g.blocks > 0 && g.unitsMost <= g.units &&
       g.waitMost >= 1 && g.oldest > 0 && (g.oldestFl & 7) === 7 && g.budget > 0 && g.returns > 0 && g.readyW > 0 &&
       g.readyS > g.readyW && FG.ghostStats([d]).unit === FG.UNIT_US &&
       g.cost.every((x) => x[0] > 0 && x[1] > 0) && g.stageSum > 0 && g.stageMax > 0 && g.search.n >= 4,
       "the ghosts' window: none now, the keys' 8 in the engines; the JUNO-106's work and starts; the budget, the waits, " +
       "the oldest back (held, a key's, into a tail's); the numbers in force; the GHOSTS stage");
    const s = FG.stats([d]), L = FG.sectionLines("GHOSTS", s), has = (re) => L.some((l) => re.test(l));
    /* (an engine's line with its parts after a wrap joined again: a host timing's note can push its starts to the next) */
    const J = L.join("\n").replace(/\n  (?=starts )/g, ", ").split("\n"), hasJ = (re) => J.some((l) => re.test(l));
    const at = L.findIndex((l) => /^VOICES /.test(l));
    ok(s.ghostStage && s.ghostStage.inside === "VOICES" && s.ghostStage.worst === g.stageMax && s.ghosts.made[0] === 4 &&
       (!s.ghostStage.mean || /^  GHOSTS /.test(L[at + 1])), "the ghosts' stage: a part of VOICES, its row right under it");
    ok(has(/^ghosts: now 0 \(held 0, muted 0, the keys' 0, let go 0; returning 0\), the most 4 at once/) &&
       has(/^  made 4: taken 4$/) && has(/^  back 4: into free engines 0, into tails' 4, the most \d a block$/) &&
       has(/^  the budget \d+ a block, \d returns: spent \d+ in \d+ blocks? \(\d+ a block\), the most \d+/) &&
       hasJ(/^  JUNO-106: steps \d+ x [\d.]+ us (\(probe over the measure\) )?\(weight \d+ = [\d.]+\), starts 4 x [\d.]+ us/) &&
       has(/the oldest note back [\d.]+ s \(held, a key's, into a tail's\)/) && !has(/dropped|ended|given up/),
       "the ghosts' lines: now and the most, made, back, the budget, the JUNO-106's work, the oldest back");
    ok(L.every((l) => l.length < 100 && /^[\x20-\x7E]*$/.test(l)), "the ghosts' lines: plain ASCII, under 100 characters");
    const two = FG.ghostStats([d, d]);
    ok(two.made[0] === 8 && two.backTail === 8 && two.most === 4 && two.units === 2 * g.units, "two windows' ghosts: summed, the most");
    /* drops by kind and by held / let go, returns given up: their lines (the bytes as a fuller window would hold them) */
    const x = JSON.parse(JSON.stringify(g));
    x.gone[5] = [0, 3]; x.gone[7] = [2, 0]; x.gone[0] = [1, 1]; x.gave[0] = 1; x.gave[3] = 2; x.deferred = 1; x.tailLost = 1;
    x.fullNew = [4, 0, 1, 0];
    x.now = Object.assign({}, x.now, { ghosts: 2, held: 2, byEngine: [0, 2, 0, 0] });
    x.first[2] = { n: 1, us: 9 };
    x.probe = 0;                                        /* (the times as set: no pair's cost to take out) */
    const gs = FG.ghostStats([Object.assign({}, d, { c2: Object.assign({}, c, { gh: x }) })]), GL = FG.ghostLines(gs);
    ok(gs.heldDropped === 3 && GL.some((l) => /^  ended 2: cut 2 \(1 held\)$/.test(l)) &&
       GL.some((l) => /^  dropped 5: list full: let go 3, list full: songs' held 2 \(2 held\), at a deferred start 1$/.test(l)) &&
       GL.some((l) => /^  not kept, the list full 5: let go 4, songs' held 1$/.test(l)) &&
       GL.some((l) => /^  returns given up 3: a new note 1, a mute 2$/.test(l)) && GL.some((l) => /tails cut for returns that never came back 1/.test(l)) &&
       /^ghosts: now 2 \(held 2, .*\), by engine JUNO-106 2,?\s+the most/.test(GL.join(" ")) &&
       /  SAMPLER: .*first blocks back 1 x 9\.00 us \(its seek \d+ = [\d.]+\)/.test(GL.join(" ")),
       "the ghosts' lines: the ended and the dropped by kind (the held apart), the notes not kept, the returns given up, " +
       "the tails lost, the ghosts by engine, the sampler's seek weight");
    /* the times net of the timer pairs: each count's calls' pairs out (a walk spending nothing: its own and two for each
     * piece timed inside it); none measured: as measured */
    const y = JSON.parse(JSON.stringify(g));
    Object.assign(y, { probe: 0.25, idle: { n: 10, us: 40, inside: 5 }, search: { n: 8, us: 20 }, ends: { n: 4, us: 6 } });
    y.checks = [{ n: 4, us: 10 }, { n: 0, us: 0 }, { n: 0, us: 0 }];
    const yl = FG.ghostLines(FG.ghostStats([Object.assign({}, d, { c2: Object.assign({}, c, { gh: y }) })])).join(" ");
    const yp = FG.ghostPairs(y), inner = 4 + 8 + sumOf(y.starts.map((s) => s.n)) + sumOf(y.fwd.map((f) => f.n));
    ok(/walks spending nothing 10 x 3\.50 us(?! \(probe)/.test(yl) && /searches 8 x 2\.25 us(?! \(probe)/.test(yl) &&
       /checks 4 x 2\.25 us(?! \(probe)/.test(yl) && /the ends' walks 4 x 1\.25 us(?! \(probe)/.test(yl) &&
       /the times net of their timer pairs, 0\.250 us each/.test(yl) &&
       yp.stage === 4 + 10 + y.blocks + 2 * inner && yp.work === y.blocks + 2 * (inner - 5),
       "the ghosts' times net of their timer pairs: each call's, the walks' own and what they hold");
    /* a probe over what it measured (netted below nothing, or a stage's timer reads past its time): said where */
    const yo = Object.assign(JSON.parse(JSON.stringify(y)), { probe: 3, stageSum: 50 });
    const ol = FG.ghostLines(FG.ghostStats([Object.assign({}, d, { c2: Object.assign({}, c, { gh: yo }) })]));
    const oj = ol.join(" ");
    ok(/searches 8 x 0\.00 us \(probe over the measure\)/.test(oj) && /checks 4 x 0\.00 us \(probe over the measure\)/.test(oj) &&
       /walks spending nothing 10 x 0\.00 us \(probe over the measure\)/.test(oj) &&
       /GHOSTS 50 us \(its timer reads \d+: probe over the measure\)/.test(oj) && ol.every((l) => l.length < 100),
       "the ghosts' times: a probe over what it measured said in the line it spoils");
    const z = Object.assign(JSON.parse(JSON.stringify(y)), { probe: null });
    ok(/the times as measured/.test(FG.ghostLines(FG.ghostStats([Object.assign({}, d, { c2: Object.assign({}, c, { gh: z }) })]))
       .join(" ")), "the ghosts' times: no pair's cost measured, as measured");
    ok(/\(weight \d+ = [\d.]+/.test(yl) && FG.UNIT_US === 1 && Math.abs(FG.UNIT_FIRST_US - 15700 / 480 / 360) < 1e-12 &&
       FG.ghostLines(FG.ghostStats([Object.assign({}, d, { c2: Object.assign({}, c, { gh: Object.assign({}, y, { budget: 960 }) }) })]))
       .join(" ").includes(`(the unit ${FG.UNIT_US.toFixed(3)})`), "the budget's unit fixed, whatever the budget in force");
    /* the GHOSTS row: over the windows with their 37 only (one lost counts no ghost work) */
    const s1 = FG.stats([d]).ghostStage, s2 = FG.stats([d, Object.assign({}, d, { c2: null })]).ghostStage;
    ok(s1 && s2 && Math.abs(s2.mean - s1.mean) < 1e-9 && Math.abs(s2.share - s1.share) < 1e-9 && s2.p95 === s1.p95 &&
       s2.windows === 1, "the GHOSTS row: a window without its 37 left out of its mean, share and p95");
    console.log(L.filter((l) => /^(ghosts|  )/.test(l)).join("\n"));
}

/* the ghosts' layout's round trip (ninth and tenth files): every count of a window set to a value of its own by the
   debug build's test hook, in its fields' order, each decoded where it belongs (a kind added on one side only, or a
   field moved, shifts what follows) */
if (process.argv[10]) {
    const d = FG.decode(new Uint8Array(fs.readFileSync(process.argv[9]))), c = FG.decode2(new Uint8Array(fs.readFileSync(process.argv[10])));
    ok(d && c && FG.join(d, c) && c.gh, "the filled window: its 31 and its 37 joined, its ghosts decoded");
    let v = 1000;
    const n = () => v++;
    /* (a kind's counts as the hook sets them: by name, in the place map's order, each where the map puts that name) */
    const byName = (at, f) => { const a = []; for (const k of Object.keys(at)) a[at[k]] = f(); return a; };
    const want = {                                      /* (the hook's order: player.c, player_gh_fill) */
        made: byName(FG.GH_MADE_AT, n), madeTb: n(), backFree: n(), backTail: n(), tailLost: n(), backMute: n(),
        backMuted: n(), gone: byName(FG.GH_GONE_AT, () => [n(), n()]), deferred: n(), backQuiet: n(),
        gave: byName(FG.GH_GAVE_AT, n),
        fwd: byName(FG.GH_ENGINE_AT, () => ({ n: n(), steps: 100000 + n(), us: 200000 + n() })),
        starts: byName(FG.GH_ENGINE_AT, () => ({ n: n(), us: 300000 + n() })),
        first: byName(FG.GH_ENGINE_AT, () => ({ n: n(), us: 400000 + n() })),
        checks: byName(FG.GH_CHECK_AT, () => ({ n: n(), us: 500000 + n() })),
        search: { n: n(), us: 600000 + n() }, idleN: n(), idleUs: 600000 + n(), ends: { n: n(), us: 600000 + n() },
        units: 700000 + n(), unitsMost: n(), blocks: n(), stop4: n(), stopB: n(), waitMost: n(), wayLong: n(),
        oldest: 800000 + n(), oldestFl: 5, fullNew: byName(FG.GH_RANK_AT, n), look: { n: n(), us: 900000 + n() },
        probe: n() / 32, idleInside: n(), held: n(), loadCap: 4200, most: 21, wayMost: 2, backMost: 3,
    };
    const g = c && c.gh, got = g && Object.assign({}, g, { idleN: g.idle.n, idleUs: g.idle.us, idleInside: g.idle.inside });
    const bad = g ? Object.keys(want).filter((k) => JSON.stringify(want[k]) !== JSON.stringify(got[k])) : ["all"];
    ok(!bad.length && v === 1106 && g.checks.length === 3,
       `the filled window: every field decoded where it was put (${bad.join(", ") || "all right"})`);
    ok(FG.ghostLines(FG.ghostStats([d])).length > 0 && !g.now.ghosts, "the filled window: its lines made, no ghost now");
    /* each name's label where the page reads its count: the place maps against the label lists (one reordered alone
     * fails here; the device's enums against the maps fail above) */
    const LABEL = [
        [FG.GH_MADE_AT, FG.GH_MADE, { TAKEN: "taken", BORN: "started", BORN_MUTED: "started muted", MUTED: "muted",
                                      COUNT: "a smaller count", C2_BEGUN: "core 2's begun", C2_NEVER: "core 2's never begun" }],
        [FG.GH_GONE_AT, FG.GH_GONE, { CUT: "cut", KEEP: "let go 10 s ago", SELF: "ended by themselves",
                                      SELF_BACK: "found ended at their return", INST: "instruments gone",
                                      FULL_LET: "list full: let go", FULL_MUTED: "list full: muted",
                                      FULL_SONG: "list full: songs' held", FULL_KEY: "list full: keys' held",
                                      QUIET: "under hearing at their return", JREL: "JUNO-106 RELEASE over",
                                      OFF_INST: "their track off their instrument", FUS_CELL: "FUSION cell or track changed",
                                      FUS_OLD: "an older note's FUSION layer", SAMP_END: "sampler ended by its commands" }],
        [FG.GH_GAVE_AT, FG.GH_GAVE, { NOTE: "a new note", COUNT: "a smaller count", CMD: "a command", MUTE: "a mute" }],
        [FG.GH_CHECK_AT, FG.GH_CHECKS, { PLAIN: "others'", SAMP: "samplers'", FUS: "FUSION layers'" }],
        [FG.GH_RANK_AT, FG.GH_RANKS, { LET: "let go", MUTED: "muted", SONG: "songs' held", KEY: "keys' held" }],
        [FG.GH_ENGINE_AT, FG.GH_ENGINES, { DX7: "DX7", JUNO: "JUNO-106", SAMPLER: "SAMPLER", CHIP: "CHIP" }],
    ];
    const wrong = LABEL.flatMap(([at, list, names]) => Object.keys(names).filter((k) => list[at[k]] !== names[k] ||
        Object.keys(at).length !== list.length || Object.keys(names).length !== list.length));
    const ML = FG.ghostLines(FG.ghostStats([d])).join(" ");
    ok(!wrong.length && ML.includes(`, muted ${want.made[FG.GH_MADE_AT.MUTED]},`) &&
       ML.includes(`a smaller count ${want.made[FG.GH_MADE_AT.COUNT]},`) && ML.includes(`  SAMPLER: steps ` +
       `${want.fwd[FG.GH_ENGINE_AT.SAMPLER].steps} x`), `the kinds' labels where their names' counts land (${wrong.join(", ") ||
       "all right"})`);
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
        if (version >= 4) {                             /* (the start check: the firmware started, its check 21.5 ms) */
            r[120] = 1; r[121] = 0; r[122] = 0; r[123] = 1; put32(124, 21500); r[128] = 3; r[130] = 1; r[132] = 1;
        }
        return pack(0x36, r);
    };
    const m4 = records(4, 136, 0), d4 = FG.decodeRecords(m4), l4 = d4 && FG.recordsLines(d4);
    ok(d4 && d4.start && d4.start.did === 1 && d4.start.checkUs === 21500 && d4.start.starts === 3 && d4.start.p3 === 1 &&
       l4.some((l) => /it read the reset reasons 00000001/.test(l)) && d4.start.part &&
       d4.start.loader === 1 && l4.some((l) => /the start check: the firmware; its CRC-32 of the firmware 21\.5 ms; 3 starts/.test(l)) &&
       l4.some((l) => /the update loader in the staging: PT\/FM's, kept; the debug part: in/.test(l)),
       "36 RECORDS (version 4, 136 B): the start check's decision, its time, the loader kept, the debug part");
    {
        const r = FG.decodeRecords(records(4, 136, 0));
        r.start.did = 2; r.start.why = 1 | 16;
        ok(FG.recordsLines(r).some((l) => /the update loader \(UPDATE MODE\) \(OCT- held, no update loader kept\)/.test(l)),
           "RECORDS' lines: update mode and why");
    }
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
