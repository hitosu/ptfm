/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FILES: the demo disk and a sound dropped on the TABLE, a song moved to the FM-1 panel with what it uses, the views,
 * WRITE and READ on the virtual FM-1, each kind's tab opened, the page at a phone's width. */
module.exports = async api => {
    const { evaluate, check, shot, until, sleep } = api;
    await api.fresh();
    await shot("01-empty");
    await api.drop([api.file("dist/firmware/demo.ptfmdsk"), api.sound()]);
    const n = await until("ED.app.st.L.length > 100 && ED.app.st.L.some(i => i.kind === 0x10 && !i.owner) && ED.app.st.L.length", 30);
    const st = await api.status();
    check(n === 122 && /ADDED DEMO\.PTFMDSK: 5 SAMPLERS, 1 CARTRIDGE, 18 SONGS, 64 PATTERNS, 33 SAMPLES/.test(st) && /SINE\.WAV/.test(st),
        "files: the demo disk and a sound on the TABLE (" + n + " items): " + st);
    await shot("02-added");
    /* the first song moved: its patterns, its sampler and samples with it */
    const moved = await evaluate(`(() => { const s = ED.app.st.L.find(i => i.kind === 0x21); ED.files.selectIds("L", [s.id]); ED.files.transfer("L", [s.id]);
        const R = ED.app.st.R; return R.filter(i => i.kind === 0x21).length + " " + R.filter(i => i.kind === 0x20).length + " " + R.filter(i => i.kind === 0x04).length + " " + R.filter(i => i.kind === 0x10).length; })()`);
    check(moved === "1 6 1 6" && /→ FM-1: SONG 00 PUNCH IN · \+ 6 PATTERNS, 1 SAMPLER, 6 SAMPLES/.test(await api.status()),
        "files: a song moved with what it uses (songs, patterns, samplers, samples: " + moved + ")");
    await shot("03-moved");
    for (const [l, r] of [["icons", "list"], ["list", "icons"], ["details", "details"]]) {
        await evaluate(`ED.files.setPrefs({ view: { L: "${l}", R: "${r}" } }); ED.files.render(); 1`);
        await sleep(200);
        check(await evaluate(`document.querySelectorAll(".panel[data-side=L] [data-id]").length > 0 && document.querySelectorAll(".panel[data-side=R] [data-id]").length > 0`),
            "files: the views " + l + " / " + r + " list the items");
        if (l === "icons") await shot("04-views");
    }
    /* WRITE to the virtual FM-1, then READ it back */
    evaluate("ED.fm1.write(); 1");
    const dlg = await until("(() => { const d = document.querySelector('#dlg'); return d && !d.hidden && /WRITE TO THE FM-1/.test(d.innerText) && d.innerText; })()", 60);
    check(/WRITES 14 RECORDS/.test(dlg || ""), "files: WRITE's dialog: " + String(dlg).replace(/\n+/g, " | ").slice(0, 160));
    await shot("05-write-dialog");
    await api.button("WRITE");
    const w = await until("(() => { const s = document.querySelector('#status').textContent; return /WRITTEN|REFUSED|DID NOT/.test(s) && s; })()", 120);
    check(/WRITTEN TO THE VIRTUAL FM-1: 14 RECORDS, 0 REMOVED/.test(w) && await evaluate("ED.model.pending(ED.app.st).count") === 0,
        "files: written, nothing left to write: " + w);
    await evaluate("ED.fm1.read().then(() => 1)");
    const rd = await api.status();
    check(/READ FROM THE VIRTUAL FM-1: 14 ITEMS \(0 RECORDS READ, 14 ALREADY KNOWN\)/.test(rd), "files: READ back, nothing read again: " + rd);
    /* each kind's tab */
    const opened = await evaluate(`(() => {
        const R = ED.app.st.R, L = ED.app.st.L, V = ED.files.viewItems("R");
        const its = [R.find(i => i.kind === 0x20), R.find(i => i.kind === 0x21), V.find(i => i.fw && i.kind === 1), L.find(i => i.kind === 0x04),
            L.find(i => i.kind === 0x10), V.find(i => i.kind === 0x30)];
        for (const it of its) ED.tabs.open(it);
        return ED.tabs.list.map(t => t.base.kind.toString(16) + (t.body.textContent.length > 20 ? "" : " EMPTY")).join(",");
    })()`);
    check(opened === "20,21,1,4,10,30", "files: a tab for a pattern, a song, a built-in DX7, a sampler, a sample, the settings: " + opened);
    await shot("06-sample-tab");
    await evaluate("ED.tabs.show('files'); 1");
    /* a TABLE item bigger than an FM-1 holds plays (the preview's own room) */
    const big = await evaluate(`(async () => {
        const M = ED.model, R = ED.rec, n = 700000, noise = Array.from({ length: n }, (_, i) => ((i * 2654435761) >>> 24) - 128);
        const it = { id: M.newId(), side: "L", kind: 0x10, num: null, orig: null, link: {}, owner: null, src: "NOISE.WAV", name: "BIG NOISE",
            entry: M.packedEntry({ length: n, rate: 44100, coding: 1, name: "BIG NOISE" }), recs: [{ sub: 0, ver: 1, payload: R.ptrEncode(noise, 8) }] };
        ED.app.commit(Object.assign({}, ED.app.st, { L: ED.app.st.L.concat([it]) }), "A BIG SOUND");
        await ED.player.play(it);
        await new Promise(r => setTimeout(r, 1500));
        const s = document.querySelector("#status").textContent, playing = ED.player.isPlaying(it);
        ED.player.stop();
        return [Math.round(it.recs[0].payload.length / 1024), playing, s];
    })()`);
    check(big[0] > 412 && big[1] && !/CANNOT PLAY/.test(big[2]), "files: a " + big[0] + " KB sound, more than an FM-1 holds, plays: " + JSON.stringify(big));
    await api.size(400, 800, { mobile: true });
    await sleep(400);
    check(await evaluate("document.documentElement.scrollWidth <= innerWidth"), "files: nothing wider than a phone's screen");
    await shot("07-narrow");
};
