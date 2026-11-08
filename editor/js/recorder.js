/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The recorder (specs/editor.md, Recording and cutting sounds: The recorder): ● RECORD on the TABLE opens it over the
 * page. INPUT the inputs the browser offers (the last one picked found again by its name), at the input's own rate,
 * the browser's voice processing off; the meter (its peak held a second, CLIP a second after full scale); RECORD /
 * STOP (Space too) a take, no limit, its waveform growing as it comes in (the box 10 s wide, or the take's length once
 * longer; its end light red); the take's waveform, a part of it selected by a drag (its ends dragged move them; TRIM
 * SILENCE selects its sound, the SAMPLE tab's rule; WHOLE TAKE all again), ▶ (the page's own player: the selection, a
 * white line where it plays) and SAVE: the selection, or the whole take, a loose sample on the TABLE, 16 bits at 44.1
 * kHz, mixed, named TAKE00, TAKE01 ... (the naming rule), its source RECORDING and the time, what it kept its original,
 * the recorder closed; RECORD again replaces the take. Closing (✕, Esc)
 * stops a take that runs and asks SAVE, DISCARD or STAY for one not saved; an input that goes away stops the take,
 * kept. Nothing is heard while recording. The input's chunks come from an AudioWorklet (a data: URL), else a
 * ScriptProcessorNode. */
(function (ED) {
"use strict";
const U = ED.ui, h = U.h, KEEP = "ptfm.editor.recorder.input";
const LIVE_B = 256, LIVE_SPAN = 10;             /* a take's waveform while it runs: a min / max every 256 frames; the box
                                                 * 10 s wide till the take is longer */
const WORKLET = `class R extends AudioWorkletProcessor { process(i) { const c = i[0]; if (c && c.length) this.port.postMessage(c.map(x => x.slice())); return true; } }
registerProcessor("ptfm-rec", R);`;
let r = null;                                   /* the open recorder */

function remembered() { try { return localStorage.getItem(KEEP) || ""; } catch (e) { return ""; } }
function remember(label) { try { localStorage.setItem(KEEP, label); } catch (e) { /* (no storage: not kept) */ } }

async function inputs() {
    try { return (await navigator.mediaDevices.enumerateDevices()).filter(d => d.kind === "audioinput"); } catch (e) { return []; }
}
/* the input opened at its own rate, the browser's voice processing off */
async function openInput(deviceId) {
    const audio = { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: { ideal: 2 } };
    if (deviceId) audio.deviceId = { exact: deviceId };
    const stream = await navigator.mediaDevices.getUserMedia({ audio });
    const track = stream.getAudioTracks()[0], set = track.getSettings ? track.getSettings() : {};
    let ctx;
    try { ctx = new AudioContext(set.sampleRate ? { sampleRate: set.sampleRate } : {}); } catch (e) { ctx = new AudioContext(); }
    const node = ctx.createMediaStreamSource(stream);
    let tap = null;
    try {
        await ctx.audioWorklet.addModule("data:text/javascript;charset=utf-8," + encodeURIComponent(WORKLET));
        tap = new AudioWorkletNode(ctx, "ptfm-rec", { numberOfInputs: 1, numberOfOutputs: 0, channelCountMode: "explicit", channelCount: Math.max(1, Math.min(2, set.channelCount || 2)) });
        node.connect(tap);
    } catch (e) {
        const sp = ctx.createScriptProcessor(2048, 2, 2);  /* (no worklet: the page's thread) */
        sp.onaudioprocess = ev => { const b = ev.inputBuffer, out = []; for (let c = 0; c < b.numberOfChannels; c++) out.push(b.getChannelData(c).slice()); if (r) r.chunk(out); };
        node.connect(sp); sp.connect(ctx.destination);     /* (its output silent: nothing heard while recording) */
        tap = sp;
    }
    if (tap.port) tap.port.onmessage = ev => { if (r) r.chunk(ev.data); };
    await ctx.resume();
    return { stream, track, ctx, node, tap, rate: ctx.sampleRate, label: track.label || "" };
}
function closeInput(inp) {
    if (!inp) return;
    try { inp.node.disconnect(); inp.tap.disconnect(); } catch (e) { /* */ }
    try { inp.stream.getTracks().forEach(t => t.stop()); } catch (e) { /* */ }
    try { inp.ctx.close(); } catch (e) { /* */ }
}

async function open() {
    if (r) return;
    if (ED.player && ED.player.stop) ED.player.stop();
    const sel = h("select", { disabled: true }), meter = h("canvas", { width: 600, height: 16, class: "meter" }), clip = h("span", { class: "clip" }, "CLIP");
    const recBtn = h("button", { class: "btn primary", disabled: true }, "● RECORD"), say = h("div", { class: "hint" }, "ASKING FOR THE INPUT…");
    const wave = h("canvas", { width: 900, height: 120 }), len = h("span", { class: "hint" }, "");
    const play = h("button", { class: "btn small", disabled: true }, "▶"), save = h("button", { class: "btn primary", disabled: true }, "SAVE");
    const trimBtn = h("button", { class: "btn small", disabled: true }, "TRIM SILENCE"), wholeBtn = h("button", { class: "btn small", disabled: true }, "WHOLE TAKE");
    const box = h("div", { class: "dlg rec", role: "dialog", "aria-modal": "true", "aria-label": "RECORD" },
        h("h3", null, "RECORD", h("span", { class: "grow" }), h("button", { class: "btn small", title: "CLOSE (ESC)", onclick: () => close() }, "✕")),
        h("div", { class: "field" }, h("label", null, "INPUT"), sel), h("div", { class: "rowbtns" }, meter, clip), say,
        h("div", { class: "rowbtns" }, recBtn), h("div", { class: "wavebox" }, wave),
        h("div", { class: "rowbtns" }, play, trimBtn, wholeBtn, len, h("span", { class: "grow" }), save));
    const wrap = document.querySelector("#dlg");
    wrap.textContent = ""; wrap.append(box); wrap.hidden = false;
    r = { inp: null, take: null, saved: true, on: false, chunks: [], frames: 0, peak: 0, peakAt: 0, clipAt: 0, raf: 0,
        live: null,                             /* the running take's waveform: min / max a LIVE_B frames (channel 1's) */
        img: null, headX: -1, playNow: null,    /* the take's waveform drawn, where its white line is, what ▶ started */
        sel: null, playFrom: 0,                 /* the part selected ([a, b) frames; null: the whole take), where ▶ began */
        chunk(ch) {
            let pk = 0;
            for (const c of ch) for (let i = 0; i < c.length; i++) { const v = Math.abs(c[i]); if (v > pk) pk = v; }
            const now = performance.now();
            if (pk >= r.peak || now - r.peakAt > 1000) { r.peak = pk; r.peakAt = now; }
            r.level = pk;
            if (pk >= 0.999) r.clipAt = now;
            if (r.on) {
                r.chunks.push(ch); r.frames += ch[0].length;
                const L = r.live, a = ch[0], b = ch[ch.length - 1];   /* (the channels mixed, as the take will be) */
                for (let i = 0; i < a.length; i++) {
                    const v = (a[i] + b[i]) * 0.5;
                    if (v < L.mn0) L.mn0 = v;
                    if (v > L.mx0) L.mx0 = v;
                    if (++L.k === LIVE_B) { L.mn.push(L.mn0); L.mx.push(L.mx0); L.k = 0; L.mn0 = 1; L.mx0 = -1; }
                }
            }
        } };
    const paintMeter = () => {
        if (!r) return;
        const g = meter.getContext("2d"), W = meter.width, Hh = meter.height, x = v => Math.max(0, Math.min(W, (1 + Math.log10(Math.max(v, 1e-4)) / 4) * W));
        g.fillStyle = "#111"; g.fillRect(0, 0, W, Hh);
        g.fillStyle = U.COL.lgreen; g.fillRect(0, 0, x(r.level || 0), Hh);
        g.fillStyle = U.COL.white; g.fillRect(x(r.peak) - 1, 0, 2, Hh);
        clip.style.opacity = performance.now() - r.clipAt < 1000 ? 1 : 0.15;
        if (r.on) { len.textContent = "REC " + secs(r.frames / (r.inp ? r.inp.rate : 44100)); drawLive(); }
        else drawHead();
        r.raf = requestAnimationFrame(paintMeter);
    };
    /* a take running: what came in so far, across the box (LIVE_SPAN seconds, or the take once longer), its end light red */
    function drawLive() {
        const g = wave.getContext("2d"), W = wave.width, Hh = wave.height, L = r.live, rate = r.inp ? r.inp.rate : 44100;
        const per = Math.max(r.frames, LIVE_SPAN * rate) / LIVE_B / W;     /* (the min / max pairs a pixel) */
        g.fillStyle = "#000"; g.fillRect(0, 0, W, Hh);
        g.fillStyle = U.COL.yellow;
        let px = 0;
        for (; px < W; px++) {
            const a = Math.floor(px * per), e = Math.min(L.mn.length, Math.max(a + 1, Math.floor((px + 1) * per)));
            if (a >= L.mn.length) break;
            let mn = 1, mx = -1;
            for (let i = a; i < e; i++) { if (L.mn[i] < mn) mn = L.mn[i]; if (L.mx[i] > mx) mx = L.mx[i]; }
            g.fillRect(px, (1 - mx) * Hh / 2, 1, Math.max(1, (mx - mn) * Hh / 2));
        }
        g.fillStyle = U.COL.lred; g.fillRect(Math.min(px, W - 2), 0, 2, Hh);
    }
    /* ▶ playing the take: a white line where it is (the waveform drawn again under it as it moves) */
    function drawHead() {
        if (!r.take || !r.img) return;
        const t = r.playNow && ED.player && ED.player.ownTime ? ED.player.ownTime(r.playNow) : -1;
        const W = wave.width, x = t < 0 ? -1 : Math.min(W - 2, Math.round((r.playFrom + t * r.take.rate) / r.take.ch[0].length * W));
        if (x === r.headX) return;
        const g = wave.getContext("2d");
        g.putImageData(r.img, 0, 0);
        if (x >= 0) { g.fillStyle = U.COL.white; g.fillRect(x, 0, 2, wave.height); }
        r.headX = x;
        if (t < 0) r.playNow = null;
    }
    const secs = s => Math.floor(s / 60) + ":" + String(Math.floor(s % 60)).padStart(2, "0") + "." + Math.floor(s * 10 % 10);
    async function use(deviceId) {
        closeInput(r.inp); r.inp = null;
        recBtn.disabled = true;
        try { r.inp = await openInput(deviceId); }
        catch (e) { say.textContent = "THE BROWSER WAS NOT ALLOWED THE INPUT: NOTHING CAN BE RECORDED (" + String(e.name || e.message || e).toUpperCase() + ")"; return; }
        r.inp.track.addEventListener("ended", () => { if (r && r.on) stop("THE INPUT WENT AWAY"); else if (r) say.textContent = "THE INPUT WENT AWAY"; });
        remember(r.inp.label);
        say.textContent = r.inp.label.toUpperCase() + " · " + r.inp.rate + " HZ";
        recBtn.disabled = false;
        const list = await inputs();
        sel.textContent = "";
        for (const d of list) sel.append(h("option", { value: d.deviceId, selected: d.label === r.inp.label }, (d.label || "AN INPUT").toUpperCase()));
        sel.disabled = false;
    }
    sel.onchange = () => use(sel.value);
    function start() {
        if (!r.inp) return;
        if (r.playNow && ED.player && ED.player.now === r.playNow) ED.player.stop();   /* (the take ▶ played: stopped) */
        r.playNow = null; r.img = null; r.headX = -1; r.sel = null;
        r.live = { mn: [], mx: [], k: 0, mn0: 1, mx0: -1 };
        r.chunks = []; r.frames = 0; r.on = true; r.take = null; r.saved = true;
        recBtn.textContent = "■ STOP"; sel.disabled = true; play.disabled = save.disabled = trimBtn.disabled = wholeBtn.disabled = true;
    }
    function stop(why) {
        if (!r.on) return;
        r.on = false;
        recBtn.textContent = "● RECORD"; sel.disabled = false;
        const n = r.chunks.reduce((a, c) => a + c[0].length, 0), nc = r.chunks.reduce((a, c) => Math.max(a, c.length), 1);
        const ch = Array.from({ length: nc }, () => new Float32Array(n));
        let o = 0;
        for (const c of r.chunks) { for (let k = 0; k < nc; k++) ch[k].set(c[Math.min(k, c.length - 1)], o); o += c[0].length; }
        r.chunks = [];
        r.live = null;
        /* mixed to one channel at once, as PT/FM's samples are: what SAVE puts on the TABLE goes to the FM-1 as it is */
        r.take = n ? { rate: r.inp.rate, ch: nc > 1 ? [ED.slices.mono(ch)] : ch, bits: 16, at: new Date(), stereo: nc > 1 } : null;
        r.saved = !r.take;
        if (why) say.textContent = why;
        play.disabled = save.disabled = trimBtn.disabled = !r.take;
        wholeBtn.disabled = true;
        lenText();
        drawTake();
    }
    /* the take's length, and the part SAVE keeps when one is selected */
    function lenText() {
        if (!r.take) { len.textContent = "NOTHING RECORDED"; return; }
        const n = r.take.ch[0].length, rt = r.take.rate, s = r.sel;
        len.textContent = "THE TAKE: " + secs(n / rt) + (r.take.stereo ? ", STEREO MIXED TO MONO" : "") +
            (s ? " · SAVE KEEPS " + secs(s[0] / rt) + " – " + secs(s[1] / rt) + " (" + secs((s[1] - s[0]) / rt) + ")" : "");
    }
    /* the take whole across the box; a selection: its ground lit, its ends yellow, the rest dimmed */
    function drawTake() {
        const g = wave.getContext("2d"), W = wave.width, Hh = wave.height;
        g.fillStyle = "#000"; g.fillRect(0, 0, W, Hh);
        r.img = null; r.headX = -1;
        if (!r.take) return;
        const x = r.take.ch[0], n = x.length, s = r.sel, X = f => Math.round(f / n * W);
        if (s) { g.fillStyle = "#3a3418"; g.fillRect(X(s[0]), 0, X(s[1]) - X(s[0]), Hh); }
        g.fillStyle = U.COL.yellow;
        for (let px = 0; px < W; px++) {
            let mn = 1, mx = -1;
            const i0 = Math.floor(px * n / W);
            for (let i = i0, e = Math.max(i + 1, Math.floor((px + 1) * n / W)); i < e; i += Math.max(1, (e - i) >> 6)) { if (x[i] < mn) mn = x[i]; if (x[i] > mx) mx = x[i]; }
            g.globalAlpha = s && (i0 < s[0] || i0 >= s[1]) ? 0.3 : 1;
            g.fillRect(px, (1 - mx) * Hh / 2, 1, Math.max(1, (mx - mn) * Hh / 2));
        }
        g.globalAlpha = 1;
        if (s) { g.fillRect(X(s[0]), 0, 2, Hh); g.fillRect(Math.max(0, X(s[1]) - 2), 0, 2, Hh); }
        r.img = g.getImageData(0, 0, W, Hh);        /* (under the white line while ▶ plays) */
    }
    /* the selection changed: none when it is the whole take; ▶ playing the take stopped (what it played is another part) */
    function selChanged() {
        const n = r.take.ch[0].length;
        if (r.sel && (r.sel[1] - r.sel[0] < 2 || (r.sel[0] <= 0 && r.sel[1] >= n))) r.sel = null;
        if (r.playNow && ED.player && ED.player.now === r.playNow) ED.player.stop();
        r.playNow = null;
        wholeBtn.disabled = !r.sel;
        lenText();
        drawTake();
    }
    /* dragging across the waveform selects (from the press to the let-go); near a selection's end, it moves that end */
    const frameAt = e => { const rc = wave.getBoundingClientRect(), n = r.take.ch[0].length; return Math.max(0, Math.min(n, Math.round((e.clientX - rc.left) / rc.width * n))); };
    let drag = null;
    wave.addEventListener("pointerdown", e => {
        if (!r || !r.take || r.on) return;
        const p = frameAt(e), near = Math.max(1, r.take.ch[0].length / 120), s = r.sel;
        drag = s && Math.abs(p - s[0]) < near ? { kind: "start" } : s && Math.abs(p - s[1]) < near ? { kind: "end" } : { kind: "new", a: p };
        try { wave.setPointerCapture(e.pointerId); } catch (err) { /* (a pointer already gone) */ }
    });
    wave.addEventListener("pointermove", e => {
        if (!drag || !r || !r.take || r.on) return;
        const p = frameAt(e), near = Math.max(1, r.take.ch[0].length / 120);
        if (drag.kind === "start" && r.sel) r.sel = [Math.min(p, r.sel[1] - 1), r.sel[1]];
        else if (drag.kind === "end" && r.sel) r.sel = [r.sel[0], Math.max(p, r.sel[0] + 1)];
        else if (drag.kind === "new" && Math.abs(p - drag.a) > near) { drag.kind = "drawn"; r.sel = [Math.min(drag.a, p), Math.max(drag.a, p)]; }
        else if (drag.kind === "drawn") r.sel = [Math.min(drag.a, p), Math.max(drag.a, p)];
        else return;
        selChanged();
    });
    wave.addEventListener("pointerup", () => { drag = null; });
    /* TRIM SILENCE: the sound within the selection (or the take), as the SAMPLE tab's: 5 ms before the first sound over
     * the silence level to 5 ms after the last; WHOLE TAKE: all again */
    trimBtn.onclick = () => {
        if (!r.take || r.on) return;
        const n = r.take.ch[0].length, a = r.sel ? r.sel[0] : 0, b = r.sel ? r.sel[1] : n;
        const [x, y] = ED.slices.trimSilence(ED.slices.mono(r.take.ch, a, b), r.take.rate);
        if (x === 0 && y === b - a) { say.textContent = "NO SILENCE AT ITS ENDS: NOTHING TRIMMED"; return; }
        r.sel = [a + x, a + y];
        selChanged();
    };
    wholeBtn.onclick = () => { if (r.take && !r.on) { r.sel = null; selChanged(); } };
    recBtn.onclick = () => r.on ? stop() : start();
    play.onclick = () => {
        if (!r.take) return;
        const s = r.sel, a = s ? s[0] : 0, b = s ? s[1] : r.take.ch[0].length;
        ED.conv.audition({ rate: r.take.rate, channels: r.take.ch.length, ch: r.take.ch }, a, b, s ? "THE TAKE'S SELECTION" : "THE TAKE");
        r.playNow = ED.player && ED.player.now;     /* (the page's own player's: its line follows while that plays) */
        r.playFrom = a;
    };
    /* what SAVE keeps (the selection, or the whole take), its original written first: the browser not keeping it refuses
     * SAVE, the take kept in the dialog */
    async function saveTake() {
        const take = r && r.take, s = r && r.sel;
        if (!take) return false;
        const part = s ? { rate: take.rate, ch: take.ch.map(c => c.slice(s[0], s[1])), bits: take.bits, at: take.at } : take;
        save.disabled = true;
        const kept = await ED.keep.original(part.ch);
        if (!r || r.take !== take) return false;                 /* (closed, or RECORD again, meanwhile) */
        if (!kept) { save.disabled = false; say.textContent = "THE BROWSER COULD NOT KEEP THE TAKE (ITS STORAGE IS FULL OR OFF): NOT SAVED"; return false; }
        const t = take.at, stamp = "RECORDING " + t.toLocaleDateString("en-GB", { day: "2-digit", month: "short" }).toUpperCase() + " " +
            String(t.getHours()).padStart(2, "0") + ":" + String(t.getMinutes()).padStart(2, "0");
        const res = ED.cutting.addSamples(ED.app.st, "L", [{ snd: part, how: { chan: "mix", rate: 44100, bits: 16 }, name: "TAKE" }], stamp);
        if (!res.ok) { save.disabled = false; say.textContent = res.msg; return false; }
        const it = res.st.L.find(i => i.id === res.ids[0]), long = ED.model.isLong(it) ? " · " + ED.model.longText(it) + ": CUT IT IN ITS TAB" : "";
        ED.app.commit(res.st, "A TAKE SAVED ON THE TABLE: " + ED.model.sampleEntry(it, res.st.L).name + long, { ok: true, select: { side: "L", ids: res.ids } });
        r.saved = true;
        return true;
    }
    save.onclick = () => saveTake().then(ok => { if (ok) close(true); });   /* (saved: the recorder closes; the status
                                                                             * line names the sample) */
    function close(force) {
        if (!r) return;
        if (r.on) stop();
        if (!r.saved && !force) {
            U.dialog({ title: "THE TAKE IS NOT SAVED", body: h("p", null, "SAVE PUTS IT ON THE TABLE AS A SAMPLE; DISCARD DROPS IT."),
                buttons: [{ label: "SAVE", cls: "primary", onClick: () => { reopen(); saveTake().then(ok => { if (ok) close(true); }); } },
                    { label: "DISCARD", cls: "danger", onClick: () => { reopen(); close(true); } }, { label: "STAY", onClick: reopen }] });
            return;
        }
        cancelAnimationFrame(r.raf);
        closeInput(r.inp);
        document.removeEventListener("keydown", onKey, true);
        r = null;
        U.closeDialog ? U.closeDialog() : (wrap.hidden = true, wrap.textContent = "");
    }
    function reopen() { wrap.textContent = ""; wrap.append(box); wrap.hidden = false; }
    function onKey(e) {
        if (!r || wrap.hidden || !box.isConnected) return;
        if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); close(); }
        else if (e.key === " " && !(e.target && /^(INPUT|SELECT|TEXTAREA|BUTTON)$/.test(e.target.tagName))) { e.preventDefault(); e.stopPropagation(); if (!recBtn.disabled) recBtn.onclick(); }
    }
    document.addEventListener("keydown", onKey, true);
    paintMeter();
    drawTake();
    const want = remembered(), list = await inputs(), found = list.find(d => d.label && d.label === want);
    await use(found ? found.deviceId : "");
}
/* a take not saved: a reload asks first */
if (typeof addEventListener !== "undefined") addEventListener("beforeunload", e => { if (r && (r.on || !r.saved)) { e.preventDefault(); e.returnValue = ""; } });

ED.recorder = { open, get open_() { return !!r; } };
})(globalThis.ED = globalThis.ED || {});
