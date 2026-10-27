/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the virtual screen driver. It works as the firmware's display stack does
 * (fw/device/lcd.c and gfx.c), so UI code written for it reads like the C it will become.
 *
 * const lcd = PTFM.createLcd(dev):
 *   the panel (ST7789): 240 x 240 pixels of RGB565 in its own memory, written only in windows:
 *     lcd.fill(x, y, w, h, c)          a window of one colour (lcd_fill)
 *     lcd.blit(x, y, w, h, px)         a window of pixels, a Uint16Array of RGB565 (lcd_blit)
 *   the canvas (gfx.c): one strip of up to 240 x 124 pixels to draw in, then blit:
 *     lcd.gfx.begin(w, h, bg)          (cv_begin) the strip, filled with bg
 *     lcd.gfx.oy                       (cv_oy) added to every y drawn: -y0 draws screen rows from y0
 *     lcd.gfx.pset / rect / line(...)  (cv_pset, cv_rect, cv_line), clipped to the strip
 *     lcd.gfx.text(x, y, font, s, c)   (cv_text) top left at x, y; returns the end x
 *     lcd.gfx.textWidth(font, s)       (text_w)
 *     lcd.gfx.blit(x, y), blitFrom(x, y, r0)   (cv_blit, cv_blit_from)
 *   PTFM.RGB(r, g, b)                  gfx.c's RGB(): 8-bit components to RGB565
 *   lcd.stats                          windows sent, their bytes, and the time they take at the
 *                                      measured 60 MHz rate (a full frame, 115,200 B: ~30.2 ms)
 * Fonts are fonts.js's (S 8x16, L 16x32, T 6x12): whole pixels, as the firmware's pixel fonts.
 * The page shows the panel's memory at the next animation frame, its RGB565 widened as the host
 * screenshots do (c * 255 / 31, / 63). */
window.PTFM = window.PTFM || {};

PTFM.RGB = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

PTFM.createLcd = function (dev) {
    "use strict";
    const W = 240, H = 240, CV_MAX = 240 * 124;
    const FULL_US = 30200, FULL_BYTES = W * H * 2;   /* a full frame at 60 MHz (measured on the FM-1) */
    const C5 = [], C6 = [];
    for (let i = 0; i < 64; i++) {
        C6[i] = Math.floor(i * 255 / 63);
        if (i < 32) C5[i] = Math.floor(i * 255 / 31);
    }

    const gram = new Uint16Array(W * H);             /* the panel's memory, black at power-on */
    const img = new ImageData(W, H);
    for (let i = 3; i < img.data.length; i += 4) img.data[i] = 255;
    const stats = { windows: 0, bytes: 0, us: 0 };
    let due = false;

    function show() {
        due = false;
        dev.glass.putImageData(img, 0, 0);
        dev.present();
    }
    function window_(w, h) {                         /* lcd_window, then w * h pixels sent */
        stats.windows++;
        stats.bytes += w * h * 2;
        stats.us += w * h * 2 * FULL_US / FULL_BYTES;
        if (!due) {
            due = true;
            requestAnimationFrame(show);
        }
    }
    function put(i, c) {
        const o = i * 4;
        gram[i] = c;
        img.data[o] = C5[c >> 11];
        img.data[o + 1] = C6[c >> 5 & 63];
        img.data[o + 2] = C5[c & 31];
    }
    function fill(x, y, w, h, c) {
        if (!w || !h || x >= W || y >= H) return;
        w = Math.min(w, W - x);
        h = Math.min(h, H - y);
        window_(w, h);
        for (let j = 0; j < h; j++)
            for (let i = 0; i < w; i++) put((y + j) * W + x + i, c);
    }
    function blit(x, y, w, h, px) {
        if (!w || !h) return;
        window_(w, h);
        for (let j = 0; j < h; j++)
            for (let i = 0; i < w; i++)
                if (x + i < W && y + j < H) put((y + j) * W + x + i, px[j * w + i]);
    }

    /* gfx.c's canvas */
    const cv = { px: new Uint16Array(CV_MAX), w: 0, h: 0 };
    function glyph(font, ch) {                       /* gfx.c: lower case folds when a font has none; missing: "?" */
        let c = ch.codePointAt(0);
        const last = font.first + font.glyphs.length - 1;
        if (c >= 97 && c <= 122 && last < 97) c -= 32;
        if (c < font.first || c > last || (c >= 127 && c < 160)) c = 63;
        return font.glyphs[c - font.first];
    }
    const gfx = {
        oy: 0,
        begin(w, h, bg) {
            if (w * h > CV_MAX) h = Math.floor(CV_MAX / w);
            cv.w = w;
            cv.h = h;
            cv.px.fill(bg, 0, w * h);
        },
        blit(x, y) { blit(x, y, cv.w, cv.h, cv.px); },
        blitFrom(x, y, r0) { if (r0 < cv.h) blit(x, y + r0, cv.w, cv.h - r0, cv.px.subarray(r0 * cv.w)); },
        pset(x, y, c) {
            y += gfx.oy;
            if (x >= 0 && x < cv.w && y >= 0 && y < cv.h) cv.px[y * cv.w + x] = c;
        },
        rect(x, y, w, h, c) {
            const x1 = Math.min(x + w, cv.w), y1 = Math.min(y + h + gfx.oy, cv.h);
            for (let j = Math.max(y + gfx.oy, 0); j < y1; j++)
                cv.px.fill(c, j * cv.w + Math.max(x, 0), j * cv.w + Math.max(x1, Math.max(x, 0)));
        },
        line(x0, y0, x1, y1, c) {                    /* Bresenham, as cv_line */
            const dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
            let err = dx + dy;
            for (let guard = 2000; guard--;) {
                const e2 = 2 * err;
                gfx.pset(x0, y0, c);
                if (x0 === x1 && y0 === y1) break;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
        },
        text(x, y, font, s, c) {
            const k = font.scale || 1, hex = Math.ceil(font.w / 8) * 2, bits = hex * 4;
            for (const ch of s) {
                const g = glyph(font, ch);
                for (let r = 0; r < font.h; r++) {
                    const row = parseInt(g.substr(r * hex, hex), 16);
                    for (let b = 0; b < font.w; b++) {
                        if (!(row >> (bits - 1 - b) & 1)) continue;
                        for (let i = 0; i < k * k; i++) gfx.pset(x + b * k + i % k, y + r * k + (i / k | 0), c);
                    }
                }
                x += font.w * k;
            }
            return x;
        },
        textWidth(font, s) { return [...s].length * font.w * (font.scale || 1); },
    };

    return { fill, blit, gfx, stats, gram };
};
