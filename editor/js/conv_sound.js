/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Sounds into samples (specs/editor.md, Drops and conversions; specs/files/ptr.md): a WAV read here at its own rate and
 * depth (PCM 8, 16, 24, 32 bits, float 32), anything else the browser decodes (FLAC at the rate its header says, the
 * rest at 44.1 kHz); the part kept, mono or a side, resampled to the rate picked (a windowed sinc, the cutoff at the
 * lower rate's band), rounded to 8 or 16 bits, coded in PTR.
 *   decodeSound(name, bytes) -> { rate, channels, bits, length, ch: [Float32Array] }
 *   suggestRate(snd), guessNote(snd), trim(snd), drawWave(canvas, snd, start, end), audition(snd, start, end)
 *   soundToSample(snd, { start, end, chan ("mix", "l", "r"), rate, bits, name }) -> { audio (PTR), entry } */
(function (ED) {
"use strict";
const R = ED.rec, K = R.KIND, U = ED.util;
ED.conv = ED.conv || {};

function readWav(b) {
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    const tag = o => String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3]);
    if (tag(0) !== "RIFF" || tag(8) !== "WAVE") return null;
    let o = 12, fmt = null, data = null;
    while (o + 8 <= b.length) {
        const id = tag(o), len = dv.getUint32(o + 4, true);
        if (id === "fmt ") fmt = { format: dv.getUint16(o + 8, true), channels: dv.getUint16(o + 10, true), rate: dv.getUint32(o + 12, true), bits: dv.getUint16(o + 22, true) };
        if (fmt && fmt.format === 0xFFFE && len >= 26) fmt.format = dv.getUint16(o + 32, true);   /* WAVE_FORMAT_EXTENSIBLE: its sub-format */
        if (id === "data") { data = { at: o + 8, len: Math.min(len, b.length - o - 8) }; break; }
        o += 8 + len + (len & 1);
    }
    if (!fmt || !data || !(fmt.format === 1 || fmt.format === 3)) return null;
    const bps = fmt.bits / 8, frames = Math.floor(data.len / (bps * fmt.channels)), ch = [];
    for (let c = 0; c < fmt.channels; c++) ch.push(new Float32Array(frames));
    for (let i = 0; i < frames; i++) for (let c = 0; c < fmt.channels; c++) {
        const p = data.at + (i * fmt.channels + c) * bps;
        let v;
        if (fmt.format === 3) v = fmt.bits === 64 ? dv.getFloat64(p, true) : dv.getFloat32(p, true);
        else if (fmt.bits === 8) v = (b[p] - 128) / 128;
        else if (fmt.bits === 16) v = dv.getInt16(p, true) / 32768;
        else if (fmt.bits === 24) v = ((b[p] | b[p + 1] << 8 | b[p + 2] << 16) << 8 >> 8) / 8388608;
        else v = dv.getInt32(p, true) / 2147483648;
        ch[c][i] = v;
    }
    return { rate: fmt.rate, channels: fmt.channels, bits: fmt.format === 3 ? 32 : fmt.bits, length: frames, ch };
}
function flacRate(b) {                              /* STREAMINFO's sample rate and depth */
    if (String.fromCharCode(b[0], b[1], b[2], b[3]) !== "fLaC") return null;
    const rate = (b[18] << 12) | (b[19] << 4) | (b[20] >> 4), bits = (((b[20] & 1) << 4) | (b[21] >> 4)) + 1;
    return { rate, bits };
}
ED.conv.decodeSound = async function (name, b) {
    const w = readWav(b);
    if (w) return w;
    const f = flacRate(b), rate = f && f.rate ? f.rate : 44100;
    const ctx = new OfflineAudioContext(1, 1, rate);
    const buf = await ctx.decodeAudioData(b.slice().buffer);
    const ch = [];
    for (let c = 0; c < buf.numberOfChannels; c++) ch.push(buf.getChannelData(c).slice());
    return { rate: buf.sampleRate, channels: buf.numberOfChannels, bits: f ? f.bits : 16, length: buf.length, ch };
};

/* ---- looking at it ---- */
function mono(snd, chan, start, end) {
    const n = end - start, out = new Float32Array(n);
    for (let i = 0; i < n; i++) {
        if (chan === "l" || snd.channels === 1) out[i] = snd.ch[0][start + i];
        else if (chan === "r") out[i] = snd.ch[1][start + i];
        else { let s = 0; for (const c of snd.ch) s += c[start + i]; out[i] = s / snd.ch.length; }
    }
    return out;
}
/* a radix-2 FFT, in place */
function fft(re, im) {
    const n = re.length;
    for (let i = 1, j = 0; i < n; i++) {
        let bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
    }
    for (let len = 2; len <= n; len <<= 1) {
        const a = -2 * Math.PI / len, wr = Math.cos(a), wi = Math.sin(a);
        for (let i = 0; i < n; i += len) {
            let cr = 1, ci = 0;
            for (let k = 0; k < len / 2; k++) {
                const ur = re[i + k], ui = im[i + k], vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}
/* the highest frequency the sound holds (its power within 60 dB of the loudest band), and the rate that keeps it */
const RATES = [8000, 11025, 16000, 22050, 32000, 44100, 48000];
ED.conv.highest = function (snd) {
    const N = 4096, x = mono(snd, "mix", 0, snd.length), pow = new Float64Array(N / 2);
    const frames = Math.max(1, Math.min(32, Math.floor(x.length / N)));
    const stride = Math.max(N, Math.floor((x.length - N) / frames));
    for (let f = 0; f < frames; f++) {
        const re = new Float64Array(N), im = new Float64Array(N);
        for (let i = 0; i < N; i++) re[i] = (x[f * stride + i] || 0) * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / N));
        fft(re, im);
        for (let k = 0; k < N / 2; k++) pow[k] += re[k] * re[k] + im[k] * im[k];
    }
    let peak = 0;
    for (const p of pow) peak = Math.max(peak, p);
    let top = 0;
    for (let k = 1; k < N / 2; k++) if (pow[k] > peak * 1e-6) top = k;
    return top * snd.rate / N;
};
ED.conv.suggestRate = function (snd) {
    const f = ED.conv.highest(snd);
    for (const r of RATES) if (r >= f * 2.1 && r <= snd.rate) return r;
    return snd.rate;
};
/* its pitch, by autocorrelation on a steady part: a MIDI note, or null when it has none clear */
ED.conv.guessNote = function (snd) {
    const n = Math.min(4096, snd.length >> 1);
    if (n < 512) return null;
    /* the loudest stretch (a sound's attack or silence round it would mislead) */
    let at = 0, best0 = -1;
    const all = mono(snd, "mix", 0, snd.length);
    for (let a = 0; a + n <= all.length; a += n >> 1) {
        let e = 0;
        for (let i = a; i < a + n; i += 4) e += all[i] * all[i];
        if (e > best0) { best0 = e; at = a; }
    }
    const x = mono(snd, "mix", Math.max(0, at), Math.max(0, at) + n);
    let e0 = 0;
    for (const v of x) e0 += v * v;
    if (e0 < 1e-6) return null;
    let best = 0, lag = 0;
    const lo = Math.floor(snd.rate / 2000), hi = Math.min(n >> 1, Math.floor(snd.rate / 30));
    for (let l = lo; l < hi; l++) {
        let s = 0;
        for (let i = 0; i + l < n; i++) s += x[i] * x[i + l];
        s /= e0;
        if (s > best) { best = s; lag = l; }
    }
    if (best < 0.75 || !lag) return null;
    const note = Math.round(69 + 12 * Math.log2(snd.rate / lag / 440));
    return note >= 12 && note <= 127 ? note : null;
};
/* OPTIMIZE's three levels for a sample cut from snd (specs/editor.md, The tabs: SAMPLE): the cut each makes, from the
 * cut as it is. used: the part its instruments play, in the sound's frames ({ first, last }), or null (none plays
 * it); compensated: its instruments' LEVEL can make a lower level up.
 *   LOSSLESS: nothing heard changes: what no instrument plays cut off (before the first START, past the last END or
 *     LOOP END), and the silent tail (below -50 dB) after the last thing heard;
 *   SMALLER: and the proposed rate (the sound's highest frequency kept);
 *   SMALLEST: and 8 bits, a softer top end (a low-pass at a quarter of the rate), the level 6 dB lower (when LEVEL makes
 *     it up). */
ED.conv.optimize = function (snd, cut, used, compensated) {
    let start = cut.start, end = cut.end;
    if (used) { start = Math.max(start, Math.min(used.first, end - 1)); end = Math.min(end, Math.max(used.last, start + 1)); }
    const tail = ED.conv.trim(snd)[1];
    end = Math.max(start + 1, Math.min(end, tail));
    const lossless = Object.assign({}, cut, { start, end });
    const proposed = (() => { try { return ED.conv.suggestRate(snd); } catch (e) { return cut.rate; } })();
    const smaller = Object.assign({}, lossless, { rate: Math.min(cut.rate, proposed) });
    const smallest = Object.assign({}, smaller, { bits: 8, soften: 0.25 }, compensated ? { gain: Math.min(cut.gain || 0, -6) } : {});
    return { lossless, smaller, smallest };
};
ED.conv.trim = function (snd) {
    const x = mono(snd, "mix", 0, snd.length), th = 0.003;
    let a = 0, b = x.length;
    while (a < b && Math.abs(x[a]) < th) a++;
    while (b > a && Math.abs(x[b - 1]) < th) b--;
    return [Math.max(0, a - 16), Math.min(x.length, b + 64)];
};
ED.conv.drawWave = function (cv, snd, start, end) {
    const g = cv.getContext("2d"), W = cv.width, H = cv.height, n = snd.length;
    g.fillStyle = "#000"; g.fillRect(0, 0, W, H);
    for (let c = 0; c < snd.channels; c++) {
        const y0 = H * (c + 0.5) / snd.channels, hh = H / snd.channels / 2, d = snd.ch[c];
        g.fillStyle = "#F0E074";
        for (let x = 0; x < W; x++) {
            let lo = 0, hi = 0;
            const a = Math.floor(x * n / W), b = Math.max(a + 1, Math.floor((x + 1) * n / W));
            for (let i = a; i < b && i < n; i++) { if (d[i] < lo) lo = d[i]; if (d[i] > hi) hi = d[i]; }
            g.fillRect(x, y0 - hi * hh, 1, Math.max(1, (hi - lo) * hh));
        }
    }
    g.fillStyle = "rgba(0,0,0,0.65)";
    g.fillRect(0, 0, start / n * W, H);
    g.fillRect(end / n * W, 0, W, H);
    g.fillStyle = "#84E884"; g.fillRect(Math.round(start / n * W), 0, 2, H);
    g.fillStyle = "#F87C74"; g.fillRect(Math.round(end / n * W) - 2, 0, 2, H);
};
let actx = null, asrc = null;
ED.conv.audition = function (snd, start, end) {
    if (!actx) actx = new AudioContext();
    if (asrc) try { asrc.stop(); } catch (e) { /* */ }
    const buf = actx.createBuffer(snd.channels, Math.max(1, end - start), snd.rate);
    for (let c = 0; c < snd.channels; c++) buf.getChannelData(c).set(snd.ch[c].subarray(start, end));
    asrc = actx.createBufferSource();
    asrc.buffer = buf;
    asrc.connect(actx.destination);
    actx.resume();
    asrc.start();
};

/* ---- resampling: a Lanczos (a = 8) windowed sinc, its cutoff at the lower of the two rates' band ---- */
function resample(x, from, to) {
    if (from === to) return x;
    const ratio = to / from, n = Math.max(1, Math.floor(x.length * ratio)), out = new Float32Array(n);
    const fc = Math.min(1, ratio), A = 8, half = Math.ceil(A / fc);
    const sinc = t => t === 0 ? 1 : Math.sin(Math.PI * t) / (Math.PI * t);
    for (let i = 0; i < n; i++) {
        const c = i / ratio, c0 = Math.floor(c);
        let s = 0, wsum = 0;
        for (let k = c0 - half + 1; k <= c0 + half; k++) {
            const t = (c - k) * fc;
            if (Math.abs(t) >= A) continue;
            const w = sinc(t) * sinc(t / A);
            wsum += w;
            if (k >= 0 && k < x.length) s += x[k] * w;
        }
        out[i] = wsum ? s / wsum : 0;
    }
    return out;
}
ED.conv.resample = resample;
ED.conv.quantize = function (x, bits) {
    const max = bits === 8 ? 127 : 32767, out = new Int16Array(x.length);
    for (let i = 0; i < x.length; i++) out[i] = Math.max(-max - 1, Math.min(max, Math.round(x[i] * (max + 1))));
    return out;
};

/* a 2-pole low-pass (RBJ, Q 0.707) at frac of the rate, in place: a softer top end, which PTR codes in fewer bits */
function soften(x, frac) {
    const w0 = 2 * Math.PI * frac, c = Math.cos(w0), al = Math.sin(w0) / (2 * Math.SQRT1_2), a0 = 1 + al;
    const b0 = (1 - c) / 2 / a0, b1 = (1 - c) / a0, a1 = -2 * c / a0, a2 = (1 - al) / a0;
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < x.length; i++) {
        const v = x[i], y = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = v; y2 = y1; y1 = y; x[i] = y;
    }
    return x;
}
/* the part of a sound picked into a sample: its audio in PTR and its entry (specs/files/instruments.md). s: start,
 * end (the sound's frames), chan, rate, bits; soften (a low-pass at that fraction of the rate) and gain (dB, 0 or
 * less: a lower level codes in fewer bits; its instruments' LEVEL makes it up) when OPTIMIZE asks */
ED.conv.soundToSample = function (snd, s) {
    let x = resample(mono(snd, s.chan, s.start, s.end), snd.rate, s.rate);
    if (s.soften) x = soften(Float32Array.from(x), s.soften);     /* (a copy: x may be the sound's own channel) */
    if (s.gain) { const g = Math.pow(10, s.gain / 20); x = x.map(v => v * g); }
    const q = ED.conv.quantize(x, s.bits);
    if (q.length >= 1 << 20) throw new Error("A SAMPLE OF 2^20 SAMPLES OR MORE: KEEP A SHORTER PART, OR A LOWER RATE");
    const audio = R.ptrEncode(q, s.bits);
    return { audio, entry: { length: q.length, rate: s.rate, coding: s.bits === 16 ? 2 : 1, name: R.cleanName(s.name, 10) || "SAMPLE" } };
};
})(globalThis.ED = globalThis.ED || {});
