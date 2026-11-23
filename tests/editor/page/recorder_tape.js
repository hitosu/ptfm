/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Takes that are Juno tapes (specs/editor.md, Takes that are Juno tapes): the recorder's input a fake one playing a
 * Juno-106 tape the test makes (juno_synth.js, in the page): the line counting while the take runs; at STOP the line
 * saying what the take holds (two dumps) and SAVE AS SAMPLE / SAVE AS CARTRIDGE; a selected part read on its own (one
 * dump), SAVE AS CARTRIDGE: a Juno-106 cartridge TAKE00 of 64 on the TABLE, from RECORDING, the recorder closed; a tape
 * that does not read: the line saying why, SAVE AS CARTRIDGE greyed, and closing asking with it greyed there too; a
 * good one saved from that question, TAKE01. */
const fs = require("fs"), path = require("path");
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    /* the input: a stream standing for one, and its source in the recorder's own context a node the tape plays into
     * (a stream carried from another context drops a few ms now and then in a headless Chrome, which a Juno-106 dump's
     * one checksum would not survive; the recorder's own way from its source on is the page's) */
    await evaluate(fs.readFileSync(path.join(__dirname, "..", "juno_synth.js"), "utf8") + "; 1");
    await evaluate(`(() => {
        navigator.mediaDevices.getUserMedia = async () => new AudioContext().createMediaStreamDestination().stream;
        AudioContext.prototype.createMediaStreamSource = function () { const g = this.createGain(); window.__feed = { ctx: this, node: g }; return g; };
        window.__playTape = o => {
            const P = JunoSynth.patches106(128, 21), dumps = o.two ? [P.slice(0, 64), P.slice(64)] : [P.slice(0, 64)];
            const f = window.__feed, rate = f.ctx.sampleRate;
            const x = JunoSynth.tape106(dumps, { rate, gap: 1, damage: o.damage ? [{ dump: 0, byte: 500 }] : [] });
            const b = f.ctx.createBuffer(1, x.length, rate);
            b.copyToChannel(x, 0);
            const s = f.ctx.createBufferSource();
            s.buffer = b; s.connect(f.node); s.start(f.ctx.currentTime + 0.3);
            return 0.3 + x.length / rate;
        };
        return 1; })()`);
    const openRec = async () => {
        await evaluate(`[...document.querySelectorAll("button")].find(b => b.textContent === "● RECORD").click(); 1`);
        let ok = false;
        for (let i = 0; i < 40 && !ok; i++) { await sleep(150); ok = await evaluate(`(() => { const b = document.querySelector(".dlg.rec .rowbtns .btn.primary"); return !!b && !b.disabled; })()`); }
        return ok;
    };
    const click = label => evaluate(`(() => { const b = [...document.querySelectorAll(".dlg.rec button")].find(x => x.textContent === ${JSON.stringify(label)}); if (b) b.click(); return !!b; })()`);
    const keys = () => evaluate(`[...document.querySelectorAll(".dlg.rec .rowbtns button")].filter(b => !b.hidden).map(b => b.textContent + (b.disabled ? "(OFF)" : ""))`);
    const line = () => evaluate(`document.querySelector(".dlg.rec .tapeline").textContent`);
    const take = async o => {
        await click("● RECORD");
        const secs = await evaluate(`__playTape(${JSON.stringify(o)})`);
        return secs;
    };

    check(await openRec(), "recorder (tapes): the recorder open on the fake input");
    const secs = await take({ two: true });
    await sleep(2900);
    const mid = await line();
    check(/^JUNO-106 TAPE: READING… (\d+) OF 64$/.test(mid) && +/(\d+) OF/.exec(mid)[1] > 0 && +/(\d+) OF/.exec(mid)[1] < 64,
        "recorder (tapes): while the take runs, the line counts the patches come in right: " + mid);
    await shot("00-reading");
    await sleep(secs * 1000 - 2900 + 400);
    await click("■ STOP");
    await sleep(400);
    const at = [await line(), await keys()];
    check(at[0] === "JUNO-106 TAPE: 2 DUMPS, 128 PATCHES, CHECKSUMS RIGHT" && at[1].includes("SAVE AS SAMPLE") && at[1].includes("SAVE AS CARTRIDGE"),
        "recorder (tapes): at STOP, the line says what the take holds, SAVE AS SAMPLE and SAVE AS CARTRIDGE: " + JSON.stringify(at));
    await shot("01-stopped");
    /* the first dump alone, selected: read on its own */
    await evaluate(`(() => { const c = document.querySelector(".dlg.rec .wavebox canvas"), rc = c.getBoundingClientRect(), y = rc.top + rc.height / 2;
        const ev = (t, f) => c.dispatchEvent(new PointerEvent(t, { clientX: rc.left + f * rc.width, clientY: y, pointerId: 1, bubbles: true }));
        ev("pointerdown", 0.005); ev("pointermove", 0.2); ev("pointermove", 0.5); ev("pointerup", 0.5); return 1; })()`);
    await sleep(500);
    const part = await line();
    check(part === "JUNO-106 TAPE: 64 PATCHES, CHECKSUM RIGHT", "recorder (tapes): a part selected is read on its own (one dump): " + part);
    await shot("02-part");
    await click("SAVE AS CARTRIDGE");
    await sleep(300);
    const got = await evaluate(`(() => { const it = ED.app.st.L.find(i => i.kind === ED.rec.KIND.JUNO); if (!it) return null; const c = ED.rec.unpackCart(it.kind, it.recs[0].payload);
        return [c.name, c.slots.length, c.slots[5].name, it.src, !document.querySelector(".dlg.rec"), document.querySelector("#status").textContent, !!it.sound]; })()`);
    check(got && got[0] === "TAKE00" && got[1] === 64 && got[2] === "PATCH05" && /^RECORDING /.test(got[3]) && got[4] && /A TAKE SAVED ON THE TABLE AS A JUNO-106 CARTRIDGE: TAKE00/.test(got[5]) && !got[6],
        "recorder (tapes): SAVE AS CARTRIDGE: TAKE00, 64 patches, from RECORDING, no audio, the recorder closed: " + JSON.stringify(got));
    /* a tape that does not read */
    check(await openRec(), "recorder (tapes): opened again");
    const s2 = await take({ damage: true });
    await sleep(s2 * 1000 + 300);
    await click("■ STOP");
    await sleep(400);
    const badAt = [await line(), await keys()];
    check(badAt[0] === "JUNO-106 TAPE? THE CHECKSUM IS WRONG: RECORD IT AGAIN (TRY ANOTHER LEVEL)" && badAt[1].includes("SAVE AS CARTRIDGE(OFF)") && badAt[1].includes("SAVE AS SAMPLE"),
        "recorder (tapes): a tape that did not read: the line says why, SAVE AS CARTRIDGE greyed: " + JSON.stringify(badAt));
    await shot("03-not-read");
    await evaluate(`document.querySelector(".dlg.rec h3 button").click(); 1`);
    await sleep(200);
    const asked = await evaluate(`(() => { const d = document.querySelector("#dlg .dlg"); return d ? [d.querySelector("h3").textContent, [...d.querySelectorAll(".acts button")].map(b => b.textContent + (b.disabled ? "(OFF)" : ""))] : null; })()`);
    check(asked && asked[0] === "THE TAKE IS NOT SAVED" && asked[1].join() === "SAVE AS SAMPLE,SAVE AS CARTRIDGE(OFF),DISCARD,STAY",
        "recorder (tapes): closing with a tape take not saved asks, SAVE AS CARTRIDGE greyed there too: " + JSON.stringify(asked));
    await evaluate(`[...document.querySelectorAll("#dlg .acts button")].find(b => b.textContent === "STAY").click(); 1`);
    await sleep(200);
    /* a good one, saved from the question */
    const s3 = await take({});
    await sleep(s3 * 1000 + 300);
    await click("■ STOP");
    await sleep(400);
    await evaluate(`document.querySelector(".dlg.rec h3 button").click(); 1`);
    await sleep(200);
    await evaluate(`[...document.querySelectorAll("#dlg .acts button")].find(b => b.textContent === "SAVE AS CARTRIDGE").click(); 1`);
    await sleep(400);
    const two = await evaluate(`[ED.app.st.L.filter(i => i.kind === ED.rec.KIND.JUNO).map(i => ED.rec.unpackCart(i.kind, i.recs[0].payload).name), !document.querySelector(".dlg.rec")]`);
    check(two[0].join() === "TAKE00,TAKE01" && two[1], "recorder (tapes): SAVE AS CARTRIDGE from the closing question: TAKE01, the recorder closed: " + JSON.stringify(two));
};
