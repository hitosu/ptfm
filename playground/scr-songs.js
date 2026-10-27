/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: SONGS (specs/ui/screens/songs.md): the list of songs and, below, the
 * current song's order of patterns, 8 a line. EDIT on a song moves the cursor into its order. */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, B = U.B, E = U.E, P = PTFM.player;
    const Y0 = U.BODY_Y, LIST = 7;
    let order = false, pos = 0, clip = null, picked = false;

    const song = () => M.state.songs[U.song];
    /* on the list, the position PLAY starts from, boxed in the order (specs/ui/screens/songs.md): the one ALGORITHM
     * picked (picked), else where PLAY would go on: the playing position while the song plays, where it was paused,
     * else its first. Playing, ALGORITHM moves only the box */
    function startPos() {
        const s = song();
        if (!s || !s.order.length) return 0;
        if (picked) return U.clamp(pos, 0, s.order.length - 1);
        const h = P.at(), k = P.kept.lastSong === U.song && P.kept.song[U.song];
        if (P.playing && P.playingSong() === U.song && h) return h.pos;
        return k ? U.clamp(k.pos, 0, s.order.length - 1) : 0;
    }
    function rename(s) {
        U.textEntry("SONG NAME", s.name, 16, B.EDIT, name => { if (name != null) U.edit(() => { s.name = name || "NO NAME"; }); });
    }
    /* the list becomes `after` (in place: one edit); the player's song indexes follow */
    function relist(before, after) {
        const all = M.state.songs;
        all.splice(0, all.length, ...after);
        P.songsRemap(i => { const k = after.indexOf(before[i]); return k < 0 ? null : k; });
    }
    let propsO = null, propsSong = -1;            /* the properties overlay open: its song's timing is being edited */
    function props() {
        const s = song();
        propsSong = U.song;
        propsO = U.props("SONG " + M.hex(U.song) + " PROPERTIES", [
            { name: "NAME", get: () => s.name, edit: () => rename(s) },          /* EDIT types it, as everywhere */
            { name: "TEMPO", get: () => M.hex(s.tempo) + " (" + M.bpm(s) + " BPM)",
              change: (d, c) => U.edit(() => { s.tempo = U.clamp(s.tempo + d * (c ? 10 : 1), M.TEMPO_MIN, M.TEMPO_MAX); }) },
            { name: "TICKS A ROW", get: () => String(s.ticks), change: d => U.edit(() => { s.ticks = U.clamp(s.ticks + d, 1, 31); }) },
            { name: "ROWS A BEAT", get: () => String(s.rpb), change: d => U.edit(() => { s.rpb = U.clamp(s.rpb + d, 1, 16); }) },
            { name: "SWING", get: () => s.swing + " %", change: (d, c) => U.edit(() => { s.swing = U.clamp(s.swing + d * (c ? 5 : 1), 50, 75); }) },
            { name: "AT THE END", get: () => s.atEnd, change: () => U.edit(() => { s.atEnd = s.atEnd === "LOOP" ? "STOP" : "LOOP"; }) },
            { name: "LOOP FROM", get: () => "POSITION " + M.hex(s.loopFrom),
              change: d => U.edit(() => { s.loopFrom = U.clamp(s.loopFrom + d, 0, Math.max(0, s.order.length - 1)); }) },
        ], B.GLO);
    }
    /* SEL, MOVE: the song floats; SELECT shows it at its new place, SEL puts it down, HOME takes it back. Nothing
     * refers to songs, so no question */
    let mv = null;
    const placed = () => {                               /* the list with the song afloat put at mv.at */
        const before = M.state.songs, s = before[mv.from];
        const others = before.filter(x => x !== s);
        others.splice(mv.at, 0, s);
        return others;
    };
    const listed = () => mv ? placed() : M.state.songs.slice();
    function putDown() {
        const { from, at } = mv, before = M.state.songs.slice(), after = placed();
        mv = null;
        if (at === from) return;
        U.edit(() => { relist(before, after); U.song = at; });
        U.toast("SONG MOVED TO " + M.hex(at));
    }
    /* REVERT (specs/ui/saving.md#revert): the song as saved, its record whole (its order, name, properties, mixer and
     * FX setup; not its patterns: each is the EDITOR's to revert); what plays hears it at once */
    function revertSong() {
        const i = U.song;
        U.askRevert("REVERT SONG " + M.hex(i) + " TO SAVED?", null, () => {
            M.revertThing("song", i);
            U.song = U.clamp(U.song, 0, Math.max(0, M.state.songs.length - 1));
            if (order) { if (song()) keepOrder(); else { order = false; sess = null; } }   /* (the order's copy now the saved one) */
            pos = U.clamp(pos, 0, Math.max(0, ((song() || {}).order || []).length - 1));
        });
    }
    /* (no song at all: the same menu, only NEW does something) */
    function listMenu() {
        const s = song(), i = U.song, label = "SONG " + M.hex(i);
        const items = ["NEW", "CLONE", "MOVE", "DELETE", "RENAME"];
        if (P.held()) items.push("UNLOAD SONG");         /* (a song held: the keys back on the keys' song) */
        if (s && M.unsavedThing("song", i)) items.push("REVERT");
        U.menu("songs", label, items, a => {
            if (a === "UNLOAD SONG") { P.unload(); return; }
            if (a === "REVERT") { revertSong(); return; }
            const all = M.state.songs, before = all.slice();
            /* (song FE is the keys': the user's end at FD) */
            if ((a === "NEW" && all.length > M.SONG_LAST) || (a === "CLONE" && s && all.length - 1 >= M.SONG_LAST)) { U.toast("NO FREE SONG NUMBER"); return; }
            /* NEW: its first pattern picked first (NEW SONG STARTS WITH), or a new empty one, 8 x 64 at the first free
             * number; the song at the end of the list: nothing shifts */
            if (a === "NEW") U.patternList("NEW SONG STARTS WITH", U.pattern, n => {
                const free = n === "NEW" ? M.freePattern() : n;
                if (free < 0) { U.toast("NO FREE PATTERN"); return; }
                U.edit(() => {
                    if (n === "NEW") M.state.patterns[free] = M.newPattern(free, 8, 64);
                    all.push(Object.assign(M.newSong(), { order: [free] }));
                    U.song = all.length - 1;
                });
            }, B.SEL, { plus: true });
            if (!s) return;
            if (a === "CLONE") U.edit(() => {            /* after the song, a copy */
                const c = structuredClone(s); c.name = (s.name.slice(0, 12) + " 2").trim();
                const after = before.slice();
                after.splice(i + 1, 0, c);
                relist(before, after); U.song = i + 1;
            });
            if (a === "DELETE") {                            /* (ITS PATTERNS TOO, ticked at first: the song's own) */
                const own = M.songOwn(s), k = own.items.length;
                const rows = [{ name: "ITS PATTERNS TOO", count: k, on: k > 0, note: k ? k + "  " + M.kb(own.bytes) : "NONE" }];
                U.checklist("DELETE SONG " + M.hex(i) + "?", rows, ["DELETE", "KEEP"], (x, rs) => {
                    if (x !== "DELETE") return;
                    const np = rs[0].on ? k : 0;
                    const ok = U.edit(() => {
                        const was = M.state.songs.slice(), after = was.filter(y => y !== s);
                        relist(was, after); U.song = U.clamp(i, 0, after.length - 1);
                        if (np) for (const p of own.items) M.state.patterns[p.n] = null;
                    });
                    if (ok && np) U.toast("SONG " + M.hex(i) + " DELETED, " + (np === 1 ? "1 PATTERN TOO" : np + " PATTERNS TOO"));
                }, null, { sub: s.name, foot: own.shared ? own.shared + " SHARED WITH OTHER SONGS STAY" : "", bare: true });
            }
            if (a === "MOVE") { mv = { from: i, at: i }; U.toast("SELECT MOVES IT, SEL PUTS IT DOWN"); }
            if (a === "RENAME") rename(s);
        });
    }
    /* the order's edit session (specs/ui/controls.md): HOME puts the order back as it was at EDIT, and
     * drops a pattern cloned meanwhile (looked up each time: REVERT replaces the model's objects) */
    let sess = null;
    function keepOrder() {
        const i = U.song;
        sess = U.session();
        sess.add("order", () => M.state.songs[i] && M.state.songs[i].order, v => { if (M.state.songs[i]) M.state.songs[i].order = v; });
    }
    function editPattern() {
        const s = song();
        if (!s.order.length) return;
        const n = s.order[pos];
        U.ownAsk(() => { U.pattern = n; U.goDeep("EDITOR", { row: 0 }); });   /* (the EDITOR's own pattern's warning first) */
    }
    function orderMenu() {
        const s = song();
        /* only what can act (specs/ui/dialogs.md): an empty order has no position to delete, copy or clone */
        const has = s.order.length > 0;
        U.menu("order", "POSITION " + M.hex(pos), [has && "EDIT PATTERN", "NEW PATTERN", "INSERT", has && "DELETE", has && "COPY",
                                                   clip != null && "PASTE", has && "CLONE PATTERN",
                                                   M.unsavedThing("song", U.song) && "REVERT"], a => {
            if (a === "EDIT PATTERN") editPattern();
            if (a === "NEW PATTERN") {                   /* an empty one, the size of the one here, after it */
                const free = M.freePattern(), here = M.pattern(s.order[pos]);
                if (free < 0) { U.toast("NO FREE PATTERN"); return; }
                sess.add("pattern" + free, () => M.state.patterns[free], v => { M.state.patterns[free] = v; });
                if (!U.edit(() => {                           /* (not made, the edit memory full: no toast) */
                    M.state.patterns[free] = M.newPattern(free, here ? here.tracks : 8, here ? here.rows : 64);
                    if (s.order.length) pos++;
                    s.order.splice(pos, 0, free);
                })) return;
                U.toast("PATTERN " + M.hex(free) + " MADE");
            }
            if (a === "INSERT") U.edit(() => { s.order.splice(pos, 0, s.order[pos] != null ? s.order[pos] : 0); });
            if (a === "DELETE") U.edit(() => { s.order.splice(pos, 1); pos = U.clamp(pos, 0, Math.max(0, s.order.length - 1)); });
            if (a === "COPY") { clip = s.order[pos]; U.toast("PATTERN " + M.hex(clip) + " COPIED"); }
            if (a === "PASTE" && clip != null) U.edit(() => { s.order.splice(has ? pos + 1 : 0, 0, clip); if (has) pos++; });
            if (a === "CLONE PATTERN") {
                const free = M.freePattern(), n = s.order[pos], p = M.pattern(n);
                if (free < 0 || !p) { U.toast("NO FREE PATTERN"); return; }
                sess.add("pattern" + free, () => M.state.patterns[free], v => { M.state.patterns[free] = v; });
                if (!U.edit(() => { const c = structuredClone(p); c.n = free; M.state.patterns[free] = c; s.order[pos] = free; })) return;
                U.toast(M.hex(n) + " CLONED TO " + M.hex(free));
            }
            if (a === "REVERT") revertSong();
        });
    }

    const scr = {
        name: "SONGS", group: "MAKE",
        does: [B.EDIT, B.SEL, B.ENV, B.GLO, B.LFO, B.SEQ],
        combines: [],
        editing: (what, key) => what === "time" && key === propsSong && U.overlays.includes(propsO),
        page() {                                             /* the song at the cursor of how many: `3/18` */
            const n = M.state.songs.length;
            return n ? M.pageOf(mv ? mv.at : U.song, n) : null;
        },
        sub: () => [null, "REPEAT", "NEXT"][P.playMode],    /* the play mode, but ONE PLAY */
        lit(b) { if (b === B.EDIT && order) return 1; if (b === B.SEL && mv) return 1; if (b === B.SEQ) return P.playMode ? 1 : .25; },
        /* PLAY NEXT took the next song on: the cursor follows it, on the list (not in an order being edited, nor with
         * a song afloat) */
        chained(n) { if (!order && !mv) { U.song = n; pos = 0; picked = false; } },
        boxAt: () => startPos(),                             /* (where PLAY starts: for the checks) */
        /* PLAY on the list: the position ALGORITHM picked, from its first row (then the box follows what plays);
         * null: as ever (the same song goes on from its place) */
        playFrom() {
            if (order || !picked) return null;
            const p = startPos();                            /* (the pick, within the order) */
            picked = false;
            return p;
        },
        selLines: () => order ? ["K1: 8 PATTERNS AT A TIME"] : [],     /* SEL held's layer */
        knob(e, s, coarse) {
            const sg = song();
            if (!order) {
                const n = M.state.songs.length;
                if (e === E.SELECT && mv) mv.at = U.clamp(mv.at + Math.sign(s), 0, n - 1);   /* a song afloat */
                else if (e === E.SELECT) {
                    const was = U.song;
                    U.song = U.clamp(U.song + s, 0, n - 1);
                    if (U.song !== was) { pos = 0; picked = false; }   /* (another song: its own place, else its first) */
                } else if (e === E.ALGORITHM && sg && sg.order.length) {  /* the position PLAY starts from; playing, only the box */
                    pos = U.clamp(startPos() + s, 0, sg.order.length - 1);
                    picked = true;
                    U.showKnob("PLAY FROM", "POS " + M.hex(pos));
                }
                return;
            }
            if (e === E.SELECT) pos = U.clamp(pos + s * 8, 0, Math.max(0, sg.order.length - 1));
            else if (e === E.ALGORITHM) pos = U.clamp(pos + s, 0, Math.max(0, sg.order.length - 1));
            else if (e === E.K1 && sg.order.length) {
                const ns = M.patternNums().filter(n => n < 0xF0);   /* (the user's alone, 00-EF: the built-in
                                                             * patterns and the EDITOR's own are no song's) */
                if (!ns.length) return;
                let i = Math.max(0, ns.indexOf(sg.order[pos]));     /* (one not among them: from the first, as the
                                                                     * firmware's) */
                i = U.clamp(i + s * (coarse ? 8 : 1), 0, ns.length - 1);
                U.edit(() => { sg.order[pos] = ns[i]; });
                U.showKnob("K1 PATTERN", M.hex(ns[i]));
            }
        },
        tap(b) {
            const sg = song();
            if (mv) { if (b === B.SEL) putDown(); return true; }            /* a song afloat: only SEL (and HOME) */
            if (b === B.EDIT) {
                if (order) { order = false; sess = null; return true; }            /* keeping the changes */
                order = true; pos = startPos();                                    /* (on the box) */
                keepOrder();
                return true;
            }
            if (b === B.GLO) { props(); return true; }
            if (b === B.SEQ) {                                                   /* the play mode, round; at once on the songs playing */
                P.playMode = (P.playMode + 1) % 3;
                U.toast(["ONE PLAY", "REPEAT SONG", "PLAY NEXT"][P.playMode]);
                if (P.what === "song") P.holdMode();
                return true;
            }
            if (b === B.SEL) { order ? orderMenu() : listMenu(); return true; }
            if (b === B.ENV) {
                U.patternList("PATTERNS", order ? sg.order[pos] : sg.order[0], n => {
                    if (order) U.edit(() => { if (sg.order.length) sg.order[pos] = n; else sg.order.push(n); });
                    else U.ownAsk(() => { U.pattern = n; U.toast("PATTERN " + M.hex(n) + " FOR THE EDITOR"); });  /* (its warning first) */
                });
                return true;
            }
            if (b === B.LFO && order && sg.order.length) { U.edit(() => { sg.order.splice(pos, 1); pos = U.clamp(pos, 0, Math.max(0, sg.order.length - 1)); }); return true; }
            return false;
        },
        back() {
            if (mv) { mv = null; U.toast("NOT MOVED"); return true; }
            if (order) { sess.drop(); order = false; sess = null; return true; }     /* HOME: dropping them */
            return false;
        },
        footer() {
            const s = song();
            return s ? [M.bpm(s) + " BPM " + U.fmtTime(U.songSeconds(s)), D.LGREY] : null;
        },
        draw() {
            /* a song afloat (MOVE) is drawn at the place it would go, boxed yellow. The list scrolls a song at
             * a time, the cursor kept in the middle, as the song list's (specs/ui/screens/songs.md) */
            const songs = listed(), cur = mv ? mv.at : U.song, top = Math.max(0, Math.min(cur - (LIST >> 1), songs.length - LIST));
            const playingSong = M.state.songs[P.playingSong()];
            for (const [x, w] of [[8, "NR"], [26, "NAME"], [132, "BPM"], [156, "LEN"], [186, "TIME"]])
                D.text(x, Y0 + 2, F.T, w, D.CYAN);           /* (the column names, each over its column; LEN: the positions) */
            for (let i = top; i < Math.min(songs.length, top + LIST); i++) {
                const s = songs[i], y = Y0 + 16 + (i - top) * 13, on = i === cur, isPlaying = s === playingSong;
                if (on && !order) D.box(0, y - 1, 240, 13, mv ? D.YELLOW : D.WHITE);
                if (isPlaying && P.playing) D.play(1, y + 1, D.GREEN);
                const col = on ? D.WHITE : D.LGREY;
                D.text(8, y, F.T, M.hex(i), on ? D.WHITE : D.DGREY);
                D.text(26, y, F.T, s.name, isPlaying && P.playing ? D.GREEN : col);   /* (green while it plays) */
                D.text(132, y, F.T, String(M.bpm(s)).padStart(3), col);
                D.text(156, y, F.T, String(s.order.length).padStart(3), col);
                D.text(186, y, F.T, U.fmtTime(U.songSeconds(s)), col);
            }
            const s = song(), y0 = Y0 + 16 + LIST * 13 + 4;
            D.hline(0, y0 - 3, 240, D.DGREY);
            if (!s) {
                if (!M.state.songs.length) D.text(0, y0, F.T, "NO SONGS: SEL MAKES ONE OR UPLOAD A DISK", D.DGREY);
                return;
            }
            D.runs(0, y0, F.T, [["ORDER ", D.CYAN], [s.name, D.WHITE], [order ? "  (EDIT)" : "", D.LBLUE]]);
            if (!order) {                                    /* (its key's hint: the play mode) */
                const m = [" ONE PLAY", " REPEAT SONG", " PLAY NEXT"][P.playMode];
                D.runs(240 - D.tw(F.T, "SEQ" + m), y0, F.T, [["SEQ", D.YELLOW], [m, P.playMode ? D.GREEN : D.LGREY]]);
            }
            const h = P.at(), playPos = P.playing && P.playingSong() === U.song && h ? h.pos : -1;
            if (!order && !mv) pos = startPos();                 /* on the list: where PLAY starts, boxed (EDIT opens there) */
            const lines = 5, first = Math.max(0, Math.floor(pos / 8) - 3);
            for (let l = first; l < first + lines; l++) {
                const y = y0 + 14 + (l - first) * 13;
                if (l * 8 >= s.order.length && l > 0) break;
                D.text(0, y, F.T, M.hex(l * 8), D.DGREY);
                for (let k = 0; k < 8; k++) {
                    const i = l * 8 + k;
                    if (i >= s.order.length) break;
                    const x = 22 + k * 27, n = s.order[i], p = M.pattern(n);
                    if (i === playPos) D.rect(x - 3, y - 1, 24, 13, D.DEEP + D.BLUE);
                    if (i === pos && !mv) D.box(x - 3, y - 1, 24, 13, D.WHITE);
                    D.text(x, y, F.T, M.hex(n), !p ? D.RED : order && i === pos ? D.WHITE : i === playPos ? D.LGREEN : D.LGREY);
                }
            }
            if (!s.order.length) D.text(22, y0 + 14, F.T, "EMPTY: ENV ADDS A PATTERN", D.DGREY);
        },
    };
    U.register(scr);
})();
