/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the PLAY group: PLAYBACK (what plays, followed, not editable:
 * specs/ui/screens/playback.md) and FX (LIVE, EDIT and MIXER: punch-ins, the chain, routing, GATE
 * patterns: specs/ui/screens/fx.md). LOOPS is scr-loops.js. */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, B = U.B, E = U.E, P = PTFM.player;
    const Y0 = U.BODY_Y;

    /* ===================================================================== PLAYBACK === */
    const pb = { view: 8, left: 0, track: 0 };      /* track: the cursor track ALGORITHM moves */
    function pbWhere() {
        const h = P.at();
        /* LOOPS' loops, playing on outside LOOPS (or what played last): their one pattern */
        if (P.what === "loops") return { loops: true, row: P.playing && h ? h.row % Math.max(1, M.state.loops.rows) : P.kept.loops.row || 0, pos: -1 };
        if (h && (P.playing || P.stepped) && h.pat >= 0) return h;
        if (P.what === "loop") return { pat: P.kept.loop.pat, row: P.kept.loop.row, pos: -1 };
        const i = P.kept.lastSong != null ? P.kept.lastSong : U.song, s = M.state.songs[i], k = P.kept.song[i] || { pos: 0, row: 0 };
        return s && s.order.length ? { pat: s.order[Math.min(k.pos, s.order.length - 1)], row: k.row, pos: k.pos } : null;
    }
    const pbSongIndex = () => P.playingSong() >= 0 ? P.playingSong() : (P.kept.lastSong != null ? P.kept.lastSong : U.song);
    const pbSong = () => M.state.songs[pbSongIndex()];
    /* K4 the playback speed, K3 the songs' volume (specs/ui/screens/playback.md): the speed a share of each song's
     * own tempo, a DJ deck's way, turned in BPM (1 a click, SEL held 10), half to double; the volume dB on each song's
     * level, its voices alone, DJ's VOLUME's steps (a dB, SEL held 6; +6 to -24, then OFF). The player keeps both
     * (P.speed, P.volume) through other songs, back when anything else plays; heard at once, not stored. LFO resets
     * the one turned last (K4 at first). The words as DJ's: `K4 TEMPO 124.0 BPM -3.2%`, `K3 VOLUME -6 DB` */
    let lastK = "K4";
    const own10 = sg => Math.floor((Math.floor(sg.tempo * 2400 / ((sg.ticks || 6) * (sg.rpb || 4))) + 5) / 10);
    function setSpeed(sg, share) {
        P.setSpeed(share);
        lastK = "K4";
        const own = own10(sg), now = Math.floor((own * P.speed() * 65536 + 32768) / 65536), c = Math.trunc((now - own) * 1000 / own);
        U.showKnob("K4 TEMPO", Math.floor(now / 10) + "." + now % 10 + " BPM " + (c < 0 ? "-" : "+") + Math.floor(Math.abs(c) / 10) + "." + Math.abs(c) % 10 + "%");
    }
    function setVolume(db) {
        P.setVolume(db);
        lastK = "K3";
        const v = P.volume();
        U.showKnob("K3 VOLUME", v <= -25 ? "OFF" : (v > 0 ? "+" : "") + v + " DB");
    }
    U.register({
        name: "PLAYBACK", group: "PLAY",
        does: [B.EDIT, B.GLO, B.SEQ, B.ENV, B.LFO],
        seq: () => pb.view === 1 ? "1 TRACK" : pb.view + " TRACKS",   /* SEQ: 8, 4, 2, 1 tracks */
        page() {
            const w = pbWhere();
            if (!w) return null;
            if (w.loops) return "LOOPS";
            if (w.pos < 0) return w.pat === M.EDITOR_PATTERN ? "EDITOR" : "PAT " + M.hex(w.pat);     /* (its own: no number) */
            const s = pbSong(), n = s.order.length;
            return M.of(w.pos, n, 2);
        },
        knob(e, s, coarse) {
            const w = pbWhere();
            if (e === E.ALGORITHM) {                         /* the cursor track, one at a time; the view follows it */
                const p = w && (w.loops ? U.loopsPattern() : M.pattern(w.pat));
                if (p) pb.track = U.clamp(pb.track + Math.sign(s), 0, p.tracks - 1);
                return;
            }
            if (e === E.K2) {                                /* the view: 1, 2, 4, 8 tracks, clockwise more; it stops at both ends */
                const VIEWS = [1, 2, 4, 8], i = U.clamp(VIEWS.indexOf(pb.view) + Math.sign(s), 0, VIEWS.length - 1);
                pb.view = VIEWS[i];
                U.showKnob("K2 VIEW", pb.view === 1 ? "1 TRACK" : pb.view + " TRACKS");
                return;
            }
            if (w && w.loops) return;                        /* the loops: watched here, stepped and moved in LOOPS */
            if (e === E.K4) {                                /* the playback speed: 1 BPM a click (SEL held: 10) */
                const sg = pbSong(), own = sg && own10(sg);
                if (!w || w.pos < 0 || !own) return;         /* (not the EDITOR's pattern loop) */
                const [lo, hi] = M.tempoRange(sg);
                const now = U.clamp(Math.floor((own * P.speed() * 65536 + 32768) / 65536) + Math.sign(s) * (coarse ? 100 : 10), Math.round(lo * 10), Math.round(hi * 10));
                setSpeed(sg, Math.floor(now * 65536 / own) / 65536);
                return;
            }
            if (e === E.K3) {                                /* the songs' volume: a dB a click (SEL held: 6) */
                if (!w || w.pos < 0 || !pbSong()) return;
                setVolume(U.clamp(P.volume() + Math.sign(s) * (coarse ? 6 : 1), -25, 6));
                return;
            }
            if (e === E.SELECT) P.step(Math.sign(s));
            else if (e === E.K1) {                           /* another position: playing, when the row ends */
                const r = P.skip(Math.sign(s)), sg = r && M.state.songs[r.song];
                if (sg) U.showKnob("K1 POSITION", M.of(r.pos, sg.order.length, 2) + " PATTERN " + M.hex(sg.order[r.pos]));
            }
        },
        tap(b) {
            if (b === B.SEQ) {                               /* 8, 4, 2, 1 tracks, round (as the EDITOR's) */
                pb.view = pb.view === 8 ? 4 : pb.view === 4 ? 2 : pb.view === 2 ? 1 : 8;     /* (the view keeps the cursor track) */
                U.toast(pb.view === 1 ? "1 TRACK" : pb.view + " TRACKS");
                return true;
            }
            if (b === B.GLO) {
                /* SCROLL; UNLOAD SONG while anything is held (the song's name, THE LOOPS, DJ; EDIT: done, the panel
                 * closed) */
                U.props("PLAYBACK VIEW", [{ name: "SCROLL", get: () => U.set("PLAYBACK SCROLL"),
                    change: () => U.edit(() => { M.state.settings["PLAYBACK SCROLL"] = U.set("PLAYBACK SCROLL") === "ROWS" ? "PAGES" : "ROWS"; }) },
                    P.held() && { name: "UNLOAD SONG", get: () => P.heldName(), edit: () => { U.close(); P.unload(); } }]
                    .filter(Boolean));
                return true;
            }
            if (b === B.EDIT) {
                const w = pbWhere();
                if (!w) return true;
                if (w.loops) { U.go("LOOPS"); return true; }  /* the loops are edited in LOOPS */
                const go = () => { U.pattern = w.pat; U.goDeep("EDITOR", { row: w.row }); };
                if (w.pat === M.EDITOR_PATTERN) go(); else U.ownAsk(go);   /* (the EDITOR's own pattern's warning first) */
                return true;
            }
            if (b === B.LFO) {                               /* the knob turned last back: the volume 0 dB, the speed 100 % */
                const w = pbWhere(), sg = pbSong();
                if (w && w.pos >= 0 && sg) { if (lastK === "K3") setVolume(0); else setSpeed(sg, 1); }
                return true;
            }
            /* ENV: the songs, as STATUS' list: PLAY plays the highlighted one from its start (it becomes the
             * current song; PLAYBACK follows it), ENV only closes */
            if (b === B.ENV) {
                const cur = P.playingSong() >= 0 ? P.playingSong() : U.song;
                U.songList("SONGS", cur, null, k => { U.song = k; P.kept.song[k] = { pos: 0, row: 0 }; P.playSong(k); });   /* (the one playing too: from its start) */
                return true;
            }
            return false;
        },
        footer() {
            const w = pbWhere();
            if (!w) return null;
            const where = w.loops ? "LOOPS " : w.pos >= 0 ? "POS " + M.hex(w.pos) + " " : w.pat === M.EDITOR_PATTERN ? "" : "PAT " + M.hex(w.pat) + " ";
            return [where + "ROW " + M.hex(w.row), D.LGREY];
        },
        draw() {
            const w = pbWhere(), p = w && (w.loops ? (M.state.loops.rows ? U.loopsPattern() : null) : M.pattern(w.pat));
            if (!p) { D.text(0, Y0 + 40, F.T, "NOTHING HAS PLAYED: PLAY STARTS THE SONG", D.DGREY); return; }   /* 40 characters: the full width */
            const V = U.VIEWS[pb.view], rows = V.rows;
            let top = U.set("PLAYBACK SCROLL") === "ROWS" ? w.row - (rows >> 1) : Math.floor(w.row / rows) * rows;
            top = U.clamp(top, U.set("PLAYBACK SCROLL") === "ROWS" ? -(rows >> 1) : 0, Math.max(0, p.rows - 1));
            if (top < 0) top = 0;
            pb.track = U.clamp(pb.track, 0, p.tracks - 1);   /* the view scrolls only to keep the cursor track in it */
            if (pb.track < pb.left) pb.left = pb.track;
            if (pb.track >= pb.left + pb.view) pb.left = pb.track - pb.view + 1;
            pb.left = U.clamp(pb.left, 0, Math.max(0, p.tracks - pb.view));
            U.grid(p, { view: pb.view, top, left: pb.left, cur: null, play: w.row, col: pb.track });
        },
    });

    /* ========================================================================== FX === */
    /* the pages (specs/ui/screens/fx.md): 0 LIVE, 1 EDIT, 2 MIXER; SEQ turns them */
    const LIVE = 0, EDIT = 1, MIXER = 2, PAGES = ["LIVE", "EDIT", "MIXER"];
    /* a knob's name in 6 characters (specs/ui/screens/fx.md): the long ones shortened so, the rest cut */
    const SHORT = { PATTERN: "PATTRN", FEEDBACK: "FEEDBK", "PRE-DELAY": "PRE-DL", CEILING: "CEIL",
                    RELEASE: "RELEAS", SUSTAIN: "SUSTN", "BEATS A BAR": "BEATS", "PING-PONG": "PINGPG" };
    const knobShort = n => (SHORT[n] || n).slice(0, 6);
    /* a knob's value in words (M.fxValue); in LOOPS FILTER's SOURCE names the loop on its track (`LOOP 03`) */
    function kval(name, i, v) {
        if (name === "FILTER" && i === 3 && v >= 1 && v <= 16 && PTFM.audio.fxSong() === M.state.loops) {
            if (v - 1 < M.state.loops.list.length) return "LOOP " + M.hex(v - 1);   /* (loop i plays on track i) */
        }
        return M.fxValue(name, i, v);
    }
    const fx = { page: LIVE, sel: 0, kpage: 0, last: -1, move: false, gateEdit: false, sess: null, mixT: 0, mixR: 0, bypass: false,
                 held: {} };                                 /* held: GATE's step keys down while its steps are edited */
    const setup = () => PTFM.audio.fxSong().fx;
    const mixer = () => PTFM.audio.fxSong().mixer;
    /* what plays (specs/ui/screens/fx.md): the song's FX and mixer commands (# and =) set knobs,
     * switches and mixer values over its setup; a knob turned here takes over until the song's next
     * command for it. The page shows what plays: a value a command moved off the setup's, in the
     * effect's colour */
    const playing = () => P.playing ? PTFM.audio.stats : null;
    function knobNow(name, i) {
        const S = playing(), o = S && S.fxOver ? S.fxOver[name + "/" + i] : undefined, v = setup().knobs[name][i];
        return { v: o !== undefined ? o : v, cmd: o !== undefined && o !== v };
    }
    function onNow(name) {
        const S = playing(), o = S && S.fxOverOn ? S.fxOverOn[name] : undefined, v = !!setup().on[name];
        return { v: o !== undefined ? o : v, cmd: o !== undefined && o !== v };
    }
    function mixNow(s, t, k) {
        const S = playing(), row = S && S.mixLive ? S.mixLive[(s === M.state.loops ? 16 : 0) + t] : null;
        const o = row ? row[k] : undefined, v = s.mixer[t][k];
        return { v: o !== undefined ? o : v, cmd: o !== undefined && o !== v };
    }
    /* DJ's own setup (specs/ui/screens/fx.md, dj.md#effects): CHORUS, DELAY and REVERB as sends, the LIMITER
     * and the METRONOME; the decks' songs keep the rest, not shown; no MIXER page (the decks play their songs') */
    const DJ_FX = ["CHORUS", "DELAY", "REVERB", "LIMITER", "METRONOME"];
    const isDj = () => PTFM.audio.fxSong() === M.state.dj;
    /* the keys' song's (no song at all): its MIXER the KEYS column alone, as it has no tracks */
    const isKeys = () => PTFM.audio.fxSong() === M.state.keys;
    const pageCount = () => isDj() ? 2 : PAGES.length;
    const liveFx = () => isDj() ? LIVE_FX.filter(n => DJ_FX.includes(n)) : LIVE_FX;
    /* the list: the chain in its order (in SENDS: SENDS then its three, indented; in GLOBAL the SENDS
     * item is kept in the chain but not shown), then DUCK and the METRONOME, apart */
    const visible = s => s.chain.filter(n => s.routing === "SENDS" ? !M.SENDS.includes(n) : n !== "SENDS");
    function items() {
        if (isDj()) return ["SENDS", ...M.SENDS, "LIMITER", "METRONOME"];
        const s = setup(), out = [];
        for (const n of visible(s)) {
            out.push(n);
            if (n === "SENDS") out.push(...M.SENDS);
        }
        out.push("DUCK", "METRONOME");
        return out;
    }
    const selName = () => items()[U.clamp(fx.sel, 0, items().length - 1)];
    const fxEdit = fn => { U.edit(fn); PTFM.audio.fxNow(); };
    /* the FX setup FX edits, for REVERT: whose it is (a song's index, "loops", "dj" or "keys") and its words (GLO's
     * title, REVERT's question) */
    function fxOwner() {
        const o = PTFM.audio.fxSong();
        if (o === M.state.dj) return { key: "dj", name: "DJ" };
        if (o === M.state.loops) return { key: "loops", name: "THE LOOPS" };
        if (o === M.state.keys) return { key: "keys", name: "THE KEYS" };
        return { key: M.state.songs.indexOf(o), name: o.name };
    }
    /* GLOBAL puts CHORUS, DELAY and REVERB where SENDS was; SENDS takes the place of the first of them */
    function setRouting(r) {
        const s = setup();
        if (s.routing === r) return;
        fxEdit(() => {
            if (r === "GLOBAL") {
                s.chain = s.chain.filter(n => !M.SENDS.includes(n));
                s.chain.splice(s.chain.indexOf("SENDS") + 1, 0, ...M.SENDS);
            } else {
                s.chain = s.chain.filter(n => n !== "SENDS");
                s.chain.splice(s.chain.findIndex(n => M.SENDS.includes(n)), 0, "SENDS");
            }
            s.routing = r;
        });
        U.toast("ROUTING " + r);
    }
    /* LIMITER, DUCK, the METRONOME and, in SENDS, the sends keep their places (specs/ui/screens/fx.md) */
    const fixedFx = (s, name) => name === "DUCK" || name === "METRONOME" || name === "LIMITER" || (s.routing === "SENDS" && M.SENDS.includes(name));
    function moveSel(d) {
        const s = setup(), name = selName(), vis = visible(s);
        if (fixedFx(s, name)) { U.toast(name + " DOES NOT MOVE"); return; }
        const i = vis.indexOf(name), j = i + d;
        if (j < 0 || j >= vis.length || vis[j] === "LIMITER") return;
        fxEdit(() => {
            const a = s.chain.indexOf(name), b = s.chain.indexOf(vis[j]);
            s.chain[a] = vis[j];
            s.chain[b] = name;
        });
        fx.sel = items().indexOf(name);
    }
    const knobsOf = name => M.FX[name] ? M.FX[name].knobs : [];
    function fxPicture(name, y, h) {
        const s = setup(), col = M.FX[name] ? M.FX[name].col : D.LGREY, w = 236, x = 2;
        const k = s.knobs[name] ? s.knobs[name].map((v, i) => knobNow(name, i).v) : [], on = M.FX[name] ? onNow(name).v : false, c = on ? col : D.DGREY;
        if (name === "GATE" && fx.gateEdit) k[0] = s.knobs.GATE[0];      /* its steps being edited: that pattern, whatever a command plays */
        /* the decided pictures (fx-pics.js), in the effect's colour, dark grey while it is off */
        if (PTFM.fxPics[name]) {
            const mb = name === "METRONOME" && on ? P.metroBeat() : -1, beat = mb < k[1] ? mb : -1, st = PTFM.audio.stats;
            let open = 0, hit = false;
            if (name === "FILTER" && on && k[2] && !isDj()) {  /* its envelope as it moves (the mix's FILTER) */
                if (st.filterHits !== fx.hits) { fx.hits = st.filterHits; fx.hitAt = U.now(); }
                open = st.filterOpen || 0;
                hit = fx.hitAt != null && U.now() - fx.hitAt < 100;
            }
            return PTFM.fxPics[name]({ x, y, w, h, col: c, deep: D.DEEP + c, k, editing: fx.gateEdit, open, hit,
                                       step: on && P.playing && st.gateStep != null ? st.gateStep : -1, beat });
        }
        if (name === "SENDS" && isDj()) {                  /* DJ's: the decks' songs feed them */
            D.text(x, y + 4, F.T, "DJ'S OWN, SHARED BY THE DECKS", D.LGREY);
            D.text(x, y + 18, F.T, "EACH DECK'S SONG SENDS AS IT WAS MADE", D.DGREY);
            D.text(x, y + 30, F.T, "ITS SENDS FOLLOW THE CROSSFADER", D.DGREY);
            D.text(x, y + 44, F.T, "AND ITS DJ FILTER", D.DGREY);
        } else if (name === "SENDS") {
            D.text(x, y + 4, F.T, "CHORUS, DELAY AND REVERB RETURN HERE", D.LGREY);
            D.text(x, y + 18, F.T, "ABOVE: THE DRY MIX ONLY", D.DGREY);
            D.text(x, y + 30, F.T, "BELOW: EVERYTHING", D.DGREY);
            D.text(x, y + 44, F.T, "MIXER, CHO DLY REV: EACH TRACK'S SEND", D.DGREY);
        }
    }
    function drawEffects() {
        const s = setup(), list = items(), sel = U.clamp(fx.sel, 0, list.length - 1), act = PTFM.audio.stats.fx || {};
        list.forEach((n, i) => {
            const y = Y0 + 1 + i * 11, on = i === sel, apart = n === "DUCK" || n === "METRONOME";
            const indent = s.routing === "SENDS" && M.SENDS.includes(n) ? 12 : 0;
            if (apart && list[i - 1] !== "DUCK" && n === "DUCK") D.hline(0, y - 1, 240, D.DGREY);
            if (on) D.box(0, y - 1, 240, 12, fx.move ? D.YELLOW : D.WHITE);
            if (n === "SENDS") { D.diamond(4, y - 1, D.LBLUE); D.text(14, y, F.T, "SENDS", D.LBLUE); return; }
            const col = M.FX[n].col, sw = onNow(n);
            D.text(4 + indent, y, F.T, n, sw.v ? col : D.DGREY);
            D.text(100, y, F.T, sw.v ? "ON" : "OFF", sw.cmd ? col : sw.v ? D.GREEN : D.DGREY);
            const a = Math.min(1, act[n] || 0), fill = a > 0 ? Math.max(0, 60 + Math.round(20 * Math.log10(a))) : 0;
            D.rect(128, y + 3, 60, 5, D.DEEP + D.DGREY);    /* (the activity meter in dB: a pixel a dB, -60 to 0) */
            if (sw.v && fill > 0) D.rect(128, y + 3, fill, 5, col);
            /* on the selected row, the switch that changes it */
            if (on && s.on[n] !== undefined) D.runs(190, y, F.T, sw.v ? [["OCT-", D.YELLOW], [" OFF", D.DGREY]] : [["OCT+", D.YELLOW], [" ON", D.DGREY]]);
        });
        const name = list[sel], y = Y0 + 1 + list.length * 11 + 2;
        D.hline(0, y, 240, D.DGREY);
        const ks = knobsOf(name);
        if (name !== "SENDS" && ks.length) {
            const kp = U.clamp(fx.kpage, 0, Math.ceil(ks.length / 4) - 1);
            for (let i = 0; i < 4; i++) {
                const ki = kp * 4 + i;
                if (ki >= ks.length) break;
                const x = i * 60;
                D.runs(x, y + 3, F.T, [["K" + (i + 1), D.YELLOW], [" " + knobShort(ks[ki].n), D.CYAN]]);
                const kv = knobNow(name, ki);
                /* under its name's line, as wide (9 characters): a column's gap before the next knob */
                D.text(x, y + 15, F.T, kval(name, ki, kv.v).slice(0, 9), kv.cmd ? M.FX[name].col : ki === fx.last ? D.WHITE : D.LGREY);
            }
        }
        fxPicture(name, y + 30, 226 - y - 32);
        /* the knob page, `ALG 1/2`, at the picture's top right (not on the values' line, where K4's value
         * is): only for an effect of more than 4 knobs, where ALGORITHM turns it */
        const kn = M.FX[name] ? M.FX[name].knobs.length : 0;
        if (kn > 4) {
            const pg = M.pageOf(U.clamp(fx.kpage, 0, Math.ceil(kn / 4) - 1), Math.ceil(kn / 4));
            D.rect(194, y + 30, 46, 12, D.BLACK);
            D.runs(238 - D.tw(F.T, "ALG " + pg), y + 30, F.T, [["ALG", D.YELLOW], [" " + pg, D.DGREY]]);
        }
    }
    /* ---- FX LIVE: the punch-ins on the white keys, the effects on the black keys (a fixed key each, in
     * the FX codes' order; F#5 BYPASS), the picked effect's knobs ---- */
    const LIVE_FX = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK", "METRONOME"];
    const LIVE_X = [2, 41, 86, 163, 202, 2, 41, 86, 163, 202, 2], LIVE_ROW = [0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2];   /* as the black keys sit */
    const pick = name => { const i = items().indexOf(name); if (i >= 0 && selName() !== name) { fx.sel = i; fx.kpage = 0; fx.last = -1; } };
    function drawLive() {
        const C = PTFM.cmds;
        D.text(0, Y0 + 1, F.T, U.punchKept() ? "HELD ON: ITS KEY OR ANOTHER STOPS IT" : "PUNCH-INS: HOLD A KEY; ARP HOLDS IT ON", D.CYAN);
        for (let k = 0; k < 16; k++) {                       /* on chips, a row a group of four */
            const x = (k % 4) * 60, y = Y0 + 15 + Math.floor(k / 4) * 13, held = U.punch === k;
            D.rect(x + 1, y - 1, 58, 12, held ? D.RED : D.DEEP + D.LGREY);
            D.text(x + 3, y, F.T, C.short({ c: 0xC3 + k, v: 1 }), held ? D.ink(D.RED) : D.LGREY);
        }
        D.hline(0, Y0 + 69, 240, D.DGREY);
        D.text(0, Y0 + 72, F.T, "EFFECTS: BLACK KEYS", D.CYAN);
        const picked = selName();
        for (let k = 0; k < 11; k++) {
            if (k < 10 && !liveFx().includes(LIVE_FX[k])) continue;     /* (DJ: its own five only) */
            const y = Y0 + 86 + LIVE_ROW[k] * 15, n = k < 10 ? LIVE_FX[k] : "BYPASS", word = n === "METRONOME" ? "METRO" : n;
            const lit = k < 10 ? onNow(n).v && !fx.bypass : fx.bypass;
            if (n === picked) D.box(LIVE_X[k] - 3, y - 2, D.tw(F.T, word) + 5, 14, D.WHITE);
            D.text(LIVE_X[k], y, F.T, word, k === 10 ? (fx.bypass ? D.RED : D.LGREY) : lit ? M.FX[n].col : D.LGREY);
        }
        D.hline(0, Y0 + 131, 240, D.DGREY);
        const name = liveFx().includes(picked) ? picked : liveFx()[0], ks = knobsOf(name), pages = Math.ceil(ks.length / 4), kp = U.clamp(fx.kpage, 0, pages - 1);
        const lit = onNow(name).v && !fx.bypass, col = M.FX[name].col;
        D.runs(0, Y0 + 135, F.S, [[name, lit ? col : D.DGREY], [lit ? "  ON" : "  OFF", lit ? D.LGREY : D.DGREY]]);
        if (pages > 1) {                                  /* the knob page: only where ALGORITHM turns one */
            const pg = M.pageOf(kp, pages);
            D.runs(238 - D.tw(F.T, "ALG " + pg), Y0 + 137, F.T, [["ALG", D.YELLOW], [" " + pg, D.LGREY]]);
        }
        for (let i = 0; i < 4; i++) {
            const ki = kp * 4 + i, kd = ks[ki];
            if (!kd) continue;
            const x = i * 60, y = Y0 + 155, kv = knobNow(name, ki), lo = kd.list ? 0 : kd.min, hi = kd.list ? kd.list.length - 1 : kd.max;
            D.runs(x + 2, y, F.T, [["K" + (i + 1) + " ", D.YELLOW], [knobShort(kd.n), D.LGREY]]);
            D.text(x + 2, y + 13, F.T, kval(name, ki, kv.v).slice(0, 9), ki === fx.last ? D.WHITE : col);
            D.rect(x + 2, y + 28, 54, 3, D.DEEP + D.DGREY);
            D.rect(x + 2, y + 28, Math.max(1, Math.round((kv.v - lo) / Math.max(1, hi - lo) * 54)), 3, col);
        }
        /* the hint (SEQ's pages are in the header; ALGORITHM's knob page beside the knobs, where it applies) */
        D.runs(0, Y0 + 197, F.T, [["SELECT", D.YELLOW], [" FX  ", D.LGREY], ["OCT", D.YELLOW], [" OFF/ON  ", D.LGREY], ["EDIT", D.YELLOW], [" ITS PICTURE", D.LGREY]]);
    }
    /* the MIXER's rows on show, and a value in words: a byte, and what it means */
    const mixRows = s => M.MIX_ROWS.filter(r => (r.k !== "dck" || s.fx.on.DUCK) && (!["cho", "dly", "rev"].includes(r.k) || s.fx.routing === "SENDS"));
    U.mixWords = (k, v) => k === "pan" ? M.hex(v) + (v === 128 ? " MIDDLE" : v < 128 ? " LEFT" : " RIGHT") : M.hex(v) + " " + Math.round(v / 127 * 100) + "%";
    /* the MIXER's columns: a song's 16 tracks; the loops' loops, a column a loop (loop i is track i; muted or silenced
     * by another's solo: dark) */
    function mixCols(s) {
        if (s === M.state.keys) return { n: 0, track: c => c, tracksOf: () => [], word: "TRACK ", muted: () => false };
        if (s !== M.state.loops) return { n: s.mixer.length, track: c => c, tracksOf: c => [c], word: "TRACK ", muted: c => U.muted(c) };
        const L = s.list, solo = L.some(l => l.soloed);
        return { n: L.length, track: c => c, tracksOf: c => L[c] ? [c] : [], word: "LOOP ",
                 muted: c => !!L[c] && (solo ? !L[c].soloed : L[c].muted) };
    }
    /* the KEYS column, the last (specs/ui/screens/fx.md#mixer): the keys' own, one for every setup (the settings'
     * KEYS MIX), its LVL KEYS GAIN as dB + 24 (0-36); DRY: no DUCK, no sends (dark) */
    const keysNow = k => k === "lvl" ? U.set("KEYS GAIN") + 24 : U.set("KEYS MIX")[k];
    const keysDark = k => U.set("KEYS") === "DRY" && k !== "lvl" && k !== "pan";
    const keysWords = (k, v) => k === "lvl" ? (v > 24 ? "+" : "") + (v - 24) + " DB" : U.mixWords(k, v);
    function keysSet(k, v) {
        if (k === "lvl") U.changeSetting("KEYS GAIN", U.clamp(v, 0, 36) - 24);
        else U.changeSetting("KEYS MIX", { ...U.set("KEYS MIX"), [k]: U.clamp(v, 0, k === "pan" ? 255 : 127) });
    }
    function drawMixer() {
        const s = PTFM.audio.fxSong(), rowsDef = mixRows(s), C = mixCols(s);
        fx.mixR = U.clamp(fx.mixR, 0, rowsDef.length - 1);
        fx.mixT = U.clamp(fx.mixT, 0, C.n);
        const used = s.order ? Math.max(...s.order.map(n => (M.pattern(n) || { tracks: 0 }).tracks), 0) : C.n;
        const left = Math.floor(fx.mixT / 8) * 8;
        D.text(2, Y0 + 2, F.T, s === M.state.loops ? "MIXER: THE LOOPS" : "MIXER", D.CYAN);
        for (let k = 0; k < 8 && left + k <= C.n; k++) {
            const c = left + k, t = C.track(c), x = 30 + k * 26, keys = c === C.n, dim = !keys && (c >= used || C.muted(c));
            if (keys) D.text(x - 2, Y0 + 16, F.T, "KEYS", c === fx.mixT ? D.WHITE : D.CYAN);
            else D.text(x + 3, Y0 + 16, F.T, M.hex(c), c === fx.mixT ? D.WHITE : dim ? D.DGREY : D.CYAN);
            rowsDef.forEach((r, j) => {
                const now = keys ? { v: keysNow(r.k), cmd: false } : mixNow(s, t, r.k), v = now.v, y = Y0 + 30 + j * 26;
                const col = dim || (keys && keysDark(r.k)) ? D.DGREY : now.cmd ? D.WHITE : r.k === "pan" ? D.LGREY : r.k === "lvl" ? D.GREEN : r.k === "dck" ? D.BROWN : D.LBLUE;
                if (c === fx.mixT && j === fx.mixR) D.box(x - 2, y - 2, 24, 24, D.WHITE);
                if (r.k === "pan") { D.hline(x + 1, y + 10, 18, D.DGREY); D.rect(x + 9 + Math.round((v - 128) / 128 * 9), y + 5, 2, 11, col); }
                else if (r.k === "dck" && (keys ? s.fx.knobs.DUCK[0] >= 16 : C.tracksOf(c).includes(s.fx.knobs.DUCK[0])))
                    D.text(x + 7, y + 5, F.T, "S", D.BROWN);      /* (DUCK's source: a track, or the keys) */
                else {
                    const hgt = Math.round(v / (keys && r.k === "lvl" ? 36 : 127) * 18);   /* (KEYS' LVL: -24 to +12 dB) */
                    D.rect(x + 6, y + 2, 8, 18, D.DEEP + D.DGREY); D.rect(x + 6, y + 20 - hgt, 8, hgt, col);
                }
            });
        }
        rowsDef.forEach((r, j) => D.text(2, Y0 + 35 + j * 26, F.T, r.n, j === fx.mixR ? D.WHITE : D.CYAN));
        const r = rowsDef[fx.mixR];
        if (fx.mixT === C.n) {
            D.text(2, Y0 + 196, F.T, "KEYS, EVERYWHERE  " + r.n + " " + keysWords(r.k, keysNow(r.k)) + (U.set("KEYS") === "DRY" ? "  DRY" : ""), D.LGREY);
            return;
        }
        const now = mixNow(s, C.track(fx.mixT), r.k);
        D.text(2, Y0 + 196, F.T, C.word + M.hex(fx.mixT) + "  " + r.n + " " + U.mixWords(r.k, now.v) + (now.cmd ? "  BY THE SONG" : "") +
               (C.muted(fx.mixT) ? "  MUTED" : ""), now.cmd ? D.WHITE : D.LGREY);
    }
    U.register({
        name: "FX", group: "PLAY",
        does: [B.SEL, B.GLO, B.SEQ, B.LFO, B.EDIT, B.ENV, B.ARP, B.OCTM, B.OCTP],
        combines: [B.ARP],
        /* SEL + OCT on EDIT, where the keys play notes: the keys' octave (OCT alone switches the picked effect there) */
        combo: (m, b) => m === B.SEL && (b === B.OCTM || b === B.OCTP) && fx.page === EDIT
            ? () => U.octaveStep(b === B.OCTP ? 1 : -1) : null,
        selLines: () => {                                  /* SEL held's layer (KEYS' LVL: by 6 dB) */
            const sng = PTFM.audio.fxSong(), rows = mixRows(sng);
            if (fx.page === EDIT) return ["K1-K4: BIG STEPS", "OCT: THE OCTAVE"];   /* (SEL + OCT: combo) */
            if (fx.page !== MIXER) return ["K1-K4: BIG STEPS"];
            return [fx.mixT === mixCols(sng).n && rows[U.clamp(fx.mixR, 0, rows.length - 1)].k === "lvl" ? "K1: BY 6 DB" : "K1: BY 16"];
        },
        /* LIVE and EDIT: the effect under the cursor is being edited, the hand's alone (its knobs, its ON / OFF; the
         * others follow their commands); MIXER: the mixer shown (the loops', or the song's) */
        editing(what, key) {
            if (what === "fx") return fx.page !== MIXER && key === selName();
            if (what === "mix") return fx.page === MIXER && (key >= 16) === (PTFM.audio.fxSong() === M.state.loops);
            return false;
        },
        sub: () => PAGES[fx.page],                         /* the header's subpage: LIVE, EDIT, MIXER */
        seq: () => M.pageOf(fx.page, pageCount()),          /* SEQ turns them: `SEQ 1/3` in the header (DJ's: 1/2) */
        arpHandsFree: () => fx.page === LIVE,               /* LIVE's keys are punch-ins: ARP starts on the last note played */
        enter() {
            if (fx.page >= pageCount()) fx.page = LIVE;      /* (DJ's setup has no MIXER) */
            if (isDj() && !DJ_FX.includes(selName())) { fx.sel = items().indexOf("CHORUS"); fx.kpage = 0; fx.last = -1; }   /* (DJ's: its first effect picked) */
            PTFM.audio.fxWatch(fx.page === EDIT);
        },
        lit(b) {
            if (b === B.EDIT && fx.gateEdit) return 1;
            if (b === B.SEL && fx.move) return 1;
            if (b === B.OCTM || b === B.OCTP) { const n = selName(), s = setup(); return fx.page !== MIXER && s.on[n] !== undefined ? (s.on[n] === (b === B.OCTP) ? 1 : .25) : 0; }
            if (b === B.EDIT && fx.page === LIVE) return .25;   /* the picked effect on EDIT */
            if (b === B.EDIT || b === B.ENV) return fx.page === EDIT && selName() === "GATE" ? .25 : 0;
        },
        knob(e, s, coarse) {
            const LF = liveFx();
            if (fx.page === LIVE && !LF.includes(selName())) pick(LF[0]);     /* (EDIT's SENDS item picked) */
            if (fx.page === LIVE && e === E.SELECT) {          /* the effects in the black keys' order */
                const i = LF.indexOf(selName());
                pick(LF[U.clamp((i < 0 ? 0 : i) + Math.sign(s), 0, LF.length - 1)]);
                return;
            }
            if (fx.page === MIXER) {
                const sng = PTFM.audio.fxSong(), mx = sng.mixer, rowsDef = mixRows(sng), C = mixCols(sng);
                if (e === E.ALGORITHM) fx.mixT = U.clamp(fx.mixT + Math.sign(s), 0, C.n);
                else if (e === E.SELECT) fx.mixR = U.clamp(fx.mixR + Math.sign(s), 0, rowsDef.length - 1);
                else if (e === E.K1 && fx.mixT === C.n) {      /* KEYS: a setting, every setup's */
                    const r = rowsDef[U.clamp(fx.mixR, 0, rowsDef.length - 1)];
                    keysSet(r.k, keysNow(r.k) + s * (coarse ? (r.k === "lvl" ? 6 : 16) : 1));
                    U.showKnob(r.n + " KEYS", keysWords(r.k, keysNow(r.k)));
                } else if (e === E.K1) {
                    const r = rowsDef[fx.mixR], ts = C.tracksOf(fx.mixT);
                    if (!ts.length) return;
                    const v =U.clamp(mx[ts[0]][r.k] + s * (coarse ? 16 : 1), 0, r.max || 127);
                    fxEdit(() => { for (const t of ts) mx[t][r.k] = v; });     /* a loop: all its tracks */
                    for (const t of ts) PTFM.audio.mixUnset((sng === M.state.loops ? 16 : 0) + t, r.k);
                    U.showKnob(r.n + " " + M.hex(fx.mixT), U.mixWords(r.k, v));
                }
                return;
            }
            /* a GATE step's key held: ALGORITHM its ratchets, ON, X2, X3, X4 (every held step alike) */
            const heldSteps = Object.keys(fx.held).map(Number).filter(k => U.keysHeld().includes(U.WHITE[k]));
            if (e === E.ALGORITHM && fx.gateEdit && fx.page === EDIT && heldSteps.length) {
                const g = M.state.gates[setup().knobs.GATE[0]];
                for (const k of heldSteps) fx.held[k].turned = true;
                fxEdit(() => { for (const k of heldSteps) g.steps[k] = U.clamp(g.steps[k] + Math.sign(s), 1, 4); });
                U.showKnob("STEP " + M.dec(heldSteps[0] + 1), ["OFF", "ON", "X2", "X3", "X4"][g.steps[heldSteps[0]]]);
                return;
            }
            if (e === E.SELECT) {
                if (fx.move) return moveSel(Math.sign(s));
                fx.sel = U.clamp(fx.sel + Math.sign(s), 0, items().length - 1); fx.kpage = 0; fx.last = -1; fx.gateEdit = false; return;
            }
            const name = selName(), ks = knobsOf(name);
            if (e === E.ALGORITHM) { fx.kpage = U.clamp(fx.kpage + Math.sign(s), 0, Math.max(0, Math.ceil(ks.length / 4) - 1)); return; }
            const ki = fx.kpage * 4 + (e - E.K1), kd = ks[ki];
            if (!kd) return;
            fx.last = ki;
            const st = setup(), from = knobNow(name, ki).v;     /* from what plays: no jump off a command's value */
            fxEdit(() => {
                const v = from;
                if (kd.list) st.knobs[name][ki] = U.clamp(v + Math.sign(s), 0, kd.list.length - 1);
                else st.knobs[name][ki] = U.clamp(v + s * (kd.step || 1) * (coarse ? 10 : 1), kd.min, kd.max);
            });
            PTFM.audio.fxUnset(name, ki);                    /* the knob turned wins over a command's value */
            U.showKnob(name + " " + kd.n, kval(name, ki, st.knobs[name][ki]));
        },
        tap(b) {
            const name = selName(), st = setup();
            if (b === B.SEQ) {
                fx.page = (fx.page + 1) % pageCount(); fx.gateEdit = false; fx.move = false;
                PTFM.audio.fxWatch(fx.page === EDIT);            /* the activity meters, only while shown */
                return true;
            }
            if (b === B.GLO) {
                const dj = isDj();                          /* DJ's: always SENDS */
                /* THE KEYS: their TEMPO (3-600, by 10 with SEL held) and SWING (50-75 %, by 5) first, the keys' song's own
                 * (a song's are SONGS' properties); heard at once */
                const k = () => M.state.keys;
                const time = isKeys() ? [
                    { name: "TEMPO", get: () => k().tempo + " BPM",
                      change: (s, c) => fxEdit(() => { k().tempo = U.clamp(k().tempo + Math.sign(s) * (c ? 10 : 1), M.TEMPO_MIN, M.TEMPO_MAX); }) },
                    { name: "SWING", get: () => k().swing + " %",
                      change: (s, c) => fxEdit(() => { k().swing = U.clamp(k().swing + Math.sign(s) * (c ? 5 : 1), 50, 75); }) }] : [];
                U.props("FX PROPERTIES FOR", time.concat([{ name: "ROUTING", get: () => setup().routing, fixed: dj,
                                                            change: dj ? null : () => setRouting(setup().routing === "GLOBAL" ? "SENDS" : "GLOBAL") }])
                    /* KEYS, EVERYWHERE: SETTINGS' KEYS, for every setup (specs/ui/screens/fx.md#the-keys-dry); not on DJ,
                     * where the keys play no notes */
                    .concat(dj ? [] : [{ name: "KEYS, EVERYWHERE", get: () => U.set("KEYS"),
                        change: () => { U.changeSetting("KEYS", U.set("KEYS") === "DRY" ? "THROUGH FX" : "DRY"); U.toast("KEYS " + U.set("KEYS")); } }]),
                    B.GLO, fxOwner().name);
                return true;
            }
            if (fx.page === MIXER) {
                if (b === B.LFO) {
                    const sng = PTFM.audio.fxSong(), r = mixRows(sng)[fx.mixR];
                    if (fx.mixT === mixCols(sng).n) { keysSet(r.k, r.k === "lvl" ? 24 : M.KEYS_MIX_DEF[r.k]); return true; }   /* (KEYS: 0 DB, 80, 00, 7F) */
                    fxEdit(() => { sng.mixer[fx.mixT][r.k] = r.def; });
                    return true;
                }
                if (b === B.OCTM || b === B.OCTP) return false;
                return b === B.SEL || b === B.EDIT || b === B.ENV;
            }
            if ((b === B.OCTP || b === B.OCTM) && st.on[name] !== undefined) {
                fxEdit(() => { st.on[name] = b === B.OCTP; });
                PTFM.audio.fxUnset(name, -1);
                U.toast(name + (st.on[name] ? " ON" : " OFF"));
                return true;
            }
            if (b === B.LFO && fx.last >= 0 && M.FX[name]) {
                fxEdit(() => { st.knobs[name][fx.last] = M.FX[name].knobs[fx.last].def; });
                U.showKnob(name + " " + M.FX[name].knobs[fx.last].n, kval(name, fx.last, st.knobs[name][fx.last]));
                return true;
            }
            if (b === B.SEL) {
                if (fx.move) { fx.move = false; U.toast("MOVED"); return true; }
                const own = fxOwner();
                U.menu("fx", name, [!isDj() && fx.page === EDIT && "MOVE", "RESET",
                                                            M.unsavedThing("fx", own.key) && "REVERT"], a => {
                    /* the chain as it was, for HOME to put back (it is heard in its new order as it moves) */
                    if (a === "MOVE" && fixedFx(st, name)) { U.refuse(B.SEL); U.toast(name + " DOES NOT MOVE"); return; }
                    if (a === "MOVE") { fx.move = { chain: st.chain.slice(), sel: fx.sel }; U.toast("SELECT MOVES IT, SEL PUTS IT DOWN"); }
                    if (a === "RESET" && M.FX[name]) fxEdit(() => { st.knobs[name] = M.FX[name].knobs.map(k => k.def); });
                    /* REVERT (specs/ui/saving.md#revert): the whole FX setup FX edits as saved (a song's other changes
                     * stay); what plays goes on with it from the next block */
                    if (a === "REVERT") U.askRevert("REVERT THE EFFECTS TO SAVED?", "FOR " + own.name, () => {
                        M.revertThing("fx", own.key);
                        fx.sel = U.clamp(fx.sel, 0, items().length - 1);
                        PTFM.audio.fxNow();
                    });
                });
                return true;
            }
            if (b === B.EDIT && fx.page === LIVE) {            /* the picked effect, on EDIT (LIVE and EDIT share it) */
                fx.page = EDIT; fx.gateEdit = false; fx.move = false;
                PTFM.audio.fxWatch(true);
                return true;
            }
            if (b === B.EDIT && name === "GATE" && fx.page === EDIT) {
                if (fx.gateEdit) { fx.gateEdit = false; fx.sess = null; return true; }      /* keeping the changes */
                /* the steps' session (specs/ui/controls.md): HOME puts back the gate patterns and GATE's
                 * knobs as they were (the song looked up each time: REVERT replaces the model's objects) */
                const sng = PTFM.audio.fxSong(), si = M.state.songs.indexOf(sng), of = () => si < 0 ? M.state.loops : M.state.songs[si];
                fx.sess = U.session();
                fx.sess.add("gates", () => M.state.gates, v => { M.state.gates = v; });
                fx.sess.add("knobs", () => of() && of().fx.knobs.GATE, v => { if (of()) of().fx.knobs.GATE = v; });
                if (st.knobs.GATE[0] < 16) {                     /* a built-in pattern: edit a copy among yours */
                    const g = M.state.gates[st.knobs.GATE[0]];
                    const k = 16 + M.state.gates.slice(16).findIndex(x => x.steps.filter(Boolean).length <= 1);
                    const to = k >= 16 ? k : 16;
                    fxEdit(() => { M.state.gates[to].steps = g.steps.slice(); st.knobs.GATE[0] = to; });
                    PTFM.audio.fxUnset("GATE", 0);                /* the copy plays, over a command's pattern, as a knob turned would */
                    U.toast(g.name + " COPIED TO " + M.state.gates[to].name);
                }
                fx.gateEdit = true;
                return true;
            }
            if (b === B.ENV && name === "GATE" && fx.page === EDIT) {
                const o = U.open({ kind: "list", title: "GATE PATTERNS", opener: B.ENV, sel: st.knobs.GATE[0],
                    knob(e, s) { if (e === E.SELECT) this.sel = U.clamp(this.sel + s, 0, 31); },
                    tap(bb) { if (bb === B.ENV) { U.close(this); fxEdit(() => { st.knobs.GATE[0] = this.sel; }); return true; } return false; },
                    draw() {
                        U.panel(2, Y0 + 2, 236, 206, this.title);
                        const top = Math.max(0, Math.min(this.sel - 6, 32 - 14));
                        for (let i = top; i < top + 14; i++) {
                            const g = M.state.gates[i], y = Y0 + 18 + (i - top) * 13;
                            if (i === this.sel) D.box(4, y - 1, 232, 13, D.WHITE);
                            D.text(8, y, F.T, M.hex(i) + " " + g.name, i < 16 ? D.LGREY : D.LBLUE);
                            g.steps.forEach((v, k) => D.rect(130 + k * 6, y + 2, 4, 7, v ? D.LBLUE : D.DEEP + D.DGREY));
                        }
                    } });
                void o;
                return true;
            }
            return false;                                    /* (ARP: the arpeggiator, ui.js's) */
        },
        key(n, down) {
            if (fx.page === LIVE) {                          /* LIVE: the keys play no notes */
                const wi = U.whiteIndex(n), bi = U.blackIndex(n);
                if (wi >= 0 && down) U.punchDown(wi);                     /* while held, or held on by ARP */
                else if (wi >= 0) U.punchUp(wi);                          /* (ended already: leaving FX ends it) */
                else if (bi === 10) { fx.bypass = down; PTFM.audio.fxBypass(down); }        /* F#5: BYPASS, while held */
                else if (bi >= 0 && down && liveFx().includes(LIVE_FX[bi])) pick(LIVE_FX[bi]);
                return true;
            }
            /* GATE's steps (specs/ui/screens/fx.md): a white key switches its step on and off; held, ALGORITHM
             * sets its ratchets. An OFF step comes on as the key goes down; an ON one goes off as it comes up,
             * unless ALGORITHM turned meanwhile */
            if (fx.gateEdit && fx.page === EDIT && U.whiteIndex(n) >= 0) {
                const g = M.state.gates[setup().knobs.GATE[0]], k = U.whiteIndex(n);
                if (down) {
                    fx.held[k] = { was: g.steps[k], turned: false };
                    if (!g.steps[k]) fxEdit(() => { g.steps[k] = 1; });
                } else {
                    const h = fx.held[k];
                    delete fx.held[k];
                    if (h && h.was && !h.turned) fxEdit(() => { g.steps[k] = 0; });
                }
                return true;
            }
            return false;
        },
        keyLeds(keys) {
            if (fx.page === LIVE) {
                /* the white keys: each group of four's first dim, the punch-in running bright; the black
                 * keys: an effect on dim, the picked one bright; BYPASS dark until held */
                U.punchLeds(keys);
                const picked = selName();
                U.BLACK.forEach((n, i) => { keys[n] = i === 10 ? (fx.bypass ? 1 : 0) : !liveFx().includes(LIVE_FX[i]) ? 0 : LIVE_FX[i] === picked ? 1 : onNow(LIVE_FX[i]).v ? .25 : 0; });
                return true;
            }
            if (!fx.gateEdit) return false;
            /* GATE's steps: OFF dark, ON half; a ratchet blinks its count (X2 twice ...), then a pause,
             * over and over; the playing step's key dark as the playhead passes; both from the clock */
            const g = M.state.gates[setup().knobs.GATE[0]], now = U.now(), cyc = now % 1400, play = P.playing && onNow("GATE").v ? PTFM.audio.stats.gateStep : -1;
            U.WHITE.forEach((n, i) => {
                const v = g.steps[i];
                keys[n] = i === play ? 0 : v === 0 ? 0 : v === 1 ? .5 : cyc < v * 240 && cyc % 240 < 140 ? 1 : .25;
            });
            return true;
        },
        back() {
            if (fx.move) {                                   /* HOME: the chain as it was */
                const was = fx.move;
                fx.move = false;
                if (setup().chain.join() !== was.chain.join()) fxEdit(() => { setup().chain = was.chain; });
                fx.sel = was.sel;
                U.toast("NOT MOVED");
                return true;
            }
            if (fx.gateEdit) {                               /* HOME: dropping the changes */
                if (fx.sess.drop()) PTFM.audio.fxNow();
                fx.gateEdit = false; fx.sess = null;
                return true;
            }
            return false;
        },
        leave() {
            fx.gateEdit = false; fx.sess = null;
            if (fx.bypass) { fx.bypass = false; PTFM.audio.fxBypass(false); }
            if (fx.page === LIVE && !U.punchKept()) U.punchOff();     /* (one ARP holds on plays on) */
            PTFM.audio.fxWatch(false);
            PTFM.audio.fxMine(null);
        },
        footer() {
            if (fx.page === MIXER) {
                const C = mixCols(PTFM.audio.fxSong());
                return [fx.mixT === C.n ? "KEYS" : C.word + M.hex(fx.mixT), D.LGREY];
            }
            const n = selName(), cost = { GATE: .4, FILTER: 1.2, DRIVE: .8, "LO-FI": .5, CHORUS: 2.2, DELAY: 1.4, REVERB: 7.5, LIMITER: .6, DUCK: .3, METRONOME: .1 }[n];
            return cost != null ? [n + " " + cost.toFixed(1) + "% CPU", D.LGREY] : null;
        },
        draw() {
            if (fx.page >= pageCount()) fx.page = LIVE;       /* (the decks started meanwhile: DJ's setup, no MIXER) */
            PTFM.audio.fxMine(fx.page !== MIXER && M.FX[selName()] ? selName() : null);   /* (the worklet's: no LFO on it) */
            if (fx.page === LIVE) drawLive(); else if (fx.page === EDIT) drawEffects(); else drawMixer();
        },
    });
})();
