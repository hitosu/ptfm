/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: what the EDITOR and PLAYBACK share: the pattern grid (the EDITOR's views of
 * 8, 4 and 2 tracks, the focus row, the playing row's band, the marked block, beat rows), the pattern
 * list (ENV) and the questions about references (specs/ui/saving.md).
 *
 * A cell holds up to four commands and the grid shows one: in the 2-track view the one in the slot
 * the EDITOR's K1 picks (the first when K1 is on the volume), with a pip under it for each command the
 * cell holds, the one shown white. */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, C = PTFM.cmds;
    const Y0 = U.BODY_Y;

    /* the views: tracks shown, row height, focus row height, the track numbers' height, rows a page */
    U.VIEWS = {
        8: { n: 8, rowH: 12, focusH: 16, head: 12, rows: 16 },
        4: { n: 4, rowH: 16, focusH: 20, head: 12, rows: 12 },
        2: { n: 2, rowH: 24, focusH: 24, head: 14, rows: 8 },
        1: { n: 1, rowH: 24, focusH: 24, head: 14, rows: 8 },
    };
    U.isFusion = n => M.state.carts.some(c => c.type === "FUSION" && c.slots.some(s => s && s.p.pattern === n));

    const missing = c => c.inst != null && !M.inst(c.inst);
    /* a cell's colour: its instrument's engine, or the track's current instrument's when it names none */
    const noteCol = (c, inst) => c.note === M.OFF ? D.LGREY : missing(c) ? D.RED : inst != null && M.inst(inst) ? M.instCol(inst) : D.LGREY;

    /* o: {view, top, left, cur {row, track} or null, play row or null, block {r0, r1, t0, t1}, float
     * (a carried block: {cells, rows, tracks, row, track}), slot (the EDITOR's K1: 0 the volume, 1-4 a
     * command), hot (rows to light: a FUSION's rows the keys play), y0} */
    U.grid = function (p, o) {
        const V = U.VIEWS[o.view], fusion = U.isFusion(p.n), rpb = (M.state.songs[U.song] || { rpb: 4 }).rpb;
        const labW = 14, n =Math.min(V.n, p.tracks - o.left);
        const colW = Math.floor((240 - labW) / V.n);
        let y = o.y0 || Y0;
        /* the track numbers; in the 2-track view, what K1 picked above its column */
        for (let k = 0; k < n; k++) {
            const t = o.left + k, x = labW + k * colW, s = M.hex(t);
            if (o.view === 2) {
                D.text(x, y + (V.head - 12), F.T, s, U.muted(t) ? D.DGREY : t === (o.cur && o.cur.track) ? D.WHITE : D.CYAN);
                if (o.slot != null) D.text(o.slot ? x + 79 : x + 61, y + (V.head - 12), F.T, o.slot ? "CMD" + o.slot : "VOL", D.DGREY);
                continue;
            }
            if (o.view === 1) {                             /* the track and its instrument at the cursor's row (PLAYBACK: the playing row) */
                D.text(x, y + (V.head - 12), F.T, s, U.muted(t) ? D.DGREY : o.cur ? D.WHITE : D.CYAN);
                const ti = M.trackInst(p, o.cur ? o.cur.row : o.play != null ? o.play : o.top, t);
                if (ti != null && M.inst(ti)) D.text(x + 18, y + (V.head - 12), F.T, M.inst(ti).name, U.muted(t) ? D.DGREY : M.instCol(ti));
                continue;
            }
            D.text(x + Math.floor((colW - 12) / 2), y + (V.head - 12), F.T, s, U.muted(t) ? D.DGREY : t === (o.cur ? o.cur.track : o.col) ? D.WHITE : D.CYAN);
        }
        if (o.left > 0) D.text(0, y, F.T, "<", D.DGREY);
        if (o.left + V.n < p.tracks) D.text(234, y, F.T, ">", D.DGREY);
        y += V.head;
        const focus = o.cur ? o.cur.row : null;
        for (let r = o.top; r < Math.min(p.rows, o.top + V.rows); r++) {
            const isF = r === focus, h = isF ? V.focusH : V.rowH;
            /* a cursor track without a cursor (PLAYBACK's): its column faintly lit, under the playing row */
            if (o.col != null && o.col >= o.left && o.col < o.left + n) D.rect(labW + (o.col - o.left) * colW, y, colW - 1, h, D.DEEP + D.DGREY);
            if (o.play === r) D.rect(0, y, 240, h, D.DEEP + D.BLUE);
            if (o.hot && o.hot.includes(r)) D.rect(0, y, 240, h, D.DEEP + D.GREEN);     /* a FUSION's row a key plays */
            if (isF) { D.hline(0, y, 240, D.DGREY); D.hline(0, y + h - 1, 240, D.DGREY); }
            const lab = M.hex(r), ty = y + Math.floor((h - 12) / 2);
            /* the row numbers in hex, as everywhere; beat rows by the current song's ROWS A BEAT, a bar's
             * first brighter; a FUSION pattern's rows are notes: the rows of its Cs stand out */
            const beat = fusion ? r % 12 === 0 : r % rpb === 0;
            D.text(0, ty, F.T, lab, r % (rpb * 4) === 0 && !fusion ? D.LCYAN : beat ? D.CYAN : D.DGREY);
            for (let k = 0; k < n; k++) {
                const t = o.left + k, x = labW + k * colW;
                const b = o.block;
                if (b && r >= b.r0 && r <= b.r1 && t >= b.t0 && t <= b.t1) D.rect(x, y + (isF ? 1 : 0), colW - 1, h - (isF ? 2 : 0), D.DEEP + D.MAGENTA);
                let c = p.cells[r * 16 + t];
                const fl = o.float;
                if (fl && r >= fl.row && r < fl.row + fl.rows && t >= fl.track && t < fl.track + fl.tracks) {
                    c = fl.cells[(r - fl.row) * fl.tracks + (t - fl.track)];
                    D.rect(x, y, colW - 1, h, D.DEEP + D.MAGENTA);
                }
                /* the 1-track view names parameters on rows without a note too: the track's instrument there */
                const inst = c && (c.note != null || o.view === 1) && c.inst == null ? M.trackInst(p, r, t) : c ? c.inst : null;
                const sel = o.cur && isF && t === o.cur.track ? o.slot : null;
                drawCell(c, inst, x, y, h, o.view, isF, U.muted(t), fusion, o.slot, sel);
                if (o.cur && isF && t === o.cur.track) D.box(x - 1, y, colW, h, D.WHITE);
            }
            y += h;
        }
    };
    /* a command in the grid's 3 characters: a group's sign in its colour (an instrument parameter in the
     * engine's, an FX command in the effect's), dim when it is ignored (a global one in a FUSION row) */
    function drawCmd(x, y, cmd, inst, col, ignored) {
        const s = C.text(cmd);
        if (!cmd) { D.text(x, y, F.T, s, D.DGREY); return; }
        if (ignored) { D.text(x, y, F.T, s, D.DGREY); return; }
        const g = C.group(cmd.c);
        let sc = col;
        if (g === "PARAM" && inst != null && M.inst(inst)) sc = M.instCol(inst);
        if (g === "FX") { const f = C.FX[cmd.c]; sc = f && f.length > 1 ? M.FX[f[0]].col : col; }
        D.text(x, y, F.T, s[0], sc);
        D.text(x + 6, y, F.T, s.slice(1), col);
    }
    function drawCell(c, inst, x, y, h, view, isF, muted, fusion, slot, sel) {
        const S = F.S, T = F.T;
        const dim = col => muted ? D.DGREY : col;
        if (view === 8) {
            const crop = isF ? null : D.CROP_S;
            if (!c || c.note == null) { D.text(x, y, S, c && !M.cellEmpty(c) ? "..." : "---", D.DGREY, crop); return; }
            if (missing(c)) D.rect(x, y, 24, h, D.DEEP + D.RED);
            D.text(x, y, S, M.noteName(c.note), dim(noteCol(c, inst)), crop);
            return;
        }
        if (view === 4) {
            const ty = y + Math.floor((h - 12) / 2);
            if (!c) { D.text(x, ty, T, "--- ..:..", D.DGREY); return; }
            const col = dim(noteCol(c, inst));
            if (missing(c)) D.rect(x + 24, ty, 30, 12, D.DEEP + D.RED);
            D.text(x, ty, T, M.noteName(c.note), c.note == null ? (M.cellEmpty(c) ? D.DGREY : D.LGREY) : col);
            D.text(x + 24, ty, T, c.inst == null ? "..:.." : M.refText(c.inst), c.inst == null ? D.DGREY : missing(c) ? D.RED : col);
            return;
        }
        if (view === 1) {
            /* 1 track, the whole cell: the note in S; beside it, in two lines of T, the instrument over the
             * volume, then the four commands two a line, each its 3 characters and its short name, all light
             * blue (the sign in its group's colour); the field K1 picked, in the cursor's cell, all white */
            const col = c ? dim(noteCol(c, inst)) : D.DGREY, engine = inst != null ? M.instType(inst) : null;
            D.text(x, y + 4, S, c ? M.noteName(c.note) : "---", c && c.note != null ? col : D.DGREY);
            const ix = x + 28;
            if (c && missing(c)) D.rect(ix, y, 30, 12, D.DEEP + D.RED);
            D.text(ix, y, T, c && c.inst != null ? M.refText(c.inst) : "..:..", c && c.inst != null ? (missing(c) ? D.RED : col) : D.DGREY);
            D.text(ix, y + 12, T, c && c.vol != null ? M.hex(c.vol) : "..", sel === 0 ? D.WHITE : c && c.vol != null ? dim(D.BROWN) : D.DGREY);
            for (let k = 0; k < 4; k++) {
                const cx = x + 64 + (k % 2) * 84, cy = y + (k >> 1) * 12, cmd = c && c.cmds[k], on = sel === k + 1;
                if (!cmd) { D.text(cx, cy, T, "...", on ? D.WHITE : D.DGREY); continue; }
                const ignored = fusion && C.global(cmd), name = C.short(cmd, engine);
                if (on) { D.text(cx, cy, T, C.text(cmd) + " " + name, D.WHITE); continue; }
                drawCmd(cx, cy, cmd, inst, dim(D.LBLUE), ignored);
                D.text(cx + 24, cy, T, name, ignored ? D.DGREY : dim(D.LBLUE));
            }
            return;
        }
        /* 2 tracks: the note in S (B in the focus row), the rest in T, one command, the pips */
        const nf = isF ? F.B : S, nw = isF ? 36 : 24, gap = 4;
        const ny = isF ? y : y + 4, ty = y + 6;
        const col = c ? dim(noteCol(c, inst)) : D.DGREY;
        D.text(x, ny, nf, c ? M.noteName(c.note) : "---", c && c.note != null ? col : D.DGREY);
        let tx = x + nw + gap;
        if (c && missing(c)) D.rect(tx, ty, 30, 12, D.DEEP + D.RED);
        D.text(tx, ty, T, c && c.inst != null ? M.refText(c.inst) : "..:..", c && c.inst != null ? (missing(c) ? D.RED : col) : D.DGREY);
        tx += 36;
        D.text(tx, ty, T, c && c.vol != null ? M.hex(c.vol) : "..", c && c.vol != null ? dim(D.BROWN) : D.DGREY);
        if (sel === 0) D.hline(tx, ty + 12, 12, D.WHITE);
        tx += 18;
        const k = slot ? slot - 1 : 0, cmds = c ? c.cmds : [], cmd = cmds[k];
        drawCmd(tx, ty, cmd, inst, dim(D.LBLUE), fusion && cmd && C.global(cmd));
        if (sel) D.hline(tx, ty + 12, 18, D.WHITE);
        for (let i = 0; i < cmds.length; i++) D.rect(tx + i * 5, ty + 14, 3, 2, i === k ? D.WHITE : dim(D.LBLUE));
    }

    /* ------------------------------------------- references (specs/ui/saving.md) --- */
    /* MOVE, SWAP or DELETE of something used asks first: YES makes the change with every reference it
     * touches, pauses playback and saves it with everything pending in one step (a SAVE: storage full asks);
     * NO makes it and leaves the references; HOME does nothing. */
    U.refAsk = function (label, uses, change) {
        if (!uses) { U.edit(() => change(false)); return; }
        U.ask([label + " IS USED " + uses + " TIME" + (uses === 1 ? "" : "S"), "UPDATE REFERENCES?"], ["YES", "NO"], a => {
            if (a === "YES") {
                if (!U.edit(() => change(true))) return;   /* (refused, the edit memory full: nothing to save) */
                M.version++;                            /* (instruments may have moved: the audio's copies sent again) */
                U.saveRefs(uses);
            } else if (a === "NO") U.edit(() => change(false));
        });
    };

    /* ------------------------------------------- the EDITOR's own pattern (specs/ui/screens/editor.md) --- */
    /* before another pattern replaces the EDITOR's own in the EDITOR: with anything in it, the warning first (YES
     * empties it, an edit, then go(); NO nothing); empty, go() at once */
    U.ownEmpty = () => {                                  /* (its change deleted: the firmware's empty one, 8 x 64) */
        const n = M.EDITOR_PATTERN;
        if (M.builtinChanged("pattern", M.pattern(n))) U.edit(() => { M.state.patterns[n] = M.builtinOriginal("pattern", n); });
    };
    U.ownAsk = function (go) {
        if (!M.patternHolds(M.EDITOR_PATTERN)) { go(); return; }
        U.ask(["EDITOR CHANGES WILL BE LOST", "DO YOU WANT TO CONTINUE?"], ["YES", "NO"], a => {
            if (a !== "YES") return;
            U.ownEmpty();
            go();
        });
    };

    /* ---------------------------------------------------------- the cartridge list --- */
    /* ENV: every cartridge: its ID, name, type (in its colour) and slots used. ENV picks the
     * highlighted one; SEL: menu(list), the actions on it, where the screen has them */
    U.cartList = function (title, currentId, pick, menu) {
        const cs = () => M.state.carts;
        return U.open({
            kind: "list", title, opener: U.B.ENV, sel: Math.max(0, M.cartIndex(currentId)),
            /* MOVE (from the menu): the cartridge floats; SELECT takes it to the next free number either
             * way, the list showing it in its new place; SEL puts it down: done(number); HOME takes it back */
            float(id, done) { this.mv = { id, at: id, done }; U.toast("SELECT MOVES IT, SEL PUTS IT DOWN"); },
            knob(e, s) {
                if (e !== U.E.SELECT) return;
                if (this.mv) {                               /* within the user's numbers, 00-EF */
                    let k = this.mv.at;
                    do k += Math.sign(s); while (k >= 0 && k <= M.USER_LAST && M.cart(k) && k !== this.mv.id);
                    if (k >= 0 && k <= M.USER_LAST) this.mv.at = k;
                    return;
                }
                this.sel = U.clamp(this.sel + s, 0, cs().length - 1);
            },
            tap(b) {
                if (this.mv) {
                    if (b === U.B.SEL) { const { id, at, done } = this.mv; this.mv = null; if (at !== id) done(at); }
                    return true;
                }
                if (b === this.opener) { U.close(this); pick(cs()[this.sel]); return true; }
                if (b === U.B.SEL && menu) { menu(this); return true; }
                return false;
            },
            cancel() {
                if (this.mv) { this.mv = null; U.toast("NOT MOVED"); return; }
                U.close(this);
            },
            draw() {
                const y = Y0 + 2, mv = this.mv;
                U.panel(2, y, 236, 206, this.title);
                D.text(8, y + 16, F.T, "ID NAME         TYPE      SLOTS", D.DGREY);
                /* a cartridge afloat is listed under the number it would take, boxed yellow */
                const list = mv ? cs().map(c => ({ c, id: c.id === mv.id ? mv.at : c.id })).sort((a, b) => a.id - b.id)
                                : cs().map(c => ({ c, id: c.id }));
                const sel = mv ? list.findIndex(x => x.c.id === mv.id) : this.sel;
                const ROWS = 13, top = Math.max(0, Math.min(sel - 6, list.length - ROWS));   /* scrolls with the cursor */
                list.slice(top, top + ROWS).forEach(({ c, id }, k) => {
                    const i = top + k, yy = y + 30 + k * 13, on = i === sel, used = c.slots.filter(Boolean).length;
                    if (on) D.box(4, yy - 1, 232, 13, mv ? D.YELLOW : D.WHITE);
                    D.text(8, yy, F.T, M.hex(id), M.asBuilt("cart", c) ? D.BROWN : on ? D.WHITE : D.LGREY);    /* built in: brown */
                    D.text(26, yy, F.T, c.name, on ? D.WHITE : D.LGREY);
                    D.text(110, yy, F.T, M.TYPE_NAMES[c.type], M.ENGINES[c.type].col);
                    D.text(170, yy, F.T, M.dec(used, 3) + "/" + M.dec(c.size, 3), D.LGREY);
                });
            },
        });
    };

    /* ---------------------------------------------------------------- the song list --- */
    /* ENV on PLAYBACK and STATUS: every song: its number, name, tempo, length; the one playing green. ENV
     * again picks the highlighted one (pick; with none, it just closes the list); HOME closes; play(k),
     * where given (STATUS): PLAY starts that one */
    U.songList = function (title, current, pick, play) {
        const ss = () => M.state.songs;
        const o = U.open({
            kind: "list", title, opener: U.B.ENV, sel: U.clamp(current, 0, ss().length - 1),
            knob(e, s) { if (e === U.E.SELECT) this.sel = U.clamp(this.sel + s, 0, ss().length - 1); },
            tap(b) {
                if (b === this.opener) { U.close(this); if (pick) pick(this.sel); return true; }    /* (no pick: ENV just closes it) */
                return false;
            },
            cancel() { U.close(this); },
            draw() {
                const y = Y0 + 2, P = PTFM.player, playing = P.playing ? P.playingSong() : -1;
                U.panel(2, y, 236, 206, this.title);
                for (const [x, w] of [[8, "NR"], [26, "NAME"], [152 - D.tw(F.T, "BPM"), "BPM"], [164, "TIME"]])
                    D.text(x, y + 16, F.T, w, D.DGREY);      /* (the column heads, each over its column) */
                const ROWS = 13, top = Math.max(0, Math.min(this.sel - 6, ss().length - ROWS));
                ss().slice(top, top + ROWS).forEach((s, k) => {
                    const i = top + k, yy = y + 30 + k * 13, on = i === this.sel, col = on ? D.WHITE : i === playing ? D.GREEN : D.LGREY;
                    if (on) D.box(4, yy - 1, 232, 13, D.WHITE);
                    D.text(8, yy, F.T, M.hex(i), col);
                    D.text(26, yy, F.T, s.name, col);
                    const bpm = String(M.bpm(s));
                    D.text(152 - D.tw(F.T, bpm), yy, F.T, bpm, on ? D.WHITE : D.CYAN);
                    D.text(164, yy, F.T, U.fmtTime(U.songSeconds(s)), col);
                });
            },
        });
        if (play) o.play = function () { U.close(this); play(this.sel); };   /* (ui.js gives PLAY to an overlay with play()) */
        return o;
    };

    /* ------------------------------------------------------------- the pattern list --- */
    /* ENV: every pattern of the pool: its number, tracks x rows, a map of its notes, the songs that
     * use it (an unused one is dim); never the EDITOR's own. The key that opened it picks; SEL: NEW, CLONE, MOVE,
     * DELETE. opts.plus: `+ NEW PATTERN` first, picked as "NEW" (SONGS' NEW: the new song's first pattern; opts.plusText
     * its own words: LOOPS' SAVE TO PATTERN `NEW`); opts.except: a pattern left out (STORE IN A PATTERN: the EDITOR's) */
    U.patternList = function (title, current, pick, opener = U.B.ENV, opts = {}) {
        const list = () => M.patternNums().filter(n => n !== opts.except);
        let ns = list();
        const P = PTFM.player, heard = {}, plus = opts.plus ? 1 : 0;
        const patAt = i => i >= plus ? ns[i - plus] : null;           /* (the line's pattern; null: + NEW PATTERN) */
        const at = ns.indexOf(current);
        const o = U.open({
            /* for a FUSION slot (opts.fusionKeys): the keys play the highlighted pattern as the FUSION would */
            key(n, down) {
                if (!opts.fusionKeys) return false;
                if (down) heard[n] = P.fusionRowOn("pl" + n, ns[this.sel], U.midi(n), U.set("KEYS VOLUME"));
                else if (heard[n]) { P.noteOff(heard[n]); delete heard[n]; }
                return true;
            },
            kind: "list", title, opener, sel: at >= 0 ? at + plus : 0,
            knob(e, s) {
                if (e !== U.E.SELECT) return;
                if (this.mv) { this.floatStep(Math.sign(s)); return; }
                this.sel = U.clamp(this.sel + s, 0, ns.length + plus - 1);
                const n = patAt(this.sel);                                  /* the preview follows */
                if (P.auditioning() >= 0 && P.auditioning() !== n) { if (n == null) P.auditionEnd(); else P.audition(n, opts.timing); }
            },
            /* PLAY: the highlighted pattern heard on its own; again: it stops. Picking one or leaving ends it.
             * opts.timing(n): its timing (LOOPS' LOAD: its song's), else the current song's */
            play() {
                const n = patAt(this.sel);
                if (n == null) return;
                if (P.auditioning() === n) P.auditionEnd(); else P.audition(n, opts.timing);
            },
            tap(b) {
                if (this.mv) { if (b === U.B.SEL) this.putDown(); return true; }
                if (b === this.opener) { P.auditionEnd(); U.close(this); const n = patAt(this.sel); pick(n == null ? "NEW" : n); return true; }
                if (b === U.B.SEL) { menu(); return true; }
                if (b === U.B.ARP) { this.play(); return true; }   /* (ARP does what PLAY does: a shortcut the hint leaves out) */
                return false;
            },
            lit(b) { return b === U.B.ARP && P.auditioning() >= 0 ? 1 : undefined; },   /* ARP lit while the preview plays */
            cancel() {
                if (this.mv) { this.mv = null; U.toast("NOT MOVED"); return; }
                P.auditionEnd(); U.close(this);
            },
            draw() {
                ns = list();
                /* a pattern afloat (MOVE) is listed under the number it would take, boxed yellow */
                const mv = this.mv, nums = mv ? ns.filter(x => x !== mv.n).concat(mv.at).sort((a, b) => a - b) : ns;
                const sel = mv ? nums.indexOf(mv.at) + plus : this.sel;
                const y = Y0 + 2, rows = 14;                            /* (13 lines, then the hint) */
                U.panel(2, y, 236, 206, this.title);
                D.text(8, y + 16, F.T, "NR  SIZE   NOTES BY TRACK     SONGS", D.DGREY);
                const top = Math.max(0, Math.min(sel - 6, nums.length + plus - rows + 1));
                for (let l = top; l < Math.min(nums.length + plus, top + rows - 1); l++) {
                    const i = l - plus, yy = y + 30 + (l - top) * 12, on = l === sel;
                    if (i < 0) {                                         /* + NEW PATTERN: an empty one, 8 x 64 */
                        if (on) D.box(4, yy - 1, 232, 12, D.WHITE);
                        D.text(8, yy, F.T, opts.plusText || "+ NEW PATTERN", D.WHITE);
                        continue;
                    }
                    const p = M.pattern(mv && nums[i] === mv.at ? mv.n : nums[i]);
                    const uses = M.songsUsing(p.n), fus = U.isFusion(p.n), dim = !uses && !fus;
                    if (on) D.box(4, yy - 1, 232, 12, mv ? D.YELLOW : D.WHITE);
                    D.text(8, yy, F.T, M.hex(nums[i]), M.asBuilt("pattern", p) ? D.BROWN : on ? D.WHITE : dim ? D.DGREY : D.LGREY);    /* built in: brown */
                    D.text(32, yy, F.T, M.dec(p.tracks) + "X" + String(p.rows).padEnd(3), dim ? D.DGREY : D.CYAN);
                    for (let t = 0; t < 16; t++) {
                        if (t >= p.tracks) continue;
                        let k = 0;
                        for (let r = 0; r < p.rows; r++) { const c = p.cells[r * 16 + t]; if (c && c.note != null && c.note !== M.OFF) k++; }
                        const col = !k ? D.DGREY : k < p.rows / 8 ? D.LGREY : k < p.rows / 3 ? D.WHITE : D.YELLOW;
                        D.rect(80 + t * 6, yy + 4, 4, 4, dim && k ? D.DGREY : col);
                    }
                    D.text(186, yy, F.T, fus ? "FUSION" : uses ? String(uses) : "-", fus ? D.LMAGENTA : dim ? D.DGREY : D.LGREY);
                }
                D.runs(8, y + 30 + (rows - 1) * 12, F.T, [["PLAY", D.YELLOW], [P.auditioning() >= 0 ? ": STOP HEARING IT" : ": HEAR IT", D.LGREY]]);
            },
        });
        /* a built-in pattern (F0-FD, specs/ui/saving.md#fws; the firmware has none yet) is edited as any, never moves
         * or goes, and once changed BACK TO BUILT-IN drops the change (the firmware's own again) */
        function menu() {
            if (plus) return;                                   /* (opened by SEL, which picks: no menu) */
            const n = ns[o.sel], p = M.pattern(n), label = "PATTERN " + M.hex(n);
            const items = p.builtin ? ["NEW", "CLONE", M.builtinChanged("pattern", p) && "BACK TO BUILT-IN"]
                        : ["NEW", "CLONE", "MOVE", "DELETE"];
            U.menu("plist", label, items, a => {
                const free = M.freePattern();
                if ((a === "NEW" || a === "CLONE") && free < 0) { U.toast("ALL " + (M.USER_LAST + 1) + " PATTERNS USED"); return; }
                /* (not made, the edit memory full: no toast, nothing selected) */
                if (a === "NEW" && U.edit(() => { M.state.patterns[free] = M.newPattern(free, 8, 64); })) {
                    o.sel = list().indexOf(free); U.toast("PATTERN " + M.hex(free) + " MADE");
                }
                if (a === "CLONE" && U.edit(() => {           /* a copy in the user area, editable */
                    const c = structuredClone(p); c.n = free; delete c.rom; delete c.builtin;
                    M.state.patterns[free] = c;
                })) { o.sel = list().indexOf(free); U.toast(M.hex(n) + " CLONED TO " + M.hex(free)); }
                if (a === "DELETE") U.refAsk(label, M.patternUses(n), upd => {
                    M.state.patterns[n] = null;
                    if (upd) {                                   /* (a song whose whole order is it keeps it: never
                                                                  * left with no positions; a FUSION slot: no pattern) */
                        for (const s of M.state.songs) if (s.order.some(x => x !== n)) {
                            s.order = s.order.filter(x => x !== n);
                            if (s.loopFrom >= s.order.length) s.loopFrom = 0;   /* (LOOP FROM past the end: the start) */
                        }
                        for (const c of M.state.carts) if (c.type === "FUSION") for (const sl of c.slots) if (sl && sl.p.pattern === n) sl.p.pattern = null;
                    }
                    o.sel = Math.min(o.sel, list().length - 1);
                    U.toast(label + " DELETED");
                });
                if (a === "MOVE") moveTo(n);
                if (a === "BACK TO BUILT-IN") U.edit(() => {         /* (an edit: REVERT brings the change back) */
                    M.state.patterns[n] = M.builtinOriginal("pattern", n);
                    U.toast(label + " BUILT-IN AGAIN");
                });
            }, U.B.SEL);
        }
        /* MOVE: the pattern floats; SELECT takes it to the next free number either way, the list showing
         * it in its new place; SEL puts it down (after the question about references), HOME takes it back */
        function moveTo(n) {
            if (M.freePattern() < 0) { U.toast("NO FREE NUMBER"); return; }
            o.mv = { n, at: n };
            U.toast("SELECT MOVES IT, SEL PUTS IT DOWN");
        }
        function putDown() {
            const { n, at: to } = o.mv;
            o.mv = null;
            if (to === n) return;
            U.refAsk("PATTERN " + M.hex(n), M.patternUses(n), upd => {
                const p = M.state.patterns[n];
                M.state.patterns[n] = null;
                p.n = to;
                M.state.patterns[to] = p;
                if (upd) {
                    for (const s of M.state.songs) s.order = s.order.map(x => x === n ? to : x);
                    for (const c of M.state.carts) if (c.type === "FUSION") for (const sl of c.slots) if (sl && sl.p.pattern === n) sl.p.pattern = to;
                }
                o.sel = list().indexOf(to);
                U.toast("PATTERN " + M.hex(n) + " MOVED TO " + M.hex(to));
            });
        }
        o.floatStep = d => {
            let k = o.mv.at;
            do k += d; while (k >= 0 && k <= M.USER_LAST && M.state.patterns[k] && k !== o.mv.n);   /* the user's numbers */
            if (k >= 0 && k <= M.USER_LAST) o.mv.at = k;
        };
        o.putDown = putDown;
        return o;
    };

    U.fmtTime = s => { s = Math.round(s); return M.dec(Math.floor(s / 60)) + ":" + M.dec(s % 60); };
    /* a song's length, at its own tempo and ticks (tempo commands left out) */
    U.songSeconds = s => s.order.reduce((a, n) => { const p = M.pattern(n); return a + (p ? p.rows : 0); }, 0) * s.ticks * 2.5 / s.tempo;
})();
