/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PLAY's LEDs while the loops play (specs/ui/transport.md#plays-two-leds): a visual metronome by LOOPS' METRONOME, on or
 * off: a quick flash a beat (100 ms), the bar's first red, the others green; between, both dark; none while they do not
 * play. On the harness's virtual clock with the worklet's code running (fw/tests/ui3_test.c checks the firmware's).
 *   node tests/playground/harness.js tests/playground/loops_metro.js */
"use strict";
module.exports = async t => {
    const { U, M, P, ok, leds } = t, B = U.B, LS = () => M.state.loops, G = 41;
    await t.sound();
    await t.sleep(100);
    P.pause();
    await t.sleep(100);
    await t.homeKey(22);
    U.edit(() => {
        const j = LS();
        j.list = []; j.rows = 64; j.tempo = 120; j.rpb = 4; j.countIn = "OFF";   /* (no loops: they play with none; a beat 500 ms) */
        j.fx.on.METRONOME = false;
        j.fx.knobs.METRONOME[0] = 29;                   /* (FOLLOW) */
        j.fx.knobs.METRONOME[1] = 4;
    });
    P.mutesChanged();
    await t.sleep(200);
    /* 1 a red flash, 2 a green one, 0 between, -1 none of them */
    const kind = () => { t.frame(); const g = leds[G], r = leds[B.PLAY]; return r === 1 && !g ? 1 : g === 1 && !r ? 2 : !g && !r ? 0 : -1; };
    /* the flashes in ms, a look every 2 ms (one under way at the start not counted) */
    const watch = async ms => {
        const m = { seq: "", odd: 0, gaps: [], lens: [], beatWrong: 0 };
        let was = kind(), at = -1;
        for (let x = 0; x < ms; x += 2) {
            await t.sleep(2);
            const k = kind();
            if (k < 0) m.odd++;
            if (k > 0 && was <= 0) {
                if (at >= 0) m.gaps.push(x - at);
                m.seq += k === 1 ? "R" : "G";
                if (P.metroBeat() >= 0 && (P.metroBeat() === 0) !== (k === 1)) m.beatWrong++;   /* (on: as it clicks) */
                at = x;
            }
            if (k <= 0 && was > 0 && at >= 0) m.lens.push(x - at);
            was = k;
        }
        m.lo = Math.min(...m.gaps); m.hi = Math.max(...m.gaps);
        return m;
    };
    const dark = async ms => { let d = true; for (let x = 0; x < ms; x += 5) { await t.sleep(5); t.frame(); d = d && leds[G] === 0 && leds[B.PLAY] === .25; } return d; };
    ok(await dark(1000), "LOOPS' visual metronome: stopped, no flash (PLAY's green dark, its red dim)");
    await t.tap("PLAY");
    let m = await watch(2600);
    const lens = m.lens.length && Math.min(...m.lens) >= 94 && Math.max(...m.lens) <= 106;
    ok(P.playing && P.what === "loops" && m.seq === "GGGRG" && m.lo >= 492 && m.hi <= 508 && lens && !m.odd,
       "LOOPS' visual metronome, its METRONOME off at FOLLOW, 4 beats: a flash a beat, the bar's first red (" + m.seq + ", gaps " +
       m.lo + "-" + m.hi + " ms, flashes " + m.lens.join() + " ms, " + m.odd + " odd)");
    U.edit(() => { LS().fx.knobs.METRONOME[0] = 60; });   /* (a TEMPO of its own: a beat a second, the loops' 120 BPM aside) */
    await t.sleep(1100);
    m = await watch(4100);
    const r = m.seq.indexOf("R");
    const every4 = r >= 0 && [...m.seq].every((c, i) => (c === "R") === ((i - r) % 4 === 0));
    ok(P.playing && m.seq.length >= 4 && m.seq.length <= 5 && every4 && m.lo >= 992 && m.hi <= 1008 && !m.odd,
       "LOOPS' visual metronome, its METRONOME off at 60 BPM of its own: a flash a second, every 4th red (" + m.seq + ", gaps " + m.lo + "-" + m.hi +
       " ms, " + m.odd + " odd)");
    U.edit(() => { LS().fx.knobs.METRONOME[0] = 29; LS().fx.on.METRONOME = true; });   /* (on, heard: the flashes on its clicks) */
    await t.sleep(600);
    ok(P.metroBeat() >= 0, "LOOPS' visual metronome: its METRONOME switched on while they play, it clicks");
    m = await watch(2100);
    ok(P.playing && m.seq.length >= 4 && m.seq.length <= 5 && m.seq.includes("R") && m.lo >= 492 && m.hi <= 508 && !m.beatWrong && !m.odd,
       "LOOPS' visual metronome, its METRONOME on at FOLLOW: the flashes on its clicks, red on the bar's first (" + m.seq + ", gaps " + m.lo + "-" +
       m.hi + " ms, " + m.beatWrong + " off its beat)");
    await t.tap("PLAY");                                /* (they stop, back to the start) */
    await t.sleep(100);
    ok(!P.playing && await dark(1000), "LOOPS' visual metronome: stopped again, no flash (only while the loops play)");
};
module.exports.virtual = true;
