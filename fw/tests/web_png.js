/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The native session's screens (P6 PPM, 240 x 240) as PNGs at 2x, to look at; each PPM is removed.
 *   node web_png.js A.ppm [B.ppm ...] */
"use strict";
const fs = require("fs"), zlib = require("zlib");

const CRC = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = b => { let c = ~0; for (const x of b) c = CRC[(c ^ x) & 255] ^ (c >>> 8); return ~c >>> 0; };
function chunk(type, data) {
    const t = Buffer.from(type), len = Buffer.alloc(4), c = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    c.writeUInt32BE(crc(Buffer.concat([t, data])));
    return Buffer.concat([len, t, data, c]);
}

for (const path of process.argv.slice(2)) {
    const ppm = fs.readFileSync(path);
    const head = ppm.toString("latin1", 0, 32).match(/^P6\s+(\d+)\s+(\d+)\s+255\s/);
    if (!head) throw new Error(path + ": not a P6 PPM");
    const w = +head[1], h = +head[2], px = ppm.subarray(head[0].length), S = 2;
    const raw = Buffer.alloc((w * S * 3 + 1) * h * S);
    for (let y = 0; y < h * S; y++) {
        const o = y * (w * S * 3 + 1);
        for (let x = 0; x < w * S; x++) px.copy(raw, o + 1 + x * 3, ((y / S | 0) * w + (x / S | 0)) * 3, ((y / S | 0) * w + (x / S | 0)) * 3 + 3);
    }
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w * S, 0);
    ihdr.writeUInt32BE(h * S, 4);
    ihdr[8] = 8; ihdr[9] = 2;                       /* 8 bits, RGB */
    fs.writeFileSync(path.replace(/\.ppm$/, ".png"), Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
        chunk("IHDR", ihdr), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]));
    fs.unlinkSync(path);
}
