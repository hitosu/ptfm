/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The FM-1's own pictures (specs/editor.md, The tabs): the firmware's drawing code, compiled into the page's
 * WebAssembly (editor/wasm/ed_pics.c), draws each one from the item's bytes exactly as on the FM-1's screen; shown at
 * a whole number of the display's pixels a screen pixel. One instance of the firmware on the page's thread draws them
 * (its flash holds the image a picture reads: a sample instrument's samples, a FUSION slot's pattern) and measures
 * AUTO LEVEL.
 *   ED.pics.ready() -> Promise<the firmware>; instrument(it, st, slot, what, page, group) -> a canvas or null;
 *   fx(effect, fx64, song) -> a canvas or null; image(recs) lays an image into its flash (once, kept while equal);
 *   WHAT { DETAILS, EDIT, CHIP_ROW, WAVE, SAMPLES } */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND;
const WHAT = { DETAILS: 0, EDIT: 1, CHIP_ROW: 2, WAVE: 3, SAMPLES: 4 };
let fw = null, starting = null, loaded = null;
function ready() {
    if (fw) return Promise.resolve(fw);
    if (!starting) starting = (async () => {
        const wasm = Uint8Array.from(atob(PTFM_ED_WASM.wasm), c => c.charCodeAt(0));
        /* (the preview's room: a picture or AUTO LEVEL of anything on the TABLE, however big) */
        fw = (await new ED.Firmware(await WebAssembly.compile(wasm)).start(null)).preview();
        return fw;
    })();
    return starting;
}
const available = () => !!(fw && fw.ex && fw.ex.ed_picture);

/* the answer {x, y, w, h, px} into a canvas: RGB565 to RGB, scale screen pixels a picture pixel */
function canvasOf(at, scale) {
    const mem = fw.mem(), dv = new DataView(mem.buffer, at, 16);
    const x = dv.getInt32(0, true), y = dv.getInt32(4, true), w = dv.getInt32(8, true), hh = dv.getInt32(12, true);
    if (!w || !hh) return null;
    const px = new Uint16Array(mem.buffer, at + 16, 240 * 240);
    const c = document.createElement("canvas");
    c.width = w; c.height = hh;
    c.className = "pic";
    const s = scale || (innerWidth < 560 ? 1 : 2);
    c.style.width = w * s + "px";
    const g = c.getContext("2d"), img = g.createImageData(w, hh);
    for (let r = 0; r < hh; r++) for (let q = 0; q < w; q++) {
        const v = px[(y + r) * 240 + x + q], o = (r * w + q) * 4;
        img.data[o] = ((v >> 11) & 31) * 255 / 31; img.data[o + 1] = ((v >> 5) & 63) * 255 / 63; img.data[o + 2] = (v & 31) * 255 / 31; img.data[o + 3] = 255;
    }
    g.putImageData(img, 0, 0);
    return c;
}
/* an instrument's picture from its slot's bytes; the image it reads (its cartridge with its samples, a FUSION's
 * patterns) laid into this instance first */
function instrument(it, st, slot, what, page, group) {
    if (!available()) return null;
    let num = it.num != null ? it.num : 0;
    if (it.kind === K.SAMPLER || it.kind === K.FUSION) {
        try {
            const p = ED.player.plan(it, st, { slot });
            image(p.recs);
            const e = p.job && p.job.events && p.job.events.find(x => x[1] === "ed_key_on");
            if (e) num = e[2][1];
        } catch (e) { /* (drawn as the FM-1 draws what is missing) */ }
    }
    const size = R.SLOT[it.kind] ? R.SLOT[it.kind][1] : it.kind === K.SAMPLER ? R.SAMPLER_SLOT : 0;
    const b = it.recs[0].payload, at = it.kind === K.SAMPLER ? 11 + slot * R.SAMPLER_SLOT : 10 + slot * size;
    const ex = fw.ex, buf = ex.ed_pic_buf();
    fw.mem().fill(0, buf, buf + 512);
    fw.mem().set(b.subarray(at, at + size), buf);
    return canvasOf(ex.ed_picture(what, it.kind, size, num << 8 | slot, page || 0, group || 0, 0));
}
let lastRecs = new Set();
/* the image this instance's flash holds, laid in again only when it changed; every loader goes through it (AUTO
 * LEVEL too), so a picture never reads another item's image */
function image(recs) {
    const key = recs.map(r => r.kind + "." + r.num + "." + r.sub + "." + r.payload.length).join(",");
    if (key === loaded && recs.every(r => lastRecs.has(r.payload))) return;
    fw.load(recs.map(r => Object.assign({ crc: 0 }, r)));
    loaded = key; lastRecs = new Set(recs.map(r => r.payload));
}
/* FX EDIT's picture of an effect from an FX setup's 64 bytes; song { tempo, ticks, rpb } for the tempo */
function fx(effect, fx64, song) {
    if (!available()) return null;
    const ex = fw.ex, buf = ex.ed_pic_buf(), mem = fw.mem();
    mem.fill(0, buf, buf + 512);
    mem.set(fx64.subarray(0, 64), buf + 256);
    if (ex.ed_fx_setup(effect) < 0) return null;
    if (song && song.ticks && song.rpb) new DataView(mem.buffer).setInt32(buf + 18 * 4, Math.round(song.tempo * 240 / (song.ticks * song.rpb)), true);
    return canvasOf(ex.ed_fx_picture(effect), 1);
}
ED.pics = { ready, available, instrument, fx, image, WHAT, get fw() { return fw; } };
})(globalThis.ED = globalThis.ED || {});
