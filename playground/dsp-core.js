/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground, sound: the core of the audio worklet. Every dsp-*.js file adds a function to
 * PTFM.dsp; audio.js puts their source text together into one worklet module when the page loads (a
 * page opened from the file system cannot load a worklet from a file), so there is no build: edit,
 * reload. Each function gets E, the worklet's shared object, and must use nothing from the page.
 *
 * Here: the voice pool (8, 12 or 16 voices heard for everything, as SETTINGS -> VOICES, 16 counted as 12
 * while the second core does not run (audio.js sends the limit in force): a new note takes a
 * free voice, else one is taken from a note: release tails first, the new note's own track's first, then
 * the quietest; then held song notes, the quietest first, a muted track's as silent; the keys' held notes
 * last, the oldest first; a note started since the last block the last of its kind. A held note taken is
 * parked (not a TB-303's): it fades out over 64 frames, then goes on unheard, its envelopes running, until
 * a voice is free again, and comes back fading in over 256 frames; let go meanwhile, it is gone. The rest
 * taken fade out over 64 frames and go: specs/ui/screens/settings.md#voices), events placed on their
 * sample (note on / off, a note's pitch and level each tick, a track's instrument parameters and
 * mixer, an FX command, a metronome click), each voice rendered into its track's buffer, then the
 * mixer and effects (dsp-fx.js), and the figures the screens show (voices, scopes, levels, steals)
 * posted back about 25 times a second.
 *
 * Instrument parameters (the commands 0x40-0x7F): E.params[engine](p, code, value, proc) writes one
 * into a copy of the patch; a track's stay until its instrument changes, and new notes on the track
 * start with them; an engine's refresh(v) takes a sounding note's changed patch on.
 *
 * DJ (specs/ui/screens/dj.md): two decks, one on tracks 0-15, one on 16-31 (deck 0 and deck 1 here,
 * whichever side each is on). The pool's priority: a note finding no free voice takes one from the deck
 * the crossfader favours less, never from the deck heard for a deck faded out; each deck's KEY and BRAKE
 * bend its voices. */
window.PTFM = window.PTFM || {};
PTFM.dsp = (PTFM.dsp || []).concat(function (E) {
    "use strict";
    const SR = sampleRate, BLK = 128, NTRK = 34, KEYS = 33;
    const MOST = 16, SLOTS = 20, FADE_OUT = 64, FADE_IN = 256;   /* heard at most (16: 10 + core 2's 6); the pool with
                                                                  * the parked; frames */
    E.SR = SR;
    E.BLK = BLK;
    E.engines = {};
    E.dB = v => Math.pow(10, v / 20);
    E.mtof = m => 440 * Math.pow(2, (m - 69) / 12);
    E.KEYS = KEYS;
    E.NTRK = NTRK;
    E.params = {};
    E.clone = p => JSON.parse(JSON.stringify(p));
    E.clamp = (v, a, b) => v < a ? a : v > b ? b : v;
    /* a sine table for the engines: 4096 points, read with interpolation */
    const SIN = new Float32Array(4097);
    for (let i = 0; i <= 4096; i++) SIN[i] = Math.sin(i / 4096 * Math.PI * 2);
    E.sin = ph => {                               /* ph in cycles */
        let x = (ph - Math.floor(ph)) * 4096;
        const i = x | 0;
        return SIN[i] + (SIN[i + 1] - SIN[i]) * (x - i);
    };

    class Proc extends AudioWorkletProcessor {
        constructor() {
            super();
            this.events = [];
            this.sorted = true;
            this.insts = {};
            this.samples = {};
            this.max = 12;
            this.pool = [];
            this.byId = new Map();
            this.age = 0;
            this.ageSeen = 0;                     /* this.age at the last block's end: a note started since has no peak yet */
            this.parks = 0;                       /* notes parked, counted: the last parked comes back first */
            this.tracks = Array.from({ length: NTRK }, () => new Float32Array(BLK));
            this.tmp = new Float32Array(BLK);
            this.unheard = new Float32Array(BLK); /* what a parked voice renders, thrown away */
            this.tparams = {};                    /* track -> {code: value}: its instrument's parameters, set by commands */
            this.steals = 0;
            this.statsAt = 0;
            this.dj = null;                       /* DJ's mix: {left (0 or 16), xf 0-32, sharp, preview, ...}, null off DJ */
            this.djTaken = 0;                     /* notes that took a voice from the quieter deck */
            this.shift = [0, 0];                  /* each deck's bend now: its KEY and its BRAKE, semitones */
            this.songGain = 1;                    /* PLAYBACK's K3 on the song's voices (tracks 0-15) */
            this.keysGain = 1;                    /* SETTINGS' KEYS GAIN on the keys' voices */
            this.brake = [null, null];            /* {t0, up}: a deck's BRAKE (0.5 s down) or its spin up (0.1 s) */
            this.fireT = new Uint8Array(NTRK);    /* FILTER's envelope's sources this block: a track's (or the keys', */
            this.heldT = new Uint8Array(NTRK);    /* KEYS) notes started, held */
            this.fx = E.createFx(this);
            this.port.onmessage = e => this.msg(e.data);
        }
        msg(m) {
            switch (m.t) {
                case "batch": for (const x of m.list) this.msg(x); return;
                case "on": case "off": case "ctl": case "click": case "pulse": case "iparam": case "iclear": case "tmix": case "fxcmd": case "tpunch": case "tbar": case "brake": case "fire":
                case "cutp": case "killp": case "fxempty":
                    this.events.push(m); this.sorted = false; return;
                case "dj":
                    this.dj = m.on ? m : null;
                    if (!m.on) { this.brake = [null, null]; this.shift = [0, 0]; }
                    this.fx.msg(m);
                    return;
                case "inst": this.insts[m.ref] = m; return;
                case "samples": for (const s of m.list) this.samples[s.key] = { data: Float32Array.from(s.data, v => v / 128), rate: s.rate }; return;
                case "voices": this.max = Math.min(MOST, m.n); this.trim(); return;   /* (the limit in force: audio.js) */
                case "songgain": this.songGain = m.g; return;   /* (PLAYBACK's K3: the song's voices, not the keys') */
                case "keysgain": this.keysGain = m.g; return;   /* (SETTINGS' KEYS GAIN: the keys' voices alone) */
                /* what was scheduled from `time` on goes; the notes sounding let go at once, or at `at` (a DJ deck
                 * jumping: its notes end where the new rows start) */
                case "flush": {
                    this.events = this.events.filter(e => !(e.id && e.id[0] === m.prefix && e.time >= m.time));
                    if (m.at != null) {
                        for (const v of this.pool) if (v.on && !v.rel && v.id && v.id[0] === m.prefix) this.events.push({ t: "off", id: v.id, time: m.at });
                        this.sorted = false;
                    } else for (const v of this.pool) if (v.id && v.id[0] === m.prefix) this.release(v);
                    return;
                }
                case "alloff":                   /* (every voice and the effects' tails, as the firmware's) */
                    this.events = this.events.filter(e => e.t === "click" || e.t === "pulse");
                    for (const v of this.pool) { v.kill = true; }
                    this.fx.silence();
                    this.fx.msg({ t: "empty" });
                    return;
                default: this.fx.msg(m);
            }
        }
        heard(v) { return v.on && !v.park; }    /* sounding in a voice of the count (not fading out, not parked) */
        /* fewer voices: the held notes above the count are parked, the quietest first (a TB-303's let go); their
         * tails ring on */
        trim() {
            let n = this.pool.filter(v => this.heard(v) && !v.rel).length;
            for (; n > this.max; n--) {
                const v = this.victim(x => !x.rel, -1);
                if (!v) break;
                if (v.engine === "TB303") this.release(v);
                else { v.park = "out"; v.fade = FADE_OUT; v.parkedAt = ++this.parks; }
            }
        }
        /* how much a voice would be missed, the least first: [rank, loudness] (the firmware's pl_worth) */
        worth(v, who) {
            let loud = this.fx.mutes[v.track] ? 0 : v.peak || 0, rank;
            if (v.rel) rank = v.track === who ? 0 : 1;
            else if (v.track !== KEYS) rank = 2;
            else { rank = 3; loud = 0; }
            if (v.age > this.ageSeen) loud = Infinity;
            return [rank, loud];
        }
        victim(ok, who) {                        /* the voice to take among those heard that ok() lets; ties: the oldest */
            let best = null, bw = null;
            for (const v of this.pool) {
                if (!this.heard(v) || !ok(v)) continue;
                const w = this.worth(v, who);
                if (!best || w[0] < bw[0] || (w[0] === bw[0] && (w[1] < bw[1] || (w[1] === bw[1] && v.age < best.age)))) { best = v; bw = w; }
            }
            return best;
        }
        /* the notes the pool holds at most, parked and fading ones included: the count and the 4 past VOICES 16,
         * 16 at least (VOICES 16: 20; 12 and 8: 16; the firmware's pl_places) */
        places() { return Math.max(MOST, this.max + SLOTS - MOST); }
        /* a voice object for a new note: a free one, a new one (20 at most with the parked), else the note parked
         * longest goes (unheard: nothing to hear of it), a song's before the keys'; null if every other is still
         * fading out. full: for a note taken or born parked, a free one only while the pool holds fewer notes
         * than places() */
        slot(full) {
            const room = !full || this.pool.filter(v => v.on).length < this.places();
            let f = room ? this.pool.find(v => !v.on) : null;
            if (!f && room && this.pool.length < SLOTS) { f = { on: false }; this.pool.push(f); }
            if (!f) {
                const key = v => v.track === KEYS ? 1 : 0;
                for (const v of this.pool)
                    if (v.park === "parked" && (!f || key(v) < key(f) || (key(v) === key(f) && v.parkedAt < f.parkedAt))) f = v;
                if (f) { f.on = false; if (f.id && this.byId.get(f.id) === f) this.byId.delete(f.id); }
            }
            return f;
        }
        /* the keys first, however quiet (specs/ui/screens/settings.md#voices): a song's note never takes a voice
         * from a note held on the keys; with every voice a key's, or a key's note parked and waiting for one, the
         * song's note starts parked (bornParked), to come in as a voice comes free, after the keys' */
        bornParkedSlot() {
            const s = this.slot(true);
            this.bornParked = !!s;
            return s;
        }
        alloc(m) {
            const who = m && m.track >= 0 ? m.track : -1;
            if (who >= 0 && this.pool.some(v => v.on && v.park === "parked" && !v.rel && v.track === KEYS))
                return this.bornParkedSlot();
            if (this.pool.filter(v => this.heard(v)).length < this.max)
                return this.slot() || (this.pool.push({ on: false }), this.pool[this.pool.length - 1]);
            let v;
            if (this.dj && m && m.track >= 0 && m.track < 32) {
                v = this.djSteal(m.track >> 4, who);
                if (!v) return null;                          /* nothing it may take: the note is not played */
            } else v = this.victim(() => true, who);
            if (!v) return null;
            if (who >= 0 && v.track === KEYS && !v.rel) return this.bornParkedSlot();   /* (the keys hold every voice) */
            this.steals++;
            const s = this.slot(true);
            if (!s) { if (v.id) this.byId.delete(v.id); return v; }   /* (no room to fade: cut at once) */
            v.park = "out";                                   /* (taken: it fades out, then a held one is parked) */
            v.fade = FADE_OUT;
            v.back = 0;
            if (!v.rel && v.engine !== "TB303") v.parkedAt = ++this.parks;
            else { v.going = true; if (v.id && this.byId.get(v.id) === v) this.byId.delete(v.id); }
            return s;
        }
        unpark() {                                /* voices free in the count: the notes parked last come back, the keys' first */
            let n = this.pool.filter(v => this.heard(v)).length;
            const key = v => v.track === KEYS ? 1 : 0;
            for (; n < this.max; n++) {
                let b = null;
                for (const v of this.pool)
                    if (v.on && v.park === "parked" && !v.rel && (!b || key(v) > key(b) || (key(v) === key(b) && v.parkedAt > b.parkedAt))) b = v;
                if (!b) return;
                b.park = null;
                b.back = FADE_IN;
            }
        }
        /* DJ's crossfader gain of deck d (0: tracks 0-15, 1: 16-31), 0-1; where its LFO has it now (xfNow,
         * dsp-fx.js), else where it is set */
        djGain(d) {
            const dj = this.dj, xf = dj.xfNow != null ? dj.xfNow : dj.xf, x = xf / 32, left = d * 16 === dj.left;
            if (dj.sharp) return left ? (xf <= 30 ? 1 : (32 - xf) / 2) : (xf >= 2 ? 1 : xf / 2);
            return left ? Math.cos(x * Math.PI / 2) : Math.sin(x * Math.PI / 2);
        }
        /* the voice deck d's note takes with the pool full (specs/ui/screens/dj.md#voices): from the deck the
         * crossfader favours less (in the middle, the right deck), in the pool's order (worth); only when it
         * has none, the note's own deck's. A deck faded out completely never takes one from the other: the
         * quieter is then itself */
        djSteal(d, who) {
            const dj = this.dj, rightD = dj.left === 0 ? 1 : 0, g = [this.djGain(0), this.djGain(1)];
            const q = Math.abs(g[0] - g[1]) < 1e-6 ? rightD : g[0] < g[1] ? 0 : 1;
            const pick = k => this.victim(v => v.track < 32 && v.track >> 4 === k, who);
            let v = pick(q);
            if (!v && q !== d) v = pick(d);
            if (!v) return null;
            if (v.track >> 4 !== d) this.djTaken++;
            return v;
        }
        /* each deck's bend: its KEY, and BRAKE's fall (to a stop in 0.5 s, held there), BACKSPIN's (from 3x, high,
         * to a stop in 1 s) or a spin up (0.1 s) */
        djShift(t) {
            for (let d = 0; d < 2; d++) {
                const b = this.brake[d];
                let s = 0;
                if (b && !b.up) s = Math.max(-96, 12 * Math.log2(Math.max(1e-4, b.fast * Math.min(1, 1 - (t - b.t0) / b.dur))));
                else if (b) { const k = (t - b.t0) / .1; if (k >= 1) this.brake[d] = null; else s = 12 * Math.log2(Math.max(1e-4, k)); }
                this.shift[d] = (this.dj && this.dj.key ? this.dj.key[d] : 0) + s;
            }
        }
        /* the patch a note starts with: the instrument's, with its track's parameters and the note's own */
        patch(inst, m) {
            const tp = m.track >= 0 && !m.asEdited ? this.tparams[m.track] : null, set = E.params[inst.engine];
            if (!set || (!tp && !m.params)) return inst.p;
            const p = E.clone(inst.p);
            if (tp) for (const c in tp) set(p, +c, tp[c], this);
            if (m.params) for (const x of m.params) set(p, x.c, x.v, this);
            return p;
        }
        noteOn(m) {
            const inst = this.insts[m.ref];
            if (!inst || this.fx.hush) return;          /* (CUT held: no note starts; the time goes on) */
            const eng = E.engines[inst.engine];
            if (!eng) return;
            let v = this.byId.get(m.id);
            if (v && v.on && v.eng === eng && inst.engine === "TB303") {        /* the 303's own voice: legato or retrigger */
                v.p = this.patch(inst, m);
                v.ownP = v.p !== inst.p;
                eng.note(v, m.midi, m, this);
                v.rel = false;
                v.vol = m.vol / 127;
                v.bend = v.bendIn = 0;
                return;
            }
            if (m.leg) {                                 /* LEG (commands.md#legato): the held note goes on at this one */
                const h = this.byId.get(m.leg);
                if (h && h.on && !h.rel && !h.cutting && h.park !== "out" && h.ref === m.ref) {   /* (still sounding, not
                                                  * taken, the same instrument) */
                    this.byId.delete(m.leg);
                    h.id = m.id;
                    this.byId.set(m.id, h);
                    h.leg = m.midi - h.midi;             /* (its pitch there: a bend on top of the replay's) */
                    h.vol = m.vol / 127;
                    if (h.eng.key) h.eng.key(h, m.midi);
                    return;
                }
                if (h && h.on) { this.release(h); this.byId.delete(m.leg); }   /* else a plain note, as any */
                if (!this.fx.hush) this.fireT[m.track < 0 ? KEYS : m.track] = 1;   /* (its FILTER envelope hit) */
            }
            if (v && v.on) this.release(v);
            this.bornParked = false;
            v = this.alloc(m);
            const born = this.bornParked;
            this.bornParked = false;
            if (!v || (born && inst.engine === "TB303")) return;   /* (a TB-303 slides note to note in its voice) */
            const scope = v.scope || new Float32Array(256);
            for (const k in v) delete v[k];
            const track = m.track < 0 ? KEYS : m.track, sh = this.dj && track < 32 ? this.shift[track >> 4] : 0;
            Object.assign(v, { on: true, id: m.id, ref: m.ref, eng, engine: inst.engine, track, key: !!m.key, age: ++this.age,
                               rel: false, vol: m.vol / 127, gain: E.dB(inst.level || 0) * (E.GAIN[inst.engine] || 1),
                               midi: m.midi, scope, sw: 0, peak: 0, bend: sh, bendIn: 0 });
            if (born) { v.park = "parked"; v.parkedAt = ++this.parks; }    /* (unheard until a voice is free) */
            this.byId.set(m.id, v);
            const p = this.patch(inst, m);
            v.p = p;
            v.ownP = p !== inst.p;
            eng.start(v, p, m.midi, m, this);
        }
        /* a parameter command: the track keeps it, and its sounding notes take it on */
        param(ev) {
            (this.tparams[ev.track] = this.tparams[ev.track] || {})[ev.code] = ev.value;
            for (const v of this.pool) {
                if (!v.on || v.track !== ev.track || !E.params[v.engine]) continue;
                if (!v.ownP) { v.p = E.clone(v.p); v.ownP = true; }
                E.params[v.engine](v.p, ev.code, ev.value, this);
                if (v.eng.refresh) v.eng.refresh(v, ev.code, this);
            }
        }
        release(v) {
            if (!v.on || v.rel) return;
            v.rel = true;
            v.eng.release(v, this);
        }
        apply(ev) {
            if (ev.t === "on") return this.noteOn(ev);
            if (ev.t === "fire") { if (!this.fx.hush) this.fireT[ev.track < 0 ? KEYS : ev.track] = 1; return; }
            if (ev.t === "click") return this.fx.click(ev);
            if (ev.t === "pulse") return this.fx.pulse(ev);
            if (ev.t === "iparam") return this.param(ev);
            if (ev.t === "iclear") { delete this.tparams[ev.track]; return; }
            if (ev.t === "tmix" || ev.t === "fxcmd") return this.fx.timed(ev);
            if (ev.t === "tpunch") return this.fx.msg({ t: "punch", k: ev.k, on: ev.on });
            if (ev.t === "tbar") return this.fx.msg({ t: "bar", time: ev.time, deck: ev.deck });       /* GATE's step 1 */
            if (ev.t === "fxempty") return this.fx.msg({ t: "empty" });     /* the effects' tails gone (PLAYBACK's K1) */
            if (ev.t === "brake") { this.brake[ev.deck] = ev.clear ? null : { t0: ev.time, up: !!ev.up, fast: ev.fast || 1, dur: ev.dur || .5 }; return; }
            if (ev.t === "cutp" || ev.t === "killp") {   /* a replay's notes cut: silent, then gone (A.cutAt) */
                for (const v of this.pool) {
                    if (!v.on || !v.id || v.id[0] !== ev.prefix) continue;
                    if (ev.t === "cutp") { v.vol = 0; v.cutting = true; }
                    else if (v.cutting) { v.kill = true; v.cutting = false; }
                }
                return;
            }
            const v = this.byId.get(ev.id);
            if (!v || !v.on) return;
            if (ev.t === "off") { this.release(v); this.byId.delete(ev.id); }
            else if (ev.t === "ctl") { v.bendIn = ev.bend; v.vol = ev.vol; }
        }
        renderVoices(a, b) {
            const tmp = this.tmp, dj = !!this.dj;
            for (const v of this.pool) {
                if (!v.on) continue;
                if (v.kill) { v.on = false; if (v.id && this.byId.get(v.id) === v) this.byId.delete(v.id); continue; }
                v.bend = (v.bendIn || 0) + (v.leg || 0) + (dj && v.track < 32 ? this.shift[v.track >> 4] : 0);   /* (a legato's
                                                  * interval; DJ: its deck's KEY and BRAKE) */
                if (v.park === "parked") {        /* (unheard, its time going on; let go meanwhile: gone) */
                    let alive = !v.rel;
                    if (alive) { this.unheard.fill(0, a, b); alive = v.eng.render(v, this.unheard, a, b, this); }
                    if (!alive) { v.on = false; if (v.id && this.byId.get(v.id) === v) this.byId.delete(v.id); }
                    continue;
                }
                tmp.fill(0, a, b);
                const alive = v.eng.render(v, tmp, a, b, this);
                const g = v.gain * v.vol * (v.track < 16 ? this.songGain : v.track === KEYS ? this.keysGain : 1),
                      out = this.tracks[v.track] || this.tracks[KEYS];
                let pk = 0;
                for (let i = a; i < b; i++) {
                    let s = tmp[i] * g;
                    if (v.park === "out") s *= v.fade > 0 ? v.fade-- / FADE_OUT : 0;   /* (taken: fading out) */
                    else if (v.back > 0) s *= 1 - v.back-- / FADE_IN;                 /* (back from parking: fading in) */
                    out[i] += s;
                    v.scope[v.sw++ & 255] = s;
                    const q = s < 0 ? -s : s;
                    if (q > pk) pk = q;
                }
                v.peak = Math.max(v.peak * .9, pk);
                if (alive && v.park === "out" && v.fade <= 0) {   /* faded out: a held note waits parked, the rest go */
                    if (v.rel || v.going) { v.on = false; if (v.id && this.byId.get(v.id) === v) this.byId.delete(v.id); }
                    else { v.park = "parked"; v.peak = 0; }
                } else if (!alive) { v.on = false; if (v.id && this.byId.get(v.id) === v) this.byId.delete(v.id); }
            }
        }
        process(inputs, outputs) {
            const out = outputs[0], L = out[0], R = out[1] || out[0];
            const t0 = currentTime, t1 = t0 + BLK / SR;
            for (const t of this.tracks) t.fill(0);
            if (this.dj) this.djShift(t0);
            if (!this.sorted) { this.events.sort((x, y) => x.time - y.time); this.sorted = true; }
            let pos = 0;
            while (this.events.length && this.events[0].time < t1) {
                const ev = this.events.shift();
                const off = Math.max(0, Math.min(BLK, Math.round((ev.time - t0) * SR)));
                if (off > pos) { this.renderVoices(pos, off); pos = off; }
                ev.off = off;
                this.apply(ev);
            }
            if (pos < BLK) this.renderVoices(pos, BLK);
            this.unpark();
            this.ageSeen = this.age;
            this.heldT.fill(0);                         /* (FILTER's envelope: the notes held, parked and muted ones too) */
            for (const v of this.pool) if (v.on && !v.rel) this.heldT[v.key ? KEYS : v.track] = 1;
            this.fx.process(this.tracks, L, R, t0);
            this.fireT.fill(0);
            if (t0 - this.statsAt > .04) { this.statsAt = t0; this.post(); }
            return true;
        }
        post() {
            const voices = [];
            const count = { DX7: 0, JUNO: 0, TB303: 0, SAMPLER: 0, CHIP: 0 };
            let parked = 0;
            for (const v of this.pool) {
                if (!v.on) continue;
                if (v.park === "parked") { parked++; continue; }   /* (not heard) */
                count[v.engine]++;
                const s = new Float32Array(64);
                for (let i = 0; i < 64; i++) s[i] = v.scope[(v.sw - 256 + i * 4) & 255];
                voices.push({ id: v.id, track: v.track === KEYS ? -1 : v.track, engine: v.engine, midi: v.midi, rel: v.rel, scope: s, peak: v.peak,
                              key: v.p && v.p.key, pos: v.pos });                 /* a sample voice: which sample, where it plays */
            }
            this.port.postMessage({ t: "stats", voices, count, steals: this.steals, parked, max: this.max, fx: this.fx.stats(), djTaken: this.djTaken });
        }
    }
    E.Proc = Proc;
});
