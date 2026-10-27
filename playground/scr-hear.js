/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the HEAR group and SYSTEM: VISUAL (the voices' scopes, the output's scope,
 * its waveform scrolling by, its spectrum, the tracks' levels), STATUS (the start screen,
 * performance, storage, the device) and SETTINGS; specs/ui/screens/visual.md, status.md,
 * settings.md. */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, B = U.B, E = U.E, P = PTFM.player, A = PTFM.audio;
    const Y0 = U.BODY_Y, H = U.BODY_H;

    /* ======================================================================= VISUAL === */
    const VIEWS = ["VOICES", "SCOPE", "WAVE", "SPECTRUM", "LEVELS", "FOMA"];
    const vis = { view: 0, zoom: 2, gain: 3, stereo: true, freeze: false, wave: [], buf: [new Float32Array(2048), new Float32Array(2048)], fbuf: new Float32Array(1024) };
    const engCol = e => (M.ENGINES[e] || { col: D.LGREY }).col;

    /* FOMA (specs/ui/screens/visual.md#foma): the mascot, a little FM-1 with a cat's ears, flying as Nyan Cat does,
     * facing right, but bouncing round the body as a DVD player's logo does (a new colour at each wall, sparkles in a
     * corner); its tail a rainbow of six stripes straight back from it to the left edge, its upper edge the left
     * channel's wave and its lower edge the right's, reshaped once a step (a row); stars going by, a star on each beat,
     * then a cross, then a dot, half of them half a beat later. As the firmware's (fw/core/ui/scr_visual.c): a frame each 33 ms, 16 x 12 cells of
     * 3 x 3 pixels */
    const MAS_PIC = ["......B.....B...", ".....BEB...BEB..", "BBBBBBBBBBBBBBB.", "BKKKKKKKKKKKKKB.", "BKKKKKKWKKKWKKB.",
                     "BKKKKKKWKKKWKKB.", "BKKKKKKKKPKKKKB.", "BBBBBBBBBBBBBBB.", "BWkWkWWkWkWkWWB.", "BBBBBBBBBBBBBBB.",
                     "................", "................"];
    const MAS_COLS = [D.LRED, D.YELLOW, D.LGREEN, D.LCYAN, D.LBLUE, D.LMAGENTA];
    const MAS_BOW = [D.LRED, D.BROWN, D.YELLOW, D.LGREEN, D.LBLUE, D.LMAGENTA];   /* the tail, top down */
    const FS = 3, FW = 16, FH = 12, SC = 6, SR = 4, SV = 3, LAPS = 64;   /* (the stars: 6 columns x 4 bands, 3 px a frame) */
    /* sl, sr: the tail's edges, SCOPE's left and right from its back on, taken anew once a step (shaped: the step) */
    const mas = { x: 40, y: Y0 + 30, vx: 2, vy: 1.25, col: 0, frame: 0, hop: false, step: 0, sx: 0, shaped: -1, ouch: 0,
                  sl: new Float32Array(240), sr: new Float32Array(240), cornerAt: 0, corners: 0, t: 0 };
    function masMove() {
        const x1 = 240 - FS * (FW - 1), y0 = Y0, y1 = Y0 + H - FS * (FH - 1);
        let wall = 0;
        mas.x += mas.vx; mas.y += mas.vy;
        if (mas.x <= 0 || mas.x >= x1) { mas.x = mas.x <= 0 ? 0 : x1; mas.vx = -mas.vx; wall |= 1; }
        if (mas.y <= y0 || mas.y >= y1) { mas.y = mas.y <= y0 ? y0 : y1; mas.vy = -mas.vy; wall |= 2; }
        if (mas.ouch) mas.ouch--;
        if (wall) { mas.col = (mas.col + 1) % MAS_COLS.length; mas.ouch = 8; }   /* (a bump: its eyes shut 8 frames) */
        if (wall === 3) { mas.cornerAt = performance.now(); mas.corners++; }
        mas.frame = (mas.frame + 1) & 255;
        mas.sx = (mas.sx + SV) % (240 * LAPS);
        const h = P.playing && P.at();                      /* while something plays: a hop on the beat, a step a row */
        mas.hop = !!h && P.onBeat();
        mas.step = h ? h.row & 255 : mas.frame >> 2;
        if (mas.step !== mas.shaped) {                       /* the tail's shape, once a step: SCOPE's, from its trigger */
            mas.shaped = mas.step;
            const len = [128, 256, 384, 512, 768, 1024][vis.zoom];
            let z = 0;
            for (let i = 1; i < 2048 - len; i++) if (vis.buf[0][i - 1] <= 0 && vis.buf[0][i] > 0) { z = i; break; }
            for (let x = 0; x < 240; x++) {
                const i = z + Math.floor(x * len / 240);
                mas.sl[x] = vis.buf[0][i];
                mas.sr[x] = vis.buf[1][i];
            }
        }
    }
    function masStar(x, y, shape) {                          /* 0 a dot, 1 a cross, 2 a star: cells of 2 x 2 px */
        D.rect(x - 1, y - 1, 2, 2, shape ? D.WHITE : D.LGREY);
        if (shape) for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
            D.rect(x - 1 + 2 * dx, y - 1 + 2 * dy, 2, 2, shape === 2 ? D.WHITE : D.LGREY);
            if (shape === 2) D.rect(x - 1 + 4 * dx, y - 1 + 4 * dy, 2, 2, D.LGREY);
        }
    }
    function masDraw() {
        const now = performance.now();
        if (!vis.freeze) for (let n = 0; n < 5 && now - mas.t >= 33; n++) { mas.t = mas.t ? mas.t + 33 : now; masMove(); }
        if (now - mas.t > 200) mas.t = now;                 /* (away a while: no catching up) */
        const bw = 240 / SC, bh = Math.floor(H / SR);
        for (let i = 0; i < SC * SR; i++) {                  /* the night: the stars going by to the left */
            const jx = (Math.imul(i, 2654435761) >>> 16) % (bw - 8);   /* (its place across: kept, so they stay spread) */
            const pos = (i % SC) * bw + jx + 240 * LAPS - mas.sx;
            let h = (Math.imul(Math.floor(pos / 240) % LAPS + 1, 0x9E3779B1) ^ Math.imul(i + 1, 0x85EBCA6B)) >>> 0;
            h = (h ^ (h >>> 15)) >>> 0;                     /* (each time round: anew) */
            h = Math.imul(h, 0x2C1B3C6D) >>> 0;
            h = (h ^ (h >>> 12)) >>> 0;
            /* on its beat a star, then a cross, then a dot; half of them (a checkerboard) half a beat later */
            const y = Y0 + Math.floor(i / SC) * bh + 6 + h % (bh - 12), p = (mas.step + ((i % SC + Math.floor(i / SC)) & 1) * 2) & 3;
            masStar(pos % 240, y, [2, 1, 0, 0][p]);
        }
        const x0 = Math.floor(mas.x), y0 = Math.floor(mas.y) - (mas.hop ? 3 : 0), blink = mas.frame % 90 < 3 || mas.ouch > 0, legs = (mas.frame >> 2) & 1;
        /* the tail, from its back to the left edge, as Nyan Cat's waving rainbow: its upper edge the left channel's
         * wave, its lower edge the right's (both moving the same way), each +-9 px at the louder one's peak (near
         * silence: straight); the six stripes share what lies between */
        /* the edges smoothed so a busy wave never tears it: each column the mean of SCOPE's 17 points round it, then
         * 1-2-1 with its neighbours */
        const nc = Math.floor((x0 + FS - 1) / FS) + 1, e = [[], []], f = [[], []];
        let pk = 1024 / 32768;
        for (let i = 0; i < nc; i++) {
            const k = i * FS, a = Math.max(0, k - 8), b = Math.min(240, k + 9);
            [mas.sl, mas.sr].forEach((w, c) => { let sum = 0; for (let j = a; j < b; j++) sum += w[j]; e[c][i] = sum / (b - a); });
        }
        for (let i = 0; i < nc; i++) for (let c = 0; c < 2; c++) {
            f[c][i] = (e[c][Math.max(0, i - 1)] + 2 * e[c][i] + e[c][Math.min(nc - 1, i + 1)]) / 4;
            pk = Math.max(pk, Math.abs(f[c][i]));
        }
        for (let i = 0, x = x0; i < nc; i++, x -= FS) {      /* (held at its back: the swing grows over 8 columns) */
            const g = Math.min(i, 8) / 8, up = y0 + 6 - Math.trunc(f[0][i] * 9 * g / pk), dn = y0 + 30 - Math.trunc(f[1][i] * 9 * g / pk);
            for (let s = 0; s < 6; s++) {
                const a = up + Math.trunc((dn - up) * s / 6), b = up + Math.trunc((dn - up) * (s + 1) / 6);
                D.rect(x, a, FS, b - a, MAS_BOW[s]);
            }
        }
        for (let r = 0; r < FH; r++) for (let c = 0; c < FW; c++) {
            let p = MAS_PIC[r][c];
            if (r === 10) p = [1 + legs, 4 + legs, 10 - legs, 13 - legs].includes(c) ? "L" : ".";
            if (p === ".") continue;
            if (p === "W" && r === 4 && blink) p = "K";
            const col = { B: MAS_COLS[mas.col], E: D.LMAGENTA, K: D.DGREY, W: D.WHITE, k: D.BLACK, P: D.LMAGENTA, L: D.LGREY }[p];
            D.rect(x0 + FS * c, y0 + FS * r, FS, FS, col);
        }
        const since = now - mas.cornerAt;
        if (mas.cornerAt && since < 1500) {                  /* a corner: sparkles round it, and a word */
            const t = Math.floor(since / 60);
            for (let i = 0; i < 8; i++) {
                const dx = (i & 1 ? 1 : -1) * (12 + t + (i >> 1) * 5), dy = (i & 2 ? 1 : -1) * (8 + t + (i >> 2) * 4);
                const cx = x0 + FS * FW / 2 + dx, cy = y0 + FS * FH / 2 + dy, col = MAS_COLS[(i + t) % MAS_COLS.length];
                D.hline(cx - 2, cy, 5, col); D.vline(cx, cy - 2, 5, col);
            }
            D.text(D.centre(F.S, "CORNER!", 0, 240), Y0 + H / 2 - 8, F.S, "CORNER!", D.YELLOW);
        }
    }
    function grab() {
        const an = A.analysers();
        if (!an || vis.freeze) return !!an;
        an[0].getFloatTimeDomainData(vis.buf[0]);
        an[1].getFloatTimeDomainData(vis.buf[1]);
        an[0].getFloatFrequencyData(vis.fbuf);
        let pk = 0;
        for (let i = 0; i < 2048; i += 2) pk = Math.max(pk, Math.abs(vis.buf[0][i]), Math.abs(vis.buf[1][i]));
        vis.wave.push(pk);
        if (vis.wave.length > 240) vis.wave.shift();
        return true;
    }
    U.register({
        name: "VISUAL", group: "HEAR",
        does: [B.SEQ, B.GLO],
        sub: () => VIEWS[vis.view],                         /* the header's subpage: the view by name */
        seq: () => M.pageOf(vis.view, VIEWS.length),        /* SEQ turns the views: `SEQ 1/5` in the header */
        knob(e, s) {
            if (e === E.SELECT) vis.view = U.clamp(vis.view + Math.sign(s), 0, VIEWS.length - 1);     /* the views, as pages */
            if (e === E.K1) { vis.zoom = U.clamp(vis.zoom + Math.sign(s), 0, 5); U.showKnob("K1 ZOOM", String(vis.zoom + 1)); }
            if (e === E.K2) { vis.gain = U.clamp(vis.gain + Math.sign(s), 0, 7); U.showKnob("K2 GAIN", "X" + Math.pow(2, vis.gain - 3).toFixed(2).replace(/\.?0+$/, "")); }
            /* (a touch the help keeps quiet: K4 lifts FOMA or drops it, a turn (24 clicks) the body's height) */
            if (e === E.K4 && VIEWS[vis.view] === "FOMA") mas.y = U.clamp(mas.y - Math.sign(s) * H / 24, Y0, Y0 + H - FS * (FH - 1));
        },
        tap(b) {
            if (b === B.SEQ) { vis.view = (vis.view + 1) % VIEWS.length; return true; }
            if (b === B.GLO) {
                U.props("VISUAL OPTIONS", [
                    { name: "VIEW", get: () => vis.stereo ? "STEREO" : "MONO", change: () => { vis.stereo = !vis.stereo; } },
                    { name: "FREEZE", get: () => vis.freeze ? "ON" : "OFF", change: () => { vis.freeze = !vis.freeze; } }]);
                return true;
            }
            return false;
        },
        /* (FOMA's: its score, the corners hit; none yet: nothing) */
        footer: () => [VIEWS[vis.view] === "FOMA" ? (mas.corners ? "SCORE: " + mas.corners + (vis.freeze ? " FROZEN" : "") : vis.freeze ? "FROZEN" : "")
                       : VIEWS[vis.view] + (vis.freeze ? " FROZEN" : ""), D.LGREY],
        draw() {
            const live = grab(), g = Math.pow(2, vis.gain - 3);
            const v = VIEWS[vis.view];
            if (v === "FOMA") { masDraw(); return; }        /* (silent before the sound starts: its tail straight) */
            if (!live && v !== "VOICES" && v !== "LEVELS") { D.text(4, Y0 + 90, F.T, "CLICK THE PAGE TO START THE SOUND", D.DGREY); return; }
            if (v === "VOICES") {
                const n = A.voicesInForce(), cols = 4, rows = n / 4, tw = 60, th = Math.floor(H / rows);   /* (a tile a
                                                                                       * voice of the limit in force) */
                const voices = A.stats.voices || [];
                for (let i = 0; i < n; i++) {
                    const x = (i % cols) * tw, y = Y0 + Math.floor(i / cols) * th, vc = voices[i];
                    D.box(x, y, tw - 1, th - 1, D.DGREY);
                    if (!vc) { D.text(x + 3, y + 2, F.T, "--", D.DGREY); continue; }
                    const col = engCol(vc.engine);
                    D.text(x + 3, y + 2, F.T, vc.track < 0 ? "KEY" : vc.track >= 16 ? "L" + M.hex(vc.track - 16) : "T" + M.hex(vc.track), col);
                    D.text(x + tw - 22, y + 2, F.T, M.noteName(vc.midi), vc.rel ? D.DGREY : D.LGREY);
                    let py = null;
                    const mid = y + 14 + (th - 16) / 2, amp = (th - 18) / 2;
                    for (let k = 0; k < 56; k++) {
                        const s = vc.scope[k] * g * 6, yy = Math.round(mid - Math.max(-1, Math.min(1, s)) * amp);
                        if (py != null) D.line(x + 2 + k - 1, py, x + 2 + k, yy, vc.rel ? D.DGREY : col);
                        py = yy;
                    }
                }
                return;
            }
            if (v === "SCOPE") {
                const len = [128, 256, 384, 512, 768, 1024][vis.zoom];
                let z = 0;
                for (let i = 1; i < 2048 - len; i++) if (vis.buf[0][i - 1] <= 0 && vis.buf[0][i] > 0) { z = i; break; }
                const traces = vis.stereo ? [[0, D.LGREEN], [1, D.LCYAN]] : [[-1, D.LGREEN]];
                traces.forEach(([ch, col], k) => {
                    const h = H / traces.length, mid = Y0 + h * k + h / 2;
                    D.hline(0, Math.round(mid), 240, D.DEEP + D.DGREY);
                    let py = null;
                    for (let x = 0; x < 240; x++) {
                        const i = z + Math.floor(x / 240 * len);
                        const s = ch < 0 ? (vis.buf[0][i] + vis.buf[1][i]) / 2 : vis.buf[ch][i];
                        const yy = Math.round(mid - Math.max(-1, Math.min(1, s * g * 2)) * (h / 2 - 2));
                        if (py != null) D.line(x - 1, py, x, yy, col);
                        py = yy;
                    }
                    if (vis.stereo) D.text(2, Y0 + h * k + 2, F.T, ch ? "R" : "L", D.DGREY);
                });
                return;
            }
            if (v === "WAVE") {
                const mid = Y0 + H / 2, n = vis.wave.length;
                for (let x = 0; x < n; x++) {
                    const h = Math.min(1, vis.wave[x] * g * 1.5) * (H / 2 - 4);
                    D.vline(240 - n + x, Math.round(mid - h), Math.max(1, Math.round(h * 2)), x === n - 1 ? D.WHITE : D.BROWN);
                }
                D.hline(0, mid, 240, D.DEEP + D.DGREY);
                return;
            }
            if (v === "SPECTRUM") {
                const bars = 40, bw = 6;
                for (let b = 0; b < bars; b++) {
                    const f0 = 30 * Math.pow(600, b / bars), f1 = 30 * Math.pow(600, (b + 1) / bars);
                    const i0 = Math.floor(f0 / 22050 * 1024), i1 = Math.max(i0 + 1, Math.floor(f1 / 22050 * 1024));
                    let m = -140;
                    for (let i = i0; i < i1 && i < 1024; i++) m = Math.max(m, vis.fbuf[i]);
                    const h = U.clamp((m + 90 + vis.gain * 4) / 80, 0, 1) * (H - 16);
                    const col = b < 12 ? D.LRED : b < 26 ? D.YELLOW : D.LCYAN;
                    D.rect(b * bw, Y0 + H - 12 - h, bw - 1, h, col);
                }
                D.text(0, Y0 + H - 11, F.T, "30", D.DGREY); D.text(108, Y0 + H - 11, F.T, "700", D.DGREY); D.text(210, Y0 + H - 11, F.T, "18K", D.DGREY);
                return;
            }
            /* LEVELS: a meter for each track */
            const lv = A.stats.levels || [];
            for (let t = 0; t < 16; t++) {
                const x = t * 15, h = Math.min(1, (lv[t] || 0) * g * 4) * (H - 20);
                D.rect(x + 2, Y0 + 2, 11, H - 20, D.DEEP + D.DGREY);
                D.rect(x + 2, Y0 + H - 18 - h, 11, h, U.muted(t) ? D.DGREY : h > (H - 20) * .85 ? D.RED : D.GREEN);
                D.text(x + 1, Y0 + H - 13, F.T, M.hex(t), U.muted(t) ? D.DGREY : D.CYAN);
            }
        },
    });

    /* ======================================================================= STATUS === */
    const PAGES = ["PT/FM", "PERFORMANCE", "STORAGE", "USER FLASH MAP", "RAM MAP", "DEVICE"];
    /* USER FLASH MAP and RAM MAP (specs/ui/screens/status.md): a 4 x 4 square a KB, 48 a line from (0, 33); a legend under
     * it: a 6 x 6 swatch, the name at x + 10, its size (right-aligned to x 150, or after the name). The playground
     * has no flash: its things laid one kind after the other, as their estimates (M.storage); the RAM the FM-1's,
     * the edits and DJ's borrow live */
    const MAP_Y = Y0 + 16;
    const mapSquare = (i, col) => { D.rect((i % 48) * 5, MAP_Y + Math.floor(i / 48) * 5, 4, 4, col); return MAP_Y + (Math.floor(i / 48) + 1) * 5; };
    const legend = (x, y, col, name, size, sizeCol, right) => {
        D.rect(x, y + 3, 6, 6, col);
        D.text(x + 10, y, F.T, name, D.LGREY);
        D.text(right ? right - D.tw(F.T, size) : x + 10 + D.tw(F.T, name) + 6, y, F.T, size, sizeCol);
    };
    /* (the FM-1's release build; LOOPS has no RAM of its own: its changes are edits, its lanes ~0.6 KB of SOUND) */
    const RAM = { total: 512 * 1024, ramText: 4096, effects: 150050, screen: 57600 + 3840, dx7: 28676, sound: 22144 + 6400 + 614,
                  poolSystem: 8576 + 4096, variables: 49024, spare: 91472, dj: 13116 };
    const stat = { page: 0, up0: performance.now() };
    const kb = M.kb;
    const cpuCol = v => v < 50 ? D.GREEN : v < 80 ? D.YELLOW : D.RED;
    const freeCol = k => k < .1 ? D.RED : k < .25 ? D.YELLOW : D.GREEN;
    function figures() {
        const S = A.stats, st = M.storage();
        const used = S.usedNow || 0, max = A.voicesInForce();   /* (the most over the last tenth of a second; the limit in
                                                                 * force: 16 is 12 while the second core does not run) */
        const editsFree = M.EDITS_MAX - M.editsUsed;
        const up = Math.floor((performance.now() - stat.up0) / 1000);
        return { S, st, used, max, editsFree, up };
    }
    const f = (x, y, name, value, col, w) => { D.text(x, y, F.T, name, D.CYAN); D.text(x + w - D.tw(F.T, value), y, F.T, value, col); };
    U.register({
        name: "STATUS", group: "HEAR",
        does: [B.LFO, B.ENV, B.EDIT],
        sub: () => PAGES[stat.page],                        /* the header's subpage: the page by name */
        knob(e, s) { if (e === E.SELECT || e === E.ALGORITHM) stat.page = U.clamp(stat.page + Math.sign(s), 0, PAGES.length - 1); },   /* the pages: either knob */
        tap(b) {
            if (b === B.LFO) { A.resetCounts(); U.toast("COUNTS ZEROED"); return true; }
            /* ENV: the song list; PLAY starts the highlighted song from its start, as the current song, and
             * goes to SONGS; ENV again just closes the list */
            if (b === B.ENV) {
                const cur = P.playingSong() >= 0 ? P.playingSong() : U.song;
                U.songList("SONGS", cur, null, k => { U.song = k; P.kept.song[k] = { pos: 0, row: 0 }; P.playSong(k); U.go("SONGS"); });
                return true;
            }
            if (b === B.EDIT) { U.go("SETTINGS"); return true; }   /* (as HOME + MONO: a plain switch) */
            return false;
        },
        footer() { const st = M.storage(); return ["FREE " + kb(st.userFree), freeCol(st.userFree / M.FLASH.user)]; },   /* the user area, where saves go */
        draw() {
            const { S, st, used, max, editsFree, up } = figures();
            const pg = PAGES[stat.page];
            if (pg === "PT/FM") {
                const y0 = Y0 + 8;
                const NAME = [["P", D.LRED], ["T", D.LCYAN], ["/", D.YELLOW], ["F", D.LGREEN], ["M", D.LMAGENTA]];
                D.runs(D.centre(F.L, "PT/FM", 0, 240), y0, F.L, NAME);
                /* the title: the name in full, the author and the version under it */
                const full = "POCKET TRACKER / FUSION MACHINE";
                D.text(D.centre(F.T, full, 0, 240), y0 + 34, F.T, full, D.LGREY);
                const ver = U.version, dev = U.dev ? " DEV" : "", by = "BY HITOSU  " + ver + dev;
                D.runs(D.centre(F.T, by, 0, 240), y0 + 46, F.T, [["BY ", D.LGREY], ["HITOSU", D.WHITE], ["  " + ver, D.DGREY], [dev, D.YELLOW]]);
                /* the navigation hints, together: the help, then the two knobs that move about (SELECT up
                 * and down, ALGORITHM sideways) */
                const hl = [["PRESS ", D.LGREY], ["HOME + LFO", D.WHITE], [" FOR HELP", D.LGREY]];
                D.runs(D.centre(F.S, "PRESS HOME + LFO FOR HELP", 0, 240), y0 + 64, F.S, hl);
                const kx = (240 - 126) >> 1, ky = y0 + 82;                  /* 126 px: words, arrows, gap */
                D.text(kx, ky, F.T, "SELECT", D.WHITE);
                D.updown(kx + 39, ky + 1, D.LGREY);
                D.text(kx + 60, ky, F.T, "ALGORITHM", D.WHITE);
                D.leftright(kx + 117, ky + 3, D.LGREY);
                const y = y0 + 100, L = 0, R = 126;
                const cpu = Math.round(S.cpuNow), mx = Math.round(S.cpuMax), c2 = A.c2On();
                const cpu2 = Math.round(S.cpu2Now), mx2 = Math.round(S.cpu2Max);
                D.runs(L, y, F.T, [["CPU 1 ", D.CYAN], [String(cpu).padStart(3) + "%", cpuCol(cpu)], [" MAX ", D.CYAN], [String(mx).padStart(3) + "%", cpuCol(mx)]]);
                if (c2) D.runs(R, y, F.T, [["CPU 2 ", D.CYAN], [String(cpu2).padStart(3) + "%", cpuCol(cpu2)], [" MAX ", D.CYAN], [String(mx2).padStart(3) + "%", cpuCol(mx2)]]);
                else D.runs(R, y, F.T, [["CPU 2 ", D.CYAN], [" OFF", D.DGREY]]);
                D.runs(L, y + 12, F.T, [["LATE ", D.CYAN], [String(S.late).padStart(4), S.late ? D.RED : D.DGREY]]);
                D.runs(R, y + 12, F.T, [["STEALS ", D.CYAN], [String(S.steals || 0).padStart(3), S.steals ? D.RED : D.DGREY]]);
                D.runs(L, y + 24, F.T, [["VOICES ", D.CYAN], [M.dec(used) + "/" + M.dec(max), U.voicesCol(used, max)], [" MAX ", D.CYAN],
                                        [M.dec(S.usedMax || 0), U.voicesCol(S.usedMax || 0, max)]]);
                D.runs(R, y + 24, F.T, [["HALF ", D.CYAN], [String(S.half).padStart(4) + " US", cpuCol(S.half / 58.04)]]);
                D.runs(L, y + 36, F.T, [["EDITS FREE ", D.CYAN], [kb(editsFree), freeCol(editsFree / M.EDITS_MAX)]]);
                const un = U.unsavedCount();                 /* (DJ's stand moved counts, as the firmware's) */
                D.runs(R, y + 36, F.T, [["UNSAVED ", D.CYAN], [String(un), un ? D.YELLOW : D.DGREY]]);
                D.runs(L, y + 48, F.T, [["FLASH FREE ", D.CYAN], [kb(st.userFree), freeCol(st.userFree / M.FLASH.user)]]);
                const bat = S.usb ? "USB" : S.battery + "/3";
                D.runs(L, y + 60, F.T, [["BATTERY ", D.CYAN], [bat, S.usb ? D.LBLUE : S.battery >= 2 ? D.GREEN : S.battery === 1 ? D.YELLOW : D.RED]]);
                D.runs(R, y + 60, F.T, [["UP ", D.CYAN], [M.dec(Math.floor(up / 3600)) + ":" + M.dec(Math.floor(up / 60) % 60) + ":" + M.dec(up % 60), D.LGREY]]);
                const s = M.state.songs[P.playingSong() >= 0 ? P.playingSong() : U.song];
                D.text(0, y + 82, F.T, "SONG", D.CYAN);
                D.text(30, y + 80, F.S, s ? s.name : "-", D.WHITE);
                return;
            }
            if (pg === "PERFORMANCE") {
                const y = Y0 + 2, w = 116;
                /* each core's row: its share now, on average, the most (core 2 off: OFF under NOW, the rest blank) */
                D.text(96 - D.tw(F.T, "NOW"), y, F.T, "NOW", D.CYAN);
                D.text(156 - D.tw(F.T, "AVERAGE"), y, F.T, "AVERAGE", D.CYAN);
                D.text(210 - D.tw(F.T, "MOST"), y, F.T, "MOST", D.CYAN);
                const core = (cy, name, now, avg, most, on) => {
                    D.text(0, cy, F.T, name, D.CYAN);
                    const put = (x, v, col) => D.text(x - D.tw(F.T, v), cy, F.T, v, col);
                    if (!on) { put(96, "OFF", D.DGREY); return; }
                    put(96, Math.round(now) + "%", cpuCol(now)); put(156, Math.round(avg) + "%", cpuCol(avg)); put(210, Math.round(most) + "%", cpuCol(most));
                };
                core(y + 12, "CPU 1", S.cpuNow, S.cpuAvg || 0, S.cpuMax, true);
                core(y + 24, "CPU 2", S.cpu2Now, S.cpu2Avg || 0, S.cpu2Max, A.c2On());
                f(0, y + 36, "WORST HALF", S.half + " US", cpuCol(S.half / 58.04), w);
                f(124, y + 36, "LATE", String(S.late), S.late ? D.RED : D.DGREY, w);
                /* used / most / limit: the sounding count in its colour, the most and the limit in the most's */
                const vu = M.dec(used) + "/", vm = M.dec(S.usedMax || 0) + "/" + M.dec(max);
                D.text(0, y + 48, F.T, "VOICES", D.CYAN);
                D.runs(w - D.tw(F.T, vu + vm), y + 48, F.T, [[vu, U.voicesCol(used, max)], [vm, U.voicesCol(S.usedMax || 0, max)]]);
                f(124, y + 48, "STEALS", String(S.steals || 0), S.steals ? D.RED : D.DGREY, w);
                const COST = { DX7: 5.6, JUNO: 4.4, TB303: 2.8, SAMPLER: 1.6 };
                ["DX7", "JUNO", "TB303", "SAMPLER"].forEach((e, i) => {
                    const n = (S.countNow || {})[e] || 0;
                    f((i % 2) * 124, y + 64 + Math.floor(i / 2) * 12, M.ENGINES[e].short, n + " X " + COST[e] + "%", n ? M.ENGINES[e].col : D.DGREY, w);
                });
                f(0, y + 88, "EFFECTS", (S.fxCost || 0).toFixed(1) + "%", D.LGREY, w);
                /* the last 30 s: core 1's bars, core 2's white marks */
                const gy = y + 106, gh = 100, g = S.graph || [], g2 = S.graph2 || [];
                D.box(0, gy, 240, gh, D.DGREY);
                for (const k of [50, 80]) D.hline(1, Math.round(gy + gh - k / 100 * gh), 238, D.DEEP + (k === 50 ? D.YELLOW : D.RED));
                g.forEach((v, i) => { const h = Math.round(v / 100 * (gh - 2)); D.vline(239 - g.length * 2 + i * 2, gy + gh - 1 - h, h, cpuCol(v)); });
                g2.forEach((v, i) => { if (v != null) D.rect(239 - g2.length * 2 + i * 2, gy + gh - 1 - Math.round(v / 100 * (gh - 2)), 2, 1, D.WHITE); });
                D.text(2, gy + 2, F.T, "CPU, LAST 30 S", D.DGREY);
                return;
            }
            if (pg === "STORAGE") {
                /* the user area (specs/ui/screens/status.md), where everything the user keeps goes: its bar with
                 * compaction's blocks and the room for the EDITOR's own pattern counted used */
                const y = Y0 + 2;
                D.text(0, y, F.T, "FLASH", D.CYAN);
                D.text(0, y + 14, F.T, "USER", D.LGREY);
                D.box(60, y + 14, 120, 10, D.DGREY);
                D.rect(61, y + 15, Math.round(Math.min(1, st.userUsed / M.FLASH.user) * 118), 8, D.GREEN);
                D.text(186, y + 14, F.T, kb(M.FLASH.user - st.userUsed) + " FREE", freeCol(st.userFree / M.FLASH.user));
                D.text(0, y + 32, F.T, "FILES BY TYPE", D.CYAN);
                f(0, y + 46, "PATTERNS", kb(st.patt), D.LGREY, 116);
                f(124, y + 46, "COUNT", String(M.patternNums().length), D.LGREY, 116);
                f(0, y + 58, "INSTRUMENTS", kb(st.inst), D.LGREY, 116);
                f(124, y + 58, "CARTRIDGES", String(M.state.carts.length), D.LGREY, 116);
                f(0, y + 70, "SAMPLES", kb(st.smp), D.LGREY, 116);
                f(124, y + 70, "SONGS", String(M.state.songs.length), D.LGREY, 116);
                D.text(0, y + 88, F.T, "NOT SAVED", D.CYAN);
                const un = U.unsavedCount();
                f(0, y + 102, "CHANGES", String(un), un ? D.YELLOW : D.DGREY, 116);
                f(124, y + 102, "THEIR RAM", kb(M.editsUsed), un ? D.YELLOW : D.DGREY, 116);
                f(0, y + 114, "EDITS FREE", kb(M.EDITS_MAX - M.editsUsed), freeCol(1 - M.editsUsed / M.EDITS_MAX), 116);
                D.text(0, y + 134, F.T, "SAVED: IN THIS TAB ONLY (PLAYGROUND)", D.DGREY);
                return;
            }
            if (pg === "USER FLASH MAP") {
                const KINDS = [["SAMPLES", st.smp, D.YELLOW], ["INSTRUMENTS", st.inst, D.LBLUE], ["PATTERNS", st.patt, D.CYAN],
                               ["SONGS", st.song, D.GREEN], ["LOOPS", st.loops, D.LGREEN], ["OTHER", st.other, D.LGREY]];
                const n = M.FLASH.user / 1024, kept = M.FLASH.reserve / 1024;
                D.text(0, Y0 + 2, F.T, "FLASH " + kb(M.FLASH.user) + ", A SQUARE 1K", D.CYAN);
                let i = 0, y = MAP_Y;
                for (const [, b, col] of KINDS) for (let k = Math.ceil(b / 1024); k > 0 && i < n - kept; k--) y = mapSquare(i++, col);
                while (i < n - kept) y = mapSquare(i++, D.DEEP + D.LGREY);
                while (i < n) y = mapSquare(i++, D.MAGENTA);            /* (KEPT FOR SAVING: drawn as the last free KB) */
                y += 8;
                for (const [name, b, col] of KINDS) { legend(0, y, col, name, kb(b), D.WHITE, 150); y += 12; }
                legend(0, y, D.MAGENTA, "KEPT FOR SAVING", kb(M.FLASH.reserve), D.WHITE, 150);
                legend(0, y + 12, D.DEEP + D.LGREY, "FREE", kb(st.userFree), freeCol(st.userFree / M.FLASH.user), 150);
                return;
            }
            if (pg === "RAM MAP") {
                const r = RAM, used = Math.min(M.editsUsed, M.EDITS_MAX), dj = P.what === "dj" ? r.dj : 0;
                const system = r.total - (r.effects + M.EDITS_MAX + r.screen + r.dx7 + r.sound + r.variables + r.spare);
                D.text(0, Y0 + 2, F.T, "RAM " + kb(r.total) + ", A SQUARE 1K", D.CYAN);
                const SEG = [[r.ramText, D.MAGENTA], [r.effects, D.GREEN], [used, D.YELLOW], [M.EDITS_MAX - used, D.DEEP + D.YELLOW],
                             [r.screen, D.LBLUE], [r.dx7, D.LRED], [r.sound, D.LCYAN], [r.poolSystem, D.MAGENTA],
                             [r.variables, D.LGREY], [r.spare - dj, D.DEEP + D.LGREY], [dj, D.BROWN],
                             [Math.max(0, system - r.ramText - r.poolSystem), D.MAGENTA]];
                let s = 0, end = SEG[0][0], y = MAP_Y;
                for (let i = 0; i * 1024 < r.total; i++) {                /* a square the part its KB's middle lies in */
                    while (s < SEG.length - 1 && i * 1024 + 512 >= end) end += SEG[++s][0];
                    y = mapSquare(i, SEG[s][1]);
                }
                y += 8;
                const L = [["EFFECTS", kb(r.effects), D.GREEN], ["EDITS", kb(used) + " OF " + kb(M.EDITS_MAX), D.YELLOW],
                           ["SCREEN", kb(r.screen), D.LBLUE], ["DX7", kb(r.dx7), D.LRED], ["SOUND", kb(r.sound), D.LCYAN],
                           ["VARIABLES", kb(r.variables), D.LGREY], ["SPARE", kb(r.spare), D.DEEP + D.LGREY],
                           ["DJ", kb(dj), D.BROWN], ["SYSTEM", kb(system), D.MAGENTA]];
                L.forEach(([name, size, col], j) => legend(j < 5 ? 0 : 120, y + (j % 5) * 12, col, name, size, D.WHITE, 0));
                D.text(0, y + 5 * 12 + 4, F.T, "DIM: ITS ROOM NOT IN USE NOW", D.DGREY);
                return;
            }
            const y = Y0 + 2;
            f(0, y, "BATTERY", S.usb ? "USB POWER" : S.battery + "/3 BARS",
              S.usb ? D.LBLUE : S.battery >= 2 ? D.GREEN : S.battery === 1 ? D.YELLOW : D.RED, 236);     /* as the header's bars */
            f(0, y + 14, "FIRMWARE", U.version + (U.dev ? " DEV" : ""), D.LGREY, 236);
            f(0, y + 28, "OUTPUT", U.set("OUTPUT"), D.LGREY, 236);
            f(0, y + 42, "MASTER", Math.round((PTFM.ui.master != null ? PTFM.ui.master : .5) * 100) + "%", D.LGREY, 236);
            f(0, y + 56, "SCREEN FRAME", String(U.fps || 0).padStart(2) + " FPS  " + String(U.frameMs || 0).padStart(2) + " MS", D.LGREY, 236);
            f(0, y + 70, "UP", M.dec(Math.floor(up / 3600)) + ":" + M.dec(Math.floor(up / 60) % 60) + ":" + M.dec(up % 60), D.LGREY, 236);
            f(0, y + 84, "VOICES", String(max), D.LGREY, 236);
            f(0, y + 98, "THEME", U.set("THEME"), D.LGREY, 236);
            f(0, y + 112, "SECOND CORE", A.c2State(), D.LGREY, 236);      /* (never STOPPED here) */
        },
    });

    /* ===================================================================== SETTINGS === */
    /* two pages, SEQ or ALGORITHM turning them: the list and MIDI (specs/connections.md); msel: MIDI's
     * pick, through its items and then the mappings */
    const set = { sel: 0, page: 0, msel: 0 };
    const PAGE_NAMES = [null, "MIDI"];
    const applied = {};                                      /* (each setting's value as last applied) */
    function apply(k, v) {
        applied[k] = v;
        if (k === "THEME") D.theme(v);
        if (k === "VOICES") A.voices(v);
        if (k === "OUTPUT") { A.output(v === "MONO"); A.pulseSide(); }     /* (SYNC PULSE L / R: with SYNC INTERNAL) */
        if (k === "LINE GAIN") A.gain(v);                    /* USB GAIN: the playground has no USB audio */
        if (k === "KEYS GAIN") A.keysGain(Math.pow(10, v / 20));   /* the keys' voices as heard */
        if (k === "KEYS") A.keysDry(v === "DRY");            /* the keys heard past every effect, or not */
        if (k === "KEYS MIX") A.keysMix(v);                  /* FX MIXER's KEYS column: their PAN, DUCK, sends */
        if (k === "KEYS OCTAVE") U.octave = v;
        if (k === "ARP RHYTHM") PTFM.player.rhythm(v);       /* a repeat running takes it at once */
        if (k === "ARP PATTERN" || k === "ARP LATCH") U.arpSettings();     /* the arpeggio running too */
    }
    U.applySettings = () => { for (const s of M.SETTINGS) apply(s.k, U.set(s.k)); apply("KEYS MIX", U.set("KEYS MIX")); };
    /* a setting changed from elsewhere (FX's GLO sets KEYS): an edit, applied at once */
    U.changeSetting = (k, nv) => { if (U.set(k) === nv) return; U.edit(() => { M.state.settings[k] = nv; }); apply(k, nv); };
    /* VOICES 16 picked while the second core does not run: it counts as 12 till it runs (settings.md#voices) */
    function voicesTold(k) {
        if (k === "VOICES" && +U.set(k) === 16 && !A.c2On()) U.toast("16 VOICES: 12 WITHOUT CPU 2");
    }
    /* TOOLS' pick is no setting: never kept nor an edit, the first again at a power-on, SETTINGS' REVERT and REVERT ALL
     * (the firmware's settings read anew) */
    const TOOLS_DEF = M.SETTINGS.find(s => s.k === "TOOLS").def;
    let tool = TOOLS_DEF;
    const val = k => k === "TOOLS" ? tool : U.set(k);
    U.tool = () => tool;                                /* (the pick: for the checks) */
    U.pickTool = v => { tool = v; };
    /* UNDO and REVERT put settings back: what changed is applied again (THEME, VOICES, LINE GAIN ...); the
     * rest, KEYS OCTAVE's octave turned since included, stays */
    M.onRestore = kind => {
        if (!kind || kind === "settings") tool = TOOLS_DEF;
        for (const s of M.SETTINGS) if (U.set(s.k) !== applied[s.k]) apply(s.k, U.set(s.k));
        if (U.set("KEYS MIX") !== applied["KEYS MIX"]) apply("KEYS MIX", U.set("KEYS MIX"));
    };
    const fmt = (s, v) => s.fmt ? s.fmt(v) : String(v) + (s.unit || "");

    /* ------------------------------------------------------------ the MIDI page --- */
    /* the FM-1's controls by their numbers in the MIDI map (specs/files/midi.md) */
    const KNOB_NAMES = ["SELECT", "ALGORITHM", "K1", "K2", "K3", "K4", "PRESETS"];
    const ctlName = n => n < 7 ? KNOB_NAMES[n] : n === 7 ? "MASTER" : n < 22 ? U.BNAME[n - 8] : "KEY " + M.noteName(53 + n - 22);
    const midiName = mp => (mp.msg === "NOTE" ? "NOTE " + mp.num : "CC " + mp.num) + " CH " + (mp.ch + 1);
    const midiRows = () => M.MIDI_SETTINGS.length + M.state.midi.length;
    const midiItem = () => set.msel < M.MIDI_SETTINGS.length ? M.MIDI_SETTINGS[set.msel] : null;
    const midiMap = () => set.msel >= M.MIDI_SETTINGS.length ? M.state.midi[set.msel - M.MIDI_SETTINGS.length] : null;
    /* LEARN (EDIT on it): an FM-1 control touched (it does nothing else then), then the outside MIDI
     * control moved: its first message names it, what follows within a second tells its kind. HOME
     * cancels. The playground has no MIDI in from an outside controller: it waits at the second step */
    function learn() {
        if (M.state.midi.length >= M.MIDI_MAX) { U.refuse(B.EDIT); U.toast("64 MAPPINGS: DELETE ONE FIRST"); return; }
        U.open({
            kind: "learn", ctl: null,
            capture(w) {
                if (this.ctl != null) return;                /* waiting for the MIDI control: the FM-1's do nothing */
                this.ctl = w.knob != null ? w.knob : w.master ? 7 : w.button != null ? 8 + w.button : 22 + w.key;
            },
            key() { return true; },
            knob() {},
            tap() { return true; },
            cancel() { U.close(this); },
            draw() {
                const lines = this.ctl == null ? ["MIDI LEARN", "", "TOUCH AN FM-1 CONTROL", "A KNOB, A BUTTON OR A KEY"]
                                               : ["MIDI LEARN", ctlName(this.ctl), "MOVE THE MIDI CONTROL", "A KNOB: TURN IT. A PAD: PRESS IT"];
                const h = lines.length * 12 + 40, y = U.BODY_Y + Math.floor((U.BODY_H - h) / 2);
                U.panel(8, y, 224, h, null);
                lines.forEach((l, i) => D.text(D.centre(F.T, l, 8, 224), y + 8 + i * 12, F.T, l, i === 0 ? D.WHITE : i === 1 ? D.YELLOW : D.LGREY));
                D.runs(D.centre(F.T, "HOME: CANCEL", 8, 224), y + h - 16, F.T, [["HOME", D.YELLOW], [": CANCEL", D.LGREY]]);
            },
        });
    }
    function midiDraw() {
        const ROWS = 14, n = midiRows(), top = U.clamp(set.msel - 6, 0, Math.max(0, n - ROWS));
        for (let k = 0; k < Math.min(ROWS, n - top); k++) {
            const i = top + k, y = Y0 + 3 + k * 15, on = i === set.msel;
            if (on) D.box(0, y - 2, 240, 15, D.WHITE);
            if (i < M.MIDI_SETTINGS.length) {
                const it = M.MIDI_SETTINGS[i];
                D.text(4, y, F.T, it.k, on ? D.WHITE : D.CYAN);
                const s = it.ctl ? "" : it.learn ? M.state.midi.length + " OF " + M.MIDI_MAX : fmt(it, U.set(it.k)), def = it.learn || U.set(it.k) === it.def;
                D.text(236 - D.tw(F.T, s), y, F.T, s, on ? D.WHITE : def ? D.LGREY : D.YELLOW);
            } else {                                         /* a mapping: the FM-1's control, the MIDI one, its kind */
                const mp = M.state.midi[i - M.MIDI_SETTINGS.length];
                D.text(4, y, F.T, ctlName(mp.ctl), on ? D.WHITE : D.YELLOW);
                D.text(94, y, F.T, midiName(mp), on ? D.WHITE : D.LGREY);
                const kd = M.MIDI_KINDS[mp.kind];
                D.text(236 - D.tw(F.T, kd), y, F.T, kd, on ? D.WHITE : D.DGREY);
            }
        }
        if (!M.state.midi.length && n - top < ROWS) D.text(4, Y0 + 3 + (n - top) * 15, F.T, "NO MAPPINGS YET: EDIT ON LEARN", D.DGREY);
    }
    /* CHECK REFERENCES' answer: what is missing, a line each; SELECT scrolls, HOME closes */
    function missingList(lines) {
        const ROWS = 14;
        U.open({
            kind: "list", title: "MISSING REFERENCES: " + lines.length, top: 0,
            knob(e, s) { if (e === E.SELECT) this.top = U.clamp(this.top + s, 0, Math.max(0, lines.length - ROWS)); },
            tap() { return false; },
            draw() {
                const y = Y0 + 2;
                U.panel(2, y, 236, 206, this.title);
                lines.slice(this.top, this.top + ROWS).forEach((l, i) => D.text(8, y + 18 + i * 13, F.T, l.slice(0, 37), D.LGREY));
            },
        });
    }
    /* CLEAN UP asks what goes, as a checklist: the unused patterns and samples ticked, the unused
     * instruments not (they are most of a bank: a library, not leftovers); the title says what the
     * ticked lines free */
    function cleanUp(after) {
        const cl = M.cleanable();
        const rows = [["UNUSED PATTERNS", "patterns", true], ["UNUSED SAMPLES", "samples", true], ["UNUSED INSTRUMENTS", "instruments", false]]
            .map(([name, k, on]) => {
                const n = cl[k].items.length;
                return { name, k, count: n, on: on && n > 0, note: n ? n + "  " + kb(cl[k].bytes) : "NONE" };
            });
        if (!rows.some(r => r.count)) { U.toast("NOTHING TO CLEAN UP"); return; }
        U.checklist(rs => "CLEAN UP: FREES " + kb(rs.reduce((a, r) => a + (r.on ? cl[r.k].bytes : 0), 0)), rows, ["CLEAN UP", "KEEP"], (a, rs) => {
            if (a !== "CLEAN UP") return;
            const on = k => rs.find(r => r.k === k).on;
            let n = 0;
            U.edit(() => {
                if (on("patterns")) for (const p of cl.patterns.items) { M.state.patterns[p.n] = null; n++; }
                if (on("samples")) for (const { c, k } of cl.samples.items) {
                    c.samples[k] = Object.assign({}, c.samples[k], { name: "(DELETED)", length: 0, deleted: true }); n++;
                }
                if (on("instruments")) for (const { c, k } of cl.instruments.items) { c.slots[k] = null; n++; }
            });
            U.toast("CLEANED UP: " + n + " DELETED");
            if (after) after();                              /* (storage full: then the save, if it fits now) */
        });
    }
    U.cleanUp = cleanUp;
    function runTool(t) {
        /* CHECK STORAGE: every record's CRC read back on the FM-1; the playground's records cannot be damaged */
        if (t === "CHECK STORAGE") { U.progress("CHECKING STORAGE", 600, () => U.toast("STORAGE OK")); return; }
        if (t === "CHECK REFERENCES") {
            /* every reference to something that is not there: a cell's instrument, a song position's or a
             * FUSION slot's pattern */
            const miss = [];
            for (const p of M.state.patterns) if (p && p.n !== M.EDITOR_PATTERN) p.cells.forEach((c, i) => {   /* (its own never by number) */
                if (c && c.inst != null && !M.inst(c.inst))
                    miss.push("PATTERN " + M.hex(p.n) + " ROW " + M.hex(i >> 4) + " TRACK " + M.hex(i & 15) + ": " + M.refText(c.inst));
            });
            M.state.songs.forEach((s, k) => s.order.forEach((n, pos) => {
                if (!M.pattern(n)) miss.push("SONG " + M.hex(k) + " POSITION " + M.hex(pos) + ": PATTERN " + M.hex(n));
            }));
            for (const c of M.state.carts) if (c.type === "FUSION") c.slots.forEach((sl, j) => {
                if (sl && sl.p.pattern != null && !M.pattern(sl.p.pattern)) miss.push("FUSION " + M.refText(c.id << 8 | j) + ": PATTERN " + M.hex(sl.p.pattern));
            });
            U.progress("CHECKING", 600, () => miss.length ? missingList(miss) : U.toast("NO MISSING REFERENCES"));
        }
        if (t === "CLEAN UP") cleanUp();
        /* AUTO LEVEL ALL: every sound of the user area (the built-in ones keep theirs), after its question */
        if (t === "AUTO LEVEL ALL") {
            const n = M.state.carts.filter(c => !c.builtin).reduce((a, c) => a + c.slots.filter(Boolean).length, 0);
            U.ask(["AUTO LEVEL " + n + (n === 1 ? " SOUND?" : " SOUNDS?"), "EACH ONE'S LEVEL SET AGAIN"], ["LEVEL", "KEEP"], a => {
                if (a === "LEVEL") U.progress("MEASURING EVERY SOUND", 1800, () => U.toast("LEVELS SET"));
            });
        }
        if (t === "REVERT ALL") U.revert();                  /* (SAVE + OCT-'s question) */
    }
    const ABOUT = ["PT/FM " + U.version + " DEV", "POCKET TRACKER / FUSION MACHINE", "",
                   "GPL-3.0-ONLY, (C) 2026 HITOSU", "",
                   "FONTS: TERMINUS, SIL OFL 1.1", "DX7 AFTER MSFA (APACHE-2.0)", "JUNO-106 AFTER KR-106 (GPL-3.0)",
                   "TB-303 AFTER OPEN303 (MIT)"];          /* the firmware's own: the data's credits travel with the data */
    /* the MIDI page's knobs and taps: SELECT the items, then the mappings; K1 an item; LFO its default, or
     * deletes the mapping picked; EDIT on LEARN learns */
    function midiKnob(e, s) {
        if (e === E.SELECT) { set.msel = U.clamp(set.msel + Math.sign(s), 0, midiRows() - 1); return; }
        const it = midiItem();
        if (e !== E.K1 || !it || it.learn || it.ctl) return;
        const v = U.set(it.k);
        const nv = it.list ? it.list[U.clamp(it.list.indexOf(v) + Math.sign(s), 0, it.list.length - 1)] : U.clamp(v + Math.sign(s), it.min, it.max);
        U.edit(() => { M.state.settings[it.k] = nv; });
        if (it.k === "SYNC") A.pulseSide();                  /* (OUTPUT's SYNC PULSE: with SYNC INTERNAL) */
    }
    function midiTap(b) {
        const it = midiItem(), mp = midiMap();
        if (b === B.LFO) {
            if (mp) {
                U.edit(() => { M.state.midi.splice(set.msel - M.MIDI_SETTINGS.length, 1); });   /* (SEL's REVERT brings it back) */
                set.msel = Math.min(set.msel, midiRows() - 1);
                U.toast(ctlName(mp.ctl) + ": " + midiName(mp) + " DELETED");
            } else if (it && !it.learn && !it.ctl) {
                U.edit(() => { M.state.settings[it.k] = it.def; });
                if (it.k === "SYNC") A.pulseSide();
            }
            return true;
        }
        if (b === B.SEL) { selMenu(); return true; }
        if (b === B.EDIT && it && it.learn) { learn(); return true; }
        if (b === B.EDIT && it && it.ctl) { U.controller(); return true; }
        return b === B.EDIT;
    }
    /* SEL's REVERT (specs/ui/screens/settings.md#the-sel-menu): the page's own as saved, the settings record on the
     * list (the MIDI page's channels, SYNC and CLOCK OUT among them), the MIDI map on the MIDI page, applied again at
     * once; with nothing unsaved there, SEL is refused */
    function selMenu() {
        const kind = set.page === 1 ? "midi" : "settings";
        if (!M.unsavedThing(kind)) { U.refuse(B.SEL); U.toast("NOTHING TO REVERT"); return; }
        U.menu("settings", set.page === 1 ? "MIDI" : "SETTINGS", ["REVERT"], a => {
            if (a !== "REVERT") return;
            U.askRevert(kind === "midi" ? "REVERT THE MIDI MAP TO SAVED?" : "REVERT THE SETTINGS TO SAVED?", null, () => {
                M.revertThing(kind);
                set.msel = U.clamp(set.msel, 0, midiRows() - 1);
                if (kind === "settings") A.pulseSide();
            });
        });
    }
    const turnPage = d => { set.page = U.clamp(set.page + d, 0, PAGE_NAMES.length - 1); };
    U.register({
        name: "SETTINGS", group: "SYSTEM",
        does: [B.LFO, B.EDIT, B.SEQ],
        sub: () => PAGE_NAMES[set.page],                    /* the header's subpage: MIDI on the second */
        seq: () => M.pageOf(set.page, PAGE_NAMES.length),   /* SEQ turns the pages: `SEQ 1/2` in the header */
        knob(e, s) {
            if (e === E.ALGORITHM) { turnPage(Math.sign(s)); return; }
            if (set.page === 1) { midiKnob(e, s); return; }
            if (e === E.SELECT) { set.sel = U.clamp(set.sel + Math.sign(s), 0, M.SETTINGS.length - 1); return; }
            if (e !== E.K1) return;
            const it = M.SETTINGS[set.sel], k = it.k, v = val(k);
            if (k === "ABOUT") return;
            let nv;
            if (it.list) nv = it.list[U.clamp(it.list.indexOf(v) + Math.sign(s), 0, it.list.length - 1)];
            else nv = U.clamp(v + Math.sign(s) * (it.step || 1), it.min, it.max);   /* one step a click, however fast */
            if (k === "TOOLS") { tool = nv; return; }        /* (picking a tool is no change) */
            if (nv === v) return;                            /* at an end: nothing changed, nothing unsaved */
            U.edit(() => { M.state.settings[k] = nv; });
            apply(k, nv);
            voicesTold(k);
        },
        tap(b) {
            if (b === B.SEQ) { set.page = (set.page + 1) % PAGE_NAMES.length; return true; }
            if (set.page === 1) return midiTap(b);
            const it = M.SETTINGS[set.sel];
            if (b === B.LFO) {                               /* (TOOLS and ABOUT have no default to go back to) */
                if (it.k !== "TOOLS" && it.k !== "ABOUT" && U.set(it.k) !== it.def) { U.edit(() => { M.state.settings[it.k] = it.def; }); apply(it.k, it.def); voicesTold(it.k); }
                return true;
            }
            if (b === B.SEL) { selMenu(); return true; }
            if (b === B.EDIT) {
                if (it.k === "TOOLS") { runTool(tool); return true; }
                if (it.k === "ABOUT") {
                    U.open({ kind: "about", opener: B.EDIT, knob() {}, tap(bb) { if (bb === B.EDIT) { U.close(this); return true; } return true; },
                        draw() { D.rect(0, Y0, 240, H, D.BLACK); ABOUT.forEach((l, i) => D.text(0, Y0 + 2 + i * 12, F.T, l, i === 0 ? D.WHITE : l.startsWith("FONTS") ? D.CYAN : D.LGREY)); } });
                    return true;
                }
            }
            return false;
        },
        footer() {
            if (set.page === 1) {
                const it = midiItem();
                return !it ? ["LFO: DELETE", D.LGREY] : it.learn ? ["EDIT: LEARN", D.LGREY] : it.ctl ? ["EDIT: SWITCH", D.LGREY]
                     : ["DEFAULT " + fmt(it, it.def), D.DGREY];
            }
            const it = M.SETTINGS[set.sel];
            return it.k === "TOOLS" || it.k === "ABOUT" ? ["EDIT: RUN", D.LGREY] : ["DEFAULT " + fmt(it, it.def), D.DGREY];
        },
        draw() {
            if (set.page === 1) { midiDraw(); return; }
            /* 14 rows show; more scroll, the selected one kept in the middle while it can */
            const ROWS = 14, top = U.clamp(set.sel - 6, 0, Math.max(0, M.SETTINGS.length - ROWS));
            M.SETTINGS.slice(top, top + ROWS).forEach((it, k) => {
                const i = top + k, y = Y0 + 3 + k * 15, on = i === set.sel, v = val(it.k);
                if (on) D.box(0, y - 2, 240, 15, D.WHITE);
                D.text(4, y, F.T, it.k, on ? D.WHITE : D.CYAN);
                const s = fmt(it, v), col = it.k === "THEME" ? D.LMAGENTA : v === it.def ? D.LGREY : D.YELLOW;
                D.text(236 - D.tw(F.T, s), y, F.T, s, on ? D.WHITE : col);
            });
        },
    });
})();
