/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: drawing. One 240 x 240 frame of RGB565 drawn whole, then sent to the virtual
 * screen (lcd.js) in one window. Colours are palette indices, the roles of specs/ui/colours.md:
 * D.BLACK .. D.WHITE, and D.DEEP + i for colour i's deep shade (a fill, never text). A theme is a
 * palette (SETTINGS -> THEME).
 *
 *   D.clear(c), D.rect(x, y, w, h, c), D.box(...) (an outline), D.hline / vline / line / px
 *   D.text(x, y, font, s, c, crop) -> the end x; crop [r0, r1]: only glyph rows r0..r1-1, drawn
 *     from y (S in a 12-px row: D.CROP_S)
 *   D.runs(x, y, font, [[s, c], ...], crop), D.tw(font, s), D.centre(font, s, x0, w)
 *   fonts D.F.T (6 x 12), S (8 x 16), B (12 x 24 bold), L (16 x 32: S doubled) */
window.PTFM = window.PTFM || {};

PTFM.draw = (function () {
    "use strict";
    const W = 240, H = 240;
    const fb = new Uint16Array(W * H);
    const D = { W, H, fb, DEEP: 16, CROP_S: [2, 14] };
    ["BLACK", "BLUE", "GREEN", "CYAN", "RED", "MAGENTA", "BROWN", "LGREY", "DGREY", "LBLUE", "LGREEN",
     "LCYAN", "LRED", "LMAGENTA", "YELLOW", "WHITE"].forEach((n, i) => { D[n] = i; });

    /* the fonts, decoded once: per glyph, its rows as bit masks (bit w-1 = the left pixel) */
    function decode(f) {
        const hex = Math.ceil(f.w / 8) * 2, shift = hex * 4 - f.w;
        const glyphs = f.glyphs.map(g => {
            const rows = new Uint32Array(f.h);
            for (let r = 0; r < f.h; r++) rows[r] = parseInt(g.substr(r * hex, hex), 16) >>> shift;
            return rows;
        });
        return { w: f.w * (f.scale || 1), h: f.h * (f.scale || 1), gw: f.w, gh: f.h, k: f.scale || 1,
                 first: f.first, glyphs };
    }
    D.F = { T: decode(PTFM.FONTS.T), S: decode(PTFM.FONTS.S), B: decode(PTFM.FONTS.B), L: decode(PTFM.FONTS.L) };
    /* the count-in's digit (specs/ui/transport.md): S's glyphs at 4x, 32 x 64 */
    D.F.X = Object.assign({}, D.F.S, { w: D.F.S.gw * 4, h: D.F.S.gh * 4, k: 4 });

    /* the themes (specs/ui/colours.md): 16 colours each, on black but the last six (colour 0 the ground: the light
     * ones turn brightness round, the darkest colour the brightest role; COMMANDER on blue) */
    const THEMES = {
        DEFAULT: "000000 4C5CD0 4AAA4A 3AA8A8 C84C44 B054B0 C87C34 B4B4AC 5C5C5C 7C8CF8 84E884 84E8E8 F87C74 E884E8 F0E074 F4F4EC",
        EGA:     "000000 0000AA 00AA00 00AAAA AA0000 AA00AA AA5500 AAAAAA 555555 5555FF 55FF55 55FFFF FF5555 FF55FF FFFF55 FFFFFF",
        PASTEL:  "000000 8890D8 8CC88C 8CC4C4 D88C8C C890C8 D8AC84 C8C8C0 6C6C6C A8B0F0 B8F0B8 B8F0F0 F8B4AC F0B8F0 F4ECAC F8F8F0",
        NEON:    "000000 3A2AE0 30E060 00D0E0 FF2A6A B030FF FF8A00 C8C8E0 545474 8A6AFF A8FF30 30F0FF FF4AA0 FF50FF FFF030 FFFFFF",
        SUNSET:  "000000 2A6A80 8AB04A 3AA0A0 E0442E A84870 D0803A D8C4AC 6A5A50 50C0BC F0C048 60D4CC FF7A48 F08AA8 FFD870 FFF2E4",
        ICE:     "000000 3050C0 40B0A0 58A8D8 F07840 8A60D0 D89858 B8C8D8 506070 88ACF8 70E0D0 98DCF8 C4A4F8 E8B4F8 F8C080 F0F8FF",
        CLEAR:   "000000 0072B2 6CC070 A0A0A0 D55E00 8A5A84 A87848 C0C0C0 606060 3A92D8 009E73 56B4E9 E69F00 CC79A7 F0E442 FFFFFF",
        GREEN:   mono([0x3c, 0xff, 0x6c]),
        AMBER:   mono([0xff, 0xb0, 0x30]),
        WHITE:   mono([0xe8, 0xe8, 0xe8]),
        DAYLIGHT:  "D0D0C8 5060D0 2F762F 227171 B73A33 A040A0 A64B1A 2C2C28 767671 3742BE 175D17 085A61 9E221A 832592 5C5300 000000",
        PAPER:     "FFFFFF 3C78E0 29953E 2F7F8A D73A30 B03A90 B0481A 1C1C1C 999999 0A54C8 177837 0B6E7A C8201E 8040D0 766A00 000000",
        MANUAL:    "F4EEDC 4A68B0 48862F 3A7070 C04030 9A4A8A A04A1C 2A2620 958E7F 2A4AA0 386B1F 1A6A70 B02A20 7A3A8A 695E00 0A0806",
        SILVER:    "D4D4D0 949492 51514F 5F5F5E 0B0B0A 757572 595957 373736 7B7B79 595957 2A2A2A 40403E 111111 484847 1E1E1D 000000",
        LCD:       "C8D2AA 8F9978 525C43 5F694F 141E0C 737D5F 5A644A 3B452F 788264 5A644A 303A25 434D36 192311 4B553C 252F1B 0A1404",
        COMMANDER: "0000A8 000000 54D454 C8C854 E46464 C570C5 E0A050 00C0C0 6464EB A0A0FF 80FF80 A8FFFF FF8070 FF80FF FFFF54 FFFFFF",
    };
    /* a one-colour theme: every role one of about 6 intensities of one hue */
    function mono(rgb) {
        const k = [0, .30, .62, .55, 1, .45, .58, .74, .34, .58, .80, .70, .92, .66, .86, 1].map((v, i) => i === 4 ? .95 : v);
        return k.map(v => rgb.map(c => Math.round(c * v).toString(16).padStart(2, "0")).join("")).join(" ");
    }
    D.THEMES = Object.keys(THEMES);
    const pal = new Uint16Array(32), rgb = [];
    D.rgb = rgb;
    const to565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    D.theme = function (name) {
        const cs = THEMES[name].split(" ").map(h => { const v = parseInt(h, 16); return [v >> 16, v >> 8 & 255, v & 255]; });
        /* the deep shade: 30 % of the way from the ground, the fraction dropped (toward it), as the firmware's integers */
        const deep = (c, g) => g + Math.trunc((c - g) * 3 / 10);
        cs.forEach(([r, g, b], i) => {
            rgb[i] = [r, g, b];
            pal[i] = to565(r, g, b);
            pal[16 + i] = to565(deep(r, cs[0][0]), deep(g, cs[0][1]), deep(b, cs[0][2]));
        });
        D.name = name;
    };
    /* text on a fill of colour c: WHITE, or BLACK where the theme's c is nearer white than black (as gfx.c's g_ink) */
    const luma = ([r, g, b]) => 2126 * r + 7152 * g + 722 * b;
    D.ink = c => { const y = luma(rgb[c & 15]); return Math.abs(y - luma(rgb[15])) >= Math.abs(y - luma(rgb[0])) ? D.WHITE : D.BLACK; };
    D.theme("DEFAULT");

    /* clipping */
    let cx0 = 0, cy0 = 0, cx1 = W, cy1 = H;
    D.clip = (x, y, w, h) => { cx0 = Math.max(0, x); cy0 = Math.max(0, y); cx1 = Math.min(W, x + w); cy1 = Math.min(H, y + h); };
    D.noclip = () => { cx0 = 0; cy0 = 0; cx1 = W; cy1 = H; };

    D.clear = c => fb.fill(pal[c]);
    D.px = (x, y, c) => { if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) fb[y * W + x] = pal[c]; };
    D.rect = (x, y, w, h, c) => {
        const x0 = Math.max(x | 0, cx0), x1 = Math.min((x + w) | 0, cx1), y1 = Math.min((y + h) | 0, cy1), v = pal[c];
        if (x1 <= x0) return;
        for (let j = Math.max(y | 0, cy0); j < y1; j++) fb.fill(v, j * W + x0, j * W + x1);
    };
    D.hline = (x, y, w, c) => D.rect(x, y, w, 1, c);
    D.vline = (x, y, h, c) => D.rect(x, y, 1, h, c);
    D.box = (x, y, w, h, c) => { D.hline(x, y, w, c); D.hline(x, y + h - 1, w, c); D.vline(x, y, h, c); D.vline(x + w - 1, y, h, c); };
    D.line = (x0, y0, x1, y1, c) => {
        x0 |= 0; y0 |= 0; x1 |= 0; y1 |= 0;
        const dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        let err = dx + dy;
        for (let n = 0; n < 1000; n++) {
            D.px(x0, y0, c);
            if (x0 === x1 && y0 === y1) break;
            const e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    };
    D.circle = (x, y, r, c) => {
        for (let a = 0; a < 64; a++) {
            const t = a / 64 * Math.PI * 2;
            D.px(Math.round(x + Math.cos(t) * r), Math.round(y + Math.sin(t) * r), c);
        }
    };
    D.disc = (x, y, r, c) => {
        for (let j = -r; j <= r; j++) {
            const w = Math.round(Math.sqrt(r * r - j * j + r * .5));
            D.rect(x - w, y + j, 2 * w + 1, 1, c);
        }
    };

    function glyph(f, ch) {
        let n = ch.charCodeAt(0);
        if (n >= 97 && n <= 122) n -= 32;                  /* capitals only, everywhere */
        if (n < f.first || n >= f.first + f.glyphs.length) n = 63;
        return f.glyphs[n - f.first];
    }
    D.text = function (x, y, f, s, c, crop) {
        s = String(s);
        const r0 = crop ? crop[0] : 0, r1 = crop ? crop[1] : f.gh, k = f.k, v = pal[c], top = 1 << (f.gw - 1);
        for (let i = 0; i < s.length; i++, x += f.w) {
            if (s[i] === " ") continue;
            const g = glyph(f, s[i]);
            for (let r = r0; r < r1; r++) {
                const bits = g[r];
                if (!bits) continue;
                for (let b = 0; b < f.gw; b++) {
                    if (!(bits & (top >>> b))) continue;
                    const px = x + b * k, py = y + (r - r0) * k;
                    for (let j = 0; j < k; j++) {
                        const yy = py + j;
                        if (yy < cy0 || yy >= cy1) continue;
                        for (let q = 0; q < k; q++) {
                            const xx = px + q;
                            if (xx >= cx0 && xx < cx1) fb[yy * W + xx] = v;
                        }
                    }
                }
            }
        }
        return x;
    };
    D.tw = (f, s) => String(s).length * f.w;
    D.centre = (f, s, x0, w) => x0 + Math.floor((w - D.tw(f, s)) / 2);
    D.runs = function (x, y, f, runs, crop) {
        for (const [s, c] of runs) x = D.text(x, y, f, s, c, crop);
        return x;
    };

    /* the drawn symbols (specs/ui/frame.md: "symbols are drawn, not glyphs") */
    D.play = (x, y, c) => { for (let i = 0; i < 9; i++) D.rect(x, y + i, 1 + Math.min(i, 8 - i), 1, c); };     /* 5 x 9 */
    D.pause = (x, y, c) => { D.rect(x, y, 2, 9, c); D.rect(x + 4, y, 2, 9, c); };                                /* 6 x 9 */
    D.rec = (x, y, c) => D.disc(x + 3, y + 4, 3, c);                                                                /* 7 x 9 */
    /* scrolling: up and down 5 x 9, left and right 9 x 5 (an arrowhead of 3 rows at each end) */
    D.updown = (x, y, c) => { for (let i = 0; i < 3; i++) { D.hline(x + 2 - i, y + i, 1 + 2 * i, c); D.hline(x + 2 - i, y + 8 - i, 1 + 2 * i, c); } D.vline(x + 2, y + 3, 3, c); };
    D.leftright = (x, y, c) => { for (let i = 0; i < 3; i++) { D.vline(x + i, y + 2 - i, 1 + 2 * i, c); D.vline(x + 8 - i, y + 2 - i, 1 + 2 * i, c); } D.hline(x + 3, y + 2, 3, c); };
    D.diamond = (x, y, c) => { for (let i = 0; i < 7; i++) D.rect(x + 3 - Math.min(i, 6 - i), y + i + 2, 1 + 2 * Math.min(i, 6 - i), 1, c); };

    D.blitTo = lcd => lcd.blit(0, 0, W, H, fb);
    return D;
})();
