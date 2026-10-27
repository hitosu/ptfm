/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: INSTRUMENTS (specs/ui/screens/instruments.md): a cartridge's slots in two columns of 8,
 * the current instrument in detail below (DX7 operators, Juno-106 sliders, TB-303 knobs, a sample's
 * waveform, a FUSION pattern's map); EDIT: edit mode, two panels, the parameters and a picture; SEL
 * the slot menu; ENV the cartridges (and on SAMPLE the samples); GLO the cartridge; ARP the
 * arpeggiator (ui.js's, as on every screen but the EDITOR and LOOPS). */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, B = U.B, E = U.E, P = PTFM.player;
    const Y0 = U.BODY_Y;
    /* the DX7's algorithms as MSFA keeps them (dsp-dx7.js has the same table): per operator, OP6
     * first, the bus it writes (bits 0-1, 0 the output), the one it reads (bits 4-5), feedback 0xc0 */
    U.DX7_ALG = [
        [0xc1, 0x11, 0x11, 0x14, 0x01, 0x14], [0x01, 0x11, 0x11, 0x14, 0xc1, 0x14], [0xc1, 0x11, 0x14, 0x01, 0x11, 0x14],
        [0xc1, 0x11, 0x94, 0x01, 0x11, 0x14], [0xc1, 0x14, 0x01, 0x14, 0x01, 0x14], [0xc1, 0x94, 0x01, 0x14, 0x01, 0x14],
        [0xc1, 0x11, 0x05, 0x14, 0x01, 0x14], [0x01, 0x11, 0xc5, 0x14, 0x01, 0x14], [0x01, 0x11, 0x05, 0x14, 0xc1, 0x14],
        [0x01, 0x05, 0x14, 0xc1, 0x11, 0x14], [0xc1, 0x05, 0x14, 0x01, 0x11, 0x14], [0x01, 0x05, 0x05, 0x14, 0xc1, 0x14],
        [0xc1, 0x05, 0x05, 0x14, 0x01, 0x14], [0xc1, 0x05, 0x11, 0x14, 0x01, 0x14], [0x01, 0x05, 0x11, 0x14, 0xc1, 0x14],
        [0xc1, 0x11, 0x02, 0x25, 0x05, 0x14], [0x01, 0x11, 0x02, 0x25, 0xc5, 0x14], [0x01, 0x11, 0x11, 0xc5, 0x05, 0x14],
        [0xc1, 0x14, 0x14, 0x01, 0x11, 0x14], [0x01, 0x05, 0x14, 0xc1, 0x14, 0x14], [0x01, 0x14, 0x14, 0xc1, 0x14, 0x14],
        [0xc1, 0x14, 0x14, 0x14, 0x01, 0x14], [0xc1, 0x14, 0x14, 0x01, 0x14, 0x04], [0xc1, 0x14, 0x14, 0x14, 0x04, 0x04],
        [0xc1, 0x14, 0x14, 0x04, 0x04, 0x04], [0xc1, 0x05, 0x14, 0x01, 0x14, 0x04], [0x01, 0x05, 0x14, 0xc1, 0x14, 0x04],
        [0x04, 0xc1, 0x11, 0x14, 0x01, 0x14], [0xc1, 0x14, 0x01, 0x14, 0x04, 0x04], [0x04, 0xc1, 0x11, 0x14, 0x04, 0x04],
        [0xc1, 0x14, 0x04, 0x04, 0x04, 0x04], [0xc4, 0x04, 0x04, 0x04, 0x04, 0x04],
    ];
    const st = { edit: false, sess: null, mv: null, page: 0, group: 0, last: -1, mark: null, marking: false, clip: null };

    const cart = () => M.cart(U.inst >> 8);
    const slot = () => U.inst & 255;
    const inst = () => M.inst(U.inst);
    const type = () => cart() ? cart().type : null;

    /* ----------------------------------------------------------- the parameters --- */
    const get = (o, path) => path.reduce((a, k) => a == null ? a : a[k], o);
    const set = (o, path, v) => { const last = path[path.length - 1]; path.slice(0, -1).reduce((a, k) => a[k], o)[last] = v; };
    const num = (n, path, min, max, fmt) => ({ n, path, min, max, fmt });
    const list = (n, path, values, labels) => ({ n, path, values, labels });
    const onoff = (n, path) => list(n, path, [false, true], ["OFF", "ON"]);
    const LEVEL = { n: "LEVEL", level: true, min: -64, max: 63.5, fmt: v => (v > 0 ? "+" : "") + v + " DB" };
    const NAME = { name: "NAME", nameRow: true, params: [] };
    const lvlGroup = { name: "LEVEL", params: [LEVEL] };
    const PAGES = {
        DX7: [
            { name: "OPERATORS", groups: [0, 1, 2, 3, 4, 5].map(i => ({ name: "OP" + (i + 1), op: i, params: [
                num("LEVEL", ["ops", i, "level"], 0, 99), num("COARSE", ["ops", i, "coarse"], 0, 31),
                num("FINE", ["ops", i, "fine"], 0, 99), num("DETUNE", ["ops", i, "detune"], -7, 7)] })) },
            { name: "ENVELOPES", groups: [0, 1, 2, 3, 4, 5].flatMap(i => [
                { name: "OP" + (i + 1) + " RATE", op: i, params: [0, 1, 2, 3].map(k => num("R" + (k + 1), ["ops", i, "eg", "rates", k], 0, 99)) },
                { name: "OP" + (i + 1) + " LVL", op: i, params: [0, 1, 2, 3].map(k => num("L" + (k + 1), ["ops", i, "eg", "levels", k], 0, 99)) }]) },
            { name: "PITCH / LFO", groups: [
                { name: "P RATE", params: [0, 1, 2, 3].map(k => num("R" + (k + 1), ["pitch_eg", "rates", k], 0, 99)) },
                { name: "P LEVEL", params: [0, 1, 2, 3].map(k => num("L" + (k + 1), ["pitch_eg", "levels", k], 0, 99)) },
                { name: "LFO", params: [num("SPEED", ["lfo", "speed"], 0, 99), num("DELAY", ["lfo", "delay"], 0, 99),
                                        num("PM DEPTH", ["lfo", "pm_depth"], 0, 99), num("AM DEPTH", ["lfo", "am_depth"], 0, 99)] },
                { name: "LFO 2", params: [list("WAVE", ["lfo", "wave"], ["triangle", "saw down", "saw up", "square", "sine", "sample&hold"],
                                               ["TRI", "SAW-", "SAW+", "SQR", "SINE", "S+H"]),
                                          onoff("SYNC", ["lfo", "key_sync"]), num("P SENS", ["lfo", "pitch_mod_sens"], 0, 7)] }] },
            { name: "VOICE", groups: [NAME,
                { name: "VOICE", params: [num("ALGORITHM", ["algorithm"], 1, 32, v => M.hex(v - 1)), num("FEEDBACK", ["feedback"], 0, 7),
                                          onoff("OSC SYNC", ["osc_key_sync"]), num("TRANSPOSE", ["transpose"], -24, 24)] }, lvlGroup] },
        ],
        JUNO: [
            { name: "DCO", groups: [
                { name: "DCO", params: [num("LFO", ["dco", "lfo"], 0, 127), num("PWM", ["dco", "pwm"], 0, 127),
                                        num("NOISE", ["dco", "noise"], 0, 127), num("SUB", ["dco", "sub"], 0, 127)] },
                { name: "WAVES", params: [list("RANGE", ["dco", "range"], ["16'", "8'", "4'"]), onoff("PULSE", ["dco", "pulse"]),
                                          onoff("SAW", ["dco", "saw"]), list("PWM BY", ["dco", "pwm_source"], ["lfo", "manual"], ["LFO", "MAN"])] }] },
            { name: "FILTERS", groups: [
                { name: "VCF", params: [num("FREQ", ["vcf", "freq"], 0, 127), num("RES", ["vcf", "res"], 0, 127),
                                        num("ENV", ["vcf", "env"], 0, 127), num("LFO", ["vcf", "lfo"], 0, 127)] },
                { name: "VCF 2", params: [num("KYBD", ["vcf", "kybd"], 0, 127), list("ENV +/-", ["vcf", "env_polarity"], ["+", "-"]),
                                          num("HPF", ["hpf"], 0, 3)] }] },
            { name: "ENVELOPE", groups: [
                { name: "ENV", params: [num("A", ["env", "a"], 0, 127), num("D", ["env", "d"], 0, 127),
                                        num("S", ["env", "s"], 0, 127), num("R", ["env", "r"], 0, 127)] },
                { name: "VCA", params: [num("LEVEL", ["vca", "level"], 0, 127), list("MODE", ["vca", "mode"], ["env", "gate"], ["ENV", "GATE"]),
                                        list("CHORUS", ["chorus"], ["off", "I", "II"], ["OFF", "I", "II"])] },
                { name: "LFO", params: [num("RATE", ["lfo", "rate"], 0, 127), num("DELAY", ["lfo", "delay"], 0, 127)] }] },
            { name: "VOICE", groups: [NAME, lvlGroup] },
        ],
        TB303: [{ name: "KNOBS", groups: [
            { name: "KNOBS", params: [num("WAVE", ["wave"], 0, 127), num("CUTOFF", ["cutoff"], 0, 127), num("RESO", ["reso"], 0, 127),
                                      num("ENV MOD", ["envmod"], 0, 127)] },
            { name: "KNOBS 2", params: [num("DECAY", ["decay"], 0, 127), num("ACCENT", ["accent"], 0, 127)] }, NAME, lvlGroup] }],
        SAMPLER: [
            { name: "SAMPLE", groups: [
                { name: "SAMPLE", params: [{ n: "SAMPLE", path: ["sample"], min: 0, max: 63, sample: true }, num("START", ["start"], 0, 1e7),
                                           num("END", ["end"], 0, 1e7)] },
                /* RELEASE is the envelope's R4 shown as a time, here by the loop: how long the tail takes to fade
                 * once the key is let go, from the quickest up to the tail's own length */
                { name: "LOOP", params: [list("LOOP", ["loop"], ["OFF", "FORWARD", "PING-PONG"], ["OFF", "FWD", "PING"]),
                                         Object.assign(num("FROM", ["loopStart"], 0, 1e7), { long: "LOOP START" }),
                                         Object.assign(num("TO", ["loopEnd"], 0, 1e7), { long: "LOOP END" }),
                                         Object.assign(num("RELEASE", ["env", "rates", 3], 0, 99), { release: true })] },
                { name: "PITCH", params: [num("BASE NOTE", ["base"], 12, 127, M.noteName), num("TUNE", ["tune"], -128, 127),
                                          num("VOLUME", ["volume"], 0, 64), LEVEL] },
                NAME] },
            { name: "ENVELOPE", groups: [
                { name: "RATES", params: [0, 1, 2, 3].map(k => num("R" + (k + 1), ["env", "rates", k], 0, 99)) },
                { name: "LEVELS", params: [0, 1, 2, 3].map(k => num("L" + (k + 1), ["env", "levels", k], 0, 99)) }] }],
        /* a FUSION slot: its pattern (SEL's EDIT PATTERN opens it in the EDITOR, ENV picks one), its name and LEVEL */
        FUSION: [{ name: "FUSION", groups: [
            { name: "PATTERN", patternRow: true, params: [{ n: "PATTERN", path: ["pattern"], pattern: true }] }, NAME, lvlGroup] }],
    };
    /* CHIP (specs/proposals/202610251559-chip-engine.md): its fields as the engine's patch (dsp-chip.js), the tables'
     * and waves' names from the engine's own (dsp-chip-tables.js); the OSC row's knobs are the oscillator's own */
    const CT = PTFM.chipTables, CHIP_KINDS = ["vol", "arp", "pitch", "duty", "noise"];
    const KNAME = { vol: "VOL", arp: "ARP", pitch: "PITCH", duty: "DUTY", noise: "NOISE" };
    const OSCN = ["PULSE", "TRIANGLE", "WAVE", "BUZZ", "NOISE"], DUTYN = ["12.5%", "25%", "50%", "75%"], BSHAPEN = ["SAW DN", "SAW UP", "TRI"];
    const CLOCKN = ["BRK", "8BIT", "PSG 1.77", "PSG 1.00", "PSG 1.79", "16BIT", "EXACT"];
    const PSGPIC = ["\\___", "\\___", "\\___", "\\___", "/___", "/___", "/___", "/___", "\\\\\\\\", "\\___", "\\/\\/", "\\```", "////",
                    "/```", "/\\/\\", "/___"];
    const upto = n => Array.from({ length: n }, (_, i) => i);
    const ch = (n, f, labels) => list(n, [f], upto(labels.length), labels);
    const tabName = (k, v) => v ? M.hex(v) + " " + CT.TAB_NAMES[k][v - 1] : "--";
    const XN = ["X1", "X2", "X3", "X4"];
    function chipOsc(p) {
        const P = [ch("OSC", "osc", OSCN)];
        if (p.osc === 0) P.push(ch("DUTY", "duty", DUTYN));
        else if (p.osc === 2) P.push(ch("WAVE", "wave", CT.WAVE_NAMES));
        else if (p.osc === 3) P.push(ch("SHAPE", "bz_shape", BSHAPEN), ch("STEPS", "bz_steps", ["16", "32"]), ch("GATE", "bz_gate", ["OFF", "ON"]));
        return { name: "OSC", cols: [[60, 8], [105, 10], [150, 7], [195, 7]], params: P };
    }
    const tableRow = (k, i) => ({ name: KNAME[k], chipRow: k, cols: [[60, 13], [156, 2]], params: [
        num("TABLE", ["tab", i], 0, CT.TAB_COUNT[i], v => tabName(k, v)), list("SPEED", ["spd", i], [1, 2, 3, 4], XN)] });
    const chipPages = p => [
        { name: "TONE", groups: [chipOsc(p),
            { name: "NOISE", params: [ch("NOISE", "n_mix", ["OFF", "ON"]), ch("MODE", "n_short", ["LONG", "SHORT"]),
                                      num("RATE", ["n_rate"], 0, 15), ch("KEY", "n_key", ["KEY OFF", "KEY ON"])] },
            { name: "CLOCK", cols: [[60, 8], [110, 8], [160, 8], [210, 5]], params: [ch("CLOCK", "clock", CLOCKN),
                ch("CURVE", "curve", ["BRK", "8BIT", "PSG", "16BIT"]), ch("FILTER", "filter", ["BRK", "8BIT", "PSG", "NONE"]),
                ch("FRAMES", "fps50", ["60 HZ", "50 HZ"])] },
            { name: "SWEEP", params: [ch("SWEEP", "sweep", ["OFF", "DOWN", "UP"]), num("SHIFT", ["sw_shift"], 0, 7), num("PACE", ["sw_pace"], 0, 7)] },
            lvlGroup, NAME] },
        { name: "VOLUME", groups: [
            { name: "VOLUME", params: [num("VOL", ["vol"], 0, 15), ch("ENV", "env", ["NONE", "DOWN", "UP", "PSG"]),
                                       num("STEP", ["env_step"], 0, 7), num("RELEASE", ["release"], 0, 15)] },
            /* (PERIOD: K2 moves its first two hex digits, FINE its last two) */
            { name: "PSG ENV", cols: [[60, 8], [110, 8], [160, 8]], params: [num("SHAPE", ["psg_shape"], 0, 15, v => M.hex(v, 1) + " " + PSGPIC[v]),
                Object.assign(num("PERIOD", ["psg_period"], 0, 65535, v => M.hex(v || 1, 4)), { byte: 1 }),   /* (0 read as 1) */
                Object.assign(num("FINE", ["psg_period"], 0, 65535, v => M.hex(v & 255)), { byte: 0 })] },
            lvlGroup] },
        { name: "TABLES", groups: [tableRow("vol", 0), tableRow("arp", 1), tableRow("pitch", 2),
            { name: "VIBRATO", chipRow: "vib", cols: [[60, 3], [86, 3], [112, 3]], params: [
                Object.assign(num("DELAY", ["vib_delay"], 0, 62, v => String(v)), { step2: true }), list("SPEED", ["vib_speed"], [1, 2, 3, 4], XN),
                Object.assign(num("DEPTH", ["vib_depth"], 0, 62, v => String(v)), { step2: true })] },
            tableRow("duty", 3), tableRow("noise", 4)] },
        { name: "WAVE", groups: [{ name: "TABLE", cols: [[60, 13]], params: [num("TABLE", ["wave"], 0, 15, v => M.hex(v) + " " + CT.WAVE_NAMES[v])] }] }];
    const pages = () => type() === "CHIP" ? chipPages(inst() ? inst().p : M.init("CHIP").p) : PAGES[type()] || [];
    const groups = () => (pages()[st.page] || { groups: [] }).groups;
    const pval = (ins, d) => d.level ? ins.level : get(ins.p, d.path);
    /* RELEASE (a sample instrument's R4 shown as a time): ms from full to -60 dB at each R4, measured on
     * the envelope (dsp-dx7.js); 0: it never fades (R4 under 7). Neighbouring rates share a time */
    const RELEASE_MS = [0, 0, 0, 0, 0, 0, 0, 118480, 94784, 94784, 78987, 67704, 67704, 59240, 59240, 47392, 39494, 39494,
        33852, 29620, 29620, 23696, 19747, 19747, 16926, 14810, 14810, 11848, 11848, 9874, 8464, 8464, 7406, 5924, 5924, 4937,
        4232, 4232, 3704, 3704, 2962, 2469, 2469, 2116, 1852, 1852, 1482, 1235, 1235, 1058, 926, 926, 742, 742, 618, 530, 530,
        463, 372, 372, 309, 266, 266, 232, 186, 186, 155, 155, 134, 116, 116, 93, 78, 78, 67, 58, 58, 46, 46, 39, 33, 33, 29,
        23, 23, 20, 17, 17, 15, 12, 12, 10, 10, 9, 7, 7, 6, 6, 6, 4];
    const msText = ms => !ms ? "ENDLESS" : ms < 1000 ? ms + " MS" : ms < 10000 ? (ms / 1000).toFixed(1) + " S" : Math.round(ms / 1000) + " S";
    /* the tail: what plays after a release at most, START (or LOOP START) to END at the base note; a
     * longer fade is never heard, so RELEASE stops there */
    function tailMs(p) {
        const s = cart().samples[p.sample], data = s && M.audio[s.audio];
        if (!data) return 0;
        const end = Math.min(p.end || data.length, data.length), from = p.loop !== "OFF" ? p.loopStart : p.start;
        return Math.max(0, end - from) / s.rate * 1000;
    }
    function releaseStep(p, s) {                         /* right: longer, to the next time there is */
        let r = p.env.rates[3];
        const tail = tailMs(p), longest = RELEASE_MS.findIndex((ms, i) => i >= 7 && ms <= Math.max(tail, 4));
        for (let n = Math.abs(s); n > 0; n--) {
            const now = RELEASE_MS[r];
            let k = r;
            do k -= Math.sign(s); while (k >= 0 && k <= 99 && RELEASE_MS[k] === now);
            if (k > 99 || (s > 0 && k < Math.max(0, longest))) break;
            r = k;
        }
        p.env.rates[3] = r;
    }
    /* a value in its 7-character cell (specs/ui/screens/instruments.md): past 7, the space before its unit
     * goes (`-12.5DB`) */
    const cell = s => (s.length > 7 ? s.replace(/ (DB|MS|S|HZ)$/, "$1") : s).slice(0, 7);
    function ptext(ins, d) {
        const v = pval(ins, d);
        if (d.release) return msText(RELEASE_MS[v]);
        if (d.sample) { const s = cart().samples[v]; return s ? M.hex(v) : "--"; }
        if (d.pattern) return v == null ? "--" : M.hex(v) + (M.pattern(v) ? "" : " MISSING");
        if (d.values) { const i = d.values.indexOf(v); return d.labels ? d.labels[i] : String(v); }
        if (d.fmt) return d.fmt(v);
        return hexOf(d, v);
    }
    /* a value without a unit, in hex (specs/ui/frame.md, Numbers): as wide as its range, signed ones
     * with their sign; sample positions as wide as they need */
    function hexOf(d, v) {
        const w = d.max >= 1e6 ? 1 : M.hex(Math.max(Math.abs(d.min), d.max)).length;
        return d.min < 0 ? M.sgn(v, w) : M.hex(v, w);
    }
    function turn(d, s, coarse) {
        const ins = inst();
        if (!ins || !d) return;
        U.edit(() => {
            if (d.level) { ins.level = U.clamp(ins.level + s * (coarse ? 6 : .5), d.min, d.max); return; }
            if (d.release) { releaseStep(ins.p, coarse ? s * 4 : s); return; }
            const v = get(ins.p, d.path);
            if (d.pattern) {                     /* the patterns there are, in order (never the EDITOR's own) */
                const ns = M.patternNums();
                ins.p.pattern = ns[U.clamp(ns.indexOf(v) + Math.sign(s), 0, ns.length - 1)];
                return;
            }
            if (d.values) { set(ins.p, d.path, d.values[U.clamp(d.values.indexOf(v) + Math.sign(s), 0, d.values.length - 1)]); return; }
            if (d.byte != null) {                /* (a 16-bit value a byte at a time, kept as the slot keeps it: 0 stays 0,
                                                  * the engine reads it as 1) */
                const sh = 8 * d.byte, b = U.clamp((v >> sh & 255) + s * (coarse ? 16 : 1), 0, 255);
                set(ins.p, d.path, (v & ~(255 << sh)) | b << sh);
                return;
            }
            if (d.step2) { set(ins.p, d.path, U.clamp(v + s * (coarse ? 32 : 2), d.min, d.max)); return; }   /* (kept in 2s) */
            let max = d.max, step = coarse ? 16 : 1;
            if (type() === "SAMPLER") {
                const smp = cart().samples[ins.p.sample];
                if (d.sample) { max = cart().samples.length - 1; step = 1; }
                else if (smp && max > 1e6) { max = smp.length; step = Math.max(1, Math.round(smp.length / 200)) * (coarse ? 10 : 1); }
            }
            if (d.path[d.path.length - 1] === "coarse" || d.max === 7 || d.max === 3) step = 1;
            const nv = U.clamp(v + s * step, d.min, max);
            set(ins.p, d.path, nv);
            if (d.sample) whole(ins.p, nv);
        });
        U.showKnob(d.long || d.n, ptext(ins, d));
        PTFM.audio.instrument(U.inst);
    }
    /* another sample: the instrument plays it whole (specs/files/instruments.md) */
    function whole(p, k) {
        const smp = cart().samples[k];
        Object.assign(p, { sample: k, start: 0, end: smp.length, loop: "OFF", loopStart: 0, loopEnd: smp.length });
    }
    function reset(d) {
        const ins = inst();
        if (!ins || !d || d.pattern) return;
        const def = M.init(type()).p;
        U.edit(() => { if (d.level) ins.level = 0; else set(ins.p, d.path, get(def, d.path) != null ? get(def, d.path) : get(ins.p, d.path)); });
        U.showKnob(d.long || d.n, ptext(ins, d) + " (DEFAULT)");
    }

    /* edit mode's session (specs/ui/controls.md): the slot as it was when editing it started, for HOME
     * to put back (the cartridge looked up each time: REVERT replaces the model's objects) */
    function keep(ref) {
        const i = ref & 255, c = () => M.cart(ref >> 8);
        if (c()) st.sess.add(ref, () => c() && c().slots[i], v => { if (c()) c().slots[i] = v; });
    }

    /* a FUSION instrument's pattern in the EDITOR, locked: HOME comes back here
     * (specs/ui/screens/instruments.md) */
    function openFusion() {                              /* (the EDITOR's own pattern's warning first) */
        const n = inst().p.pattern;
        U.ownAsk(() => { U.pattern = n; U.goDeep("EDITOR", { row: 0x3C + 12 * U.octave, track: 0, locked: true, fusion: U.inst }); });
    }

    /* ---------------------------------------------------------------- menus --- */
    function rename() {
        const ins = inst();
        if (!ins) return;                                /* (built in too: edited as any) */
        U.textEntry("INSTRUMENT " + M.refText(U.inst), ins.name, 10, B.EDIT, n => { if (n != null) U.edit(() => { ins.name = n || "NO NAME"; }); });
    }
    /* SEL, MOVE or SWAP: the instrument floats; SELECT (ALGORITHM by 8) shows it where it would go and
     * what would take its place, SEL puts it down (after the question about references), HOME takes it
     * back. MOVE goes only to empty slots: a slot is a place cells refer to, so nothing is pushed */
    function floatStep(d) {
        const c = cart(), mv = st.mv;
        let k = mv.at;
        for (let n = Math.abs(d); n > 0; n--) {
            let j = k + Math.sign(d);
            if (mv.mode === "MOVE") while (j >= 0 && j < c.size && c.slots[j] && j !== mv.from) j += Math.sign(d);
            if (j < 0 || j >= c.size) break;
            k = j;
        }
        mv.at = k;
    }
    const shown = i => {                                 /* what slot i shows while one floats */
        const c = cart(), mv = st.mv;
        if (!mv || mv.at === mv.from) return c.slots[i];
        if (i === mv.at) return c.slots[mv.from];
        if (i === mv.from) return mv.mode === "SWAP" ? c.slots[mv.at] : null;
        return c.slots[i];
    };
    function putDown() {
        const c = cart(), { mode, from, at } = st.mv, ref = c.id << 8 | from, tref = c.id << 8 | at;
        st.mv = null;
        if (at === from) return;
        U.refAsk(M.refText(ref), M.instUses(ref) + (mode === "SWAP" ? M.instUses(tref) : 0), upd => {
            const s = c.slots[from];
            c.slots[from] = mode === "SWAP" ? c.slots[at] : null;
            c.slots[at] = s;
            if (upd) remap(ref, tref, mode === "SWAP");
            U.setInst(tref);
        });
    }
    function remap(from, to, swap) {
        for (const p of M.state.patterns) if (p) for (const c of p.cells) {
            if (!c) continue;
            if (c.inst === from) c.inst = to;
            else if (swap && c.inst === to) c.inst = from;
        }
    }
    /* the SEL menu; in edit mode only what changes the edited instrument's own record (no MOVE, SWAP or
     * DELETE: they move slots and their references), and a FUSION slot's way into its pattern */
    function slotMenu() {
        const c = cart(), i = slot(), ins = inst(), ref = U.inst;
        const range = st.mark != null ? [Math.min(st.mark, i), Math.max(st.mark, i)] : [i, i];
        const fusion = st.edit && c.type === "FUSION" && ins && M.pattern(ins.p.pattern);
        /* built in (specs/ui/screens/instruments.md): the same items, edited as any */
        /* REVERT while the slot has something unsaved; with slots marked, never */
        const unsaved = M.unsavedThing("slot", [c.id, i]);
        const items = st.edit ? [fusion && "EDIT PATTERN", "COPY", "PASTE", "INIT", "RENAME", "AUTO LEVEL", unsaved && "REVERT"]
                              : [st.mark == null && "MARK", "COPY", "PASTE", "MOVE", "SWAP", "DELETE", "INIT", "RENAME", "AUTO LEVEL",
                                 st.mark == null && unsaved && "REVERT"];
        U.menu(st.edit ? "slot-edit" : "slots", st.mark != null ? "SLOTS " + M.hex(range[0]) + "-" + M.hex(range[1]) : "SLOT " + M.refText(ref), items, a => {
            st.marking = false;
            /* MARK (specs/ui/screens/instruments.md): SELECT / ALGORITHM then stretch the slots from here, SEL their menu */
            if (a === "MARK") { st.marking = true; st.mark = i; U.toast("SELECT, ALG: MARK  SEL: ITS MENU"); return; }
            if (a === "EDIT PATTERN") openFusion();
            if (a === "COPY") { st.clip = c.slots.slice(range[0], Math.min(range[1] + 1, range[0] + 4)).map(s => s && structuredClone(s)); st.clipType = c.type; U.toast("COPIED " + st.clip.length); }
            if (a === "PASTE") {
                if (!st.clip) { U.toast("NOTHING COPIED"); return; }
                /* an instrument is its engine's: it pastes only into a cartridge of the same type */
                if (st.clipType !== c.type) { U.refuse(B.SEL); U.toast("COPIED FROM " + M.TYPE_NAMES[st.clipType] + ": NOT HERE"); return; }
                U.edit(() => st.clip.forEach((s, k) => { if (i + k < c.size) c.slots[i + k] = s && structuredClone(s); }));
            }
            if (a === "INIT") U.edit(() => { for (let k = range[0]; k <= range[1]; k++) c.slots[k] = M.init(c.type); });
            if (a === "RENAME") rename();
            if (a === "AUTO LEVEL" && ins) {
                const lv = Math.round((Math.random() * 6 - 4) * 2) / 2;
                U.progress("MEASURING " + ins.name, 700, () => { U.edit(() => { ins.level = lv; }); U.toast("LEVEL SET TO " + lv + " DB"); });
            }
            /* REVERT (specs/ui/saving.md#revert): the slot as saved, its sound so from its next note */
            if (a === "REVERT") U.askRevert("REVERT SLOT " + M.refText(ref) + " TO SAVED?", null, () => {
                M.revertThing("slot", [c.id, i]);
                PTFM.audio.instrument(ref);
            });
            if (a === "DELETE") {                        /* (marked slots: their uses together) */
                const refs = [];
                for (let k = range[0]; k <= range[1]; k++) if (c.slots[k]) refs.push(c.id << 8 | k);
                const uses = refs.reduce((n, r) => n + M.instUses(r), 0);
                U.refAsk(range[0] === range[1] ? M.refText(ref) : "SLOTS " + M.hex(range[0]) + "-" + M.hex(range[1]), uses, upd => {
                    for (let k = range[0]; k <= range[1]; k++) c.slots[k] = null;
                    if (upd) for (const p of M.state.patterns) if (p) for (const cl of p.cells) if (cl && refs.includes(cl.inst)) cl.inst = null;
                });
            }
            if ((a === "MOVE" || a === "SWAP") && ins) {
                if (a === "MOVE" && !c.slots.some(x => !x)) { U.toast("NO EMPTY SLOT: SWAP INSTEAD"); return; }
                st.mv = { mode: a, from: i, at: i };
                U.toast("SELECT MOVES IT, SEL PUTS IT DOWN");
            }
            st.mark = null;
        });
    }
    function cartList() {
        return U.cartList("CARTRIDGES", U.inst >> 8, c => U.setInst(c.id << 8 | Math.max(0, c.slots.findIndex(Boolean))), cartMenu);
    }
    /* a cartridge's menu (specs/ui/screens/instruments.md): NEW, CLONE, MOVE, DELETE, RENAME, AUTO LEVEL ALL; a
     * built-in one, edited as any, NEW, CLONE, RENAME, AUTO LEVEL ALL, and once changed BACK TO BUILT-IN (the change
     * dropped). NEW is a group: the new cartridge's type */
    const TYPES = ["DX7", "JUNO-106", "TB-303", "CHIP", "SAMPLER", "FUSION"];
    /* a sampler's samples' audio (those not deleted): what a CLONE or MOVE of it copies, 0 none */
    const sampleBytes = c => c.type !== "SAMPLER" ? 0 : (c.samples || []).reduce((n, s) => n + (s && !s.deleted ? M.bytes.sample(s) : 0), 0);
    function cartMenu(list) {
        const c = M.state.carts[list.sel], label = "CARTRIDGE " + M.hex(c.id);
        const NEW = { group: "NEW", items: TYPES };
        const items = c.builtin ? [NEW, "CLONE", "RENAME", "AUTO LEVEL ALL", M.builtinChanged("cart", c) && "BACK TO BUILT-IN"]
                    : [NEW, "CLONE", "MOVE", "DELETE", "RENAME", "AUTO LEVEL ALL"];
        if (M.unsavedThing("cart", c.id)) items.push("REVERT");          /* (last, while the cartridge has something unsaved) */
        U.menu("carts", label, items, a => {
            /* a new cartridge's number: after the last of the user's, so it ends the user's part of the list
             * (the first free one once EF is taken) */
            if (TYPES.includes(a)) {
                const ty = { "JUNO-106": "JUNO", "TB-303": "TB303" }[a] || a, id = M.freeCart();
                if (id < 0) { U.toast("ALL CARTRIDGE NUMBERS USED"); return; }
                U.edit(() => {
                    const nc = { id, name: "NEW " + a, type: ty, size: M.SIZES[ty], slots: new Array(M.SIZES[ty]).fill(null) };
                    if (ty === "SAMPLER") nc.samples = [];
                    M.state.carts.push(nc);
                    M.state.carts.sort((x, y) => x.id - y.id);
                });
                list.sel = M.cartIndex(id);
                U.toast("CARTRIDGE " + M.hex(id) + " MADE");
            }
            if (a === "CLONE") {                             /* an editable copy in the user area */
                const id = M.freeCart();
                if (id < 0) { U.toast("ALL CARTRIDGE NUMBERS USED"); return; }
                const nc = structuredClone(c); nc.id = id; nc.name = (c.name.slice(0, 9) + " 2").trim();
                delete nc.rom; delete nc.builtin;
                const put = st => { st.carts.push(structuredClone(nc)); st.carts.sort((x, y) => x.id - y.id); };
                const done = () => { list.sel = M.cartIndex(id); U.toast(M.hex(c.id) + " CLONED TO " + M.hex(id)); };
                /* a sampler with samples: its audio cannot wait among the edits, so the copy is saved at once
                 * (specs/ui/screens/instruments.md) */
                const sb = sampleBytes(c);
                if (!sb) { U.edit(() => put(M.state)); done(); return; }
                if (sb + M.bytes.cart(c) > M.storage().userFree) { U.toast("NO ROOM IN THE USER AREA"); return; }
                PTFM.player.pauseAll();
                U.progress("CLONING ITS SAMPLES", 400 + Math.min(1600, Math.round(sb / 64)), () => {
                    M.commit(put); done(); PTFM.audio.changed();
                });
            }
            if (a === "BACK TO BUILT-IN") U.edit(() => {         /* (an edit: REVERT brings the change back) */
                M.state.carts[M.cartIndex(c.id)] = M.builtinOriginal("cart", c.id);
                U.toast(label + " BUILT-IN AGAIN");
            });
            /* REVERT (specs/ui/saving.md#revert): the whole cartridge as saved, every slot and its name (a cartridge
             * made anew goes) */
            if (a === "REVERT") U.askRevert("REVERT CARTRIDGE " + M.hex(c.id) + " TO SAVED?", null, () => {
                const id = c.id;
                M.revertThing("cart", id);
                list.sel = U.clamp(M.cartIndex(id) >= 0 ? M.cartIndex(id) : list.sel, 0, M.state.carts.length - 1);
                if (!M.cart(U.inst >> 8)) U.setInst(M.state.carts[list.sel].id << 8 | Math.max(0, M.state.carts[list.sel].slots.findIndex(Boolean)));
                else PTFM.audio.instrument(U.inst);
            });
            if (a === "RENAME") U.textEntry("CARTRIDGE " + M.hex(c.id), c.name, 12, B.EDIT, n => { if (n != null) U.edit(() => { c.name = n || "NO NAME"; }); });
            if (a === "DELETE") {
                let uses = 0;
                for (let k = 0; k < c.size; k++) if (c.slots[k]) uses += M.instUses(c.id << 8 | k);
                U.refAsk("CARTRIDGE " + M.hex(c.id), uses, upd => {
                    M.state.carts.splice(M.state.carts.indexOf(c), 1);
                    if (upd) for (const p of M.state.patterns) if (p) for (const cl of p.cells) if (cl && cl.inst >> 8 === c.id) cl.inst = null;
                    list.sel = U.clamp(list.sel, 0, M.state.carts.length - 1);
                    if (!M.cart(U.inst >> 8)) U.setInst(M.state.carts[0].id << 8);
                });
            }
            if (a === "MOVE") list.float(c.id, to => {        /* afloat in the list; SEL puts it down here */
                let uses = 0;
                for (let k = 0; k < c.size; k++) if (c.slots[k]) uses += M.instUses(c.id << 8 | k);
                const old = c.id, refs = upd => {
                    if (upd) for (const p of M.state.patterns) if (p) for (const cl of p.cells) if (cl && cl.inst >> 8 === old) cl.inst = to << 8 | (cl.inst & 255);
                    if (U.inst >> 8 === old) U.inst = to << 8 | (U.inst & 255);
                    list.sel = M.cartIndex(to);
                };
                const sb = sampleBytes(c);
                if (!sb) {
                    U.refAsk("CARTRIDGE " + M.hex(c.id), uses, upd => { c.id = to; M.state.carts.sort((x, y) => x.id - y.id); refs(upd); });
                    return;
                }
                /* a sampler with samples moves in the flash at once (its audio cannot wait among the edits): its
                 * changes saved first; then the references, saved as a MOVE's always are */
                if (M.unsavedThing("cart", old) || (c.samples || []).some((s, k) => M.unsavedThing("sample", [old, k]))) {
                    U.toast("SAVE IT FIRST"); return;
                }
                const go = upd => {
                    if (sb + M.bytes.cart(c) > M.storage().userFree) { U.toast("NO ROOM IN THE USER AREA"); return; }
                    PTFM.player.pauseAll();
                    U.progress("MOVING ITS SAMPLES", 400 + Math.min(1600, Math.round(sb / 64)), () => {
                        M.commit(st => { st.carts.find(x => x.id === old).id = to; st.carts.sort((x, y) => x.id - y.id); });
                        refs(upd);
                        if (upd) { M.version++; U.saveRefs(uses); }
                        PTFM.audio.changed();
                    });
                };
                if (!uses) { go(false); return; }
                U.ask(["CARTRIDGE " + M.hex(old) + " IS USED " + uses + " TIME" + (uses === 1 ? "" : "S"), "UPDATE REFERENCES?"],
                      ["YES", "NO"], a2 => { if (a2 === "YES") go(true); else if (a2 === "NO") go(false); });
            });
            /* AUTO LEVEL ALL asks first: it changes every slot's LEVEL at once */
            if (a === "AUTO LEVEL ALL") {
                const n = c.slots.filter(Boolean).length, sounds = n + (n === 1 ? " SOUND" : " SOUNDS");
                U.ask(["AUTO LEVEL " + sounds + "?", "EACH ONE'S LEVEL SET AGAIN"], ["LEVEL", "KEEP"], b => {
                    if (b === "LEVEL") U.progress("MEASURING " + sounds, 1500, () => U.toast("LEVELS SET"));
                });
            }
        });
    }
    /* what ENV opens in edit mode: the samples on SAMPLE, the pattern list on a FUSION slot's PATTERN */
    function envList() {
        const g = st.edit && inst() ? groups()[st.group] : null;
        if (g && cart().type === "SAMPLER" && g.name === "SAMPLE" && st.page === 0) return "samples";
        if (g && g.patternRow) return "patterns";
        return null;
    }
    function sampleList() {
        const c = cart(), ins = inst();
        const o = U.open({
            kind: "list", title: "SAMPLES OF " + M.hex(c.id) + " " + c.name, opener: B.ENV, sel: ins.p.sample,
            knob(e, s) { if (e === E.SELECT) this.sel = U.clamp(this.sel + s, 0, c.samples.length - 1); },
            tap(b) {
                if (b === B.ENV) {
                    U.close(this);
                    U.edit(() => whole(ins.p, this.sel));
                    PTFM.audio.instrument(U.inst);
                    return true;
                }
                if (b === B.SEL) {
                    /* (specs/ui/screens/instruments.md) DELETE alone; a deleted sample opens nothing */
                    const k = this.sel, smp = c.samples[k], list = this;
                    if (smp.deleted) return true;
                    U.menu("samples", "SAMPLE " + M.hex(k), ["DELETE"], () => {
                        /* the instruments that use it go with it, after a warning; its index stays empty */
                        const users = c.slots.map((s, j) => s && s.p.sample === k ? j : -1).filter(j => j >= 0);
                        const del = () => {
                            U.edit(() => {
                                c.samples[k] = Object.assign({}, smp, { name: "(DELETED)", length: 0, deleted: true });
                                users.forEach(j => { c.slots[j] = null; });
                            });
                            U.toast("SAMPLE " + M.hex(k) + " DELETED");
                            if (users.includes(slot())) { U.close(list); st.edit = false; st.sess = null; }   /* its instrument went too */
                        };
                        if (!users.length) { del(); return; }
                        U.ask(["DELETE SAMPLE?", users.length + " INSTRUMENT" + (users.length === 1 ? " USES IT AND IS" : "S USE IT AND ARE"), "DELETED TOO"],
                              ["DELETE", "KEEP"], a => { if (a === "DELETE") del(); });
                    });
                    return true;
                }
                return false;
            },
            key(n, down) {                       /* the keys play the highlighted sample */
                if (down) {
                    const ref = U.inst, keep = structuredClone(ins.p);
                    whole(ins.p, this.sel);
                    M.version++;
                    U.keyOn(n, ref);
                    ins.p = keep;
                    M.version++;
                } else U.keyOff(n);
                return true;
            },
            draw() {
                const y = Y0 + 2;
                U.panel(2, y, 236, 206, this.title.slice(0, 37));
                const top = Math.max(0, Math.min(this.sel - 6, c.samples.length - 14));
                for (let i = top; i < Math.min(c.samples.length, top + 14); i++) {
                    const s = c.samples[i], yy = y + 18 + (i - top) * 13, on = i === this.sel;
                    if (on) D.box(4, yy - 1, 232, 13, D.WHITE);
                    D.text(8, yy, F.T, M.hex(i), D.DGREY);
                    D.text(24, yy, F.T, s.name.slice(0, 12), s.deleted ? D.RED : on ? D.WHITE : D.YELLOW);
                    D.text(100, yy, F.T, (s.length / s.rate).toFixed(2) + "S", D.LGREY);
                    D.text(140, yy, F.T, String(s.rate), D.DGREY);              /* the rate, Hz */
                    wave(M.audio[s.audio], 176, yy, 56, 10, s.deleted ? D.DGREY : D.BROWN);
                }
            },
        });
        return o;
    }

    /* ------------------------------------------------------------- pictures --- */
    /* where the voices playing a sample are in it: a mark each (yellow), for wave() */
    const playing = (cartId, sample) => (PTFM.audio.stats.voices || [])
        .filter(v => v.engine === "SAMPLER" && v.key === cartId + ":" + sample && v.pos != null).map(v => [v.pos, D.YELLOW]);
    function wave(data, x, y, w, h, col, mk) {
        const n = data.length, mid = y + h / 2;
        for (let i = 0; i < w; i++) {
            const a = Math.floor(n * i / w), b = Math.max(a + 1, Math.floor(n * (i + 1) / w));
            let lo = 0, hi = 0;
            for (let k = a; k < b; k += Math.max(1, ((b - a) / 16) | 0)) { const v = data[k] / 128; if (v < lo) lo = v; if (v > hi) hi = v; }
            D.vline(x + i, Math.round(mid - hi * h / 2), Math.max(1, Math.round((hi - lo) * h / 2)), col);
        }
        if (mk) for (const [pos, c] of mk) if (pos >= 0 && pos <= n) D.vline(x + Math.round(pos / n * (w - 1)), y, h, c);
    }
    /* a sample instrument's envelope over its sample, as the firmware draws it (scr_instr.c env_trace): a note at the
     * BASE NOTE on the sampler's voice as the firmware runs it (samp.c: its envelope as a DX7 operator's, MSFA's, a
     * step a 64-frame block from silence; its place in the sample in Q16 steps), drawn by place and gain. With a loop
     * the key is held while the loop comes round, until a pass enters it with R1-R3 done, and let go just before that
     * pass reaches LOOP END (the release from LOOP END to END); without one it is held to END; past END the same note
     * goes on as if END were the sample's end: the ghost, dotted */
    const SV = (() => {
        const SEMI = [...Array(12)].map((_, n) => Math.round(32768 * Math.pow(2, n / 12)));
        const FINE = [...Array(64)].map((_, j) => Math.round(32768 * Math.pow(2, j / 768)));
        const ATT = [...Array(64)].map((_, j) => Math.round(32768 * Math.pow(2, -j / 64)));
        const LUT = [0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46];
        const STATICS = [1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150,
            502740, 441000, 418950, 352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530, 145530, 125685, 110250,
            110250, 88200, 88200, 74970, 61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562, 22050, 18522,
            17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717, 6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690,
            2249, 1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837, 837, 705, 573, 573, 529, 441, 441];
        const FULL = 15 << 24, SILENT = 99 << 21, MOST = 16 << 16, N = 64;
        const inc = r => { const q = Math.min(63, (r * 41) >> 6); return (4 + (q & 3)) << (8 + (q >> 2)); };
        const gain = a => a >= SILENT ? 0 : ((32768 >> (Math.max(0, a) >> 24)) * ATT[(Math.max(0, a) >> 18) & 63]) >> 15;
        function stage(v, s) {
            v.stage = s;
            if (s > 3) return;
            const l = v.env[4 + s], r = v.env[s], a = ((l >= 20 ? 28 + l : LUT[l]) >> 1) * 64 + (127 << 5) - 4256;
            v.target = Math.max(16, a) << 16;
            v.rising = v.target > v.level;
            if (v.target === v.level || (s === 0 && l === 0)) {
                v.statics = r < 77 ? STATICS[r] : 20 * (99 - r);
                if (r < 77 && s === 0 && l === 0) v.statics = Math.trunc(v.statics / 20);
            } else v.statics = 0;
        }
        function envBlock(v) {
            if (v.statics) { v.statics -= N; if (v.statics <= 0) { v.statics = 0; stage(v, v.stage + 1); } }
            if (!(v.stage < 3 || (v.stage === 3 && !v.down)) || v.statics) return;
            const i = inc(v.env[v.stage]);
            if (v.rising) {
                if (v.level < (1716 << 16)) v.level = 1716 << 16;
                v.level += (((17 << 24) - v.level) >> 24) * i;
                if (v.level >= v.target) { v.level = v.target; stage(v, v.stage + 1); }
            } else {
                v.level -= i;
                if (v.level <= v.target) { v.level = v.target; stage(v, v.stage + 1); }
            }
        }
        function noteOn(s) {                      /* s: length, rate, start, end, loopStart, loopEnd, loop (0 off, 1, 2), base, tune, env */
            const v = { env: s.env.map(x => Math.min(99, x)), ls: s.loopStart, le: s.loopEnd, end: s.end, loop: s.loop };
            if (v.end <= 0 || v.end > s.length) v.end = s.length;
            if (v.le > v.end) v.le = v.end;
            const n = Math.trunc(s.tune / 2), oct = n >= 0 ? Math.floor(n / 768) : -Math.floor((767 - n) / 768), r = n - 768 * oct;
            const q = Math.floor(s.rate / 44100);
            let st = q * 65536 + Math.floor((s.rate - q * 44100) * 65536 / 44100);
            st = Math.floor(st * SEMI[r >> 6] / 32768);
            st = Math.floor(st * FINE[r & 63] / 32768);
            st = oct >= 0 ? (oct < 8 ? st * 2 ** oct : MOST) : (oct > -24 ? Math.floor(st / 2 ** -oct) : 0);
            v.step = st > MOST ? MOST : st || 1;
            v.pos = Math.min(s.start, v.end - 1);
            v.frac = 0; v.dir = 1;
            v.looping = v.loop !== 0 && v.le > v.ls + 1 && v.ls >= 0;
            v.on = v.end > 0 && v.pos >= 0;
            v.level = 0; v.down = true;
            stage(v, 0);
            return v;
        }
        function skip(v) {                        /* (samp_skip) */
            envBlock(v);
            for (let i = 0; i < N; i++) {
                if (v.dir > 0) { const f = v.frac + v.step; v.pos += Math.floor(f / 65536); v.frac = f % 65536; }
                else {
                    let f = v.frac - (v.step & 0xFFFF);
                    v.pos -= Math.floor(v.step / 65536);
                    if (f < 0) { f += 65536; v.pos--; }
                    v.frac = f;
                }
                if (v.looping) {
                    if (v.loop === 2) {
                        if (v.dir > 0 && v.pos >= v.le) {
                            if (!v.down) v.looping = false;
                            else { v.pos = 2 * v.le - 1 - v.pos; v.frac = 65535 - v.frac; v.dir = -1; }
                        } else if (v.dir < 0 && v.pos < v.ls) {
                            v.pos = 2 * v.ls - 1 - v.pos; v.frac = 65535 - v.frac; v.dir = 1;
                            if (!v.down) v.looping = false;
                        }
                    } else if (v.pos >= v.le) {
                        if (!v.down) v.looping = false;
                        else v.pos -= v.le - v.ls;
                    }
                }
                if (v.pos < 0 || (v.pos >= v.end - 1 && !v.looping)) { v.on = false; return; }
            }
        }
        return { noteOn, skip, off: v => { if (v.on && v.down) { v.down = false; stage(v, 3); } }, gain: v => gain(FULL - v.level), N };
    })();
    function traceLine(x0, y0, x1, y1, k, dotted) {     /* (into the kept trace: k its kind + 1) */
        const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, shallow = dx >= -dy;
        let e = dx + dy;
        for (;;) {
            if ((!dotted || !((shallow ? x0 : y0) & 1)) && x0 >= 0 && x0 < TW && y0 >= 0 && y0 < TH) trace.map[y0 * TW + x0] = k;
            if (x0 === x1 && y0 === y1) break;
            const e2 = 2 * e;
            if (e2 >= dy) { e += dy; x0 += sx; }
            if (e2 <= dx) { e += dx; y0 += sy; }
        }
    }
    /* the last trace, kept as its pixels (0 none, 1 heard, 2 a pass while the attack rises, 3 past END) and what it was
     * traced from: traced again only when that changes (a long sample's trace takes up to 41,344 blocks) */
    const TW = 240, TH = 34, trace = { key: null, map: new Uint8Array(TW * TH) };
    function envTrace(p, smp, x, y, col) {
        const len = smp.length, end = Math.min(p.end || len, len);
        const s = { length: len, rate: smp.rate || 44100, start: p.start || 0, end: len, loopStart: p.loopStart, loopEnd: Math.min(p.loopEnd, end),
                    loop: ["OFF", "FORWARD", "PING-PONG"].indexOf(p.loop), base: p.base, tune: p.tune || 0,
                    env: [...p.env.rates, ...p.env.levels] }, key = JSON.stringify([s, end]);
        if (trace.key !== key) { traceRun(s, end); trace.key = key; }
        for (let i = 0; i < TW * TH; i++) if (trace.map[i]) D.px(x + i % TW, y + Math.floor(i / TW), trace.map[i] === 2 ? D.DGREY : col);
    }
    function traceRun(s, end) {
        const len = s.length, map = trace.map, v = SV.noteOn(s);
        map.fill(0);
        if (!v.on) return;
        let px = 0, py = 0;
        const pt = (pos, g, kind, brk) => {      /* kind: 0 heard, 1 a pass while the attack rises, 2 past END */
            const X = Math.floor((pos * (TW - 1) * 2 + len) / (2 * len)), Y = TH - 1 - Math.floor((g * (TH - 1) + 16384) / 32768);
            if (!brk) traceLine(px, py, X, Y, kind + 1, kind === 2);
            px = X; py = Y;
        };
        let pass = 0, last = false, brk = true;
        pt(v.pos, 0, 0, true);
        for (let b = 0; b < 41344 && v.on; b++) {
            if (v.looping && v.down && last && v.dir > 0 && v.pos + Math.floor((v.frac + SV.N * v.step) / 65536) >= v.le) SV.off(v);
            const was = v.pos, wdir = v.dir, wloop = v.looping;
            SV.skip(v);
            const g = SV.gain(v);
            if (!pass && wloop && wdir > 0 && v.dir > 0 && was < v.ls && v.pos >= v.ls) last = v.stage >= 3;
            if (wloop && ((v.loop === 1 && v.pos < was) || (v.loop === 2 && wdir < 0 && v.dir > 0))) { pass++; last = v.stage >= 3; brk = v.loop === 1; }
            if (!v.on) break;
            pt(v.pos, g, !v.looping && v.pos >= end - 1 ? 2 : pass === 0 || last ? 0 : 1, brk);
            brk = false;
            if (!v.down && !g) break;
        }
    }
    /* a sample instrument's picture, the same on both edit pages: its waveform with the markers, and under it on the
     * same x its envelope over the region the instrument plays, its ghost past END */
    function sampPicture(p, smp, cartId, y, col) {
        const legend = 213, he = TH, hw = (legend - 2 - y - he - 4) & ~1, ye = y + hw + 4, len = smp.length;
        const mk = [[p.start, D.WHITE], [p.end, D.WHITE], ...(p.loop !== "OFF" ? [[p.loopStart, D.GREEN], [p.loopEnd, D.GREEN]] : [])];
        wave(M.audio[smp.audio], 0, y, 240, hw, D.BROWN, [...mk, ...playing(cartId, p.sample)]);
        if (len) {
            D.hline(0, ye + he - 1, 240, D.DGREY);                /* (gain 0) */
            for (const [pos, c] of mk) if (pos >= 0 && pos <= len) D.vline(Math.floor((pos * 239 * 2 + len) / (2 * len)), ye, he, c);
            envTrace(p, smp, 0, ye, col);
        }
        D.text(0, legend, F.T, "WHITE START/END  GREEN LOOP  YELLOW PLAY", D.DGREY);
    }
    function knob(x, y, r, v, label, col) {
        D.circle(x, y, r, D.DGREY);
        const a = (-135 + v / 127 * 270) * Math.PI / 180;
        D.line(x, y, x + Math.round(Math.sin(a) * (r - 2)), y - Math.round(Math.cos(a) * (r - 2)), col);
        D.text(D.centre(F.T, label, x - 20, 40), y + r + 3, F.T, label, D.CYAN);
        D.text(D.centre(F.T, M.hex(v), x - 20, 40), y + r + 15, F.T, M.hex(v), D.WHITE);
    }
    /* a DX7 algorithm: carriers along the bottom, each modulator above what it modulates */
    function algorithm(alg, x0, y0, w, h, selOp) {
        const flags = PTFM.ui.DX7_ALG[alg - 1], tgt = [], kids = [[], [], [], [], [], []];
        for (let i = 0; i < 6; i++) {
            const ob = flags[i] & 3;
            tgt[i] = -1;
            if (ob) for (let j = i + 1; j < 6; j++) if (((flags[j] >> 4) & 3) === ob) { tgt[i] = j; break; }
            if (tgt[i] >= 0) kids[tgt[i]].push(i);
        }
        const pos = [], colsOf = i => kids[i].length ? kids[i].reduce((a, k) => a + colsOf(k), 0) : 1;
        let col = 0;
        const place = (i, depth) => {
            const span = colsOf(i), c0 = col;
            if (kids[i].length) for (const k of kids[i].slice().reverse()) place(k, depth + 1); else col++;
            pos[i] = { c: c0 + (span - 1) / 2, d: depth };
        };
        for (let i = 5; i >= 0; i--) if (tgt[i] < 0 && !(flags[i] & 3)) place(i, 0);
        const ncol = Math.max(1, col), depth = Math.max(...pos.filter(Boolean).map(p => p.d)) + 1;
        const cw = Math.min(26, Math.floor(w / ncol)), rh = Math.min(22, Math.floor(h / depth));
        const xy = i => [x0 + Math.round((pos[i].c + .5) * cw), y0 + h - 12 - pos[i].d * rh];
        for (let i = 0; i < 6; i++) if (tgt[i] >= 0 && pos[i] && pos[tgt[i]]) { const [a, b] = xy(i), [c, d] = xy(tgt[i]); D.line(a, b + 6, c, d - 5, D.DGREY); }
        for (let i = 0; i < 6; i++) {
            if (!pos[i]) continue;
            const [x, y] = xy(i), op = 6 - i, carrier = !(flags[i] & 3);
            D.rect(x - 7, y - 6, 15, 13, op - 1 === selOp ? D.DEEP + D.LRED : D.BLACK);
            D.box(x - 7, y - 6, 15, 13, carrier ? D.LRED : D.LGREY);
            D.text(x - 2, y - 5, F.T, String(op), op - 1 === selOp ? D.WHITE : carrier ? D.LRED : D.LGREY);
            if ((flags[i] & 0xc0) === 0xc0) { D.hline(x + 8, y - 3, 3, D.YELLOW); D.vline(x + 10, y - 9, 6, D.YELLOW); D.hline(x - 1, y - 9, 11, D.YELLOW); }
            if (carrier) D.vline(x, y + 7, 3, D.LRED);
        }
        D.hline(x0 + 4, y0 + h - 1, ncol * cw - 8, D.LRED);
    }
    function dxEnv(op, x, y, w, h, col) {
        const r = op.eg.rates, l = op.eg.levels, dur = k => Math.max(2, (100 - r[k]) * .55);
        const pts = [[0, 0]];
        let t = 0;
        for (let k = 0; k < 3; k++) { t += dur(k); pts.push([t, l[k]]); }
        t += 14; pts.push([t, l[2]]);
        t += dur(3); pts.push([t, l[3]]);
        const sx = w / t;
        for (let k = 1; k < pts.length; k++) D.line(x + pts[k - 1][0] * sx, y + h - pts[k - 1][1] / 99 * h, x + pts[k][0] * sx, y + h - pts[k][1] / 99 * h, col);
        D.vline(x + Math.round((t - dur(3) - 14) * sx), y, h, D.DGREY);
    }
    function adsr(e, x, y, w, h, col) {
        const a = .5 + e.a / 127 * 2, d = .5 + e.d / 127 * 3, s = e.s / 127, r = .5 + e.r / 127 * 3, tot = a + d + 2 + r, sx = w / tot;
        D.line(x, y + h, x + a * sx, y, col);
        let px = x + a * sx, py = y;
        for (let k = 1; k <= 12; k++) { const nx = x + (a + d * k / 12) * sx, ny = y + h - (s + (1 - s) * Math.exp(-k / 3)) * h; D.line(px, py, nx, ny, col); px = nx; py = ny; }
        D.line(px, py, x + (a + d + 2) * sx, y + h - s * h, col);
        px = x + (a + d + 2) * sx; py = y + h - s * h;
        for (let k = 1; k <= 12; k++) { const nx = x + (a + d + 2 + r * k / 12) * sx, ny = y + h - s * Math.exp(-k / 2.5) * h; D.line(px, py, nx, ny, col); px = nx; py = ny; }
    }
    function filterCurve(freq, res, x, y, w, h, col) {
        const fc = 20 * Math.pow(2, freq / 127 * 10), q = .7 + res / 127 * 10;
        let py = null;
        for (let i = 0; i < w; i++) {
            const f = 20 * Math.pow(1000, i / w), r = f / fc;
            const mag = 1 / Math.sqrt(Math.pow(1 - r * r, 2) + Math.pow(r / q, 2)) / Math.max(1, r * r);
            const db = Math.max(-36, Math.min(18, 20 * Math.log10(mag)));
            const yy = Math.round(y + h * (18 - db) / 54);
            if (py != null) D.line(x + i - 1, py, x + i, yy, col);
            py = yy;
        }
        D.hline(x, Math.round(y + h * 18 / 54), w, D.DGREY);
    }
    /* the TB-303's pictures (specs/ui/screens/instruments.md#the-tb-303s-pictures): the engine's law (dsp-synth.js
     * tbDerive / tbControl, Open303's measured mapping). The cutoff t ms into a note, accented or not, log2 of Hz */
    const TB_L0 = Math.log2(200), TB_L1 = Math.log2(10000);
    function tbCutoff(p, t, acc) {
        const cn = p.cutoff / 127, en = p.envmod / 127, dec = acc ? 200 : 200 * Math.pow(10, p.decay / 127);
        const sc = (1 - cn) * (3.774 * en + .737) + cn * (4.195 * en + .864), off = .0483 * cn + .2944;
        let l = Math.log2(314) + cn * Math.log2(2394 / 314) + sc * (Math.exp(-t / dec) - off);
        if (acc) {                                      /* (the accent: the envelope through a 15 ms RC on top) */ const A = 1 / 15, B = 1 / dec; l += p.accent / 127 * A / (A - B) * (Math.exp(-B * t) - Math.exp(-A * t)); }
        return Math.max(TB_L0, Math.min(TB_L1, l));
    }
    const tbSpan = p => Math.max(300, Math.min(2500, Math.round(1.6 * 200 * Math.pow(10, p.decay / 127))));
    function tbEnvelope(p, x, y, w, h, col) {
        const yOf = l => Math.round(y + h - 1 - (l - TB_L0) / (TB_L1 - TB_L0) * (h - 1)), T = tbSpan(p);
        for (let i = 0; i < w; i++) {
            const t = i * T / (w - 1), yy = yOf(tbCutoff(p, t, false));
            D.vline(x + i, yy + 1, y + h - yy - 1, D.DEEP + col);
            D.px(x + i, yy, col);
        }
        const rest = yOf(tbCutoff(p, 1e9, false));
        for (let i = 0; i < w; i += 3) D.px(x + i, rest, D.DGREY);          /* CUTOFF, where it comes to rest */
        for (let i = 0; i < w; i += 2) D.px(x + i, yOf(tbCutoff(p, i * T / (w - 1), true)), D.YELLOW);   /* (an accented note) */
        const s = T < 1000 ? T + " MS" : (Math.round(T / 100) / 10).toFixed(1) + " S";
        D.text(x, y + h + 2, F.T, "0", D.DGREY);
        D.text(x + w - D.tw(F.T, s), y + h + 2, F.T, s, D.DGREY);
    }
    function tbFilter(p, x, y, w, h, col) {
        const q = .7 + p.reso / 127 * 7, ydb = db => Math.round(y + (18 - Math.max(-36, Math.min(18, db))) / 54 * (h - 1));
        const resp = (f, fc) => { const r = f / fc; return -20 * Math.log10(Math.pow(1 - r * r, 2) + Math.pow(r / q, 2)); };
        const fAt = i => 20 * Math.pow(1000, i / (w - 1)), xOf = f => Math.round(Math.log(f / 20) / Math.log(1000) * (w - 1));
        const fr = Math.pow(2, tbCutoff(p, 1e9, false)), ft = Math.pow(2, tbCutoff(p, 2, false));
        D.hline(x, ydb(0), w, D.DGREY);
        for (let i = 0; i < w; i++) {
            const yy = ydb(resp(fAt(i), fr));
            D.vline(x + i, yy + 1, y + h - yy - 1, D.DEEP + col);
            D.px(x + i, yy, col);
            if (i % 2 === 0) D.px(x + i, ydb(resp(fAt(i), ft)), D.LGREY);   /* (at the note's top) */
        }
        D.vline(x + xOf(fr), y, h, col);
        for (let yy = y; yy < y + h; yy += 2) D.px(x + xOf(ft), yy, D.LGREY);
    }
    function tbWave(v, x, y, col) {                     /* a cycle, 14 x 8: the saw and the square mixed by WAVE */
        const b = v / 127;
        let py = null;
        for (let i = 0; i < 14; i++) {
            const ph = i / 14, s = (1 - b) * (1 - 2 * ph) + b * (ph < .5 ? 1 : -1), yy = Math.round(y + 4 - s * 3.5);
            if (py != null && Math.abs(py - yy) > 1) D.vline(x + i, Math.min(py, yy), Math.abs(py - yy), col); else D.px(x + i, yy, col);
            py = yy;
        }
    }
    function fusionMap(p, x, y, w, h) {
        if (!p) { D.text(x, y + 20, F.T, "NO PATTERN: EDIT MAKES ONE", D.DGREY); return; }
        const rh = Math.max(2, Math.min(12, Math.floor((h - 16) / p.tracks)));
        const k0 = 0x35 + 12 * U.octave, k1 = 0x4F + 12 * U.octave;
        D.rect(x + Math.round(k0 / 128 * w), y, Math.round((k1 - k0 + 1) / 128 * w), p.tracks * rh, D.DEEP + D.BLUE);
        for (let r = 0; r < Math.min(128, p.rows); r++) for (let t = 0; t < p.tracks; t++) {
            const c = p.cells[r * 16 + t];
            if (!c || c.note == null || c.note === M.OFF) continue;
            D.rect(x + Math.floor(r / 128 * w), y + t * rh, Math.max(1, Math.ceil(w / 128)), rh - 1,
                   M.instCol(c.inst != null ? c.inst : M.trackInst(p, r, t)));
        }
        D.vline(x + Math.round(0x3C / 128 * w), y - 2, p.tracks * rh + 4, D.WHITE);
        D.text(x + Math.round(0x3C / 128 * w) - 8, y + p.tracks * rh + 3, F.T, "C-4", D.WHITE);
        D.text(x, y + p.tracks * rh + 3, F.T, "00", D.DGREY);
        D.text(x + w - 12, y + p.tracks * rh + 3, F.T, "7F", D.DGREY);
    }

    /* the details under the list (browsing) */
    function details(y) {
        const c = cart(), ins = inst(), h = 226 - y;
        if (!ins) { D.text(4, y + 30, F.T, "EMPTY SLOT: SEL MENU, INIT OR PASTE", D.DGREY); return; }
        const p = ins.p, col = M.ENGINES[c.type].col;
        if (c.type === "DX7") {
            D.runs(0, y, F.T, [["ALG ", D.CYAN], [M.hex(p.algorithm - 1), D.WHITE], ["  FB ", D.CYAN], [M.hex(p.feedback, 1), D.WHITE],
                               ["  LFO ", D.CYAN], [M.hex(p.lfo.speed), D.WHITE], ["  LEVEL ", D.CYAN], [ins.level + " DB", D.WHITE]]);
            D.text(0, y + 13, F.T, "OP RATIO LV R1 R2 R3 R4 L1 L2 L3 L4", D.DGREY);
            const flags = PTFM.ui.DX7_ALG[p.algorithm - 1];
            p.ops.forEach((o, i) => {
                const carrier = !(flags[5 - i] & 3), yy = y + 25 + i * 11;
                const ratio = o.mode === "ratio" ? ((o.coarse || .5) * (1 + o.fine / 100)).toFixed(2).padStart(5) : String(Math.round(Math.pow(10, o.coarse & 3) * Math.pow(9.772, o.fine / 99))).padStart(4) + "H";
                D.text(0, yy, F.T, " " + (i + 1), carrier ? col : D.LGREY);
                D.text(18, yy, F.T, ratio, o.level ? D.WHITE : D.DGREY);
                D.text(54, yy, F.T, M.hex(o.level), o.level ? D.BROWN : D.DGREY);
                D.text(72, yy, F.T, o.eg.rates.map(v => M.hex(Math.min(99, v))).join(" "), D.LGREY);
                D.text(144, yy, F.T, o.eg.levels.map(v => M.hex(Math.min(99, v))).join(" "), D.LGREY);
            });
        } else if (c.type === "JUNO") {
            const S = [["RT", p.lfo.rate, 9], ["DL", p.lfo.delay, 9], ["LF", p.dco.lfo, 11], ["PW", p.dco.pwm, 11], ["NS", p.dco.noise, 11],
                       ["SB", p.dco.sub, 11], ["HP", p.hpf * 42, 3], ["FQ", p.vcf.freq, 12], ["RS", p.vcf.res, 12], ["EN", p.vcf.env, 12],
                       ["LF", p.vcf.lfo, 12], ["KB", p.vcf.kybd, 12], ["LV", p.vca.level, 6], ["A", p.env.a, 14], ["D", p.env.d, 14],
                       ["S", p.env.s, 14], ["R", p.env.r, 14]];
            const sh = h - 30;
            S.forEach(([n, v, cl], i) => {
                const x = 2 + i * 14;
                D.rect(x + 4, y + 2, 2, sh, D.DGREY);
                const k = Math.round((1 - v / 127) * (sh - 4));
                D.rect(x + 1, y + 2 + k, 8, 4, cl);
                D.text(x + (n.length === 1 ? 2 : 0), y + sh + 5, F.T, n, D.CYAN);
            });
            D.text(0, y + h - 12, F.T, (p.dco.range + " " + (p.dco.saw ? "SAW " : "") + (p.dco.pulse ? "PULSE " : "") + "CHORUS " +
                                        p.chorus.toUpperCase() + " " + p.vca.mode.toUpperCase()).slice(0, 40), D.LGREY);
        } else if (c.type === "TB303") {
            ["wave", "cutoff", "reso", "envmod", "decay", "accent"].forEach((k, i) =>
                knob(22 + i * 39, y + 26, 14, p[k], ["WAVE", "CUTOFF", "RESO", "ENVMOD", "DECAY", "ACCENT"][i], col));
        } else if (c.type === "SAMPLER") {
            const smp = c.samples[p.sample];
            if (!smp || smp.deleted) { D.text(0, y + 20, F.T, "THE SAMPLE IS DELETED", D.RED); return; }
            D.runs(0, y, F.T, [[smp.name, col], ["  " + (smp.length / smp.rate).toFixed(2) + " S  " + smp.rate + " HZ", D.LGREY]]);
            wave(M.audio[smp.audio], 0, y + 16, 240, h - 34, D.BROWN, [[p.start, D.WHITE], [p.end, D.WHITE],
                ...(p.loop !== "OFF" ? [[p.loopStart, D.GREEN], [p.loopEnd, D.GREEN]] : []), ...playing(c.id, p.sample)]);
            D.text(0, y + h - 14, F.T, "LOOP " + p.loop + "  BASE " + M.noteName(p.base) + "  TUNE " + M.sgn(p.tune, 2), D.LGREY);
        } else if (c.type === "FUSION") {
            const pt = M.pattern(p.pattern);
            D.runs(0, y, F.T, [["PATTERN ", D.CYAN], [p.pattern == null ? "--" : M.hex(p.pattern), D.WHITE],
                               [pt ? "  " + pt.tracks + " TRACKS, " + pt.rows + " ROWS" : "", D.LGREY]]);
            fusionMap(pt, 0, y + 16, 240, h - 30);
        } else if (c.type === "CHIP") {
            const head = p.osc === 0 ? DUTYN[p.duty] : p.osc === 2 ? CT.WAVE_NAMES[p.wave]
                       : p.osc === 3 ? BSHAPEN[p.bz_shape] + " " + (p.bz_steps ? 32 : 16) : "";
            D.runs(0, y, F.T, [[OSCN[p.osc] + " ", D.CYAN], [head + "  ", D.WHITE], ["CLOCK ", D.CYAN], [CLOCKN[p.clock], D.WHITE]]);
            const [s, mx] = chipSteps(p);
            stepsPic(0, y + 14, 114, 62, s, mx, col);
            ["VOL", "ARP", "PIT", "VIB", "DTY", "NSE"].forEach((lab, r) => {
                const yy = y + 12 + 11 * r, k = ROWK[r], L = k >= 0 ? chipTable(p, k) : null;
                D.text(124, yy, F.T, lab, D.CYAN);
                if (k < 0 ? p.vib_depth : L) { if (k < 0) vibPic(146, yy, 94, 10, p, 48); else tablePic(146, yy, 94, 10, k, L, 0); }
                else D.text(146, yy, F.T, "--", D.DGREY);
            });
            D.runs(0, y + 81, F.T, [["SWEEP ", D.CYAN], [["OFF", "DOWN", "UP"][p.sweep] + "  ", D.WHITE], ["REL ", D.CYAN],
                                    [M.hex(p.release, 1) + "  ", D.WHITE], [(p.fps50 ? "50" : "60") + " HZ  ", D.WHITE], ["LEVEL ", D.CYAN],
                                    [ins.level + " DB", D.WHITE]]);
        }
    }

    /* -------------------------------------------- CHIP's pictures (in whole numbers: the firmware draws the same) --- */
    const rdiv = (a, b) => Math.floor((2 * a + b) / (2 * b));                     /* a / b rounded (a >= 0, b > 0) */
    const PSG_KHZ = [1773, 1773, 1773, 1000, 1790, 2000, 1773];                   /* the PSG envelope's clock by CLOCK */
    const PSG_HZ = [1773400, 1773400, 1773400, 1000000, 1789772, 2000000, 1773400];
    function envLevel(shape, steps, r, pos) {             /* the PSG envelope's level (dsp-chip.js's own) */
        const att = shape >> 2 & 1, alt = shape >> 1 & 1, hold = shape & 1, cont = shape >> 3 & 1, mx = steps - 1;
        if (r === 0) return att ? pos : mx - pos;
        if (!cont) return 0;
        if (hold) return (att ^ alt) ? mx : 0;
        return (alt ? (att ^ (r & 1)) : att) ? pos : mx - pos;
    }
    function chipTable(p, k) {                            /* the instrument's table of kind k: values, loop, speed, number, name */
        const n = p.tab[k];
        if (!n || n > CT.TAB_COUNT[k]) return null;
        const [at, len, loop] = CT.TAB[CT.TAB_FIRST[k] + n - 1];
        return { vals: CT.TV.slice(at, at + len), loop, speed: p.spd[k], num: n, name: CT.TAB_NAMES[CHIP_KINDS[k]][n - 1] };
    }
    function chipSteps(p) {                               /* a period or a few of the oscillator's steps, and their top */
        if (p.osc === 0) {
            const d = CT.DUTY[(p.clock === 0 ? 0 : 4) + p.duty], o = [];
            for (let i = 0; i < 24; i++) o.push(d >> (i & 7) & 1);
            return [o, 1];
        }
        if (p.osc === 1) return [CT.TRI.concat(CT.TRI), 15];
        if (p.osc === 2) { const w = CT.WAVES.slice(p.wave * 32, p.wave * 32 + 32); return [w.concat(w), 15]; }
        if (p.osc === 3) {
            const st = p.bz_steps ? 32 : 16, shape = p.bz_shape === 1 ? 12 : p.bz_shape === 2 ? 10 : 8, o = [];
            for (let r = 0; r < 2; r++) for (let i = 0; i < st; i++) o.push(p.bz_gate && i >= st / 2 ? 0 : envLevel(shape, st, r, i));
            return [o, st - 1];
        }
        const o = [];                                     /* (the noise: its long register from 1) */
        let l = 1;
        for (let i = 0; i < 64; i++) { l = (l >> 1) | ((l ^ (l >> 1)) & 1) << 14; o.push(l & 1); }
        return [o, 1];
    }
    function stepsPic(x, y, w, h, vals, max, col) {       /* steps as a line, the lowest at the bottom */
        const n = vals.length;
        D.rect(x, y + h - 1, w, 1, D.DGREY);
        let py = -1;
        for (let i = 0; i < n; i++) {
            const x0 = x + rdiv(i * w, n), x1 = x + rdiv((i + 1) * w, n) - 1, yy = y + h - 1 - rdiv(vals[i] * (h - 1), max);
            if (py >= 0 && py !== yy) D.line(x0, py, x0, yy, col);
            D.line(x0, yy, x1, yy, col);
            py = yy;
        }
    }
    function chipEnvFrame(p, f, fps) {                    /* the volume or the envelope's level at frame f, 0-F */
        if (p.env === 3) {
            const es = p.curve === 3 ? 32 : 16, pos = Math.floor(f * PSG_HZ[p.clock] / (fps * (es === 32 ? 8 : 16) * (p.psg_period || 1)));
            return rdiv(envLevel(p.psg_shape, es, Math.floor(pos / es), pos % es) * 15, es - 1);
        }
        if (p.env === 1 || p.env === 2) {                 /* (a step every STEP / 64 s; 8BIT's (STEP + 1) / 240 s) */
            const k = p.clock === 1 ? Math.floor(f * 240 / (fps * (p.env_step + 1))) : p.env_step ? Math.floor(f * 64 / (fps * p.env_step)) : 0;
            return p.env === 1 ? Math.max(0, p.vol - k) : Math.min(15, p.vol + k);
        }
        return p.vol;
    }
    function chipVolFrames(p, n, off) {                   /* the level frame by frame (0-F), the key let go at frame off */
        const fps = p.fps50 ? 50 : 60, L = chipTable(p, 0), out = [];
        let li = 0, lc = 0, rel = 15, ra = 0;
        for (let f = 0; f < n; f++) {
            let lv = 15;
            if (L) {
                lv = L.vals[Math.min(li, L.vals.length - 1)];
                if (++lc >= L.speed) { lc = 0; if (++li >= L.vals.length) li = L.loop >= 0 ? L.loop : L.vals.length; }
            }
            if (f >= off) { if (!p.release) rel = 0; else if (++ra >= p.release) { ra = 0; rel = Math.max(0, rel - 1); } }
            out.push(Math.floor((chipEnvFrame(p, f, fps) * Math.max(0, lv) * rel * 2 + 225) / 450));
        }
        return out;
    }
    function volPic(x, y, w, h, fr, off, col) {           /* the level over a note, a dotted line where the key is let go */
        const n = fr.length;
        D.rect(x, y + h - 1, w, 1, D.DGREY);
        for (let i = 0; i < n; i++) {
            const x0 = x + Math.floor(i * w / n), ww = Math.max(1, x + Math.floor((i + 1) * w / n) - x0), top = y + h - 1 - rdiv(fr[i] * (h - 2), 15);
            D.rect(x0, top, ww, y + h - 1 - top, D.DEEP + col);
            D.rect(x0, top, ww, 1, col);
        }
        const xo = x + Math.floor(off * w / n);
        for (let yy = y; yy < y + h - 1; yy += 2) D.px(xo, yy, D.DGREY);
    }
    const KMAX = [15, 24, 127, 3, 15];
    /* a table drawn for its kind: VOL, DUTY and NOISE (bright at the top) up from the floor, ARP and PITCH up and down;
     * over `frames` frames (each step SPEED frames, past its end darker), or a bar a step; a yellow line where it loops */
    function tablePic(x, y, w, h, k, L, frames) {
        const signed = k === 1 || k === 2, max = k === 1 ? Math.max(12, ...L.vals.map(Math.abs)) : KMAX[k];
        const n = frames || Math.max(8, L.vals.length), gap = w >= 3 * n ? 1 : 0, mid = y + (h >> 1), len = L.vals.length;
        const at = f => {
            if (!frames) return f < len ? f : -1;
            const i = Math.floor(f / L.speed);
            return i < len ? i : L.loop >= 0 ? L.loop + (i - len) % (len - L.loop) : len - 1;
        };
        if (signed) for (let xx = x; xx < x + w; xx += 2) D.px(xx, mid, D.DGREY);
        else D.rect(x, y + h - 1, w, 1, D.DGREY);
        for (let f = 0; f < n; f++) {
            const i = at(f);
            if (i < 0) continue;
            const v = L.vals[i], x0 = x + Math.floor(f * w / n), ww = Math.max(1, x + Math.floor((f + 1) * w / n) - x0 - gap);
            const col = frames && Math.floor(f / L.speed) >= len ? D.DEEP + D.LBLUE : D.LBLUE;
            if (signed) {
                const hh = rdiv(Math.abs(v) * ((h >> 1) - 1), max);
                D.rect(x0, v >= 0 ? mid - hh : mid, ww, hh + 1, col);
            } else {
                const hh = Math.max(1, rdiv((k === 4 ? 15 - v : v) * (h - 2), KMAX[k]));
                D.rect(x0, y + h - 1 - hh, ww, hh, col);
            }
        }
        if (L.loop >= 0) D.rect(Math.max(x, x + Math.floor(L.loop * (frames ? L.speed : 1) * w / n) - 1), y, 1, h, D.YELLOW);
    }
    function vibPic(x, y, w, h, p, frames) {              /* the vibrato over frames: flat through its delay, then its cycle */
        const mid = y + (h >> 1), d = p.vib_depth, hf = d >> 1, CY = [0, hf, d, hf, 0, -hf, -d, -hf];
        for (let xx = x; xx < x + w; xx += 2) D.px(xx, mid, D.DGREY);
        if (!d) return;
        for (let f = 0; f < frames; f++) {
            const ff = f - p.vib_delay, v = ff < 0 ? 0 : CY[Math.floor(ff / p.vib_speed) & 7], hh = rdiv(Math.abs(v) * ((h >> 1) - 1), 62);
            const x0 = x + Math.floor(f * w / frames), ww = Math.max(1, x + Math.floor((f + 1) * w / frames) - x0);
            D.rect(x0, v >= 0 ? mid - hh : mid, ww, hh + 1, D.LBLUE);
        }
        D.rect(Math.min(x + w - 1, x + Math.floor(p.vib_delay * w / frames)), y, 1, h, D.YELLOW);
    }
    const ROWK = [0, 1, 2, -1, 3, 4];                    /* TABLES' rows: the table kinds, VIBRATO (-1) between */
    function chipPicture(p, y, col) {
        const fps = p.fps50 ? 50 : 60;
        const volume = () => {
            D.text(124, y + 2, F.T, "VOLUME", D.CYAN);
            D.text(240 - D.tw(F.T, "1.5 S"), y + 2, F.T, "1.5 S", D.DGREY);
            volPic(124, y + 18, 114, 70, chipVolFrames(p, fps * 3 >> 1, fps), fps, col);
        };
        if (st.page === 0) {
            let tt = "EXACT";
            if (p.osc === 4) tt = "RATE " + M.hex(p.n_rate, 1);
            else if (p.clock !== 6) {
                const { reg, c10 } = PTFM.chip.tuning(p, 60), a = Math.abs(c10);
                tt = "$" + M.hex(reg, reg > 0xFFF ? 4 : 3) + " " + (c10 < 0 ? "-" : "+") + Math.floor(a / 10) + "." + a % 10;
            }
            D.text(0, y + 2, F.T, "WAVE", D.CYAN);
            D.text(114 - D.tw(F.T, tt), y + 2, F.T, tt, D.LGREY);
            const [s, mx] = chipSteps(p);
            stepsPic(0, y + 18, 114, 70, s, mx, col);
            volume();
        } else if (st.page === 1) {
            if (p.env === 3) {
                const es = p.curve === 3 ? 32 : 16, ms = rdiv(256 * (p.psg_period || 1), PSG_KHZ[p.clock]) + " MS", env = [];
                for (let r = 0; r < 4; r++) for (let i = 0; i < es; i++) env.push(envLevel(p.psg_shape, es, r, i));
                D.text(0, y + 2, F.T, "PSG ENVELOPE", D.CYAN);
                D.text(114 - D.tw(F.T, ms), y + 2, F.T, ms, D.DGREY);
                stepsPic(0, y + 18, 114, 70, env, es - 1, col);
            } else {
                const kind = ["NONE", "DOWN", "UP"][p.env], fr = upto(fps).map(f => chipEnvFrame(p, f, fps));
                D.text(0, y + 2, F.T, "ENVELOPE", D.CYAN);
                D.text(114 - D.tw(F.T, kind), y + 2, F.T, kind, D.DGREY);
                stepsPic(0, y + 18, 114, 70, fr, 15, col);
            }
            volume();
        } else if (st.page === 2) {
            const k = ROWK[st.group] == null ? 0 : ROWK[st.group];
            if (k < 0) {
                const h = p.vib_depth ? "DELAY " + p.vib_delay + ", " + p.vib_depth + " CENTS" : "OFF";
                D.text(0, y + 2, F.T, "VIBRATO", D.CYAN);
                D.text(240 - D.tw(F.T, h), y + 2, F.T, h, D.LGREY);
                vibPic(0, y + 16, 240, 72, p, 48);
            } else {
                const L = chipTable(p, k);
                D.text(0, y + 2, F.T, KNAME[CHIP_KINDS[k]], D.CYAN);
                if (L) {
                    const h = M.hex(L.num) + " " + L.name + ", " + L.vals.length + " STEPS" + (L.loop >= 0 ? ", LOOP " + (L.loop + 1) : "");
                    D.text(240 - D.tw(F.T, h), y + 2, F.T, h, D.LGREY);
                    tablePic(0, y + 16, 240, 72, k, L, 48);
                } else {
                    D.text(240 - D.tw(F.T, "NONE"), y + 2, F.T, "NONE", D.DGREY);
                    D.rect(0, y + 87, 240, 1, D.DGREY);
                }
            }
            D.text(0, y + 89, F.T, "0", D.DGREY);
            D.text(240 - D.tw(F.T, "48 FRAMES"), y + 89, F.T, "48 FRAMES", D.DGREY);
        } else {
            const on = p.osc === 2, nm = M.hex(p.wave) + " " + CT.WAVE_NAMES[p.wave];
            D.text(0, y + 2, F.T, "WAVE TABLE", D.CYAN);
            D.text(240 - D.tw(F.T, nm), y + 2, F.T, nm, on ? D.LGREY : D.DGREY);
            for (let i = 0; i < 32; i++) {
                const hh = Math.max(1, rdiv(CT.WAVES[p.wave * 32 + i] * 48, 15));
                D.rect(8 + i * 7, y + 66 - hh, 5, hh, on ? col : D.DGREY);
            }
            for (let j = 0; j < 16; j++) {
                const x = (j & 7) * 30, yy = y + 71 + (j >> 3) * 14;
                for (let i = 0; i < 32; i++) {
                    const hh = Math.max(1, rdiv(CT.WAVES[j * 32 + i] * 10, 15));
                    D.rect(x + 3 + (i * 3 >> 2), yy + 11 - hh, 1, hh, j === p.wave ? D.WHITE : col);
                }
                if (j === p.wave) D.box(x, yy - 1, 30, 14, D.WHITE);
            }
        }
    }
    function chipRowPic(gg, p, yy) {                      /* a TABLES row's table (or the vibrato) drawn small at its right */
        if (gg.chipRow === "vib") { if (p.vib_depth) vibPic(172, yy, 66, 10, p, 48); return; }
        const k = CHIP_KINDS.indexOf(gg.chipRow), L = chipTable(p, k);
        if (L) tablePic(172, yy, 66, 10, k, L, 0);
    }

    /* edit mode: the parameters and a picture */
    function drawEdit() {
        const c = cart(), ins = inst(), pg = pages()[st.page];
        D.rect(0, Y0, 240, U.BODY_H, D.BLACK);
        if (!ins) { D.text(4, Y0 + 40, F.T, "EMPTY SLOT: NOTHING TO EDIT", D.DGREY); return; }
        const p = ins.p, col = M.ENGINES[c.type].col, sampler = c.type === "SAMPLER";
        const picY = sampler ? Y0 + 83 : Y0, parY = sampler ? Y0 : Y0 + 100;   /* (a sampler's table: 4 rows at most, to y 95) */
        /* the picture */
        const g = groups()[st.group] || {};
        if (c.type === "DX7") {
            algorithm(p.algorithm, 0, picY + 2, 120, 92, g.op != null ? g.op : -1);
            const op = p.ops[g.op != null ? g.op : 0];
            D.text(126, picY + 2, F.T, "OP" + ((g.op != null ? g.op : 0) + 1) + " ENVELOPE", D.CYAN);
            dxEnv(op, 126, picY + 18, 110, 70, col);
        } else if (c.type === "JUNO") {
            D.text(0, picY + 2, F.T, "ENVELOPE", D.CYAN);
            adsr(p.env, 0, picY + 18, 114, 70, col);
            D.text(124, picY + 2, F.T, "FILTER", D.CYAN);
            filterCurve(p.vcf.freq, p.vcf.res, 124, picY + 18, 114, 70, col);
        } else if (c.type === "TB303") {                /* its envelope and filter, as the Juno-106's */
            D.text(0, picY + 2, F.T, "ENVELOPE", D.CYAN);
            D.text(114 - D.tw(F.T, "ACC"), picY + 2, F.T, "ACC", D.YELLOW);
            tbEnvelope(p, 0, picY + 18, 114, 66, col);
            D.text(124, picY + 2, F.T, "FILTER", D.CYAN);
            const wn = p.wave < 16 ? "SAW" : p.wave > 111 ? "SQUARE" : "SAW+SQ";
            D.text(220 - D.tw(F.T, wn), picY + 2, F.T, wn, D.LGREY);
            tbWave(p.wave, 224, picY + 4, col);
            tbFilter(p, 124, picY + 18, 114, 66, col);
        } else if (sampler) {                                     /* (both pages: ALGORITHM turns only the table) */
            const smp = c.samples[p.sample];
            if (smp && !smp.deleted) sampPicture(p, smp, c.id, picY, col);
            else D.text(0, 213, F.T, "WHITE START/END  GREEN LOOP  YELLOW PLAY", D.DGREY);
        } else if (c.type === "FUSION") {
            const pt = M.pattern(p.pattern);
            D.runs(0, picY + 2, F.T, [["PATTERN ", D.CYAN], [p.pattern == null ? "--" : M.hex(p.pattern), D.WHITE],
                                      [pt ? "  " + pt.tracks + " TRACKS, " + pt.rows + " ROWS" : "", D.LGREY]]);
            fusionMap(pt, 0, picY + 18, 240, 78);
        } else if (c.type === "CHIP") chipPicture(p, picY, col);
        /* the parameters: the selected group's knobs named above, every group's values */
        D.hline(0, parY, 240, D.DGREY);
        D.runs(0, parY + 2, F.T, [[pg.name, D.CYAN], ["  " + M.pageOf(st.page, pages().length), D.DGREY], [pages().length > 1 ? "  ALGORITHM: PAGES" : "", D.DGREY]]);
        const gs = groups(), top = Math.max(0, Math.min(st.group - 3, gs.length - 6));
        const sg = gs[st.group];
        /* the table's header: the knobs over the columns they turn, the selected group's names */
        if (sg && sg.patternRow) D.runs(0, parY + 15, F.T, [["SEL", D.YELLOW], [": EDIT IT  ", D.LGREY], ["ENV", D.YELLOW], [": PICK  ", D.LGREY],
                                                             ["K1", D.YELLOW], [": OTHER", D.LGREY]]);
        else if (sg && !sg.nameRow) {
            D.text(2, parY + 15, F.T, sg.params.length > 1 ? "K1-K" + sg.params.length : "K1", D.YELLOW);
            sg.params.forEach((d, k) => D.text(60 + k * 45, parY + 15, F.T, d.n.slice(0, 7), k === st.last ? D.WHITE : D.LGREY));
        }
        else if (sg) D.runs(0, parY + 15, F.T, [["SEL", D.YELLOW], [": TYPE A NAME", D.LGREY]]);
        for (let i = top; i < Math.min(gs.length, top + 6); i++) {
            const gg = gs[i], yy = parY + 30 + (i - top) * 12, on = i === st.group;
            if (on) D.box(0, yy - 1, 240, 12, D.WHITE);
            D.text(2, yy, F.T, gg.name.slice(0, 9), on ? D.WHITE : D.CYAN);
            if (gg.nameRow) { D.text(60, yy, F.T, ins.name, col); continue; }
            /* (a group's own columns where it has them: CHIP's wider values, TABLES' rows) */
            gg.params.forEach((d, k) => D.text(gg.cols ? gg.cols[k][0] : 60 + k * 45, yy, F.T,
                                               gg.cols ? ptext(ins, d).slice(0, gg.cols[k][1]) : cell(ptext(ins, d)), on && k === st.last ? D.WHITE : D.LGREY));
            if (gg.chipRow) chipRowPic(gg, p, yy);
        }
        if (c.type === "CHIP" && st.page === 3 && p.osc !== 2) D.text(2, parY + 42, F.T, "ONLY THE WAVE OSCILLATOR READS IT", D.DGREY);
    }

    const scr = {
        name: "INSTRUMENTS", group: "MAKE",
        does: [B.EDIT, B.SEL, B.ENV, B.GLO, B.ARP, B.LFO],
        combines: [B.ARP],
        /* on its edit page the instrument is being edited: it plays as edited */
        editing: (what, key) => what === "inst" && st.edit && key === U.inst,
        sub: () => st.edit ? "EDIT" : null,                 /* the header's subpage: its edit page */
        seq: () => st.edit && pages().length > 1 ? M.pageOf(st.page, pages().length) : null,   /* SEQ turns the edit pages */
        page() {
            if (st.edit) return null;
            const c = cart();
            return c && c.size > 16 ? M.pageOf(Math.floor(slot() / 16), Math.ceil(c.size / 16)) : null;
        },
        lit(b) {
            if (b === B.EDIT && st.edit) return 1;
            if (b === B.SEQ) return st.edit && pages().length > 1 ? .25 : 0;
            if (b === B.SEL && (st.mark != null || st.mv)) return 1;
            if (b === B.ENV && st.edit && !envList()) return 0;        /* in edit mode only on SAMPLE / PATTERN */
        },
        /* SEL held's layer: marking slots while browsing, big steps on the edit page */
        selLines: () => st.edit ? ["K1-K4: BIG STEPS"] : ["SELECT, ALGORITHM: MARK SLOTS"],
        /* an instrument afloat: the cartridge stays; else PRESETS as everywhere, empty slots skipped (SELECT and
         * ALGORITHM go over every slot) */
        presets: () => !!st.mv,
        knob(e, s, coarse) {
            const c = cart();
            if (st.edit) {
                if (e === E.SELECT) { st.group = U.clamp(st.group + Math.sign(s), 0, groups().length - 1); st.last = -1; return; }
                if (e === E.ALGORITHM) { st.page = U.clamp(st.page + Math.sign(s), 0, pages().length - 1); st.group = 0; st.last = -1; return; }
                if (e >= E.K1 && e <= E.K4) {
                    const g = groups()[st.group];
                    if (!g || g.nameRow) return;             /* (built in too: edited as any) */
                    st.last = e - E.K1;
                    turn(g.params[e - E.K1], s, coarse);
                }
                return;
            }
            if (st.mv) { if (e === E.SELECT) floatStep(Math.sign(s)); else if (e === E.ALGORITHM) floatStep(Math.sign(s) * 8); return; }
            if (st.marking && (e === E.SELECT || e === E.ALGORITHM)) return this.mark(e, s);    /* after SEL, MARK */
            if (e === E.SELECT) { st.mark = null; U.setInst(c.id << 8 | U.clamp(slot() + s, 0, c.size - 1)); }
            else if (e === E.ALGORITHM) { st.mark = null; U.setInst(c.id << 8 | U.clamp(slot() + Math.sign(s) * 8, 0, c.size - 1)); }
            else if (e === E.K1) U.stepCart(Math.sign(s));
        },
        mark(e, s) {
            if (st.edit) return;
            if (st.mark == null) st.mark = slot();
            const c = cart();
            U.setInst(c.id << 8 | U.clamp(slot() + s * (e === E.ALGORITHM ? 8 : 1), 0, c.size - 1));
        },
        tap(b) {
            const c = cart(), ins = inst();
            if (st.mv) { if (b === B.SEL) putDown(); return true; }         /* afloat: only SEL (and HOME) */
            if (b === B.EDIT) {
                if (st.edit) { st.edit = false; st.sess = null; return true; }      /* keeping the changes */
                if (c.type === "FUSION" && (!ins || ins.p.pattern == null || !M.pattern(ins.p.pattern))) {
                    /* (it ends in the EDITOR: the EDITOR's own pattern's warning first, then this again) */
                    if (M.patternHolds(M.EDITOR_PATTERN)) { U.ownAsk(() => this.tap(B.EDIT)); return true; }
                    /* an empty FUSION slot: a new pattern of 128 rows, EDIT STEP 0 (specs/files/patterns.md), opened;
                     * first offered filled with the last instrument picked, its note on each row, so the slot
                     * plays as that instrument from the start */
                    const n = M.freePattern();
                    if (n < 0) { U.toast("ALL " + (M.USER_LAST + 1) + " PATTERNS USED"); return true; }
                    /* the sounds a FUSION can be filled with: every instrument of the cartridges that are not FUSION */
                    const sounds = [];
                    for (const cc of M.state.carts) if (cc.type !== "FUSION") cc.slots.forEach((x, i) => { if (x) sounds.push(cc.id << 8 | i); });
                    let src = M.inst(U.lastInst) ? U.lastInst : sounds[0];
                    const make = fill => {
                        U.edit(() => {
                            const p = M.newPattern(n, 4, 128);
                            p.step = 0;
                            if (fill) for (let r = 12; r < 128; r++) p.cells[r * 16] = M.newCell(r, src, null);
                            M.state.patterns[n] = p;
                            if (ins) ins.p.pattern = n; else c.slots[slot()] = { name: "FUSION " + M.hex(n), level: 0, p: { pattern: n } };
                        });
                        openFusion();
                    };
                    if (src == null) { make(false); return true; }
                    const say = () => ["A NEW PATTERN FOR THIS SLOT", "FILL IT WITH " + M.refText(src) + " " + U.instName(src) + "?",
                                       "ITS NOTE ON EACH ROW: THE SAME SOUND", "K1, SELECT: ANOTHER  KEYS: HEAR IT"];
                    const q = U.ask(say(), ["FILL", "EMPTY"], a => { for (const k in heard) P.noteOff(heard[k]); if (a) make(a === "FILL"); });
                    /* K1 another cartridge, SELECT another sound in it; the keys play the one offered */
                    const heard = {};
                    q.knob = (e, s) => {
                        const i = sounds.indexOf(src), d = Math.sign(s);
                        if (e === E.SELECT) src = sounds[U.clamp(i + d, 0, sounds.length - 1)];
                        else if (e === E.K1) {
                            let k = i;
                            while (k + d >= 0 && k + d < sounds.length && sounds[k + d] >> 8 === src >> 8) k += d;
                            k = U.clamp(k + d, 0, sounds.length - 1);
                            if (d < 0) while (k > 0 && sounds[k - 1] >> 8 === sounds[k] >> 8) k--;   /* that cartridge's first */
                            src = sounds[k];
                        } else return;
                        q.lines = say();
                    };
                    q.key = (kn, down) => {
                        if (down) heard[kn] = P.noteOn("fill" + kn, U.midi(kn), src, U.set("KEYS VOLUME"), -1);
                        else if (heard[kn]) { P.noteOff(heard[kn]); delete heard[kn]; }
                        return true;
                    };
                    return true;
                }
                if (!ins) { U.toast("EMPTY SLOT: SEL, INIT"); return true; }
                st.edit = true; st.page = 0; st.group = 0; st.last = -1;
                st.sess = U.session();
                keep(U.inst);
                return true;
            }
            if (b === B.SEQ && st.edit) { st.page = (st.page + 1) % pages().length; st.group = 0; st.last = -1; return true; }
            if (b === B.LFO && st.edit) { const g = groups()[st.group]; if (g && !g.nameRow) reset(g.params[st.last >= 0 ? st.last : 0]); return true; }
            if (b === B.SEL) {                                   /* on the edit page's NAME or PATTERN: straight there */
                const g = st.edit ? groups()[st.group] : null;
                if (g && g.nameRow) rename();
                else if (g && g.patternRow && ins && M.pattern(ins.p.pattern)) openFusion();
                else slotMenu();
                return true;
            }
            if (b === B.GLO) {
                U.props("CARTRIDGE " + M.hex(c.id), [
                    { name: "NAME", get: () => c.name, edit: () => U.textEntry("CARTRIDGE " + M.hex(c.id), c.name, 12, B.EDIT, n => { if (n != null) U.edit(() => { c.name = n || "NO NAME"; }); }) },
                    { name: "TYPE", get: () => M.TYPE_NAMES[c.type], fixed: true },
                    { name: "SLOTS USED", get: () => c.slots.filter(Boolean).length + " OF " + c.size, fixed: true },
                    { name: "RENAME WITH", get: () => "EDIT", fixed: true },
                ], B.GLO);
                return true;
            }
            if (b === B.ENV) {
                const l = envList();
                if (l === "samples") { sampleList(); return true; }
                if (l === "patterns") {                          /* on a FUSION slot's PATTERN: the pattern list */
                    U.patternList("PATTERN FOR " + M.refText(U.inst), ins.p.pattern, n => { U.edit(() => { ins.p.pattern = n; }); },
                                  B.ENV, { fusionKeys: true });
                    return true;
                }
                if (!st.edit) cartList();                        /* the cartridges while browsing; edit mode keeps its instrument */
                return true;
            }
            return false;                                    /* (ARP: the arpeggiator, ui.js's) */
        },
        presetsMoved() {
            if (st.edit) keep(U.inst);                       /* editing goes on with this one: it can be dropped too */
            /* another engine may have fewer pages and groups (SEL + PRESETS into another cartridge) */
            st.page = U.clamp(st.page, 0, Math.max(0, pages().length - 1));
            st.group = U.clamp(st.group, 0, Math.max(0, groups().length - 1));
        },
        back() {
            if (st.mv) { st.mv = null; U.toast("NOT MOVED"); return true; }
            if (st.edit) { st.sess.drop(); st.edit = false; st.sess = null; return true; }      /* HOME: dropping them */
            if (st.mark != null) { st.mark = null; st.marking = false; return true; }
            return false;
        },
        leave() { st.marking = false; },
        footer() {
            const c = cart();
            return c ? [M.hex(c.id) + " " + c.name, M.ENGINES[c.type].col] : null;
        },
        draw() {
            if (st.edit) return drawEdit();
            const c = cart();
            if (!c) return;
            const used = c.slots.filter(Boolean).length;
            D.runs(0, Y0, F.S, [[M.hex(c.id) + " ", D.WHITE], [c.name + " ", D.LGREY], [M.TYPE_NAMES[c.type] + " ", M.ENGINES[c.type].col],
                                [used + "/" + c.size, D.DGREY]], D.CROP_S.map((v, i) => i ? 16 : 0));
            const cur = st.mv ? st.mv.at : slot(), page = Math.floor(cur / 16) * 16, col = M.ENGINES[c.type].col;
            const m0 = st.mark != null ? Math.min(st.mark, slot()) : -1, m1 = st.mark != null ? Math.max(st.mark, slot()) : -1;
            for (let k = 0; k < 16; k++) {
                const i = page + k;
                if (i >= c.size) break;
                const x = k < 8 ? 0 : 120, y = Y0 + 17 + (k % 8) * 12, s = shown(i), on = i === cur;
                if (i >= m0 && i <= m1) D.rect(x, y - 1, 119, 12, D.DEEP + D.MAGENTA);
                if (on) D.box(x, y - 1, 119, 12, st.mv ? D.YELLOW : D.WHITE);
                D.text(x + 2, y, F.T, M.hex(i), on ? D.WHITE : D.DGREY);
                D.text(x + 20, y, F.T, s ? s.name : "----------", s ? (on ? D.WHITE : col) : D.DGREY);
            }
            D.hline(0, Y0 + 113, 240, D.DGREY);
            details(Y0 + 116);
        },
    };
    U.register(scr);
})();
