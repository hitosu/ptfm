/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Juno banks and tapes dropped (specs/editor.md, Juno-106 banks; Juno tape recordings), on files the test makes
 * (juno_synth.js): a .syx, a .106 and a .pat in one drop, a cartridge each; a Juno-106 tape alone, its question
 * (JUNO-106 TAPE? READ IT AS A CARTRIDGE, no FOR ALL) answered CARTRIDGE; a Juno-60 tape answered CARTRIDGE (converted);
 * three files at once with FOR ALL: a good tape, one that does not read and a sound that is no tape (never asked), the
 * tape that did not read listed in a dialog with OK once the rest are in; a tape answered SAMPLE, a sample; a tape
 * dropped while the recorder is open, its question waiting till the recorder closes (the recorder kept, and opening
 * again after); a second drop while a question is open, waiting its turn. */
const fs = require("fs"), path = require("path");
const JS = require("../juno_synth.js");
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    const dir = path.join(api.ROOT, "build", "editor", "page", "juno-files");
    fs.mkdirSync(dir, { recursive: true });
    const put = (name, bytes) => { const f = path.join(dir, name); fs.writeFileSync(f, Buffer.from(bytes)); return f; };
    const P = JS.patches106(128, 11), A = P.slice(0, 64);
    const files = {
        syx: put("Basses.syx", JS.syx([JS.msg31(P[0]), ...P.slice(0, 10).map((p, i) => JS.msg30(p, 2 * i))])),
        lib: put("Mine.106", JS.lib106("My Library", P.slice(0, 40).map((p, i) => ({ name: "A" + (1 + (i >> 3)) + (1 + (i & 7)) + " Sound " + i, p })))),
        pat: put("List.pat", JS.pat(P.slice(0, 16).map((p, i) => ({ name: "Pat " + i, p })))),
        good: put("Tape106.wav", JS.wav(JS.tape106([A], { rate: 44100 }), 44100)),
        bad: put("Broken106.wav", JS.wav(JS.tape106([A], { rate: 44100, damage: [{ dump: 0, byte: 77 }] }), 44100)),
        sixty: put("Tape60.wav", JS.wav(JS.tape60([JS.patches60(56, 5)], { rate: 22050 }), 22050)),
        sine: api.sound()
    };
    await api.fresh();
    const carts = () => evaluate(`ED.app.st.L.filter(i => i.kind === ED.rec.KIND.JUNO).map(i => { const c = ED.rec.unpackCart(i.kind, i.recs[0].payload); return c.name + ":" + c.slots.length; })`);
    const dlg = () => evaluate(`(() => { const d = document.querySelector("#dlg"); if (!d || d.hidden || !d.querySelector(".dlg")) return null; const b = d.querySelector(".dlg");
        return { title: b.querySelector("h3").textContent, text: b.innerText, keys: [...b.querySelectorAll(".acts button")].map(x => x.textContent), all: !!b.querySelector("input[type=checkbox]") }; })()`);
    const waitDlg = async title => { for (let i = 0; i < 300; i++) { const d = await dlg(); if (d && (!title || d.title === title)) return d; await sleep(100); } return null; };
    const added = () => api.until(`/^ADDED /.test(document.querySelector("#status").textContent) && document.querySelector("#status").textContent`, 60);

    /* the three files of patches at once */
    await api.drop([files.syx, files.lib, files.pat]);
    const st0 = await added();
    check(JSON.stringify(await carts()) === JSON.stringify(["BASSES:10", "MY LIBRARY:40", "LIST:16"]) && /BASSES\.SYX: 1 CARTRIDGE \(1 PANEL COPY LEFT OUT\)/.test(st0),
        "juno: a .syx, a .106 and a .pat dropped: a Juno-106 cartridge each, the status line's notes: " + st0);
    await shot("00-banks");

    /* a Juno-106 tape alone: asked, no FOR ALL; CARTRIDGE */
    await api.fresh();
    await api.drop([files.good]);
    const q1 = await waitDlg("TAPE106.WAV");
    check(q1 && /JUNO-106 TAPE\? READ IT AS A CARTRIDGE/.test(q1.text) && q1.keys.join() === "CARTRIDGE,SAMPLE" && !q1.all,
        "juno: a Juno-106 tape dropped asks, CARTRIDGE or SAMPLE: " + JSON.stringify(q1));
    await shot("01-question");
    await api.button("CARTRIDGE");
    const st1 = await added();
    const c1 = await evaluate(`(() => { const it = ED.app.st.L.find(i => i.kind === ED.rec.KIND.JUNO); const c = ED.rec.unpackCart(it.kind, it.recs[0].payload);
        return [c.name, c.slots.length, c.slots[0].name, c.slots[63].name, it.src]; })()`);
    check(c1[0] === "TAPE106" && c1[1] === 64 && c1[2] === "PATCH00" && c1[3] === "PATCH3F" && /TAPE106\.WAV: 1 CARTRIDGE/.test(st1),
        "juno: CARTRIDGE: a Juno-106 cartridge of 64 named after the file, PATCH00-PATCH3F: " + JSON.stringify(c1) + " " + st1);

    /* a Juno-60 tape: asked as one, converted */
    await api.fresh();
    await api.drop([files.sixty]);
    const q2 = await waitDlg("TAPE60.WAV");
    check(q2 && /JUNO-60 TAPE\? READ IT AS A CARTRIDGE, CONVERTED TO JUNO-106/.test(q2.text), "juno: a Juno-60 tape asks, converted to Juno-106: " + (q2 && q2.text));
    await api.button("CARTRIDGE");
    const st2 = await added();
    check(JSON.stringify(await carts()) === JSON.stringify(["TAPE60:56"]), "juno: a Juno-60 tape as a Juno-106 cartridge of 56: " + st2);

    /* three at once: FOR ALL answers the rest; the sound that is no tape never asked; the tape that did not read in a
     * dialog with OK, after the rest are in */
    await api.fresh();
    await api.drop([files.good, files.bad, files.sine]);
    const q3 = await waitDlg("TAPE106.WAV");
    check(q3 && q3.all, "juno: with several files dropped, the question has FOR ALL");
    await evaluate(`(() => { const c = document.querySelector("#dlg input[type=checkbox]"); c.click(); return c.checked; })()`);
    await api.button("CARTRIDGE");
    const fail = await waitDlg("A TAPE NOT READ");
    const st3 = await api.status();
    check(fail && /BROKEN106\.WAV: NOT READ: THE TAPE'S CHECKSUM IS WRONG/.test(fail.text) && fail.keys.join() === "OK",
        "juno: the tape that did not read, answered by FOR ALL, listed in a dialog with OK: " + JSON.stringify(fail));
    check(/TAPE106\.WAV: 1 CARTRIDGE/.test(st3) && /SINE\.WAV: A SAMPLE/.test(st3) && !/BROKEN/.test(st3),
        "juno: the others in, the sound that is no tape a sample, nothing of the failed tape in the status line: " + st3);
    await shot("02-not-read");
    await api.button("OK");
    await sleep(100);
    check(!await dlg(), "juno: OK closes it");

    /* SAMPLE: a sample, as any sound */
    await api.fresh();
    await api.drop([files.good]);
    await waitDlg("TAPE106.WAV");
    await api.button("SAMPLE");
    const st4 = await added();
    check(/TAPE106\.WAV: A SAMPLE/.test(st4) && await evaluate(`ED.app.st.L.some(i => i.kind === ED.rec.KIND.SAMPLE) && !ED.app.st.L.some(i => i.kind === ED.rec.KIND.JUNO)`),
        "juno: SAMPLE: a sample, no cartridge: " + st4);

    /* a tape dropped while the recorder is open: its question waits till the recorder closes (the recorder kept) */
    await api.fresh();
    await evaluate(`[...document.querySelectorAll("button")].find(b => b.textContent === "● RECORD").click(); 1`);
    await api.until(`(() => { const b = document.querySelector(".dlg.rec .rowbtns .btn.primary"); return !!b && !b.disabled; })()`, 10);
    await api.drop([files.good]);
    await sleep(1500);
    const held = await evaluate(`[!!document.querySelector(".dlg.rec") && document.querySelector(".dlg.rec").isConnected, (document.querySelector("#dlg h3") || {}).textContent]`);
    check(held[0] && held[1] !== "TAPE106.WAV", "juno: a tape dropped while the recorder is open: the recorder stays, the question waits: " + JSON.stringify(held));
    await evaluate(`document.querySelector(".dlg.rec h3 button").click(); 1`);
    const q5 = await waitDlg("TAPE106.WAV");
    check(!!q5 && await evaluate(`!document.querySelector(".dlg.rec")`), "juno: the recorder closed, the question comes");
    await api.button("CARTRIDGE");
    const st5 = await added();
    check(JSON.stringify(await carts()) === JSON.stringify(["TAPE106:64"]), "juno: its answer taken after the wait: " + st5);
    check(await evaluate(`[...document.querySelectorAll("button")].find(b => b.textContent === "● RECORD").click(), new Promise(r => setTimeout(() => r(!!document.querySelector(".dlg.rec")), 600))`),
        "juno: the recorder opens again afterwards");
    await evaluate(`document.querySelector(".dlg.rec h3 button").click(); 1`);
    await sleep(200);

    /* a second drop while the first's question is open: it waits its turn, the question kept */
    await api.fresh();
    await api.drop([files.good]);
    await waitDlg("TAPE106.WAV");
    await api.drop([files.syx]);
    await sleep(800);
    const still = await dlg();
    check(still && still.title === "TAPE106.WAV" && !(await carts()).length, "juno: a drop made while a question is open: the question kept, nothing of it in yet");
    await api.button("CARTRIDGE");
    await api.until(`ED.app.st.L.filter(i => i.kind === ED.rec.KIND.JUNO).length === 2`, 30);
    check(JSON.stringify(await carts()) === JSON.stringify(["TAPE106:64", "BASSES:10"]), "juno: then the next drop, in turn: " + JSON.stringify(await carts()));
};
