/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The recorder (specs/editor.md, Recording and cutting sounds: The recorder), with Chrome's fake input: ● RECORD opens
 * it, the input asked for and named; RECORD / STOP a take of about a second, its waveform drawn as it comes in (its end
 * light red), its length shown; TRIM SILENCE selecting its sound, WHOLE TAKE all again; ▶ through the page's own
 * player, a white line where it plays, gone when it stops; a drag selecting a part, ▶ playing it; SAVE: the selection
 * a loose sample on the TABLE, TAKE00, 44.1 kHz 16 bits, its source RECORDING and the time, what it kept its original,
 * the recorder closed; RECORD again replaces the take; closing with a take not saved asks SAVE, DISCARD or STAY. */
module.exports = async api => {
    const { evaluate, check, shot, sleep } = api;
    await api.fresh();
    const openRec = async () => {
        await evaluate(`[...document.querySelectorAll("button")].find(b => b.textContent === "● RECORD").click(); 1`);
        let ok = false;
        for (let i = 0; i < 40 && !ok; i++) { await sleep(150); ok = await evaluate(`(() => { const b = document.querySelector(".dlg.rec .rowbtns .btn.primary"); return !!b && !b.disabled; })()`); }
        return ok;
    };
    const ready = await openRec();
    const say = await evaluate(`document.querySelector(".dlg.rec .hint").textContent`);
    check(ready && / HZ$/.test(say), "recorder: the dialog, the input opened at its rate: " + say);
    const click = label => evaluate(`(() => { const b = [...document.querySelectorAll(".dlg.rec button")].find(x => x.textContent === ${JSON.stringify(label)}); if (b) b.click(); return !!b; })()`);
    /* the waveform's pixels of a colour: their count and the columns they are in */
    const waveCols = rgb => evaluate(`(() => { const c = document.querySelector(".dlg.rec .wavebox canvas"), d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
        const cols = new Set(); let n = 0; for (let i = 0; i < d.length; i += 4) if (d[i] === ${rgb[0]} && d[i + 1] === ${rgb[1]} && d[i + 2] === ${rgb[2]}) { n++; cols.add((i >> 2) % c.width); }
        const s = [...cols].sort((a, b) => a - b); return { n, first: s[0], last: s[s.length - 1], cols: s.length }; })()`);
    const YELLOW = [0xF0, 0xE0, 0x74], LRED = [0xF8, 0x7C, 0x74], WHITE = [0xF4, 0xF4, 0xEC];
    await click("● RECORD");
    await sleep(700);
    const live = [await waveCols(YELLOW), await waveCols(LRED)];
    check(live[0].n > 0 && live[1].cols === 2 && live[1].first > 10 && live[1].first < 300 && live[0].last < live[1].first,
        "recorder: while a take runs, its waveform as it comes in, its end a light red line (about 0.7 s of 10: " + JSON.stringify(live) + ")");
    await shot("00-recording");
    await sleep(500);
    await click("■ STOP");
    await sleep(200);
    const take = await evaluate(`[...document.querySelectorAll(".dlg.rec .hint")].map(x => x.textContent)`);
    check(take.some(t => /^THE TAKE: 0:0[0-9]\.[0-9]/.test(t)), "recorder: a take of about a second: " + JSON.stringify(take));
    const whole = [await waveCols(YELLOW), await waveCols(LRED)];
    check(whole[0].last > 850 && whole[1].n === 0, "recorder: the take stopped: its waveform across the box, no red line: " + JSON.stringify(whole));
    await shot("01-take");
    /* ▶: the page's own player, in the footer (its label, ■), a white line over the take where it plays; the footer's ■
     * stopping it, the line gone */
    await click("▶");
    await sleep(400);
    const own = await evaluate(`[document.querySelector("#plabel").textContent, document.querySelector("#pbtn").textContent, ED.player.now && ED.player.now.job.kind]`);
    const head1 = await waveCols(WHITE);
    await sleep(300);
    const head2 = await waveCols(WHITE);
    await shot("02-playing");
    await evaluate(`document.querySelector("#pbtn").click(); 1`);
    await sleep(150);
    const stopped = await evaluate(`[document.querySelector("#plabel").textContent, !ED.player.now]`);
    const head3 = await waveCols(WHITE);
    check(own[0] === "THE TAKE · AS IT IS" && own[1] === "■" && own[2] === "own" && stopped[0] === "NOTHING PLAYING" && stopped[1],
        "recorder: ▶ plays the take through the page's own player, shown in the footer, its ■ stopping it: " + JSON.stringify([own, stopped]));
    check(head1.cols === 2 && head2.cols === 2 && head2.first > head1.first && head3.n === 0,
        "recorder: while ▶ plays, a white line where it is, moving on; gone once stopped: " + JSON.stringify([head1, head2, head3]));
    /* TRIM SILENCE: the sound selected (Chrome's fake input beeps with silence between); WHOLE TAKE: all again */
    const lenNow = () => evaluate(`[...document.querySelectorAll(".dlg.rec .hint")].map(x => x.textContent).find(t => /^THE TAKE/.test(t)) || ""`);
    const enabled = label => evaluate(`(() => { const b = [...document.querySelectorAll(".dlg.rec button")].find(x => x.textContent === ${JSON.stringify(label)}); return !!b && !b.disabled; })()`);
    const w0 = [await enabled("TRIM SILENCE"), await enabled("WHOLE TAKE")];
    await click("TRIM SILENCE");
    await sleep(100);
    const trimmed = [await lenNow(), await enabled("WHOLE TAKE")];
    await shot("01b-trimmed");
    check(w0[0] && !w0[1] && / · SAVE KEEPS 0:0\d\.\d – 0:0\d\.\d \(0:0\d\.\d\)$/.test(trimmed[0]) && trimmed[1],
        "recorder: TRIM SILENCE selects the take's sound, the length line naming what SAVE keeps, WHOLE TAKE lit: " + JSON.stringify([w0, trimmed]));
    await click("WHOLE TAKE");
    await sleep(100);
    const whole2 = [await lenNow(), await enabled("WHOLE TAKE")];
    check(!/SAVE KEEPS/.test(whole2[0]) && !whole2[1], "recorder: WHOLE TAKE: the whole take again, the button dimmed: " + JSON.stringify(whole2));
    /* a drag across the waveform selects; ▶ plays the selection, its line from the selection's start */
    const drag = (f0, f1) => evaluate(`(() => { const c = document.querySelector(".dlg.rec .wavebox canvas"), rc = c.getBoundingClientRect(), y = rc.top + rc.height / 2;
        const ev = (t, f) => c.dispatchEvent(new PointerEvent(t, { clientX: rc.left + f * rc.width, clientY: y, pointerId: 1, bubbles: true }));
        ev("pointerdown", ${f0}); ev("pointermove", ${(f0 + f1) / 2}); ev("pointermove", ${f1}); ev("pointerup", ${f1}); return 1; })()`);
    await drag(0.25, 0.6);
    await sleep(100);
    const dragged = await lenNow();
    const selSecs = (/\((\d+):(\d+\.\d)\)$/.exec(dragged) || []).slice(1).reduce((m, s) => m * 60 + Number(s), 0);
    check(/ · SAVE KEEPS 0:00\.\d – 0:00\.\d \(0:00\.\d\)$/.test(dragged) && await enabled("WHOLE TAKE") && selSecs > 0.2 && selSecs < 0.6,
        "recorder: a drag across the waveform selects that part (" + dragged + ")");
    await shot("01c-selected");
    await click("▶");
    await sleep(150);
    const selPlay = [await evaluate(`document.querySelector("#plabel").textContent`), await waveCols(WHITE)];
    await evaluate(`document.querySelector("#pbtn").click(); 1`);
    await sleep(150);
    check(selPlay[0] === "THE TAKE'S SELECTION · AS IT IS" && selPlay[1].cols === 2 && selPlay[1].first >= 0.25 * 900 - 3 && selPlay[1].first < 0.6 * 900,
        "recorder: ▶ with a selection plays it, the line inside it: " + JSON.stringify(selPlay));
    const n0 = await evaluate(`ED.app.st.L.length`);
    await click("SAVE");
    await sleep(300);
    const it = await evaluate(`(() => { const L = ED.app.st.L, it = L[L.length - 1], e = ED.model.sampleEntry(it, L);
        return { n: L.length, name: e.name, rate: e.rate, coding: e.coding, src: it.src, orig: !!(it.sound && it.sound.ch && it.sound.ch[0].length > 5000),
                 secs: it.sound && it.sound.ch ? it.sound.ch[0].length / it.sound.rate : 0 }; })()`);
    check(it.n === n0 + 1 && it.name === "TAKE00" && it.rate === 44100 && it.coding === 2 && /^RECORDING /.test(it.src) && it.orig &&
          Math.abs(it.secs - selSecs) < 0.11,
        "recorder: SAVE: the selection kept, a loose sample TAKE00, 44.1 kHz 16 bits, from RECORDING, what it kept its original (" +
        JSON.stringify(it) + ", the selection " + selSecs + " s)");
    const after = await evaluate(`[!document.querySelector(".dlg.rec"), document.querySelector("#status").textContent]`);
    check(after[0] && /A TAKE SAVED ON THE TABLE: TAKE00/.test(after[1]), "recorder: SAVE closes the recorder, the status line naming the sample: " + JSON.stringify(after));
    /* the originals in a store of their own: the state names the take's by its id, its channels apart, written once */
    await evaluate(`ED.keep.flush()`);
    const kept = await evaluate(`new Promise(res => { const q = indexedDB.open("ptfm-editor-beta", 1); q.onsuccess = () => { const db = q.result, s = db.transaction("kv").objectStore("kv");
        const a = s.get("state"), k = s.getAllKeys(); a.onsuccess = () => k.onsuccess = () => { const L = a.result.st.L, it = L[L.length - 1];
        res({ oid: it.sound && it.sound.oid, ch: !!(it.sound && it.sound.ch), origs: k.result.filter(x => /^orig:/.test(x)) }); db.close(); }; }; })`);
    check(kept.oid && !kept.ch && kept.origs.includes("orig:" + kept.oid), "recorder: the take's original kept apart under its id, the state naming it: " + JSON.stringify(kept));
    /* a second take, not saved (the recorder opened again): closing asks; STAY keeps it, DISCARD closes */
    check(await openRec(), "recorder: ● RECORD opens it again for the next take");
    await click("● RECORD"); await sleep(600); await click("■ STOP"); await sleep(200);
    await evaluate(`document.querySelector(".dlg.rec h3 button").click(); 1`);
    await sleep(200);
    const asked = await evaluate(`(() => { const d = document.querySelector("#dlg .dlg"); return d ? [d.querySelector("h3").textContent, [...d.querySelectorAll(".acts button")].map(b => b.textContent)] : null; })()`);
    check(asked && asked[0] === "THE TAKE IS NOT SAVED" && asked[1].join() === "SAVE,DISCARD,STAY", "recorder: closing with a take not saved asks: " + JSON.stringify(asked));
    await evaluate(`[...document.querySelectorAll("#dlg .acts button")].find(b => b.textContent === "STAY").click(); 1`);
    await sleep(200);
    check(await evaluate(`!!document.querySelector(".dlg.rec")`), "recorder: STAY: back to the dialog, the take kept");
    await evaluate(`document.querySelector(".dlg.rec h3 button").click(); 1`);
    await sleep(200);
    await evaluate(`[...document.querySelectorAll("#dlg .acts button")].find(b => b.textContent === "DISCARD").click(); 1`);
    await sleep(200);
    check(await evaluate(`!document.querySelector(".dlg.rec") && ED.app.st.L.length === ${n0 + 1}`), "recorder: DISCARD: closed, nothing more on the TABLE");
    /* a reload: the take's sample back with its original; the original dropped once no item names it */
    await api.send("Page.reload", {});
    await api.until("!!document.querySelector('#status') && /BACK AS YOU LEFT IT/.test(document.querySelector('#status').textContent)", 30);
    const back = await evaluate(`(() => { const L = ED.app.st.L, it = L[L.length - 1]; return [L.length, !!(it.sound && it.sound.ch && it.sound.ch[0].length > 5000), !!(it.sound && it.sound.oid)]; })()`);
    check(back[0] === n0 + 1 && back[1] && !back[2], "recorder: over a reload the take's sample keeps its original: " + JSON.stringify(back));
    /* an original an older page kept stereo (its CHANNELS on RIGHT): one channel at the next load, the right one */
    await evaluate(`ED.keep.flush()`);
    await evaluate(`new Promise(res => { const q = indexedDB.open("ptfm-editor-beta", 1); q.onsuccess = () => { const db = q.result, tx = db.transaction("kv", "readwrite"), s = tx.objectStore("kv");
        const a = s.get("state"); a.onsuccess = () => { const st = a.result, L = st.st.L, it = L[L.length - 1];
            it.sound.oid = "stereo-old"; it.sound.cut = Object.assign({}, it.sound.cut, { chan: "r" });
            s.put({ ch: [new Float32Array(it.sound.cut.end).fill(0.25), new Float32Array(it.sound.cut.end).fill(-0.5)] }, "orig:stereo-old"); s.put(st, "state"); };
        tx.oncomplete = () => { db.close(); res(1); }; }; })`);
    await api.send("Page.reload", {});
    await api.until("!!document.querySelector('#status') && /BACK AS YOU LEFT IT/.test(document.querySelector('#status').textContent)", 30);
    const mono = await evaluate(`(() => { const L = ED.app.st.L, it = L[L.length - 1], s = it.sound; return [L.length, s && s.ch && s.ch.length, s && s.ch && s.ch[0][7], s && s.cut && s.cut.chan]; })()`);
    check(mono[0] === n0 + 1 && mono[1] === 1 && mono[2] === -0.5 && mono[3] === "mix",
        "recorder: an original kept stereo before: one channel at the load, as its CHANNELS had it (RIGHT): " + JSON.stringify(mono));
    await evaluate(`(() => { const L = ED.app.st.L; ED.app.commit(Object.assign({}, ED.app.st, { L: L.slice(0, L.length - 1) }), "TEST: THE TAKE REMOVED"); return ED.keep.flush(); })()`);
    const left = await evaluate(`new Promise(res => { const q = indexedDB.open("ptfm-editor-beta", 1); q.onsuccess = () => { const db = q.result, k = db.transaction("kv").objectStore("kv").getAllKeys();
        k.onsuccess = () => { res(k.result.filter(x => /^orig:/.test(x)).length); db.close(); }; }; })`);
    check(left === 0, "recorder: the original dropped once no item names it (" + left + " left)");
};
