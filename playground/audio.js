/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the page's side of the sound. A browser starts audio only after a click or a
 * key, so the first one starts it (A.start). The worklet is put together from the dsp-*.js files'
 * source at that moment (no build). Notes, instruments, samples and the FX setup go to it as
 * messages; it sends back voices, scopes and levels. Before audio starts, the clock is the page's,
 * so the transport and the screens work silent.
 *
 * The CPU figures are the FM-1's, estimated from what sounds (each engine's cost a voice, measured on
 * the FM-1 with the firmware's engines, and the effects'), not this computer's. */
PTFM.audio = (function () {
    "use strict";
    const M = PTFM.model;
    const A = {
        stats: { cpu: 0, cpuAvg: 0, cpuMax: 0, half: 0, late: 0, lateAt: -1e9, skips: 0, steals: 0, voices: [], count: {},
                 used: 0, usedMax: 0, battery: 2, usb: false, fx: {}, levels: new Float32Array(34), out: 0,
                 usedNow: 0, usedHalf: 0, cpuNow: 0, countNow: {}, avgSum: 0, avgN: 0,
                 /* core 2's, the same figures (cpu etc. are core 1's; graph2 a value a graph sample) */
                 cpu2: 0, cpu2Avg: 0, cpu2Max: 0, cpu2Now: 0, avgSum2: 0, avgN2: 0, graph2: [] },
        running: false,
    };
    let ctx = null, node = null, analysers = null, master = .5, out = [];        /* MASTER at half: (.5)^2 x 2 */
    const sent = new Map();                     /* instrument ref -> the model version it was sent at */
    let version = -1, startT = performance.now(), off = 0;

    /* one clock for the page: the audio's once it runs, carried on from the page's own */
    const pageTime = () => (performance.now() - startT) / 1000;
    /* LINE GAIN's 0 dB sits 4 dB over the output stage's unity, as the firmware's (specs/ui/screens/settings.md) */
    const LINE_REF = 4;
    const lineGain = db => Math.pow(10, (db + LINE_REF) / 20);
    A.time = () => ctx && node ? ctx.currentTime + off : pageTime();
    function post(m) {
        if (!node) return;
        if (m.time != null) m.time -= off;
        out.push(m);
    }
    setInterval(() => { if (node && out.length) { node.port.postMessage({ t: "batch", list: out }); out = []; } }, 10);

    /* SETTINGS' MIDI CONTROLLER: the sound fades as CUT's (12 ms), then stops for good (only reloading brings it) */
    A.end = function () {
        A.ended = true;
        A.hush(true);
        setTimeout(() => { if (ctx) ctx.suspend(); A.running = false; }, 50);
    };
    A.start = async function () {
        if (A.ended) return;
        if (ctx) { if (ctx.state !== "running") ctx.resume(); return; }
        /* 44.1 kHz, as the FM-1; a browser that refuses the rate runs its own (the engines take sampleRate) */
        try { ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" }); }
        catch (e) { ctx = new AudioContext({ latencyHint: "interactive" }); }
        /* continue the clock where the page's left off */
        const src = "const E = {};\n" + PTFM.dsp.map(f => "(" + f.toString() + ")(E);").join("\n") +
                    "\nregisterProcessor('ptfm', E.Proc);\n";
        /* the worklet from a data: URL (a page opened from file:// takes no blob: one); where a page's
         * rules refuse both (a page shown inside another site), the same code on the page's own thread */
        for (const url of ["data:text/javascript;charset=utf-8," + encodeURIComponent(src),
                           () => URL.createObjectURL(new Blob([src], { type: "text/javascript" }))]) {
            try {
                if (!ctx.audioWorklet) break;
                await ctx.audioWorklet.addModule(typeof url === "function" ? url() : url);
                node = new AudioWorkletNode(ctx, "ptfm", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
                break;
            } catch (e) { console.warn("PT/FM sound: no worklet from this URL:", e.message); }
        }
        if (!node) node = onPageThread(ctx);
        off = pageTime() - ctx.currentTime;
        const split = ctx.createChannelSplitter(2);
        analysers = [ctx.createAnalyser(), ctx.createAnalyser()];
        for (const a of analysers) { a.fftSize = 2048; a.smoothingTimeConstant = 0; }
        node.connect(ctx.destination);
        node.connect(split);
        split.connect(analysers[0], 0);
        split.connect(analysers[1], 1);
        node.port.onmessage = e => { if (e.data.t === "stats") stats(e.data); };
        /* the samples, once */
        const list = [];
        for (const c of M.state.carts) if (c.samples) c.samples.forEach((s, i) => list.push({ key: c.id + ":" + i, data: M.audio[s.audio], rate: s.rate }));
        node.port.postMessage({ t: "samples", list });
        voicesSent = A.voicesInForce();
        node.port.postMessage({ t: "voices", n: voicesSent });
        node.port.postMessage({ t: "master", v: master });
        node.port.postMessage({ t: "output", mono: M.state.settings.OUTPUT === "MONO" });
        node.port.postMessage({ t: "pulseside", side: Math.max(0, A.pulseSideNow()) });
        node.port.postMessage({ t: "gain", v: lineGain(M.state.settings["LINE GAIN"]) });
        A.running = true;
        A.fxNow();
        PTFM.player.mutesChanged();
    };
    A.analysers = () => analysers;

    /* The worklet's code on the page's own thread, when no worklet loads: the dsp-*.js functions get the
     * worklet's globals here (sampleRate, currentTime, AudioWorkletProcessor, registerProcessor: they
     * read them when called), and a ScriptProcessorNode runs the processor 128 frames at a time, each
     * block at the time it will play. Its port is a pair of calls. Later and heavier than a worklet,
     * but it sounds. */
    function onPageThread(ctx) {
        let now = 0;
        const sp = ctx.createScriptProcessor(2048, 0, 2);
        window.sampleRate = ctx.sampleRate;
        Object.defineProperty(window, "currentTime", { get: () => now, configurable: true });
        window.AudioWorkletProcessor = class {
            constructor() { this.port = { postMessage: m => { if (sp.port.onmessage) sp.port.onmessage({ data: m }); }, onmessage: null }; }
        };
        window.registerProcessor = () => {};
        const E = {};
        for (const f of PTFM.dsp) f(E);
        const proc = new E.Proc();
        sp.port = { postMessage: m => proc.port.onmessage({ data: m }), onmessage: null };
        const l = new Float32Array(128), r = new Float32Array(128);
        sp.onaudioprocess = ev => {
            const L = ev.outputBuffer.getChannelData(0), R = ev.outputBuffer.getChannelData(1);
            for (let i = 0; i < L.length; i += 128) {
                now = ev.playbackTime + i / ctx.sampleRate;
                l.fill(0); r.fill(0);
                proc.process([], [[l, r]]);
                L.set(l, i); R.set(r, i);
            }
        };
        console.warn("PT/FM sound: on the page's thread (no audio worklet here)");
        return sp;
    }

    /* -------------------------------------------------------------- instruments --- */
    function instMsg(ref) {
        const c = M.cart(ref >> 8), inst = M.inst(ref);
        if (!c || !inst) return null;
        let p = inst.p;
        if (c.type === "SAMPLER") p = Object.assign({ key: c.id + ":" + p.sample }, p);
        if (c.type === "FUSION") return null;
        return { t: "inst", ref, engine: c.type, p, level: inst.level };
    }
    function ensure(ref) {
        if (M.version !== version) { sent.clear(); version = M.version; }
        if (sent.has(ref)) return;
        const m = instMsg(ref);
        if (m) post(m);
        sent.set(ref, true);
    }
    A.instrument = ref => { if (node) ensure(ref); };
    A.changed = () => { A.fxNow(); };

    /* --------------------------------------------------------------------- notes --- */
    /* vol 0-127 (127: the instrument at its own level); opts: accent, glide (TB-303), offset (a
     * sample's start, in samples past START), params (instrument parameters for this note alone),
     * asEdited (the instrument is being edited: its track's parameters from commands left out), leg (a
     * legato note, LEG: the held note's id, which goes on at this note if it still sounds) */
    A.noteOn = (id, time, midi, ref, vol, track, opts) => {
        if (!node) return;
        ensure(ref);
        post({ t: "on", id, time, midi, ref, vol, track, accent: !!opts.accent, glide: !!opts.glide, offset: opts.offset || 0,
               params: opts.params && opts.params.length ? opts.params : null, asEdited: !!opts.asEdited, key: !opts.song,
               leg: opts.leg || null });
    };
    /* FILTER's envelope's hits (specs/ui/screens/fx.md#filters-envelope): a track's note (its row; idp its replay's ids'
     * first letter, so a flush drops it with the replay's notes), or the keys' (-1) */
    A.fire = (track, time, idp) => { if (node) post({ t: "fire", track, time, id: idp ? idp + "~f" : undefined }); };
    A.noteOff = (id, time) => post({ t: "off", id, time });
    /* a sounding note's pitch (semitones from its note) and level (0-1), tick by tick */
    A.ctl = (id, time, bend, vol) => post({ t: "ctl", id, time, bend, vol });
    A.flush = (prefix, time, at) => post({ t: "flush", prefix, time, at: at != null ? at - off : null });
    /* at `time`, every note of a replay (its ids' first letter) cut, as NOTE CUT cuts a track's: silent at once,
     * gone 4 ms on (PLAYBACK's K1 landing: the position starts clean) */
    A.cutAt = (prefix, time) => { post({ t: "cutp", prefix, time }); post({ t: "killp", prefix, time: time + .004 }); };
    A.allOff = () => post({ t: "alloff" });
    A.hush = on => post({ t: "hush", on });             /* CUT: silent while on */
    /* a track's instrument parameters (the commands 0x40-0x7F), its mixer (LEVEL, PAN, sends, DUCK) */
    A.iparam = (track, code, value, time) => post({ t: "iparam", track, code, value, time });
    A.iclear = (track, time) => post({ t: "iclear", track, time });
    A.trackMix = (track, key, value, time) => post({ t: "tmix", track, key, value, time });
    A.trackMute = (track, on) => post({ t: "mute", track, on });
    A.click = (time, accent, sound, level) => post({ t: "click", time, accent, sound, level });
    A.punch = (k, on) => post({ t: "punch", k, on });
    /* a punch-in command's start or end at its row's time; flushed with the replay's notes (idp: their
     * ids' first letter, "s", or a DJ deck's) */
    A.punchAt = (k, on, time, idp = "s") => post({ t: "tpunch", id: idp + "~", k, on, time });
    /* a bar's start at its row's time: GATE's step 1 there (specs/ui/screens/fx.md); a DJ deck's, its own */
    A.barAt = (time, deck, idp = "s") => post({ t: "tbar", id: idp + "~", time, deck });
    A.fxBypass = on => post({ t: "bypass", on });           /* FX LIVE's BYPASS, while held */
    A.fxWatch = on => post({ t: "watch", on });             /* the EFFECTS list's meters on screen */
    A.master = v => { master = v * v * 2; post({ t: "master", v: master }); };
    A.output = mono => post({ t: "output", mono });
    /* SYNC PULSE (specs/connections.md#clock): its side as it acts (with SYNC INTERNAL), 0 none; a click's time */
    A.pulseSideNow = () => M.state.settings.SYNC === "INTERNAL" ? ["SYNC PULSE L", "SYNC PULSE R"].indexOf(M.state.settings.OUTPUT) + 1 : 0;
    A.pulseSide = () => post({ t: "pulseside", side: Math.max(0, A.pulseSideNow()) });
    A.pulse = time => post({ t: "pulse", time });
    A.gain = db => post({ t: "gain", v: lineGain(db) });              /* LINE GAIN, in dB */
    /* the voices notes take: SETTINGS' VOICES, 16 counted as 12 while the second core does not run
     * (specs/second-core.md#the-voices-on-core-2); the worklet told at a change of either (stats watches the core) */
    A.voicesInForce = () => { const n = +M.state.settings.VOICES; return n > 12 && !A.c2On() ? 12 : n; };
    let voicesSent = 0;
    A.voices = () => { voicesSent = A.voicesInForce(); post({ t: "voices", n: voicesSent }); };
    A.songGain = g => post({ t: "songgain", g });       /* PLAYBACK's K3: the song's voices (tracks 0-15) times g */
    A.keysGain = g => post({ t: "keysgain", g });       /* SETTINGS' KEYS GAIN: the keys' voices times g */
    A.keysDry = on => post({ t: "keysdry", on });       /* SETTINGS' KEYS: DRY, the keys past every effect */
    A.keysMix = m => post({ t: "keysmix", m: { ...m } });   /* FX MIXER's KEYS column: the keys' PAN, DUCK, sends */

    /* the FX setup (specs/ui/screens/fx.md): in LOOPS the loops'; else the playing song's, else the one played
     * last's (a pattern played from the EDITOR uses the current song's), else the keys' song's. FX commands change
     * what plays, not the setup kept, until the next song starts (fxReset) */
    let fxSrc = null, fxIdx = -1;
    /* the song's side: the playing song's, else the one started last (paused, stopped), else (nothing played since the
     * start, or since UNLOAD SONG) the keys' song, whatever songs there are */
    function songSrc() {
        const P = PTFM.player, U = PTFM.ui;
        /* REVERT replaces the model's objects: the song then by its place (a moved song keeps its object) */
        if (fxSrc && fxSrc !== M.state.loops && !M.state.songs.includes(fxSrc)) fxSrc = M.state.songs[fxIdx] || null;
        return P.playingSong() >= 0 ? M.state.songs[P.playingSong()]
             : fxSrc && fxSrc !== M.state.loops ? fxSrc : P.what ? M.songOrKeys(U.song) : M.state.keys;
    }
    /* the setup FX edits, DJ aside: the loops' in LOOPS (FX from it too) or while they play or played last (stopped:
     * what the keys hear), else the song's */
    function playSrc() {
        const P = PTFM.player;
        return P.what === "loops" || P.loopsShown() ? M.state.loops : songSrc();
    }
    /* the setup heard, DJ aside: what plays's; with nothing playing the loops' while LOOPS is shown or when they played
     * last (the firmware holds them: the keys through their setup), else the song's */
    function heardSrc() {
        const P = PTFM.player;
        if (P.playing) return P.what === "loops" ? M.state.loops : songSrc();
        return P.what === "loops" || P.loopsShown() ? M.state.loops : songSrc();
    }
    /* the setup FX shows and edits: DJ's on DJ (and FX from it) and wherever the decks play (specs/ui/screens/fx.md) */
    const djShown = () => { const P = PTFM.player, U = PTFM.ui; return P.what === "dj" || !!(U.screen && (U.screen.name === "DJ" || U.screen.name === "FX" && U.fxDj)); };
    A.fxSong = () => djShown() ? M.state.dj : playSrc();
    A.songFx = s => { fxSrc = s; fxIdx = M.state.songs.indexOf(s); A.fxNow(); };
    A.fxHeard = () => PTFM.player.what === "dj" ? M.state.dj : heardSrc();
    A.fxNow = function () {
        const P = PTFM.player, dj = P.what === "dj", s = dj ? M.state.dj : heardSrc();
        if (!s) return;
        post({ t: "fx", setup: s.fx, mixer: dj ? null : s.mixer, base: s === M.state.loops ? 16 : 0, tempo: P.tempo(),
               gates: M.state.gates.map(g => g.steps), id: dj ? "dj" : s === M.state.loops ? "loops" : s === M.state.keys ? "keys" : M.state.songs.indexOf(s) });
        if (dj && P.djPost) P.djPost();                         /* the decks' songs' setups and DJ's mix */
    };
    A.fxCmd = (code, value, time, deck) => post({ t: "fxcmd", code, value, time, deck });
    /* the FX page's effect under the cursor (specs/ui/screens/fx.md#the-fx-setup), the hand's alone: kept as it plays
     * when another setup comes (a song's start), and no FX LFO on it; null: none */
    let mine = null;
    A.fxMine = name => { if (name !== mine && node) { mine = name; post({ t: "mine", fx: name }); } };
    /* DJ (specs/ui/screens/dj.md): its mix (on, the left deck's tracks' base, the crossfader, PREVIEW and
     * the deck in its right ear, the filters, KEY, the properties), a deck's song's setup and mixer, a deck's
     * commands forgotten or taken over, ECHO OUT, BRAKE (timed: down, up, or cleared) */
    A.dj = m => {
        if (m.lfo) for (const a of [m.lfo.fader, m.lfo.fx, ...(m.lfo.filt || [])]) if (a && a.at) a.at -= off;   /* (an LFO's start: the worklet's clock) */
        post(Object.assign({ t: "dj" }, m));
    };
    A.djDeck = (deck, setup, mixer) => post({ t: "djdeck", deck, setup, mixer });
    A.djReset = deck => post({ t: "djreset", deck });
    A.djEcho = (deck, on, fx = "DELAY") => post({ t: "djecho", deck, on, fx });     /* ECHO OUT (DELAY), REVERB OUT */
    /* BRAKE's fall (from 1x to a stop in 0.5 s), BACKSPIN's (from `fast` x in `dur` s), a spin up (`up`) */
    A.djBrake = (deck, time, up, clear, fast = 1, dur = .5) => post({ t: "brake", deck, time, up: !!up, clear: !!clear, fast, dur });
    A.fxReset = () => post({ t: "fxreset" });
    A.fxEmptyAt = time => post({ t: "fxempty", time });     /* the effects' tails gone at `time` (a clean start) */
    A.fxUnset = (name, i) => post({ t: "fxunset", name, i });
    A.mixUnset = (track, key) => post({ t: "fxunset", track, key });
    setInterval(() => {
        if (!node) return;
        const P = PTFM.player;
        post({ t: "tempo", tempo: P.tempo() });
        if (P.what === "dj" && P.djTempos) post({ t: "djtempo", tempo: P.djTempos() });   /* each deck's, its GATE's beat */
    }, 200);

    /* ------------------------------------------------------------ the figures --- */
    /* each voice's cost on the FM-1 at 44.1 kHz, % of a half (measured with the firmware's engines:
     * DX7 and Juno-106 about 6 % a voice at 8 voices; the TB-303 and samples less; CHIP's estimated from its code: a
     * pulse under 1 %, the fastest noise about 2 %), and the effects' */
    const COST = { DX7: 5.6, JUNO: 4.4, TB303: 2.8, SAMPLER: 1.6, CHIP: 1.2 };
    const FX_COST = { GATE: .4, FILTER: 1.2, DRIVE: .8, "LO-FI": .5, CHORUS: 2.2, DELAY: 1.4, REVERB: 7.5, LIMITER: .6, DUCK: .3 };
    const win = { tenth: { at: 0, v: 0, c: 0, c2: 0, n: 0 }, half: { at: 0, v: 0, c: 0, c2: 0, n: 0 } };
    /* the second core (specs/second-core.md#the-emulator-and-the-playground): DEVICE reads STARTING for the page's
     * first 2 s (SETTINGS ON), then ON or OFF (it never stops here); it runs, and shows, only in ON */
    const C2_START = performance.now();
    A.c2State = () => M.state.settings["SECOND CORE"] === "OFF" ? "OFF" : performance.now() - C2_START < 2000 ? "STARTING" : "ON";
    A.c2On = () => A.c2State() === "ON";
    function stats(m) {
        const S = A.stats;
        if (A.voicesInForce() !== voicesSent) A.voices();   /* (the second core started, or SECOND CORE switched) */
        S.voices = m.voices;
        S.count = m.count;
        S.used = m.voices.length;
        S.usedMax = Math.max(S.usedMax, S.used);
        S.steals = m.steals - (S.stealBase || 0);
        S.fx = m.fx.act || {};
        S.levels = m.fx.levels || S.levels;
        S.duck = m.fx.duck || 0;
        S.gateStep = m.fx.step;
        S.filterOpen = m.fx.fopen || 0;                         /* (FILTER's envelope: how far open now, its hits) */
        S.filterHits = m.fx.fhits || 0;
        S.fxOver = m.fx.over || {}; S.fxOverOn = m.fx.overOn || {}; S.mixLive = m.fx.live || {};
        S.djTaken = m.djTaken || 0;
        S.djLfo = m.fx.lfo || null;                             /* (DJ's LFOs where they are: the screen draws the crossfader there) */
        const P = PTFM.player, dj = P.what === "dj", s = A.fxHeard();
        let fxc = 0;
        if (s) for (const n in FX_COST) if (S.fxOverOn[n] !== undefined ? S.fxOverOn[n] : s.fx.on[n]) fxc += FX_COST[n];
        if (s && s.fx.routing === "SENDS") fxc += 1.5;
        /* DJ: each deck's song's small effects, its DJ FILTER while not OFF, and the filtered sends (3.5 %) */
        const djc = dj && P.djCost ? P.djCost(FX_COST) : 0;
        fxc += djc;
        let v = 3;
        for (const k in m.count) v += m.count[k] * COST[k];
        S.fxCost = fxc;
        /* the split: with the second core on, the effects (not DJ's decks' small ones, which core 1 keeps) and an
         * output share of 1 are core 2's, the rest core 1's; off, everything is core 1's */
        const on = A.c2On(), noise = Math.random();
        const cpu2 = on ? Math.min(100, fxc - djc + 1 + noise * .5) : 0;
        const cpu = Math.min(100, v + fxc - (on ? fxc - djc + 1 : 0) + noise * (on ? 1 : 1.5));
        S.cpu = S.cpu * .7 + cpu * .3;
        S.cpu2 = S.cpu2 * .7 + cpu2 * .3;
        S.cpuMax = Math.max(S.cpuMax, S.cpu);
        S.cpu2Max = Math.max(S.cpu2Max, S.cpu2);
        S.half = Math.max(S.half, Math.round(S.cpu / 100 * 5804 * 1.25));
        S.graph = S.graph || [];
        const now = performance.now();
        /* as the screens show them (specs/ui/frame.md#the-header, screens/status.md), each when its time ends: over
         * each tenth of a second the most voices at once (STATUS), the CPU's mean (the header's bar, STATUS) and the
         * engines' counts; over each half second the most voices (the header) and the graph's sample, the CPU's mean
         * (AVERAGE: the samples' since the counts were zeroed; core 2's, those it ran in) */
        for (const k of ["tenth", "half"]) {
            const w = win[k];
            w.v = Math.max(w.v, S.used); w.c += S.cpu; w.c2 += S.cpu2; w.n++;
            if (now - w.at < (k === "tenth" ? 100 : 500)) continue;
            if (k === "tenth") { S.usedNow = w.v; S.cpuNow = w.c / w.n; S.cpu2Now = w.c2 / w.n; S.countNow = m.count; }
            else if (w.at) {
                S.usedHalf = w.v;
                S.graph.push(w.c / w.n); if (S.graph.length > 60) S.graph.shift();
                S.graph2.push(on ? w.c2 / w.n : null); if (S.graph2.length > 60) S.graph2.shift();   /* (none while off) */
                S.avgSum += w.c / w.n; S.avgN++; S.cpuAvg = S.avgSum / S.avgN;
                if (on) { S.avgSum2 += w.c2 / w.n; S.avgN2++; S.cpu2Avg = S.avgSum2 / S.avgN2; }
            }
            win[k] = { at: now, v: 0, c: 0, c2: 0, n: 0 };
        }
        if (S.half > 5804 && !S.lateSeen) { S.late++; S.lateAt = now; S.lateSeen = true; }
        if (S.cpu < 90) S.lateSeen = false;
    }
    A.estimate = stats;                         /* (the worklet's stats message: the tests feed it) */
    A.resetCounts = function () {
        const S = A.stats;
        S.cpuMax = S.cpu; S.half = 0; S.late = 0; S.skips = 0; S.stealBase = (S.stealBase || 0) + S.steals; S.steals = 0; S.usedMax = S.used;
        S.avgSum = 0; S.avgN = 0; S.cpuAvg = 0;
        S.cpu2Max = S.cpu2; S.avgSum2 = 0; S.avgN2 = 0; S.cpu2Avg = 0;
    };
    return A;
})();
