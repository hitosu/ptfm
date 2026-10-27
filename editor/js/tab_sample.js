/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The SAMPLE tab (specs/editor.md, The tabs: SAMPLE): its waveform with START and END dragged (or a drag across it for a
 * new cut); PLAY THE CUT; OPTIMIZE (LOSSLESS, SMALLER, SMALLEST: ED.conv.optimize); CHANNELS (mixed, left or right,
 * for a stereo source); RATE (the proposed one marked); DEPTH (8 or 16 bits); TRIM SILENCE; WHOLE SAMPLE; the
 * original's and the result's size; the instruments that play it, which follow a change on SAVE (their points, their
 * LEVEL for a level lowered). A
 * TABLE sample made from a sound keeps the file's own audio and every change starts from it; any other sample starts
 * from its own audio, so on the FM-1 panel a cut is final once written. The sample's source, OPTIMIZE's levels and its
 * sampler following a change are optimize.js's (the TABLE's OPTIMIZE SAMPLES shares them). */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, U = ED.ui, T = ED.tabs, O = ED.optimize, h = U.h, H2 = M.H2;
const RATES = [8000, 11025, 16000, 22050, 32000, 44100, 48000];

T.kinds.sample = function (t, api) {
    const side = t.side, items = api.st[side], d = api.draft;
    const src = t.src = t.src || O.source(t.base, items);
    const cut = Object.assign({}, src.cut || { start: 0, end: src.ch[0].length, chan: "mix", rate: src.rate, bits: 8 }, d.sound ? d.sound.cut : t.cut || {});
    const snd = { rate: src.rate, channels: src.ch.length, length: src.ch[0].length, ch: src.ch, bits: src.bits };
    const entry = M.sampleEntry(d, items) || { name: "SAMPLE", rate: cut.rate, length: 0, coding: 1 };
    const proposed = (() => { try { return ED.conv.suggestRate(snd); } catch (e) { return src.rate; } })();
    /* a change: the sample made again from its source */
    function redo(c, what) {
        let n;
        try { n = O.remake(api.draft, { src, snd, entry }, c); }
        catch (e) { U.status(String(e.message || e).toUpperCase(), { err: true }); return; }
        if (src.own) t.cut = c;
        api.set(n, what);
        api.refresh();
    }
    /* the cut the item had when the tab opened (its own audio: all of it), what its sampler's instruments that play it
     * use of the sound (in the sound's frames), and where a point of the sample lies in the sound and back */
    const cut0 = (t.base.sound && t.base.sound.cut) || src.cut || cut;
    const { owner, players, used } = O.context(t.base, items, src);
    /* an owned sample's entry lives in its sampler: written there on SAVE, its instruments following the change */
    if (d.owner) t.onSave = st => ({ st: O.follow(st, side, t.itemId, cut, cut0, snd.rate) });
    /* OPTIMIZE: its three levels made, their sizes shown, the one picked put in the draft (heard before SAVE) */
    const optimize = btn => {
        const lv = ED.conv.optimize(snd, cut, used, players.length > 0);
        const size = c => { try { return ED.conv.soundToSample(snd, Object.assign({ name: entry.name }, c)).audio.length + 16; } catch (e) { return null; } };
        const now = M.size(api.draft), r = btn.getBoundingClientRect();
        const line = (name, c, what) => {
            const n = size(c);
            return { label: name + " · " + (n == null ? "TOO LONG" : U.kb(n) + " (" + (n < now ? "−" + U.kb(now - n) : "NOTHING SAVED") + ")") + " · " + what,
                off: n == null || n >= now, fn: () => redo(c, "OPTIMIZE: " + name) };
        };
        U.menu(r.left, r.bottom + 2, [
            line("LOSSLESS", lv.lossless, "NOTHING HEARD CHANGES"),
            line("SMALLER", lv.smaller, "THE PROPOSED RATE, " + lv.smaller.rate + " HZ"),
            line("SMALLEST", lv.smallest, "8 BITS, A SOFTER TOP" + (lv.smallest.gain ? ", 6 DB LOWER (ITS INSTRUMENTS' LEVEL UP 6 DB)" : ""))]);
    };

    /* ---- the waveform ---- */
    const cv = h("canvas", { width: 1200, height: 220 });
    const len = snd.length;
    const wave = h("div", { class: "wavebox" }, cv);
    function drawWave(a, b) {
        const g = cv.getContext("2d"), W = cv.width, Hh = cv.height;
        g.fillStyle = "#000"; g.fillRect(0, 0, W, Hh);
        const x0 = a / len * W, x1 = b / len * W;
        g.fillStyle = "#16140a"; g.fillRect(x0, 0, x1 - x0, Hh);
        for (let c = 0; c < snd.channels; c++) {
            const data = snd.ch[c], top = c * Hh / snd.channels, hh = Hh / snd.channels;
            g.fillStyle = U.COL.yellow;
            for (let x = 0; x < W; x++) {
                const i0 = Math.floor(x * len / W), i1 = Math.max(i0 + 1, Math.floor((x + 1) * len / W));
                let mn = 1, mx = -1;
                for (let i = i0; i < i1; i += Math.max(1, (i1 - i0) >> 6)) { const v = data[i] || 0; if (v < mn) mn = v; if (v > mx) mx = v; }
                g.globalAlpha = x >= x0 && x <= x1 ? 1 : 0.35;
                g.fillRect(x, top + (1 - mx) * hh / 2, 1, Math.max(1, (mx - mn) * hh / 2));
            }
            g.globalAlpha = 1;
        }
        g.fillStyle = U.COL.white; g.fillRect(x0, 0, 2, Hh); g.fillRect(x1 - 2, 0, 2, Hh);
    }
    let dragA = null, dragKind = null, live = { start: cut.start, end: cut.end };
    const at = e => { const r = cv.getBoundingClientRect(); return Math.max(0, Math.min(len, Math.round((e.clientX - r.left) / r.width * len))); };
    cv.addEventListener("pointerdown", e => {
        const p = at(e), near = Math.max(1, len / 80);
        dragKind = Math.abs(p - live.start) < near ? "start" : Math.abs(p - live.end) < near ? "end" : "new";
        dragA = p;
        try { cv.setPointerCapture(e.pointerId); } catch (err) { /* (a pointer already gone) */ }
    });
    cv.addEventListener("pointermove", e => {
        if (dragKind == null) return;
        const p = at(e);
        if (dragKind === "start") live.start = Math.min(p, live.end - 1);
        else if (dragKind === "end") live.end = Math.max(p, live.start + 1);
        else { live.start = Math.min(dragA, p); live.end = Math.max(dragA, p, live.start + 1); }
        drawWave(live.start, live.end);
        pos.textContent = posText();
    });
    cv.addEventListener("pointerup", () => {
        if (dragKind == null) return;
        dragKind = null;
        if (live.start !== cut.start || live.end !== cut.end) redo(Object.assign({}, cut, live), "THE CUT");
    });
    const posText = () => "START " + (live.start / snd.rate).toFixed(3) + " S · END " + (live.end / snd.rate).toFixed(3) + " S · " + ((live.end - live.start) / snd.rate).toFixed(3) + " S";
    const pos = h("div", { class: "hint" }, posText());
    requestAnimationFrame(() => drawWave(cut.start, cut.end));

    /* ---- the choices ---- */
    const rates = RATES.filter(r => r <= Math.max(snd.rate, cut.rate)).concat(RATES.includes(cut.rate) ? [] : [cut.rate]).sort((a, b) => a - b);
    const ctl = T.card("THE SAMPLE",
        T.text("NAME", () => entry.name || "", v => {
            const n = Object.assign({}, api.draft, { entry: M.packedEntry(Object.assign({}, entry, { name: v })) });
            if (!n.owner) n.name = v;
            api.set(n, "NAME " + v); api.refresh();
        }, 10),
        snd.channels > 1 ? T.choice("CHANNELS", ["MIXED", "LEFT", "RIGHT"], () => ["mix", "l", "r"].indexOf(cut.chan), v => redo(Object.assign({}, cut, { chan: ["mix", "l", "r"][v] }), "CHANNELS")) : null,
        T.field("RATE", h("select", { onchange: e => redo(Object.assign({}, cut, { rate: +e.target.value }), "RATE " + e.target.value + " HZ") },
            rates.map(r => h("option", { value: r, selected: r === cut.rate }, r + " HZ" + (r === proposed ? " · PROPOSED" : "") + (r === snd.rate ? " · THE SOURCE'S" : ""))))),
        T.choice("DEPTH", ["8 BITS", "16 BITS"], () => cut.bits === 16 ? 1 : 0, v => redo(Object.assign({}, cut, { bits: v ? 16 : 8 }), "DEPTH " + (v ? 16 : 8) + " BITS")),
        h("div", { class: "rowbtns", style: "margin-top:8px" },
            h("button", { class: "btn small", onclick: () => ED.player.toggle(api.draft, { st: api.stWith() }) }, "▶ PLAY THE CUT"),
            h("button", { class: "btn small primary", title: "MAKE IT TAKE LESS ROOM: LOSSLESS, SMALLER OR SMALLEST, EACH WITH WHAT IT SAVES", onclick: e => optimize(e.currentTarget) }, "OPTIMIZE ▾"),
            h("button", { class: "btn small", onclick: () => { const [a, b] = ED.conv.trim(snd); redo(Object.assign({}, cut, { start: a, end: b }), "TRIM SILENCE"); } }, "TRIM SILENCE"),
            h("button", { class: "btn small", onclick: () => redo(Object.assign({}, cut, { start: 0, end: len }), "WHOLE SAMPLE") }, "WHOLE SAMPLE")));
    const origBytes = snd.length * snd.channels * Math.ceil((src.bits || 16) / 8);
    const playedBy = [];
    if (owner) R.unpackSampler(owner.recs[0].payload).instruments.forEach((s, i) => { if (s && s.sample === d.idx) playedBy.push((side === "R" ? H2(owner.num) : "--") + ":" + H2(i) + " " + s.name); });
    const info = T.card("SIZES AND USES", h("div", { class: "sinfo" },
        h("div", null, h("b", null, src.own ? "ITS AUDIO" : "THE ORIGINAL"), U.kb(origBytes) + " · " + snd.rate + " HZ · " + snd.channels + (snd.channels > 1 ? " CHANNELS" : " CHANNEL") + (src.own ? " (NO ORIGINAL: A CUT IS FINAL ONCE SAVED)" : "")),
        h("div", null, h("b", null, "THE RESULT"), U.kb(M.size(api.draft)) + " AS STORED · " + entry.rate + " HZ · " + (entry.coding === 2 ? 16 : 8) + " BITS · " + (entry.rate ? (entry.length / entry.rate).toFixed(2) : "?") + " S"),
        h("div", null, h("b", null, "PLAYED BY"), playedBy.length ? playedBy.join(", ") : owner ? "NO INSTRUMENT" : "NOTHING YET (A SAMPLE ON ITS OWN)")));
    return h("div", null, h("div", { class: "card", style: "margin-bottom:12px" }, wave, pos), h("div", { class: "cards" }, ctl, info));
};
})(globalThis.ED = globalThis.ED || {});
