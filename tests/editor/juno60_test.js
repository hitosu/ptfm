/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The Juno-60 conversion (editor/js/juno60.js) against its generator's tables and rules (editor/tools/juno60/): the
 * generator's vectors.json (patches made up there, every field at its ends and between, with what its own twin of the
 * conversion makes of them) converted here field by field; the table fields read straight from juno60_tables.js; the
 * switches' rules (RANGE 8', CHORUS, the HPF, PWM SOURCE, ENV POLARITY, VCA MODE, SUB off); the tables' shapes (each
 * table a Juno-106 byte, rising where its quantity rises). No factory data: the vectors are made up.
 *
 *   node tests/editor/juno60_test.js */
"use strict";
const fs = require("fs"), path = require("path");
const { ED, ROOT, check, done } = require("./load.js");
require(path.join(ROOT, "editor", "js", "juno60_tables.js"));
require(path.join(ROOT, "editor", "js", "juno60.js"));
const T = ED.juno60tab, J = ED.juno60;

const vec = JSON.parse(fs.readFileSync(path.join(ROOT, "editor", "tools", "juno60", "vectors.json"), "utf8"));
const FIELDS = ["LFO RATE", "LFO DELAY", "DCO LFO", "DCO PWM", "DCO NOISE", "VCF FREQ", "VCF RES", "VCF ENV", "VCF LFO",
    "VCF KYBD", "VCA LEVEL", "ENV A", "ENV D", "ENV S", "ENV R", "DCO SUB", "SW1", "SW2"];
const bad = FIELDS.map(() => 0);
let pwmBad = 0;
for (const x of vec) {
    const r = J.convert(x.v);
    for (let k = 0; k < 18; k++) if (r.p[k] !== x.p[k]) bad[k]++;
    if (r.pwmEnv !== x.pwmEnv) pwmBad++;
}
FIELDS.forEach((f, k) => check(!bad[k], `${f}: the generator's ${vec.length} patches converted as its twin converts them (${bad[k]} differ)`));
check(!pwmBad, "PWM BY ENV counted as the twin counts it");
check(vec.length >= 300 && vec.some(x => x.pwmEnv) && vec.some(x => x.v.env_polarity) && vec.some(x => x.v.vca_gate),
    "the vectors cover the fields' ends, PWM by ENV, both polarities and both VCA modes");

/* the table fields straight from the tables */
const TABLE = { 0: "LFO_RATE", 1: "LFO_DELAY", 2: "DCO_LFO", 4: "DCO_NOISE", 5: "VCF_FREQ", 6: "VCF_RES", 10: "VCA_LEVEL", 12: "ENV_D" };
const SRC = { 0: "lfo_rate", 1: "lfo_delay", 2: "dco_lfo", 4: "dco_noise", 5: "vcf_freq", 6: "vcf_res", 10: "vca_level", 12: "env_d" };
let tabBad = 0;
for (const x of vec) {
    const r = J.convert(x.v).p;
    for (const k in TABLE) if (r[k] !== T[TABLE[k]][x.v[SRC[k]]]) tabBad++;
    if (r[11] !== (x.v.vca_gate ? T.ENV_A_GATE : T.ENV_A_ENV)[x.v.env_a]) tabBad++;
    if (r[13] !== (x.v.vca_gate ? T.ENV_S_GATE : T.ENV_S_ENV)[x.v.env_s]) tabBad++;
    if (r[15] !== (x.v.sub_on ? T.DCO_SUB[x.v.dco_sub] : 0)) tabBad++;
    if (!(x.v.pwm_source === 2 && x.v.pulse && x.v.dco_pwm) && r[3] !== T.DCO_PWM[x.v.dco_pwm]) tabBad++;
}
check(!tabBad, "every table field is its table's value (ENV A and ENV S by the VCA mode, SUB off 0, PWM but by ENV)");

/* the tables: 256 Juno-106 bytes each, rising (or, LFO DELAY read backwards, falling) with the tape's value */
const RISE = ["ENV_A_ENV", "ENV_A_GATE", "ENV_D", "VCF_FREQ", "VCF_RES", "LFO_RATE", "DCO_LFO", "DCO_PWM", "DCO_SUB", "DCO_NOISE", "VCA_LEVEL",
    "ENV_S_ENV", "ENV_S_GATE"];
for (const k of RISE) {
    const a = T[k];
    check(a.length === 256 && a.every(b => b >= 0 && b <= 127) && a.every((b, i) => !i || b >= a[i - 1]), `${k}: 256 bytes, never falling`);
}
check(T.LFO_DELAY.length === 256 && T.LFO_DELAY.every((b, i) => b <= 127 && (!i || b <= T.LFO_DELAY[i - 1])),
    "LFO_DELAY: 256 bytes, falling (the Juno-60's runs backwards)");
check(T.DCO_PWM.every(b => b <= 105), "DCO PWM never past 105 (the Juno-106's full width)");
check(T.VCA_LEVEL[0] === 0 && T.VCA_LEVEL[255] === 127 && Math.abs(T.VCA_LEVEL[128] - 64) <= 1, "VCA LEVEL: one law on both (-10 to +10 dB)");
check(T.VCF_RES[255] === 127, "VCF RES: the Juno-60's top stands as the Juno-106's 127");
check(T.HZ106.length === 4096 && T.HZ60.length === 256 && T.RSND106.length === 33 * 128 && T.R106ENV.length === 33 * 128 &&
    T.Q15.length === 106, "the rules' arrays");

/* the switches */
const base = { vcf_env: 0, vcf_lfo: 0, vcf_kybd: 0, vca_level: 128, env_a: 28, env_r: 0, env_d: 0, env_s: 255, vcf_freq: 255, dco_noise: 0,
    dco_sub: 0, vcf_res: 0, dco_pwm: 0, lfo_rate: 100, dco_lfo: 0, lfo_delay: 255, env_polarity: 0, pwm_source: 0, hpf: 1, vca_gate: 0,
    lfo_trigger: 0, chorus1: 0, pulse: 0, saw: 1, sub_on: 0, chorus2: 0, chorus_off: 1 };
const sw = o => { const r = J.convert(Object.assign({}, base, o)); return { s1: r.p[16], s2: r.p[17], r }; };
check((sw({}).s1 & 7) === 2 && (sw({ pulse: 1 }).s1 & 0x08) && (sw({ saw: 1 }).s1 & 0x10) && !(sw({ saw: 0 }).s1 & 0x10), "RANGE 8', PULSE, SAW");
check((sw({}).s1 & 0x60) === 0x20 && (sw({ chorus1: 1, chorus_off: 0 }).s1 & 0x60) === 0x40 && (sw({ chorus2: 1, chorus_off: 0 }).s1 & 0x60) === 0 &&
    (sw({ chorus1: 1, chorus2: 1, chorus_off: 0 }).s1 & 0x60) === 0, "CHORUS: off, I, II; I and II both: II");
const hpf = s2 => 3 - ((s2 >> 3) & 3);
check(hpf(sw({ hpf: 1 }).s2) === 1 && hpf(sw({ hpf: 2 }).s2) === 2 && hpf(sw({ hpf: 3 }).s2) === 3 && hpf(sw({ hpf: 0 }).s2) === T.HPF_122 &&
    (T.HPF_122 === 1 || T.HPF_122 === 2), "HPF: FLAT, 269 -> 236, 571 -> 754, 122 Hz -> " + T.HPF_122 + "; BASS BOOST never");
check([0, 1, 2, 3].every(h => hpf(sw({ hpf: h }).s2) !== 0), "BASS BOOST never chosen");
check(!(sw({ pwm_source: 0 }).s2 & 1) && (sw({ pwm_source: 1 }).s2 & 1) && (sw({ pwm_source: 2 }).s2 & 1), "PWM SOURCE: LFO, MAN; ENV becomes MAN");
check((sw({ env_polarity: 1 }).s2 & 2) && !(sw({}).s2 & 2) && (sw({ vca_gate: 1 }).s2 & 4) && !(sw({}).s2 & 4), "ENV POLARITY, VCA MODE GATE");
check(sw({ pwm_source: 2, pulse: 1, dco_pwm: 200 }).r.pwmEnv && !sw({ pwm_source: 2, pulse: 0, dco_pwm: 200 }).r.pwmEnv &&
    !sw({ pwm_source: 2, pulse: 1, dco_pwm: 0 }).r.pwmEnv, "PWM BY ENV counted only with PULSE on and DCO PWM above 0");
check(sw({ sub_on: 0, dco_sub: 255 }).r.p[15] === 0 && sw({ sub_on: 1, dco_sub: 255 }).r.p[15] === T.DCO_SUB[255], "SUB off: DCO SUB 0");
check(sw({ lfo_trigger: 1 }).r.p.join() === sw({}).r.p.join(), "LFO TRIGGER dropped");
/* a decaying PWM by ENV gives less width than a held one */
check(sw({ pwm_source: 2, pulse: 1, dco_pwm: 255, env_s: 0, env_d: 60 }).r.p[3] < sw({ pwm_source: 2, pulse: 1, dco_pwm: 255, env_s: 255 }).r.p[3],
    "PWM BY ENV: the envelope's mean width (a decay to 0 narrower than a held sustain)");

done("juno60");
