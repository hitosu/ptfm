// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
//
// The quantities the Juno-60 conversion compares (specs/editor.md, Juno-60 patches as Juno-106 ones), each defined
// the same on both sides:
//   - the Juno-60's from Ultramaster KR-106's Juno-60 model (GPL-3.0), its sound code: what its Juno-60 mode plays
//     (its parameter setting, its voice, envelope and LFO code; not its value displays), its headers included from
//     the path the build gives (KR106/Source/DSP);
//   - the Juno-106's from PT/FM's own engine (fw/core/eng/juno: its tables, indexed by the patch's byte as on the
//     FM-1, and its integer envelope and LFO stepped as the engine steps them).
// A Juno-60 value is the tape's byte (0-255); KR-106 reads it as its slider (tape / 255; ENV A from 28, LFO DELAY
// backwards from 255 to 18, as its own tape import reads them).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

#include "KR106_DSP.h"
#include "KR106_DSP_SetParam.h"
extern "C" {
#include "juno_tables.h"
}

namespace q {

constexpr double FS = 44100.0;
constexpr int ENV_MAX = 0x3FFF;
constexpr double HZ_CAP = 0.85 * FS / 2;       // PT/FM's filter: the cutoff held under 0.85 x Nyquist
constexpr double HZ_FLOOR = 20.0;              // (below, a filter's movement is not heard)

// ---------------------------------------------------------------- the Juno-60's sliders as KR-106 reads them ----
inline double slider(int v) { return v / 255.0; }
inline double attackSlider(int v) { return std::clamp((v - 28) / 227.0, 0.0, 1.0); }
inline double delaySlider(int v) { return std::clamp((255 - v) / 237.0, 0.0, 1.0); }

// ------------------------------------------------------------------------------------------- PT/FM's engine ----
inline double tick106() { return 1.0 / (JUNO_TICK_Q16 / 65536.0 * FS / 64.0); }   // s (~236.2 Hz)
inline uint16_t calcDecay(uint16_t value, uint16_t coeff)                          // juno.c calc_decay
{
    uint32_t vh = value >> 8, vl = value & 0xFFu, ch = coeff >> 8, cl = coeff & 0xFFu;
    return (uint16_t)(vh * ch + ((vh * cl) >> 8) + ((vl * ch) >> 8));
}
inline int sus106(int b) { return b ? b * 0x3F80 / 127 : 16; }                  // juno.c patch_set
// a level reached n ticks after note on (the first at note on, the next on average half a tick later)
inline double tickTime(int n) { return n <= 0 ? 0.0 : (n - 0.5) * tick106(); }

// ENV A: the times to half the peak and to the peak
inline void attack106(int b, double &half, double &peak)
{
    uint32_t env = 0, inc = JUNO_ATK[b];
    int n = 0, nh = -1;
    for (;; n++) {
        env += inc;                                    // (env_tick, ATTACK)
        if (env >= (uint32_t)ENV_MAX) env = ENV_MAX;
        if (nh < 0 && env >= ENV_MAX / 2 + 1) nh = n;
        if (env >= (uint32_t)ENV_MAX) break;
    }
    half = tickTime(nh);
    peak = tickTime(n);
}
// ENV A heard (VCA MODE ENV): the times the sound, through the VCA, takes to come within 12 dB and within 3 dB of its
// peak
inline double vca106(int envInt);
inline void attackSound106(int b, double &t12, double &t3)
{
    uint32_t env = 0, inc = JUNO_ATK[b];
    const double g12 = std::pow(10.0, -12 / 20.0), g3 = std::pow(10.0, -3 / 20.0);
    int n12 = -1, n3 = -1;
    for (int n = 0; n < 1000000; n++) {
        env += inc;
        if (env >= (uint32_t)ENV_MAX) env = ENV_MAX;
        double g = vca106((int)env) / vca106(ENV_MAX);
        if (n12 < 0 && g >= g12) n12 = n;
        if (g >= g3) { n3 = n; break; }
    }
    t12 = tickTime(n12);
    t3 = tickTime(n3);
}
// ENV D: 90 % of the way from the peak to the sustain (sustain 0)
inline double decay106(int b)
{
    uint16_t diff = ENV_MAX - 16, to = (uint16_t)((ENV_MAX - 16) / 10);
    int n = 0;
    while (diff > to && n < 1000000) { diff = calcDecay(diff, JUNO_DECREL[b]); n++; }
    return n * tick106();
}
// ENV R: from an envelope level L to a tenth of it
inline double release106(int b, double L)
{
    uint16_t env = (uint16_t)std::lround(L * ENV_MAX), to = (uint16_t)(env / 10);
    int n = 0;
    while (env > to && n < 1000000) { env = calcDecay(env, JUNO_DECREL[b]); n++; }
    return n * tick106();
}
// ENV R heard (VCA MODE ENV): the time the sound, through the VCA, takes to fall 20 dB from an envelope level L
constexpr double RELEASE_DROP_DB = 20;
inline double vca106(int envInt);
inline double releaseSound106(int b, double L)
{
    uint16_t env = (uint16_t)std::lround(L * ENV_MAX);
    double to = vca106(env) * std::pow(10.0, -RELEASE_DROP_DB / 20.0);
    int n = 0;
    while (vca106(env) > to && n < 1000000) { env = calcDecay(env, JUNO_DECREL[b]); n++; }
    return n * tick106();
}
// ENV S: the sustain's loudness through the VCA (dB), its envelope level (0-1)
inline double vca106(int envInt)                                                   // juno.c vca_target
{
    uint32_t x = ((uint32_t)envInt * 1020u) >> 8, i = x >> 8, f = x & 255u;
    int32_t g = JUNO_VCA[i] + (((JUNO_VCA[i + 1u] - JUNO_VCA[i]) * (int32_t)f) >> 8);
    return g / 32768.0;
}
inline double susDb106(int b) { return 20 * std::log10(std::max(vca106(sus106(b)), 1e-5)); }
inline double susLevel106(int b) { return sus106(b) / (double)ENV_MAX; }
// VCF FREQ: the cutoff in Hz at a DAC code (14 bits), as the engine's table reads it (before its 0.85 x Nyquist cap)
inline double hz106(int code) { return kr106::kV4Hz[std::clamp(code, 0, 0x3FFF) >> 2]; }
// VCF RES: the feedback k
inline double resK106(int b) { return JUNO_RESK[b] / 4096.0; }
// LFO RATE: Hz (4 half-sweeps of ceil(8192 / coeff) ticks)
inline double lfoHz106(int b) { int c = JUNO_LFOSPD[b]; return 1.0 / (4 * ((8192 + c - 1) / c) * tick106()); }
// LFO DELAY: the time to half depth (the hold, then the ramp in steps)
inline double lfoDelay106(int b)
{
    uint32_t hold = 0, ramp = 0, hinc = JUNO_ATK[b], rinc = JUNO_LFORAMP[b >> 4];
    bool inHold = hinc < (uint32_t)ENV_MAX;
    for (int n = 1; n < 1000000; n++) {
        int amp;
        if (inHold) {
            hold += hinc;
            if (hold >= 0x4000u) inHold = false;
            amp = 0;
        } else {
            ramp += rinc;
            if (ramp >= 1u << 16) { ramp = 0xFFFF; amp = 255; } else amp = (int)(ramp >> 8);
        }
        if (amp >= 128) return tickTime(n);
    }
    return 1e9;
}
// DCO LFO: the vibrato's peak in semitones (LFO 8191 x onset 255 x the depth's coefficient, Q24 octaves)
inline double dcoLfo106(int b)
{
    int64_t lf = ((int64_t)8191 * 255 * (int64_t)(JUNO_DCOLFO[b] * 21931)) >> 21;
    return lf / (double)(1 << 24) * 12.0;
}
// DCO PWM: the pulse's width (0.5-0.95; full at 105)
inline double pwm106(int b) { int32_t q15 = std::min(b, 105) * 32767 / 105; return 0.5 + 0.45 * q15 / 32768.0; }
// DCO SUB, DCO NOISE: against the saw (its gain 0.6 in Q15)
inline double sub106(int b) { return JUNO_SUB[b] / 19661.0; }
inline double noise106(int b) { return JUNO_NOISE[b] / 19661.0; }
// VCA LEVEL: dB
inline double level106(int b) { return 20 * std::log10(JUNO_LEVEL[b] / 4096.0); }

// ------------------------------------------------------------------------------- KR-106's Juno-60 model ----
// ENV A: KR-106's envelope itself, run in its Juno-60 mode (charging toward 1.2, stopping at the peak)
inline void attack60(int v, double &half, double &peak)
{
    kr106::ADSR a;
    a.mModel = kr106::kJ60;
    a.SetSampleRate((float)FS);
    a.SetAttackTau(kr106::ADSR::AttackTauJ60((float)attackSlider(v)));
    a.SetSustain(1.f);
    a.NoteOn();
    int n = 0, nh = -1;
    while (a.mState == kr106::ADSR::kAttack && n < 100000000) {
        a.Process();
        n++;
        if (nh < 0 && a.mEnv >= 0.5f) nh = n;
    }
    half = nh / FS;
    peak = n / FS;
}
inline double attackTau60(int v) { return kr106::ADSR::AttackTauJ60((float)attackSlider(v)); }
// ENV A heard (VCA MODE ENV): the times the sound, through the Juno-60's VCA, takes to come within 12 dB and 3 dB of
// its peak (KR-106's envelope run in its Juno-60 mode)
inline void attackSound60(int v, double &t12, double &t3)
{
    kr106::ADSR a;
    a.mModel = kr106::kJ60;
    a.SetSampleRate((float)FS);
    a.SetAttackTau((float)attackTau60(v));
    a.SetSustain(1.f);
    a.NoteOn();
    const double g12 = std::pow(10.0, -12 / 20.0), g3 = std::pow(10.0, -3 / 20.0), top = kr106::VCAGainJ60(1.f);
    int n = 0, n12 = -1, n3 = -1;
    while (a.mState == kr106::ADSR::kAttack && n < 100000000) {
        a.Process();
        n++;
        double g = kr106::VCAGainJ60(a.mEnv) / top;
        if (n12 < 0 && g >= g12) n12 = n;
        if (g >= g3) { n3 = n; break; }
    }
    t12 = n12 / FS;
    t3 = (n3 < 0 ? n : n3) / FS;
}
inline double decRelTau60(int v) { return kr106::ADSR::DecRelTauJ60((float)slider(v)); }
// ENV D: from the peak, 90 % of the way to the sustain (sustain 0: KR-106's floor 0.001)
inline double decay60(int v)
{
    kr106::ADSR a;
    a.mModel = kr106::kJ60;
    a.SetSampleRate((float)FS);
    a.SetDecayTau((float)decRelTau60(v));
    a.SetSustain(0.001f);
    a.mState = kr106::ADSR::kDecay;
    a.mEnv = 1.f;
    const float to = 0.001f + 0.1f * (1.f - 0.001f);
    int n = 0;
    while (a.mEnv > to && n < 100000000) { a.Process(); n++; }
    return n / FS;
}
// ENV R: from a level L to L / 10, heading toward KR-106's -0.1 (exactly an exponential: the closed form)
inline double releaseRatio60(double L)
{
    const double t = kr106::ADSR::kReleaseTarget;  // -0.1
    return std::log((L - t) / (L / 10 - t));
}
// ENV R heard (VCA MODE ENV): the time the sound, through the Juno-60's VCA, takes to fall 20 dB from an envelope
// level L, in time constants (the envelope's level where the VCA is 20 dB down, found by halving)
inline double releaseSoundRatio60(double L)
{
    const double t = kr106::ADSR::kReleaseTarget;
    double to = kr106::VCAGainJ60((float)L) * std::pow(10.0, -RELEASE_DROP_DB / 20.0), lo = 0, hi = L;
    for (int i = 0; i < 60; i++) { double m = (lo + hi) / 2; if (kr106::VCAGainJ60((float)m) > to) hi = m; else lo = m; }
    return std::log((L - t) / ((lo + hi) / 2 - t));
}
// ENV S: the sustain's loudness through the Juno-60's VCA (dB), and its level
inline double susLevel60(int v) { return std::max(slider(v), 0.001); }   // (KR106_DSP_SetParam kEnvS: 0.001 floor)
inline double susDb60(int v) { return 20 * std::log10(std::max((double)kr106::VCAGainJ60((float)susLevel60(v)), 1e-5)); }
// VCF FREQ: Hz
inline double hz60(int v) { return kr106::j60_vcf_freq_from_slider((float)slider(v)); }
// VCF ENV: octaves at the envelope's peak (KR106Voice.h, the J6 / J60 cutoff: env x the slider x 6.931, or 7.766
// with the polarity inverted, in natural-log units)
constexpr double ENV_SCALE_POS = 6.931, ENV_SCALE_NEG = 7.766;
inline double envOct60(int v, bool neg) { return slider(v) * (neg ? ENV_SCALE_NEG : ENV_SCALE_POS) / std::log(2.0); }
// VCF LFO: octaves at the LFO's peak
inline double vcfLfoOct60(int v) { return kr106::Voice<float>::vcfLfoDepth_j60((float)slider(v)) / 12.0; }
// VCF KYBD: the cutoff's octaves per octave (around C4)
inline double kybd60(int v) { return slider(v); }
// VCF RES: the feedback k (its soft clip included)
inline double resK60(int v) { return kr106::VCF::SoftClipK(kr106::VCF::ResK_J6((float)slider(v))); }
// LFO RATE: Hz
inline double lfoHz60(int v) { return kr106::LFO::lfoFreqJ60((float)slider(v)); }
// LFO DELAY: the time to half depth (an RC fade-in, tau = KR-106's delay)
inline double lfoDelay60(int v) { return kr106::LFO::lfoDelayJ60((float)delaySlider(v)) * std::log(2.0); }
// DCO LFO: the vibrato's peak in semitones
inline double dcoLfo60(int v) { return kr106::Voice<float>::dcoLfoDepth_j60((float)slider(v)); }
// DCO PWM: the width (KR106Voice.h: 0.5 + 0.45 x the slider)
inline double pwm60(int v) { return 0.5 + 0.45 * slider(v); }
// DCO SUB, DCO NOISE: against the saw, in the Juno-60's mix
inline double sub60(int v) { return kr106::kSubAmpJ6 * kr106::Voice<float>::dcoSubLevel_j60((float)slider(v)) / kr106::kSawAmpJ6; }
inline double noise60(int v) { return kr106::kNoiseAmpJ6 * kr106::Voice<float>::dcoNoiseLevel_j60((float)slider(v)) / kr106::kSawAmpJ6; }
// VCA LEVEL: dB (KR106_DSP_SetParam kVcaLevel: -10 to +10 dB, the same law on both)
inline double level60(int v) { return -10 + 20 * slider(v); }

}  // namespace q
