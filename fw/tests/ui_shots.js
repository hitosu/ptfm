// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
// The UI test's screenshots: each PPM (240 x 240) to a PNG at 2x to look at, and compared with the
// playground's shot of the same name (2x PNGs, tests/playground/screens.js) where there is one: the
// pixels that differ, and in which rows. Nothing fails here: the playground's figures (the CPU, the
// battery, the free memory) are its own; the list says what to look at.
//   node ui_shots.js SHOTS PLAYGROUND_SHOTS
"use strict";
const fs = require("fs"), path = require("path"), zlib = require("zlib");
const [dir, pg] = process.argv.slice(2);

function crc32(buf) {
    let c, crc = 0xFFFFFFFF;
    for (let n = 0; n < buf.length; n++) {
        c = (crc ^ buf[n]) & 0xFF;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
        crc = (crc >>> 8) ^ c;
    }
    return (crc ^ 0xFFFFFFFF) >>> 0;
}
function chunk(type, data) {
    const len = Buffer.alloc(4), t = Buffer.from(type, "ascii"), crc = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    crc.writeUInt32BE(crc32(Buffer.concat([t, data])));
    return Buffer.concat([len, t, data, crc]);
}
function png(w, h, rgb) {
    const raw = Buffer.alloc((w * 3 + 1) * h);
    for (let y = 0; y < h; y++) rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr),
                          chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}
function readPng(file) {            /* 8-bit RGB or RGBA, no interlace: enough for the playground's */
    const b = fs.readFileSync(file);
    let p = 8, w = 0, h = 0, ct = 0, idat = [];
    while (p < b.length) {
        const len = b.readUInt32BE(p), type = b.toString("ascii", p + 4, p + 8), d = b.subarray(p + 8, p + 8 + len);
        if (type === "IHDR") { w = d.readUInt32BE(0); h = d.readUInt32BE(4); ct = d[9]; }
        if (type === "IDAT") idat.push(d);
        p += 12 + len;
    }
    const bpp = ct === 6 ? 4 : 3, raw = zlib.inflateSync(Buffer.concat(idat)), out = Buffer.alloc(w * h * 3);
    const stride = w * bpp, prev = Buffer.alloc(stride), cur = Buffer.alloc(stride);
    for (let y = 0; y < h; y++) {
        const f = raw[y * (stride + 1)];
        raw.copy(cur, 0, y * (stride + 1) + 1, (y + 1) * (stride + 1));
        for (let i = 0; i < stride; i++) {
            const a = i >= bpp ? cur[i - bpp] : 0, up = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
            let v = cur[i];
            if (f === 1) v += a;
            else if (f === 2) v += up;
            else if (f === 3) v += (a + up) >> 1;
            else if (f === 4) { const pp = a + up - c, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - c);
                                v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
            cur[i] = v & 255;
        }
        cur.copy(prev);
        for (let x = 0; x < w; x++) for (let k = 0; k < 3; k++) out[(y * w + x) * 3 + k] = cur[x * bpp + k];
    }
    return { w, h, rgb: out };
}

let same = 0, differ = 0, alone = 0;
for (const f of fs.readdirSync(dir).filter(f => f.endsWith(".ppm")).sort()) {
    const b = fs.readFileSync(path.join(dir, f)), head = b.toString("latin1", 0, 20).match(/^P6\s+(\d+)\s+(\d+)\s+255\s/);
    const w = +head[1], h = +head[2], rgb = b.subarray(head[0].length);
    const big = Buffer.alloc(w * h * 12);                /* 2x, as the playground's */
    for (let y = 0; y < h * 2; y++) for (let x = 0; x < w * 2; x++) rgb.copy(big, (y * w * 2 + x) * 3, ((y >> 1) * w + (x >> 1)) * 3, ((y >> 1) * w + (x >> 1)) * 3 + 3);
    const name = f.replace(/\.ppm$/, "");
    fs.writeFileSync(path.join(dir, name + ".png"), png(w * 2, h * 2, big));
    fs.unlinkSync(path.join(dir, f));
    const ref = path.join(pg, name + ".png");
    if (!fs.existsSync(ref)) { alone++; console.log(`  ${name}: no playground shot`); continue; }
    const r = readPng(ref);
    let n = 0;
    const rows = new Set();
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const i = (y * w + x) * 3, j = (y * 2 * r.w + x * 2) * 3;
        if (Math.abs(rgb[i] - r.rgb[j]) > 8 || Math.abs(rgb[i + 1] - r.rgb[j + 1]) > 8 || Math.abs(rgb[i + 2] - r.rgb[j + 2]) > 8) { n++; rows.add(Math.floor(y / 12)); }
    }
    if (!n) { same++; console.log(`  ${name}: the same as the playground's`); continue; }
    differ++;
    console.log(`  ${name}: ${n} pixels differ, in text rows ${[...rows].sort((a, b) => a - b).join(" ")} (12-px rows from the top)`);
}
console.log(`shots: ${same} the same as the playground's, ${differ} different, ${alone} with none to compare`);
