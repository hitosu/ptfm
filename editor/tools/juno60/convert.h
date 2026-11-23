// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
//
// The Juno-60 conversion's tables and rules (specs/editor.md, Juno-60 patches as Juno-106 ones), the twin of
// editor/js/juno60.js: buildTab() makes every table and every array the rules read, rounded as the page's file
// stores them (so both sides compute from the same numbers); convert() is the page's convert(), the same integer and
// double arithmetic in the same order (vectors.json holds its answers for patches made up to test the page's).
#pragma once
#include "quant.h"
#include <functional>

namespace c60 {

// 122 Hz goes to whichever of FLAT and 236 Hz the check finds closer: 236 Hz (README: the check's figures)
constexpr int HPF_122 = 2;

struct Patch60 {             // what the page's from60() hands over (editor/js/conv_juno.js)
    int vcf_env, vcf_lfo, vcf_kybd, vca_level, env_a, env_r, env_d, env_s, vcf_freq, dco_noise, dco_sub, vcf_res, dco_pwm,
        lfo_rate, dco_lfo, lfo_delay;
    int env_polarity, pwm_source, hpf, vca_gate, lfo_trigger, chorus1, pulse, saw, sub_on, chorus2, chorus_off;
};
struct Result { uint8_t p[18]; bool pwmEnv; };

struct Tab {
    // the tables: a Juno-60 tape value (0-255) -> a Juno-106 byte (ENV A and ENV S: one a VCA MODE)
    uint8_t ENV_A_ENV[256], ENV_A_GATE[256], ENV_D[256], VCF_FREQ[256], VCF_RES[256], LFO_RATE[256], LFO_DELAY[256], DCO_LFO[256], DCO_PWM[256],
        DCO_SUB[256], DCO_NOISE[256], VCA_LEVEL[256], ENV_S_ENV[256], ENV_S_GATE[256];
    // what the per-patch rules read (log2 Hz and octaves in 1 / 10000; times in us)
    int HZ106[4096], HZ60[256], ENV60P[256], ENV60N[256], VLFO60[256], KBD60[256], CAP, FLOOR;
    // ENV R from an envelope level L, L one of 33 (j / 32, j = 0 taken as 1 / 64): the Juno-60's time constant (us);
    // its time to a tenth of L (RRAT60) and its sound's time to fall 20 dB through its VCA (RSND60), in time constants
    // x 10000; PT/FM's times (in 0.1 ms) for the same, 128 bytes a level (R106ENV, RSND106)
    long long R60TAU[256], RRAT60[33], RSND60[33], R106ENV[33 * 128], RSND106[33 * 128];
    double ACOEF[256], DCOEF[256];
    int Q15[106];
};

inline int argmin(int n, const std::function<double(int)> &err)
{
    int best = 0;
    double be = err(0);
    for (int b = 1; b < n; b++) { double e = err(b); if (e < be) { be = e; best = b; } }
    return best;
}
inline double r9(double x) { return std::round(x * 1e9) / 1e9; }
inline long long r4(double x) { return (long long)std::llround(x * 1e4); }

inline void buildTab(Tab &t)
{
    using namespace q;
    double a12_106[128], a3_106[128];
    for (int b = 0; b < 128; b++) attackSound106(b, a12_106[b], a3_106[b]);
    double h106[128], p106[128], d106[128], fq106[128], k106[128], lf106[128], ld106[128], dl106[128], w106[128],
        sb106[128], nz106[128], lv106[128], sdb106[128], sl106[128];
    for (int b = 0; b < 128; b++) {
        attack106(b, h106[b], p106[b]);
        d106[b] = decay106(b);
        fq106[b] = std::log2(hz106(b << 7));
        k106[b] = resK106(b);
        lf106[b] = lfoHz106(b);
        ld106[b] = lfoDelay106(b);
        dl106[b] = dcoLfo106(b);
        w106[b] = pwm106(b);
        sb106[b] = 20 * std::log10(std::max(sub106(b), 1e-4));
        nz106[b] = 20 * std::log10(std::max(noise106(b), 1e-4));
        lv106[b] = level106(b);
        sdb106[b] = susDb106(b);
        sl106[b] = susLevel106(b);
    }
    const double E1 = 0.001, E10 = 0.010;
    for (int v = 0; v < 256; v++) {
        // ENV A: with GATE the envelope's times to half its peak and to its peak (it moves the filter alone); with ENV
        // the sound's, through each VCA, to within 12 dB and 3 dB of its peak; each pair closest on a logarithmic scale
        double h, p, s12, s3;
        attack60(v, h, p);
        attackSound60(v, s12, s3);
        t.ENV_A_GATE[v] = argmin(128, [&](int b) {
            double a = std::log((h + E1) / (h106[b] + E1)), c = std::log((p + E1) / (p106[b] + E1));
            return a * a + c * c; });
        t.ENV_A_ENV[v] = argmin(128, [&](int b) {
            double a = std::log((s12 + E1) / (a12_106[b] + E1)), c = std::log((s3 + E1) / (a3_106[b] + E1));
            return a * a + c * c; });
        double d = decay60(v);
        t.ENV_D[v] = argmin(128, [&](int b) { return std::fabs(std::log((d + E1) / (d106[b] + E1))); });
        double f = std::log2(hz60(v));
        t.VCF_FREQ[v] = argmin(128, [&](int b) { return std::fabs(f - fq106[b]); });
        double k = resK60(v);
        t.VCF_RES[v] = argmin(128, [&](int b) { return std::fabs(k - k106[b]); });
        double lf = lfoHz60(v);
        t.LFO_RATE[v] = argmin(128, [&](int b) { return std::fabs(std::log(lf / lf106[b])); });
        double ld = lfoDelay60(v);
        t.LFO_DELAY[v] = argmin(128, [&](int b) { return std::fabs(std::log((ld + E10) / (ld106[b] + E10))); });
        double dl = dcoLfo60(v);
        t.DCO_LFO[v] = argmin(128, [&](int b) { return std::fabs(std::log((dl + 0.02) / (dl106[b] + 0.02))); });
        double w = pwm60(v);
        t.DCO_PWM[v] = argmin(106, [&](int b) { return std::fabs(w - w106[b]); });
        double sb = 20 * std::log10(std::max(sub60(v), 1e-4)), nz = 20 * std::log10(std::max(noise60(v), 1e-4));
        t.DCO_SUB[v] = argmin(128, [&](int b) { return std::fabs(sb - sb106[b]); });
        t.DCO_NOISE[v] = argmin(128, [&](int b) { return std::fabs(nz - nz106[b]); });
        double lv = level60(v);
        t.VCA_LEVEL[v] = argmin(128, [&](int b) { return std::fabs(lv - lv106[b]); });
        double sd = susDb60(v), sl = susLevel60(v);
        t.ENV_S_ENV[v] = argmin(128, [&](int b) { return std::fabs(sd - sdb106[b]); });
        t.ENV_S_GATE[v] = argmin(128, [&](int b) { return std::fabs(sl - sl106[b]); });
        // the rules' arrays
        t.HZ60[v] = (int)r4(f);
        t.ENV60P[v] = (int)r4(envOct60(v, false));
        t.ENV60N[v] = (int)r4(envOct60(v, true));
        t.VLFO60[v] = (int)r4(vcfLfoOct60(v));
        t.KBD60[v] = (int)r4(2 * kybd60(v));
        t.R60TAU[v] = std::llround(decRelTau60(v) * 1e6);
        t.ACOEF[v] = r9(1 - std::exp(-0.001 / attackTau60(v)));
        t.DCOEF[v] = r9(1 - std::exp(-0.001 / decRelTau60(v)));
    }
    for (int i = 0; i < 4096; i++) t.HZ106[i] = (int)r4(std::log2(kr106::kV4Hz[i]));
    t.CAP = (int)r4(std::log2(HZ_CAP));
    t.FLOOR = (int)r4(std::log2(HZ_FLOOR));
    for (int j = 0; j <= 32; j++) {
        double L = std::max(j, 1) / (j ? 32.0 : 64.0);
        t.RRAT60[j] = r4(releaseRatio60(L));
        t.RSND60[j] = r4(releaseSoundRatio60(L));
        for (int b = 0; b < 128; b++) {                    // (in 0.1 ms)
            t.R106ENV[j * 128 + b] = std::llround(release106(b, L) * 1e4);
            t.RSND106[j * 128 + b] = std::llround(releaseSound106(b, L) * 1e4);
        }
    }
    for (int b = 0; b < 106; b++) t.Q15[b] = b * 32767 / 105;
}

// ----------------------------------------------------------------------------------- the per-patch rules ----
// the cutoff (log2 Hz x 10000) at a DAC code, and held between the floor and the cap
inline int lhz(const Tab &t, int code) { return t.HZ106[(code < 0 ? 0 : code > 0x3FFF ? 0x3FFF : code) >> 2]; }
inline int held(const Tab &t, int x) { return x < t.FLOOR ? t.FLOOR : x > t.CAP ? t.CAP : x; }
inline int iabs(int x) { return x < 0 ? -x : x; }

// VCF ENV: the movement at the envelope's peak, from the patch's own cutoff, at its polarity's scale
inline int vcfEnv(const Tab &t, const Patch60 &v, int ea0)
{
    int l60 = t.HZ60[v.vcf_freq], m60 = v.env_polarity ? -t.ENV60N[v.vcf_env] : t.ENV60P[v.vcf_env];
    int mov60 = held(t, l60 + m60) - held(t, l60), base = held(t, lhz(t, ea0));
    int best = 0, be = 1 << 30;
    for (int b = 0; b < 128; b++) {
        int e = (2 * b * q::ENV_MAX) >> 8, ea;               // (mul8x16_hi(2 x VCF ENV, ENV_MAX))
        if (!v.env_polarity) ea = ea0 + e > 0x3FFF ? 0x3FFF : ea0 + e;
        else ea = e > ea0 ? 0 : ea0 - e;
        int err = iabs(mov60 - (held(t, lhz(t, ea)) - base));
        if (err < be) { be = err; best = b; }
    }
    return best;
}
// VCF LFO: the swing from the LFO's peak down to its trough, at the patch's own cutoff
inline int vcfLfo(const Tab &t, const Patch60 &v, int ea0)
{
    int l60 = t.HZ60[v.vcf_freq], d = t.VLFO60[v.vcf_lfo];
    int sw60 = held(t, l60 + d) - held(t, l60 - d);
    int best = 0, be = 1 << 30;
    for (int b = 0; b < 128; b++) {
        int sig = (((2 * b * 255) >> 8) * 8191) >> 9;        // (calc_vcf_lfo_signal at the LFO's peak, its onset full)
        int up = ea0 + sig > 0x3FFF ? 0x3FFF : ea0 + sig, dn = sig > ea0 ? 0 : ea0 - sig;
        int err = iabs(sw60 - (held(t, lhz(t, up)) - held(t, lhz(t, dn))));
        if (err < be) { be = err; best = b; }
    }
    return best;
}
// VCF KYBD: the cutoff two octaves up and down (C6, C2) from C4, at the patch's own cutoff
inline int vcfKybd(const Tab &t, const Patch60 &v, int ea0)
{
    int l60 = t.HZ60[v.vcf_freq], k = t.KBD60[v.vcf_kybd], b60 = held(t, l60);
    int up60 = held(t, l60 + k) - b60, dn60 = b60 - held(t, l60 - k), base = held(t, lhz(t, ea0));
    int best = 0, be = 1 << 30;
    for (int b = 0; b < 128; b++) {
        int m = (2 * b * 2304) >> 8;                          // (mul8x16_hi(2 x KYBD, 24 semitones x 96))
        int up = ea0 + m > 0x3FFF ? 0x3FFF : ea0 + m, dn = m > ea0 ? 0 : ea0 - m;
        int err = iabs(up60 - (held(t, lhz(t, up)) - base)) + iabs(dn60 - (base - held(t, lhz(t, dn))));
        if (err < be) { be = err; best = b; }
    }
    return best;
}
// a note's first second on the Juno-60's envelope, stepped by the ms (its attack toward 1.2 up to the peak, its decay
// toward the sustain): its mean level, and its level at the end (where a note is usually let go)
inline void firstSecond(const Tab &t, const Patch60 &v, double &mean, double &last)
{
    double a = t.ACOEF[v.env_a], d = t.DCOEF[v.env_d], s = v.env_s / 255.0, env = 0, sum = 0;
    if (s < 0.001) s = 0.001;
    bool attack = true;
    for (int i = 0; i < 1000; i++) {
        if (attack) { env = env + (1.2 - env) * a; if (env >= 1) { env = 1; attack = false; } }
        else env = env + (s - env) * d;
        sum = sum + env;
    }
    mean = sum / 1000;
    last = env;
}
// ENV R, from the level a note has a second in: with VCA MODE ENV the time the sound takes to fall 20 dB through each
// VCA (what is heard); with GATE (the VCA shut at once, the envelope moving the filter alone) the envelope's time to a
// tenth of its level
inline int envR(const Tab &t, const Patch60 &v)
{
    const double E = 1000.0 * 10000.0;                     // (1 ms, in the times' units)
    double mean, last;
    firstSecond(t, v, mean, last);
    long long j = std::llround(last * 32);
    if (j > 32) j = 32;
    double t60 = (double)(t.R60TAU[v.env_r] * (v.vca_gate ? t.RRAT60[j] : t.RSND60[j]));
    const long long *r106 = (v.vca_gate ? t.R106ENV : t.RSND106) + 128 * j;
    int best = 0;
    double be = 1e300;
    for (int b = 0; b < 128; b++) {
        double t106 = (double)(r106[b] * 1000000);         // (0.1 ms -> us x 10000)
        double a = (t60 + E) / (t106 + E), c = (t106 + E) / (t60 + E), err = a > c ? a : c;
        if (err < be) { be = err; best = b; }
    }
    return best;
}
// PWM SOURCE ENV (no Juno-106 counterpart): MAN, the width the envelope gives on average over a note's first second
inline int pwmEnv(const Tab &t, const Patch60 &v)
{
    double mean, last;
    firstSecond(t, v, mean, last);
    double target = v.dco_pwm / 255.0 * mean;
    int best = 0;
    double be = 1e300;
    for (int b = 0; b < 106; b++) {
        double err = std::fabs(target - t.Q15[b] / 32768.0);
        if (err < be) { be = err; best = b; }
    }
    return best;
}

inline Result convert(const Tab &t, const Patch60 &v)
{
    Result r{};
    uint8_t *p = r.p;
    p[0] = t.LFO_RATE[v.lfo_rate];
    p[1] = t.LFO_DELAY[v.lfo_delay];
    p[2] = t.DCO_LFO[v.dco_lfo];
    r.pwmEnv = v.pwm_source == 2 && v.pulse && v.dco_pwm > 0;
    p[3] = r.pwmEnv ? (uint8_t)pwmEnv(t, v) : t.DCO_PWM[v.dco_pwm];
    p[4] = t.DCO_NOISE[v.dco_noise];
    p[5] = t.VCF_FREQ[v.vcf_freq];
    p[6] = t.VCF_RES[v.vcf_res];
    int ea0 = p[5] << 7;
    p[7] = (uint8_t)vcfEnv(t, v, ea0);
    p[8] = (uint8_t)vcfLfo(t, v, ea0);
    p[9] = (uint8_t)vcfKybd(t, v, ea0);
    p[10] = t.VCA_LEVEL[v.vca_level];
    p[11] = (v.vca_gate ? t.ENV_A_GATE : t.ENV_A_ENV)[v.env_a];
    p[12] = t.ENV_D[v.env_d];
    p[13] = (v.vca_gate ? t.ENV_S_GATE : t.ENV_S_ENV)[v.env_s];
    p[14] = (uint8_t)envR(t, v);
    p[15] = v.sub_on ? t.DCO_SUB[v.dco_sub] : 0;
    int chorus = v.chorus1 && v.chorus2 ? 2 : v.chorus1 ? 1 : v.chorus2 ? 2 : 0;   // (off, I, II)
    p[16] = (uint8_t)(0x02 | (v.pulse ? 0x08 : 0) | (v.saw ? 0x10 : 0) | (chorus == 0 ? 0x20 : chorus == 1 ? 0x40 : 0));
    int hpf = v.hpf == 0 ? HPF_122 : v.hpf == 1 ? 1 : v.hpf;   // (the tape's 122 Hz, FLAT, 269 Hz, 571 Hz)
    int man = v.pwm_source != 0;                               // (LFO; MAN, and ENV's rule)
    p[17] = (uint8_t)(man | (v.env_polarity ? 2 : 0) | (v.vca_gate ? 4 : 0) | (3 - hpf) << 3);
    return r;
}

}  // namespace c60
