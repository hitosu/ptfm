/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the EDITOR (specs/ui/screens/editor.md): the pattern in 8, 4, 2 or 1 tracks;
 * REC switches writing (step by step, also while the loop plays on; a realtime take that loops); a key writes
 * its note with the current instrument; K1 picks the cell's slot (its volume, or one of its up to four
 * commands: the 2-track view shows that one, the 1-track view all four), K2 the slot's command, K3 and K4 its values (in the
 * parameter, mixer and FX groups K3 picks which, K4 sets it); LFO clears; EDIT the note editor; GLO
 * the pattern's properties; ENV the pattern list; SEL blocks and their menu; ARP the row preview. */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, B = U.B, E = U.E, P = PTFM.player, C = PTFM.cmds;
    const Y0 = U.BODY_Y;
    const ed = { view: 8, row: 0, track: 0, left: 0, rec: false, noteEd: false, sess: null, anchor: null, block: null, marking: false, float: null, slot: 1,
                 transpose: 0, tmode: false, arp: false, held: {}, chordRow: null, takeRow: 0 };
    U.editor = ed;
    let clip = null, playHoldO = null;

    /* the pattern edited: the one picked, or the EDITOR's own when that is gone (it is always there;
     * specs/ui/screens/editor.md#the-editors-own-pattern) */
    const pat = () => {
        if (!M.pattern(U.pattern)) U.pattern = M.EDITOR_PATTERN;
        return M.pattern(U.pattern);
    };
    const own = () => pat() && U.pattern === M.EDITOR_PATTERN;
    /* the EDITOR on another pattern, its cursor at the top */
    const goTo = n => { stopRecording(); U.pattern = n; ed.row = 0; ed.left = 0; clampCursor(); };
    const V = () => U.VIEWS[ed.view];
    const cellAt = (r, t) => pat().cells[r * 16 + t];
    function clampCursor() {
        const p = pat();
        ed.row = U.clamp(ed.row, 0, p.rows - 1);
        ed.track = U.clamp(ed.track, 0, p.tracks - 1);
        scrollX();
    }
    /* horizontal scrolling: the 1-track view shows the cursor's track; the 2-track view pages by 2; the
     * 4- and 8-track views keep one track of look-ahead and shift by 2 */
    function scrollX() {
        const p = pat(), n = V().n;
        if (ed.view === 1) ed.left = ed.track;
        else if (ed.view === 2) ed.left = Math.floor(ed.track / 2) * 2;
        else {
            if (ed.track >= ed.left + n - 1 && ed.left + n < p.tracks) ed.left = Math.min(ed.left + 2, p.tracks - n);
            if (ed.track <= ed.left && ed.left > 0) ed.left = Math.max(0, ed.left - 2);
            while (ed.track >= ed.left + n) ed.left++;
            while (ed.track < ed.left) ed.left--;
        }
        ed.left = U.clamp(ed.left, 0, Math.max(0, p.tracks - n));
    }
    const recording = () => !!P.take;                  /* a realtime take */
    function playRow() {
        const h = P.at();
        return (P.playing || P.stepped) && h && h.pat === U.pattern ? h.row : null;
    }
    function blockRect() {
        if (ed.block) return ed.block;
        return { r0: ed.row, r1: ed.row, t0: ed.track, t1: ed.track };
    }
    /* the block from a mark's anchor to the cursor */
    const blockOf = a => ({ r0: Math.min(a.row, ed.row), r1: Math.max(a.row, ed.row), t0: Math.min(a.track, ed.track), t1: Math.max(a.track, ed.track) });

    /* ------------------------------------------------------------------ writing --- */
    const live = () => recording();
    /* a cell written: an edit, heard at once (a take's notes too, each as it comes); the edit memory full ends a take */
    function put(r, t, c) {
        const p = pat();
        if (r < 0 || r >= p.rows || t < 0 || t >= p.tracks) return false;
        const ok = U.edit(() => { p.cells[r * 16 + t] = c; });
        if (!ok && live()) stopRecording();
        return ok;
    }
    function advance() { const p = pat(); if (p.step) ed.row = U.clamp(ed.row + p.step, 0, p.rows - 1); }

    /* where a take's key lands by SETTINGS' QUANTIZE (specs/ui/screens/editor.md#realtime-takes), from where the take is
     * heard (h): the nearest line of 2, 3 or 4 rows from the pattern's start (halves up), ROW the nearest row: waiting
     * for it when it is ahead (past the pattern's end: row 0 of the next pass, the take looping); REAL TIME the row it
     * falls in, its tick a note delay EDx (at most 15), at once. {pass, row, delay, wait} */
    function landing(h) {
        const rows = pat().rows, frac = U.clamp(P.rowFrac(), 0, .999), pos = h.row + frac, q = U.set("QUANTIZE");
        if (q === "REAL TIME") return { pass: h.pass, row: h.row, delay: Math.min(15, Math.floor(frac * P.rowTicks())), wait: false };
        const Q = { "2 ROWS": 2, "3 ROWS": 3, "4 ROWS": 4 }[q] || 1, L = Math.floor(pos / Q + .5) * Q;
        return L >= rows ? { pass: h.pass + 1, row: 0, delay: 0, wait: true } : { pass: h.pass, row: L, delay: 0, wait: L > pos };
    }
    /* a cell's note delay set (0: none), the cell's other commands kept (at most four) */
    function delayed(c, d) {
        c.cmds = c.cmds.filter(x => !(x.c === 0x0E && x.v >> 4 === 0xD));
        if (d && c.cmds.length < 4) c.cmds.push({ c: 0x0E, v: 0xD0 | d });
        return c;
    }
    const taking = () => live() && P.playing && P.what === "loop";    /* (a take heard, its count-in over) */
    function keyDown(n) {
        const m = U.midi(n);
        if (m < 12 || m > 127) return;
        /* PLAY held + a key, or the take armed (COUNT-IN KEY): a take that starts with this note, on the cursor's row (the
         * take's first: the replay plays it as it starts) */
        if ((U.held(B.PLAY) || ed.armed) && ed.rec && !recording()) {
            U.consume(B.PLAY);
            ed.armed = false;
            if (playHoldO) { U.close(playHoldO); playHoldO = null; }
            startTake(true);
            if (put(ed.row, ed.track, delayed(write(ed.row, ed.track, m), 0))) ed.held[n] = { track: ed.track, row: ed.row, pass: 0, delay: 0 };
            return;
        }
        /* a take: the note lands as QUANTIZE says and is heard as written, by its track (at its line, or at once) */
        if (taking()) {
            const h = P.at();
            if (!h || h.pat !== U.pattern) return;
            const p = pat(), at = landing(h);
            let t = ed.track;
            const busy = new Set(Object.values(ed.held).map(x => x.track));
            while (busy.has(t) && t < p.tracks - 1) t++;
            const c = delayed(write(at.row, t, m), at.delay);
            if (put(at.row, t, c)) {
                ed.held[n] = { track: t, row: at.row, pass: at.pass, delay: at.delay };
                P.takeCell(t, c, at.wait ? at.pass : -1, at.row);
            }
            return;
        }
        U.keyOn(n);
        if (live()) return;                                /* (the count-in: heard, not written) */
        /* step by step: a chord spreads to the next tracks on the same row */
        const heldNow = Object.values(ed.held);
        let row = ed.row, t = ed.track;
        if (heldNow.length && ed.chordRow != null) { row = ed.chordRow; t = Math.max(...heldNow.map(h => h.track)) + 1; }
        else { ed.chordRow = row; advance(); }
        if (t >= pat().tracks) return;
        if (put(row, t, write(row, t, m))) ed.held[n] = { track: t, row };
    }
    /* a key's note at a cell: the current instrument and KEYS VOLUME; the cell's commands stay */
    function write(r, t, m) {
        const old = cellAt(r, t), c = M.newCell(m, U.inst, U.set("KEYS VOLUME"));
        if (old) c.cmds = M.copyCell(old).cmds;
        return c;
    }
    /* a key let go in a take: its note off on the nearest row (REAL TIME: a key off Kxx at its tick, on the row it falls
     * in), at least a row after its note (past the pattern's end: the next pass's rows, the take looping), heard there
     * as written; a note there ends it already */
    function keyUp(n) {
        U.keyOff(n);
        const h = ed.held[n];
        delete ed.held[n];
        if (!Object.keys(ed.held).length) ed.chordRow = null;
        if (!h || !taking() || h.pass == null) return;
        const at = P.at();
        if (!at || at.pat !== U.pattern) return;
        const rows = pat().rows, frac = U.clamp(P.rowFrac(), 0, .999), apos = at.pass * rows + at.row + frac, from = h.pass * rows + h.row;
        let abs, k = null;
        if (U.set("QUANTIZE") === "REAL TIME") {
            k = Math.floor(frac * P.rowTicks());
            abs = Math.floor(apos);
            if (abs < from || (abs === from && k <= h.delay)) abs = from + 1;
        } else abs = Math.max(Math.floor(apos + .5), from + 1);
        const row = abs % rows, old = cellAt(row, h.track), own = abs === from;   /* (own: let go in its note's row, past its delay) */
        if (old && old.note != null && !own) return;
        const c = old ? M.copyCell(old) : M.newCell(null, null, null);
        if (k == null) c.note = M.OFF;
        else { c.cmds = c.cmds.filter(x => x.c !== 0x14); if (c.cmds.length >= 4) return; c.cmds.push({ c: 0x14, v: k }); }
        if (!put(row, h.track, c)) return;
        if (own) P.takeCell(h.track, { note: null, inst: null, vol: null, cmds: [{ c: 0x14, v: k }] }, -1, row);   /* (heard: the key off now) */
        else P.takeCell(h.track, c, abs > apos ? Math.floor(abs / rows) : -1, row);
    }

    /* the realtime take (specs/ui/screens/editor.md#realtime-takes): from the cursor's row, round the whole pattern
     * again and again until PLAY or REC, after SETTINGS' COUNT-IN; with KEY the take is armed instead, and the first
     * key starts it */
    function startTake(now) {
        if (!now && U.set("COUNT-IN") === "KEY") { ed.armed = true; return; }
        const p = pat();
        ed.takeRow = ed.row;
        P.take = { pat: p.n, start: ed.row };
        P.startTake(p.n, ed.row, now, P.COUNT_BARS[U.set("COUNT-IN")]);
    }
    function stopRecording() {
        if (!P.take) return;
        P.pause();
        P.stopCountIn();
        P.take = null;
        ed.row = ed.takeRow;
        U.toast("TAKE STOPPED");
    }

    /* ------------------------------------------------------------- K1-K4 on a cell --- */
    /* the engine a cell's parameter commands are for: its instrument's, else the track's current one's */
    function engineAt(r, t) {
        const c = cellAt(r, t), ref = c && c.inst != null ? c.inst : M.trackInst(pat(), r, t);
        return M.instType(ref != null ? ref : U.inst);
    }
    /* K1's slots: the volume, then the commands, one past the last (to add one), at most four */
    const slots = c => Math.min(4, (c ? c.cmds.length : 0) + 1);
    function clampSlot() { ed.slot = U.clamp(ed.slot, 0, slots(cellAt(ed.row, ed.track))); }
    /* a command K2 gives on landing on an entry */
    function fresh(entry, engine, taken) {
        const e = C.ENTRIES[entry];
        if (e.code == null) return null;
        if (typeof e.code === "string") {
            const codes = C.codes(e.code, engine).filter(x => !taken.includes(x));
            if (!codes.length) return null;
            const x = { c: codes[0], v: 0 };
            x.v = e.code === "PARAM" ? Math.min(64, C.max(x, engine)) : e.code === "MIXER" ? 0x7F : 0;
            return x;
        }
        return { c: e.code, v: e.sub != null ? e.sub << 4 : e.min || 0 };
    }
    function cellKnob(e, s, coarse) {
        const r = ed.row, t = ed.track, old = cellAt(r, t);
        const c = old ? M.copyCell(old) : M.newCell(null, null, null);
        const engine = engineAt(r, t);
        if (e === E.K1) {
            ed.slot = U.clamp(ed.slot + Math.sign(s), 0, slots(old));
            const x = c.cmds[ed.slot - 1];
            U.showKnob("K1", ed.slot ? "CMD " + ed.slot + " " + (x ? C.words(x, engine) : "EMPTY") : "VOLUME");
            return;
        }
        if (ed.slot === 0) {                                    /* the volume: K3 by 16, K4 by 1; past 7F: its default */
            if (e === E.K2) { U.showKnob("K3, K4", "THE VOLUME"); return; }
            const step = e === E.K3 || coarse ? 16 : 1, v = c.vol == null ? 0x80 : c.vol, nv = v + Math.sign(s) * step * Math.abs(s);
            c.vol = nv > 0x7F ? null : U.clamp(nv, 0, 0x7F);
            U.showKnob("VOLUME", c.vol == null ? ".. AS THE INSTRUMENT" : M.hex(c.vol));
        } else {
            const k = ed.slot - 1, x = c.cmds[k], en = C.entry(x), entry = C.ENTRIES[en];
            if (e === E.K2) {
                /* each code at most once in a cell: entries another slot holds are skipped */
                const others = c.cmds.filter((_, i) => i !== k);
                const ok = C.ENTRIES.map((ent, i) => i === en || ent.code == null || typeof ent.code === "string" ||
                                                     !others.some(o => o.c === ent.code) ? i : -1).filter(i => i >= 0);
                const i = ok[U.clamp(ok.indexOf(en) + s, 0, ok.length - 1)];
                const nx = i === en && x ? x : fresh(i, engine, others.map(o => o.c));
                if (nx) c.cmds[k] = nx;
                else if (x) c.cmds.splice(k, 1);
                const now = c.cmds[k];
                U.showKnob("K2", now ? C.text(now) + " " + C.ENTRIES[C.entry(now)].name : "NO COMMAND");
            } else if (!x) { U.showKnob("K" + (e - E.K1 + 1), "K2 PICKS A COMMAND"); return; }
            else if (entry.digits === 2 || entry.digits === 1) {
                if (e === E.K3 && entry.digits === 1) { U.showKnob("K3", "K4 SETS IT"); return; }
                const hi = e === E.K3, d = Math.sign(s);
                if (hi) x.v = (U.clamp((x.v >> 4) + d, 0, 15) << 4) | (x.v & 15);
                else x.v = (x.v & 0xF0) | U.clamp((x.v & 15) + d, 0, 15);
                x.v = Math.max(entry.min || 0, x.v);                /* a punch-in: never 00 */
                U.showKnob(C.text(x), C.words(x, engine));
            } else if (entry.digits === -1) {
                const g = entry.code, taken = c.cmds.filter((_, i) => i !== k).map(o => o.c);
                if (e === E.K3) {                               /* which parameter */
                    const codes = C.codes(g, engine).filter(cd => !taken.includes(cd));
                    const i = U.clamp(codes.indexOf(x.c) + Math.sign(s), 0, codes.length - 1);
                    if (codes[i] != null) { x.c = codes[i]; x.v = Math.min(x.v, C.max(x, engine)); }
                    U.showKnob("K3", C.codeName(g, engine, x.c));
                } else {                                        /* its value */
                    x.v = U.clamp(x.v + s * (coarse ? 16 : 1), 0, C.max(x, engine));
                    U.showKnob(C.text(x), C.words(x, engine));
                }
            } else { U.showKnob(C.text(x), "NO VALUE"); return; }
        }
        put(r, t, M.cellEmpty(c) ? null : c);
        clampSlot();
    }

    /* ------------------------------------------------------------------- blocks --- */
    function cut(b, clear) {
        const p = pat(), rows = b.r1 - b.r0 + 1, tracks = b.t1 - b.t0 + 1, cells = [];
        for (let r = b.r0; r <= b.r1; r++) for (let t = b.t0; t <= b.t1; t++) {
            cells.push(p.cells[r * 16 + t] ? Object.assign({}, p.cells[r * 16 + t]) : null);
            if (clear) p.cells[r * 16 + t] = null;
        }
        return { cells, rows, tracks };
    }
    /* the block's size in the clipboard, packed as a pattern's tracks are (specs/files/patterns.md): each track's
     * cells and its 0x00; the clipboard holds 2 KB (specs/ui/screens/editor.md#sel-blocks) */
    function packedBytes(b) {
        const p = pat();
        let n = 0;
        for (let t = b.t0; t <= b.t1; t++) {
            let prev = -1, last = -1;
            for (let r = 0; r <= b.r1 - b.r0; r++) {
                const c = p.cells[(b.r0 + r) * 16 + t], k = c ? Math.min(c.cmds.length, 4) : 0;
                if (M.cellEmpty(c)) continue;
                n += 1 + (c.note != null) + (c.vol != null) + (k === 1 ? 2 : k ? 2 + 2 * k : 0) + (r - prev - 1 >= 7);
                if (c.inst != null) { n += c.inst === last ? 0 : last >= 0 && last >> 8 === c.inst >> 8 ? 1 : 2; last = c.inst; }
                prev = r;
            }
            n++;
        }
        return n;
    }
    function paste(cb, r0, t0, mix) {
        const p = pat();
        for (let r = 0; r < cb.rows; r++) for (let t = 0; t < cb.tracks; t++) {
            const rr = r0 + r, tt = t0 + t;
            if (rr >= p.rows || tt >= p.tracks) continue;
            const c = cb.cells[r * cb.tracks + t];
            if (mix && (p.cells[rr * 16 + tt] || !c)) continue;
            p.cells[rr * 16 + tt] = c ? Object.assign({}, c) : null;
        }
    }
    /* the pipette (ENV held; SEL → TAKE ITS INSTRUMENT): the cursor's cell's instrument becomes the
     * current one, the keys' and PRESETS'; on a cell naming none, the one its track plays at that row */
    function takeInst(from) {
        const c = cellAt(ed.row, ed.track), ref = c && c.inst != null ? c.inst : M.trackInst(pat(), ed.row, ed.track);
        if (ref == null || !M.inst(ref)) { U.refuse(from); U.toast("NO INSTRUMENT HERE"); return; }
        U.setInst(ref);
        const t = M.instType(ref);
        U.toast(M.refText(ref) + " " + M.inst(ref).name + (t ? " " + M.ENGINES[t].short : ""));
    }
    /* SEL → RECORD PATTERN (ui.js's dialog, as LOOPS' ARP): REC puts its notes into the cursor's track
     * from the cursor's row, on to the pattern's end or once, at the song's ROWS A BEAT; only notes, each
     * the current instrument's: a step off leaves its cell as it is, and no note off is written */
    function recordPattern() {
        const rpb = (M.state.songs[U.song] || {}).rpb || 4, c = cellAt(ed.row, ed.track);
        U.patternDialog({
            opener: B.SEL, rpb, note: c && c.note != null && c.note !== M.OFF ? c.note : 60,
            title: "RECORD PATTERN INTO TRACK " + M.hex(ed.track), round: "TO THE END",
            sound: () => U.inst,
            playing: () => -1,
            where: () => [["FROM ", D.LGREY], ["ROW " + M.hex(ed.row), D.WHITE], [" OF " + M.hex(pat().rows), D.LGREY]],
            hear(m, t, len) {
                t = t == null ? PTFM.audio.time() : t;
                P.noteOn("epp", m, U.inst, U.set("KEYS VOLUME"), ed.track, t); P.noteOff("epp", t + (len || .2));
            },
            rowSec: () => { const s = M.state.songs[U.song]; return s ? s.ticks * 2.5 / s.tempo : .125; },    /* the song's tempo: PLAY's preview */
            write() {
                const rp = U.recPat, p = pat();
                if (!rp.steps.slice(0, rp.size).some(Boolean)) { U.refuse(B.REC); U.toast("NO STEPS ON"); return false; }
                const plan = U.patternPlan(p.rows, ed.row, false, U.stepRows(rpb)), tr = ed.track;
                /* the notes; with A STEP LONG the note offs, on empty cells only */
                if (!U.edit(() => {
                    for (const r of plan.on) p.cells[r * 16 + tr] = write(r, tr, rp.note);
                    for (const r of plan.off) if (!p.cells[r * 16 + tr]) p.cells[r * 16 + tr] = M.newCell(M.OFF, null, null);
                })) return false;
                U.toast("PATTERN: " + plan.on.length + (plan.on.length === 1 ? " NOTE" : " NOTES"));
                return true;
            },
        });
    }
    function blockMenu() {
        if (ed.float) { dropFloat(); return; }
        if (ed.tmode) { ed.tmode = false; U.toast("TRANSPOSED " + M.sgn(ed.transpose, 2)); return; }
        const b = blockRect();
        /* the rarer actions in groups (specs/ui/dialogs.md#levels): ROWS, TRANSFORM (INTERPOLATE needs two
         * rows: on one, TRANSPOSE shows in the group's place); the cell's: the EDITOR's own pattern's three */
        const items = [!ed.block && "MARK", !ed.block && "TAKE ITS INSTRUMENT", "COPY", "CUT", "PASTE", "PASTE MIX", "CLEAR", "MOVE", "SWAP",
               { group: "ROWS", items: ["INSERT ROW", "DELETE ROW"] }, { group: "TRANSFORM", items: ["TRANSPOSE", b.r1 > b.r0 && "INTERPOLATE"] },
               !ed.block && "NEW PATTERN", !ed.block && "LOAD A PATTERN", !ed.block && "STORE IN A PATTERN",
               !ed.block && "RECORD PATTERN", !ed.block && M.unsavedThing("pattern", U.pattern) && "REVERT"];
        U.menu("block", ed.block ? "BLOCK " + (b.r1 - b.r0 + 1) + " X " + (b.t1 - b.t0 + 1) : "CELL", items, a => {
            ed.marking = false;
            if (a === "RECORD PATTERN") { recordPattern(); return; }
            /* REVERT (specs/ui/saving.md#revert): the pattern shown as saved, every cell, its tracks, rows and edit
             * step; what plays hears it at once */
            if (a === "REVERT") {
                const n = U.pattern;
                U.askRevert(own() ? "REVERT THE PATTERN TO SAVED?" : "REVERT PATTERN " + M.hex(n) + " TO SAVED?", null, () => {
                    M.revertThing("pattern", n);
                    pat();                                       /* (gone: its own) */
                    clampCursor();
                });
                return;
            }
            /* the EDITOR's own pattern (specs/ui/screens/editor.md#the-editors-own-pattern): NEW PATTERN empties it,
             * LOAD A PATTERN copies one into it, STORE IN A PATTERN copies what the EDITOR shows into a numbered one,
             * the EDITOR following; every way to another asks first while its own holds anything (U.ownAsk) */
            if (a === "NEW PATTERN") { U.ownAsk(() => { U.ownEmpty(); goTo(M.EDITOR_PATTERN); }); return; }
            if (a === "LOAD A PATTERN") {
                U.patternList("LOAD INTO THE EDITOR", U.pattern, n => U.ownAsk(() => {
                    U.edit(() => { const c = structuredClone(M.pattern(n)); c.n = M.EDITOR_PATTERN; delete c.rom; c.builtin = true;
                                   M.state.patterns[M.EDITOR_PATTERN] = c; });
                    goTo(M.EDITOR_PATTERN);
                    U.toast("PATTERN " + M.hex(n) + " LOADED");
                }), B.SEL);
                return;
            }
            if (a === "STORE IN A PATTERN") {
                const from = U.pattern;
                U.patternList("STORE THE PATTERN IN", null, n => {
                    const to = n === "NEW" ? M.freePattern() : n;
                    if (to < 0) { U.toast("NO FREE PATTERN"); return; }
                    const go = () => {
                        U.edit(() => { const c = structuredClone(M.pattern(from)); c.n = to; delete c.rom; delete c.builtin; M.state.patterns[to] = c; });
                        if (from === M.EDITOR_PATTERN) U.ownEmpty();   /* (its notes moved into the numbered one) */
                        goTo(to);
                        U.toast("STORED IN PATTERN " + M.hex(to));
                    };
                    const store = () => (from === M.EDITOR_PATTERN ? go() : U.ownAsk(go));
                    if (!M.patternHolds(to)) { store(); return; }
                    U.ask(["PATTERN " + M.hex(to) + " WILL BE REPLACED", "DO YOU WANT TO CONTINUE?"], ["YES", "NO"], x => { if (x === "YES") store(); });
                }, B.SEL, { plus: true, except: from });
                return;
            }
            if (a === "TAKE ITS INSTRUMENT") { takeInst(B.SEL); return; }
            /* MARK (specs/ui/screens/editor.md): SELECT / ALGORITHM then stretch a block from the cursor, SEL its menu */
            if (a === "MARK") { ed.marking = true; ed.anchor = { row: ed.row, track: ed.track }; ed.block = blockOf(ed.anchor); U.toast("SELECT, ALG: MARK  SEL: ITS MENU"); return; }
            if ((a === "COPY" || a === "CUT") && packedBytes(b) > 2048) { clip = null; U.toast("BLOCK TOO BIG TO COPY"); return; }
            if (a === "COPY") { clip = cut(b, false); U.toast("COPIED " + clip.rows + " X " + clip.tracks); }
            if (a === "CUT") { U.edit(() => { clip = cut(b, true); }); ed.block = null; }
            if (a === "PASTE" || a === "PASTE MIX") {
                if (!clip) { U.toast("NOTHING COPIED"); return; }
                U.edit(() => paste(clip, ed.row, ed.track, a === "PASTE MIX"));
            }
            if (a === "CLEAR") { U.edit(() => { cut(b, true); }); ed.block = null; }
            if (a === "MOVE" || a === "SWAP") {
                const cb = cut(b, false);
                ed.float = Object.assign(cb, { mode: a, src: b, row: b.r0, track: b.t0 });
                ed.row = b.r0; ed.track = b.t0; ed.block = null;
                U.toast(a === "MOVE" ? "MOVE: SEL PUTS IT DOWN" : "SWAP: SEL PUTS IT DOWN");
            }
            if (a === "INSERT ROW" || a === "DELETE ROW") U.edit(() => {
                const p = pat();
                for (let t = b.t0; t <= (ed.block ? b.t1 : p.tracks - 1); t++) {
                    const col = [];
                    for (let r = 0; r < p.rows; r++) col.push(p.cells[r * 16 + t]);
                    if (a === "INSERT ROW") { col.splice(ed.row, 0, null); col.pop(); } else { col.splice(ed.row, 1); col.push(null); }
                    for (let r = 0; r < p.rows; r++) p.cells[r * 16 + t] = col[r];
                }
            });
            if (a === "TRANSPOSE") { ed.tmode = true; ed.transpose = 0; ed.tblock = b; U.toast("K1: TRANSPOSE, SEL: DONE"); }
            if (a === "INTERPOLATE") U.edit(() => {
                const p = pat();
                for (let t = b.t0; t <= b.t1; t++) {
                    const a0 = p.cells[b.r0 * 16 + t], a1 = p.cells[b.r1 * 16 + t];
                    /* a sweep: the first and the last row hold the same command; every row between gets it */
                    const x0 = a0 && a1 && a0.cmds.find(x => a1.cmds.some(y => y.c === x.c && C.entry(x) === C.entry(y)));
                    if (x0) {
                        const x1 = a1.cmds.find(y => y.c === x0.c), n = Math.max(1, b.r1 - b.r0);
                        for (let r = b.r0 + 1; r < b.r1; r++) {
                            const c = p.cells[r * 16 + t] || M.newCell(null, null, null), k = c.cmds.findIndex(y => y.c === x0.c);
                            const x = { c: x0.c, v: Math.round(x0.v + (x1.v - x0.v) * (r - b.r0) / n) };
                            if (k >= 0) c.cmds[k] = x; else if (c.cmds.length < 4) c.cmds.push(x);
                            p.cells[r * 16 + t] = c;
                        }
                        continue;
                    }
                    const v0 = a0 && a0.vol != null ? a0.vol : 0x7F, v1 = a1 && a1.vol != null ? a1.vol : 0x7F;
                    for (let r = b.r0; r <= b.r1; r++) {
                        const c = p.cells[r * 16 + t];
                        if (c && c.note != null && c.note !== M.OFF) c.vol = Math.round(v0 + (v1 - v0) * (r - b.r0) / Math.max(1, b.r1 - b.r0));
                    }
                }
            });
        });
    }
    function dropFloat() {
        const fl = ed.float, s = fl.src;
        U.edit(() => {
            const dest = cut({ r0: fl.row, r1: fl.row + fl.rows - 1, t0: fl.track, t1: fl.track + fl.tracks - 1 }, false);
            if (fl.mode === "MOVE") cut(s, true);
            else paste(dest, s.r0, s.t0, false);
            paste(fl, fl.row, fl.track, false);
        });
        ed.float = null;
    }
    function transpose(d) {
        const b = ed.tblock, p = pat();
        U.edit(() => {
            for (let r = b.r0; r <= b.r1; r++) for (let t = b.t0; t <= b.t1; t++) {
                const c = p.cells[r * 16 + t];
                if (c && c.note != null && c.note !== M.OFF) c.note = U.clamp(c.note + d, 12, 127);
            }
        });
        ed.transpose += d;
        U.showKnob("TRANSPOSE", M.sgn(ed.transpose, 2));
    }

    /* ---------------------------------------------------------------- GLO, ENV --- */
    function patternProps() {
        const p = pat();
        const drops = (tracks, rows) => {
            let k = 0;
            for (let r = 0; r < p.rows; r++) for (let t = 0; t < p.tracks; t++) if (p.cells[r * 16 + t] && (r >= rows || t >= tracks)) k++;
            return k;
        };
        const resize = (tracks, rows) => {
            const k = drops(tracks, rows);
            const go = () => U.edit(() => {
                const cells = new Array(rows * 16).fill(null);
                for (let r = 0; r < Math.min(rows, p.rows); r++) for (let t = 0; t < Math.min(tracks, p.tracks); t++) cells[r * 16 + t] = p.cells[r * 16 + t];
                p.cells = cells; p.rows = rows; p.tracks = tracks;
                clampCursor();
            });
            if (k) U.ask(["THIS DROPS " + k + " CELL" + (k === 1 ? "" : "S"), "MAKE THE PATTERN SMALLER?"], ["YES", "NO"], a => { if (a === "YES") go(); });
            else go();
        };
        /* (on its own pattern: no number, no USED BY) */
        U.props(own() ? "PATTERN PROPERTIES" : "PATTERN " + M.hex(p.n) + " PROPERTIES", [
            { name: "TRACKS", get: () => String(p.tracks), change: (d) => resize(U.clamp(p.tracks + d, 1, 16), p.rows) },
            { name: "ROWS", get: () => String(p.rows), change: (d, c) => resize(p.tracks, U.clamp(p.rows + d * (c ? 16 : 1), 1, M.PAT_ROWS_MAX)) },
            { name: "EDIT STEP", get: () => String(p.step), change: d => U.edit(() => { p.step = U.clamp(p.step + d, 0, 16); }) },
            !own() && { name: "USED BY", get: () => { const n = M.songsUsing(p.n); return n + (n === 1 ? " SONG" : " SONGS") + (U.isFusion(p.n) ? ", FUSION" : ""); }, fixed: true },
        ].filter(Boolean), B.GLO);
    }

    /* ------------------------------------------------------------- the note editor --- */
    /* The cell as a list of fields, a line each: NOTE, INSTR, VOLUME, CMD 1-4 (specs/ui/screens/editor.md).
     * SELECT picks the line and ALGORITHM the part (a command: what it is, then its value); K1 changes the
     * part a step, K2 a big step (an octave, a cartridge, 16, the next group of commands). A SEL tap
     * switches SELECT / ALGORITHM to the rows and tracks, and back (a tap, not a held combination:
     * specs/ui/controls.md). nf: {line 0-6, part 0-1, cells}. */
    const LINES = 7, L_NOTE = 0, L_INST = 1, L_VOL = 2, L_CMD = 3;
    const nfCell = () => cellAt(ed.row, ed.track);
    const nfCmd = () => { const c = nfCell(); return ed.nf.line >= L_CMD && c ? c.cmds[ed.nf.line - L_CMD] || null : null; };
    /* a command's entry and how many digits its value has (0: none; -1: a group's, up to C.max) */
    const entryOf = x => C.ENTRIES[C.entry(x)];
    const partsOf = x => !x ? 1 : entryOf(x).digits === 0 ? 1 : 2;
    /* the commands K1 turns through, K2's groups opened up: each tracker effect, each parameter of the
     * engine, each mixer value, each FX code; g: the group K2 jumps by */
    function kinds(engine) {
        const out = [{ c: null, g: 0 }];
        for (const e of C.ENTRIES) {
            if (e.code == null) continue;
            if (typeof e.code === "string") {
                const g = { PARAM: 5, MIXER: 6, FX: 7 }[e.code];
                for (const c of C.codes(e.code, engine)) out.push({ c, g });
            } else out.push({ c: e.code, sub: e.sub,   /* (LEG in ACC / SLD / A+S's group) */
                              g: e.code < 0x24 ? 1 : e.code < 0x30 ? 2 : e.code < 0xC3 || e.code === 0xD3 ? 3 : 4 });
        }
        return out;
    }
    const sameKind = (k, x) => x ? k.c === x.c && (k.sub == null || k.sub === x.v >> 4) : k.c == null;
    function freshOf(k, engine) {
        const g = C.group(k.c), x = { c: k.c, v: 0 };
        if (k.sub != null) x.v = k.sub << 4;
        else if (C.punch(x) >= 0) x.v = 1;
        else if (g === "PARAM") x.v = Math.min(64, C.max(x, engine));
        else if (g === "MIXER") x.v = 0x7F;
        return x;
    }
    /* the two parts of a command line as text: what it is, and its value */
    function cmdParts(x, engine) {
        if (!x) return ["NO COMMAND", null];
        const g = C.group(x.c), w = C.words(x, engine), e = entryOf(x);
        if (g === "PARAM") { const p = C.param(engine, x.c); const n = p ? p[0] : "PARAMETER " + M.hex(x.c); return [n, w.startsWith(n + " ") ? w.slice(n.length + 1) : M.hex(x.v)]; }
        if (g === "MIXER") { const n = C.codeName("MIXER", engine, x.c) || "MIXER"; return [n, M.hex(x.v)]; }
        if (g === "FX") {
            const f = C.FX[x.c];
            if (!f) return ["FX " + M.hex(x.c), M.hex(x.v)];
            if (f.length === 1) return ["FX " + f[0], C.FX_NAMES[x.v] || M.hex(x.v)];
            const n = f[0] + " " + M.FX[f[0]].knobs[f[1]].n;
            return [n, w.startsWith(n + " ") ? w.slice(n.length + 1) : M.hex(x.v)];
        }
        if (C.punch(x) >= 0) return [e.name, x.v + (x.v === 1 ? " ROW" : " ROWS")];
        if (e.digits === 0) return [e.name, null];
        return [e.name, e.digits === 1 ? M.hex(x.v & 15, 1) : M.hex(x.v)];
    }
    /* what K2 does on the part at the cursor: shown on the hint line */
    function bigName() {
        const l = ed.nf.line;
        if (l === L_NOTE) return "OCTAVE";
        if (l === L_INST) return "CARTRIDGE";
        if (l === L_VOL) return "BY 16";
        return ed.nf.part === 0 ? "NEXT GROUP" : "BY 16";
    }
    function nfOpen() {
        const line = ed.slot === 0 ? L_VOL : L_CMD + ed.slot - 1;
        ed.nf = { line, want: line, part: 0, cells: false };
        nfClamp();
    }
    function nfClose() {                                /* the grid's K1 follows the line left on */
        if (ed.nf.line === L_VOL) ed.slot = 0;
        else if (ed.nf.line >= L_CMD) ed.slot = ed.nf.line - L_CMD + 1;
        ed.noteEd = false;
        clampSlot();
    }
    function nfClamp() {
        const c = nfCell(), n = c ? c.cmds.length : 0;
        ed.nf.line = U.clamp(ed.nf.line, 0, Math.min(LINES - 1, L_CMD + n));      /* down to the first empty command */
        if (ed.nf.part >= partsOf(nfCmd()) || ed.nf.line < L_CMD) ed.nf.part = 0;
    }
    /* K1 (big false) or K2 (big true) on the part at the cursor */
    function nfKnob(s, big) {
        const r = ed.row, t = ed.track, old = nfCell(), engine = engineAt(r, t), d = s;    /* s: clicks, signed */
        const c = old ? M.copyCell(old) : M.newCell(null, null, null);
        const l = ed.nf.line;
        if (l === L_NOTE) {                             /* none, a note off, then C-0 ... G-9 */
            const idx = c.note == null ? 0 : c.note === M.OFF ? 1 : c.note - 10;
            const ni = big && idx >= 2 ? U.clamp(idx + 12 * d, 2, 117) : U.clamp(idx + d, 0, 117);
            c.note = ni === 0 ? null : ni === 1 ? M.OFF : ni + 10;
            if (c.note != null && c.note !== M.OFF && c.inst == null && M.trackInst(pat(), r, t) == null) c.inst = U.inst;
            U.showKnob(big ? "K2" : "K1", "NOTE " + M.noteName(c.note));
        } else if (l === L_INST) {                      /* as the track, then each instrument, cartridge by cartridge */
            const refs = [null];
            for (const ca of M.state.carts) ca.slots.forEach((sl, i) => { if (sl) refs.push(ca.id << 8 | i); });
            let i = Math.max(0, refs.indexOf(c.inst));
            if (big) {
                const cart = c.inst == null ? null : c.inst >> 8;
                const carts = M.state.carts.filter(ca => ca.slots.some(Boolean)).map(ca => ca.id);
                const ci = U.clamp((cart == null ? -1 : carts.indexOf(cart)) + d, -1, carts.length - 1);
                i = ci < 0 ? 0 : refs.findIndex(x => x != null && x >> 8 === carts[ci]);
            } else i = U.clamp(i + d, 0, refs.length - 1);
            c.inst = refs[i];
            U.showKnob(big ? "K2" : "K1", c.inst == null ? "AS THE TRACK" : M.refText(c.inst) + " " + U.instName(c.inst));
        } else if (l === L_VOL) {                       /* past 7F: the instrument's own */
            const v = c.vol == null ? 0x80 : c.vol, nv = v + d * (big ? 16 : 1);
            c.vol = nv > 0x7F ? null : U.clamp(nv, 0, 0x7F);
            U.showKnob("VOLUME", c.vol == null ? "AS THE INSTRUMENT" : M.hex(c.vol));
        } else {
            const k = l - L_CMD, x = c.cmds[k] || null;
            if (ed.nf.part === 0) {                     /* what it is: K1 one, K2 the next group's first */
                const others = c.cmds.filter((_, i) => i !== k).map(o => o.c);
                const ks = kinds(engine).filter(q => q.c == null || sameKind(q, x) || !others.includes(q.c));
                let i = Math.max(0, ks.findIndex(q => sameKind(q, x)));
                if (big) {                              /* on: the next group's first; back: this group's first, else the one before's */
                    const first = j => { while (j > 0 && ks[j - 1].g === ks[j].g) j--; return j; };
                    for (let n = 0; n < Math.abs(d); n++) {
                        if (d > 0) { const j = ks.findIndex((q, m) => m > i && q.g !== ks[i].g); if (j >= 0) i = j; }
                        else { const f = first(i); i = f < i ? f : f > 0 ? first(f - 1) : 0; }
                    }
                } else i = U.clamp(i + d, 0, ks.length - 1);
                const q = ks[i];
                if (q.c == null) { if (x) c.cmds.splice(k, 1); }
                else if (!sameKind(q, x)) {
                    const nx = freshOf(q, engine);
                    if (x) c.cmds[k] = nx; else { c.cmds.push(nx); ed.nf.line = ed.nf.want = L_CMD + c.cmds.length - 1; }
                }
                const now = c.cmds[ed.nf.line - L_CMD];
                U.showKnob(big ? "K2" : "K1", now ? C.text(now) + " " + entryOf(now).name : "NO COMMAND");
            } else if (x) {                             /* its value */
                const e = entryOf(x), step = big ? 16 : 1;
                if (e.digits === 1) x.v = (x.v & 0xF0) | U.clamp((x.v & 15) + d * step, 0, 15);
                else if (e.digits === 2) x.v = U.clamp(x.v + d * step, e.min || 0, 255);
                else x.v = U.clamp(x.v + d * step, 0, C.max(x, engine));
                U.showKnob(C.text(x), C.words(x, engine));
            }
        }
        put(r, t, M.cellEmpty(c) ? null : c);
        nfClamp();
    }
    /* want: the line SELECT picked; a cell with fewer commands shows the nearest, and the next cell
     * that has it goes back to it */
    function nfMove(e, s) {
        const p = pat();
        if (ed.nf.cells) {                              /* SEL tapped: the rows and tracks */
            if (e === E.SELECT) ed.row = U.clamp(ed.row + s, 0, p.rows - 1);
            else ed.track = U.clamp(ed.track + Math.sign(s), 0, p.tracks - 1);
            scrollX();
            ed.nf.line = ed.nf.want;
            nfClamp();
        } else if (e === E.SELECT) { ed.nf.line += s; nfClamp(); ed.nf.want = ed.nf.line; }
        else { ed.nf.part = U.clamp(ed.nf.part + Math.sign(s), 0, partsOf(nfCmd()) - 1); nfClamp(); }
    }
    function drawNoteEditor() {
        const c = nfCell() || M.newCell(null, null, null), engine = engineAt(ed.row, ed.track), nf = ed.nf;
        const ref = c.inst != null ? c.inst : M.trackInst(pat(), ed.row, ed.track);
        const col = ref != null && M.inst(ref) ? M.instCol(ref) : D.LGREY;
        D.rect(0, Y0, 240, U.BODY_H, D.BLACK);
        D.text(4, Y0 + 1, F.T, own() ? "ROW " + M.hex(ed.row) + "  TRACK " + M.hex(ed.track)       /* (its own: no number) */
                               : "ROW " + M.hex(ed.row) + (U.isFusion(U.pattern) && ed.row >= 12 && ed.row < 128 ? " " + M.noteName(ed.row) : "") +
                                 "  TRACK " + M.hex(ed.track) + "  PATTERN " + M.hex(U.pattern), nf.cells ? D.WHITE : D.CYAN);
        const lines = [
            ["NOTE", [M.noteName(c.note), null], c.note == null ? D.DGREY : c.note === M.OFF ? D.LGREY : col],
            ["INSTR", [c.inst != null ? M.refText(c.inst) + " " + U.instName(c.inst) : "AS THE TRACK", null], c.inst != null ? col : D.DGREY],
            ["VOLUME", [c.vol != null ? M.hex(c.vol) : "AS THE INSTRUMENT", c.vol != null ? Math.round(c.vol / 127 * 100) + " %" : null], c.vol != null ? D.BROWN : D.DGREY],
        ];
        for (let i = 0; i < 4; i++) {
            const x = c.cmds[i];
            lines.push(["CMD " + (i + 1), x ? cmdParts(x, engine) : [i === c.cmds.length ? "+ COMMAND" : "", null], x ? D.LBLUE : D.DGREY]);
        }
        lines.forEach(([lab, [a, b], k], i) => {
            const y = Y0 + 16 + i * 24, on = nf.line === i;
            if (on) { D.hline(0, y, 240, D.DGREY); D.hline(0, y + 23, 240, D.DGREY); }
            D.text(4, y + 6, F.T, lab, on ? D.WHITE : D.CYAN);
            D.text(52, y + 4, F.S, a.slice(0, 23), on && nf.part === 0 ? D.WHITE : k);
            if (b != null) D.text(236 - D.tw(F.S, b), y + 4, F.S, b, on && nf.part === 1 ? D.WHITE : k);
            /* the part at the cursor, white; an empty command line's: its dots */
            if (on && !a) D.text(52, y + 4, F.S, "...", D.WHITE);
        });
        const hint = nf.cells ? [["SELECT", "ROWS"], ["ALG", "TRACKS"], ["SEL", "FIELDS"]]
                              : [["K1", "STEP"], ["K2", bigName()], ["SEL", "CELLS"]];
        D.runs(0, Y0 + 16 + LINES * 24 + 2, F.T, hint.flatMap(([a, w], i) => [[a, D.YELLOW], [" " + w + (i < hint.length - 1 ? "  " : ""), D.LGREY]]));
    }
    /* the note editor's footer: the field at the cursor in words */
    function nfFooter() {
        const c = nfCell(), l = ed.nf.line, engine = engineAt(ed.row, ed.track);
        if (l === L_NOTE) return ["NOTE " + M.noteName(c ? c.note : null), D.LGREY];
        if (l === L_INST) return [c && c.inst != null ? M.refText(c.inst) + " " + U.instName(c.inst) : "AS THE TRACK", D.LGREY];
        if (l === L_VOL) return ["VOLUME " + (c && c.vol != null ? M.hex(c.vol) : ".."), D.LGREY];
        const x = nfCmd();
        return x ? [C.text(x) + " " + C.words(x, engine), D.LGREY] : ["CMD " + (l - L_CMD + 1) + " EMPTY", D.DGREY];
    }

    const scr = {
        name: "EDITOR", group: "MAKE",
        does: [B.EDIT, B.SEL, B.ENV, B.GLO, B.SEQ, B.ARP, B.LFO, B.REC],
        combines: [B.ARP],
        /* ENV's hold is the EDITOR's own (ui.js asks with no h); past the hold delay (h): the pipette, a
         * shortcut, its tap staying the pattern list */
        hold(b, h) {
            if (b !== B.ENV) return false;
            if (h && !U.top()) { h.used = true; takeInst(B.ENV); }
            return true;
        },
        sub: () => own() ? null : M.hex(U.pattern),         /* the header's subpage: the pattern (`EDITOR` alone on its own) */
        seq: () => ed.view === 1 ? "1 TRACK" : ed.view + " TRACKS",   /* SEQ: 8, 4, 2, 1 tracks */
        enter(from, o) {
            /* a FUSION's pattern opened from its edit page: the keys play that FUSION (REC off) */
            ed.fusion = o && o.fusion != null ? o.fusion : null;
            if (o && o.row != null) ed.row = o.row;
            if (o && o.track != null) ed.track = o.track;
            pat();
            clampCursor();
        },
        leave() {
            stopRecording();
            if (ed.arp) { ed.arp = false; P.preview(false); }
            ed.noteEd = false; ed.sess = null; ed.marking = false; ed.armed = false;
        },
        reverted() { clampCursor(); },
        page() {
            const p = pat(), rows = V().rows, n = Math.ceil(p.rows / rows);
            const top = recording() && playRow() != null ? playRow() : ed.row;
            return n > 1 ? M.pageOf(Math.floor(top / rows), n) : null;
        },
        lit(b) {
            if (b === B.REC) return ed.rec ? (recording() || P.countingIn() || ed.armed ? (U.blink(200) ? 1 : 0) : 1) : .25;
            if (b === B.EDIT && ed.noteEd) return 1;
            if (b === B.ARP && ed.arp) return 1;
            if (b === B.SEL && ed.noteEd) return ed.nf.cells ? 1 : .25;         /* lit: SELECT, ALG move cells */
            if (b === B.SEL && (ed.block || ed.float || ed.tmode)) return 1;
        },
        knob(e, s, coarse) {
            if (ed.noteEd) {                                 /* K3, K4: nothing here */
                if (e === E.SELECT || e === E.ALGORITHM) nfMove(e, s);
                else if (e === E.K1 || e === E.K2) nfKnob(s, e === E.K2);
                return;
            }
            if (ed.tmode && e === E.K1) return transpose(s * (coarse ? 12 : 1));
            if (ed.marking && (e === E.SELECT || e === E.ALGORITHM)) return scr.mark(e, s);     /* after SEL, MARK */
            if (e === E.SELECT || e === E.ALGORITHM) {
                ed.block = null; ed.anchor = null;
                const p = pat();
                if (e === E.SELECT) ed.row = U.clamp(ed.row + s * (coarse ? 16 : 1), 0, p.rows - 1);
                else ed.track = U.clamp(ed.track + Math.sign(s), 0, p.tracks - 1);
                scrollX();
                if (ed.float) { ed.float.row = ed.row; ed.float.track = ed.track; }
                return;
            }
            cellKnob(e, s, coarse);
        },
        mark(e, s) {
            if (ed.noteEd) return;                           /* no blocks in the note editor */
            if (!ed.anchor) ed.anchor = { row: ed.row, track: ed.track };
            const p = pat();
            if (e === E.SELECT) ed.row = U.clamp(ed.row + s, 0, p.rows - 1);
            else ed.track = U.clamp(ed.track + Math.sign(s), 0, p.tracks - 1);
            scrollX();
            ed.block = blockOf(ed.anchor);
        },
        tap(b) {
            if (b === B.SEQ) {                               /* 8, 4, 2, 1 tracks, round */
                ed.view = ed.view === 8 ? 4 : ed.view === 4 ? 2 : ed.view === 2 ? 1 : 8;
                clampCursor();
                U.toast(ed.view === 1 ? "1 TRACK" : ed.view + " TRACKS");
                return true;
            }
            if (b === B.EDIT) {
                if (ed.noteEd) { nfClose(); ed.sess = null; return true; }                 /* keeping the changes */
                /* the note editor's session (specs/ui/controls.md): HOME puts back the pattern's cells
                 * as they were */
                const n = U.pattern;
                ed.noteEd = true;
                nfOpen();
                ed.sess = U.session();
                ed.sess.add("cells", () => M.pattern(n) && M.pattern(n).cells, v => { if (M.pattern(n)) M.pattern(n).cells = v; });
                return true;
            }
            if (b === B.GLO) { patternProps(); return true; }
            if (b === B.ENV) {
                U.patternList("PATTERNS", U.pattern, n => U.ownAsk(() => goTo(n)));     /* (its own pattern's warning first) */
                return true;
            }
            if (b === B.SEL && ed.noteEd) {                  /* SELECT, ALG: the fields, or the rows and tracks */
                ed.nf.cells = !ed.nf.cells;
                U.toast(ed.nf.cells ? "SELECT, ALG: CELLS" : "SELECT, ALG: FIELDS");
                return true;
            }
            if (b === B.SEL) { blockMenu(); return true; }
            if (b === B.LFO) {
                const c = cellAt(ed.row, ed.track);
                if (c) put(ed.row, ed.track, null);
                else put(ed.row, ed.track, M.newCell(M.OFF, null, null));
                if (!ed.noteEd) advance();
                else nfClamp();
                clampSlot();
                return true;
            }
            if (b === B.ARP) {
                ed.arp = !ed.arp;
                if (ed.arp) {                           /* whatever plays stops first: the row is heard alone */
                    if (P.take || P.countingIn()) stopRecording();
                    else if (P.playing) P.pause();
                }
                P.preview(ed.arp, () => ({ pat: U.pattern, row: ed.row }), U.set("ARP RHYTHM"));
                U.toast(ed.arp ? "ROW PREVIEW " + U.set("ARP RHYTHM") : "ROW PREVIEW OFF");
                return true;
            }
            return false;
        },
        /* SEL held's layer: marking and big steps in the grid; the note editor has none */
        selLines: () => ed.noteEd ? [] : ["SELECT, ALGORITHM: MARK A BLOCK", ed.tmode ? "K1: AN OCTAVE" : "K4: BY 16"],
        ownArp: "preview",                                   /* ARP: the row preview, not the arpeggiator (ARP held + SELECT: its rhythm) */
        arpLayer: () => ["SELECT: THE RHYTHM, # A ROW, _ A REST"],
        rec() {
            if (ed.armed) { ed.armed = false; U.toast("TAKE CANCELLED"); return; }
            if (P.take || P.countingIn()) { stopRecording(); return; }
            ed.rec = !ed.rec;
            if (ed.rec) {                               /* (the EDITOR's loop plays on: the keys write step by step) */
                if (P.playing && P.what === "song") { P.pause(); U.toast("SONG PAUSED: WRITING"); }
                else U.toast("REC ON: KEYS WRITE");
            } else U.toast("REC OFF: KEYS PLAY");
        },
        playDown() {
            if (!ed.rec) {
                return false;
            }
            if (ed.armed) { ed.armed = false; U.toast("TAKE CANCELLED"); return "done"; }
            if (P.take || P.countingIn()) { stopRecording(); return "done"; }
            if (P.playing && P.what === "loop") { P.pause(); U.toast("LOOP STOPPED: PLAY FOR A TAKE"); return "done"; }
            if (P.playing) P.pause();
            return true;
        },
        playHold() {
            playHoldO = U.open({ kind: "layer", btn: B.PLAY, knob() {}, tap() { return true; },
                draw() {
                    const y = Y0 + 150;
                    U.panel(4, y, 232, 40, null);
                    D.runs(10, y + 6, F.T, [["LET GO", D.YELLOW], [": START THE TAKE", D.LGREY]]);
                    D.runs(10, y + 22, F.T, [["PRESS A KEY", D.YELLOW], [": START ON THAT NOTE", D.LGREY]]);
                } });
        },
        playUp(used) {
            if (playHoldO) { U.close(playHoldO); playHoldO = null; }
            if (!used) startTake(false);
        },
        key(n, down) {
            if (ed.noteEd) {
                if (down) {                     /* a key: the note, with the current instrument; the rest stays */
                    const m = U.midi(n), c = cellAt(ed.row, ed.track);
                    if (m >= 12 && m <= 127) put(ed.row, ed.track, Object.assign(c ? M.copyCell(c) : M.newCell(null, null, null), { note: m, inst: U.inst }));
                    U.keyOn(n);
                } else U.keyOff(n);
                return true;
            }
            if (!ed.rec) {
                if (ed.fusion == null || !M.inst(ed.fusion)) return false;
                if (down) U.keyOn(n, ed.fusion); else U.keyOff(n);      /* the FUSION as it is being written */
                return true;
            }
            if (down) keyDown(n); else keyUp(n);
            return true;
        },
        armed: () => ed.armed,                               /* COUNT-IN KEY: the box says so (ui.js) */
        back() {
            if (ed.armed) { ed.armed = false; U.toast("TAKE CANCELLED"); return true; }
            if (ed.float) { ed.float = null; U.toast("CANCELLED"); return true; }
            if (ed.tmode) { ed.tmode = false; return true; }
            if (ed.noteEd) { ed.sess.drop(); nfClose(); ed.sess = null; return true; }    /* HOME: dropping them */
            if (ed.block) { ed.block = null; ed.anchor = null; ed.marking = false; return true; }
            return false;
        },
        /* on the right, what K1's slot of the cursor's cell holds, in words, as the knobs show it */
        footer() {
            if (ed.noteEd) return nfFooter();
            const c = cellAt(ed.row, ed.track);
            if (ed.slot === 0) return ["VOLUME " + (c && c.vol != null ? M.hex(c.vol) : ".."), c && c.vol != null ? D.LGREY : D.DGREY];
            const x = c && c.cmds[ed.slot - 1];
            return x ? [C.text(x) + " " + C.words(x, engineAt(ed.row, ed.track)), D.LGREY] : ["CMD " + ed.slot + " EMPTY", D.DGREY];
        },
        draw() {
            if (ed.noteEd) return drawNoteEditor();
            /* recording: the cursor on the row a key pressed now lands on */
            if (recording()) { const at = P.nearestRow(); if (at && at.pat === U.pattern) ed.row = Math.min(at.row, pat().rows - 1); }
            const p = pat(), rows = V().rows, pr = playRow();
            const follow = recording() && pr != null;
            const top = Math.floor((follow ? pr : ed.row) / rows) * rows;
            const hot = ed.fusion != null ? U.keysHeld().map(n => U.midi(n)) : null;    /* the FUSION's rows the keys play */
            U.grid(p, { view: ed.view, top, left: ed.left, cur: { row: ed.row, track: ed.track }, play: pr,
                        block: ed.block, float: ed.float, slot: ed.slot, hot });
        },
    };
    U.register(scr);
})();
