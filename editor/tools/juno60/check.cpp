// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
//
// The Juno-60 conversion's check (specs/editor.md, Juno-60 patches as Juno-106 ones): each Juno-60 factory patch
// played through Ultramaster KR-106 (GPL-3.0) in its Juno-60 mode and its conversion through PT/FM's own Juno-106
// engine (fw/core/eng/juno, built here as it is), at C2, C4 and C6, 2 s held and its release, compared measure by
// measure; the yardstick measured in the same run: the Juno-106 factory patches through KR-106 in its Juno-106 mode
// and through PT/FM's engine, the same notes and measures. A converted patch passes a measure when its figure (the
// worst of its three notes) is within the 95th percentile of the Juno-106 patches' figures. KR-106 is given each byte
// as PT/FM's engine reads it (DCO LFO through KR-106's own undoing of its slider taper, DCO PWM against 105), one
// voice, its per-voice variations, its drift and its analog noise floor off, the chorus off on both sides, RANGE 8'.
// Also: the HPF's 122 Hz rendered both ways (FLAT, 236 Hz); PWM SOURCE ENV's rule against other choices.
//
//   check KR106_DIR figs SHARD SHARDS > PART.csv      a shard's renders and their figures (run.sh runs several)
//   check KR106_DIR report REPORT.md PART.csv...      the figures put together: the report
//   (KR106_DIR/tools: juno60_group1.csv, juno60_group2.csv (the Juno-60 factory tapes' values), preset-gen/
//   Factory_Patches.pat (the Juno-106 factory patches); read here, never copied)
#include "convert.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <map>
#include <array>
#include <algorithm>
#include <complex>

extern "C" {
#include "juno.h"
void dx7_init(void);                                       // (the tables the engine's pitch and tanh read)
}

using std::string;
using std::vector;

// 2 s held, 2.5 s of release, in whole blocks of both engines (256 and 64 frames)
static const int FSI = 44100, HOLD = 344 * 256, TAIL = 432 * 256, N = HOLD + TAIL;
static const int NOTES[3] = {36, 60, 84};

enum EP {
    kBenderDco = 0, kBenderVcf, kArpRate, kLfoRate, kLfoDelay, kDcoLfo, kDcoPwm, kDcoSub, kDcoNoise, kHpfFreq,
    kVcfFreq, kVcfRes, kVcfEnv, kVcfLfo, kVcfKbd, kVcaLevel, kEnvA, kEnvD, kEnvS, kEnvR,
    kTranspose, kHold, kArpeggio, kDcoPulse, kDcoSaw, kDcoSubSw, kChorusOff, kChorusI, kChorusII,
    kOctTranspose, kArpMode, kArpRange, kLfoMode, kPwmMode, kVcfEnvInv, kVcaMode,
    kBender, kTuning, kPower, kPortaMode, kPortaRate, kTransposeOffset, kBenderLfo, kAdsrMode, kMasterVol
};

// ------------------------------------------------------------------------------------------------ renders ----
typedef std::map<int, double> Params;

static vector<float> renderKR(bool j60, const Params &pr, int note)
{
    KR106DSP<float> dsp(1);
    dsp.Reset(FSI, 256);
    dsp.SetParam(kPower, 1);
    dsp.SetParam(kMasterVol, 1);
    dsp.SetParam(kAdsrMode, j60 ? 0 : 1);
    dsp.SetParam(kPortaMode, 1);
    dsp.SetParam(kPortaRate, 0);
    dsp.SetParam(kOctTranspose, 1);
    dsp.SetParam(kLfoMode, 0);
    dsp.SetParam(kChorusI, 0);
    dsp.SetParam(kChorusII, 0);
    for (auto &kv : pr) dsp.SetParam(kv.first, kv.second);
    dsp.SetDriftAmount(0);
    dsp.mNoiseFloorMul = 0;
    dsp.mMainsMul = 0;
    dsp.ForEachVoice([](kr106::Voice<float> &v) {
        for (int i = 0; i < kr106::Voice<float>::kNumVarianceParams; i++) v.SetVariance(i, 0.f);
        v.mVcfFreqOffset = 0.f;
        v.mPitchOffset = 0.f;
    });
    vector<float> out(N), l(256), r(256);
    float *o[2] = {l.data(), r.data()};
    for (int s = 0; s < FSI / 4; s += 256) dsp.ProcessBlock(nullptr, o, 2, 256);   // (settled)
    dsp.NoteOn(note, 127);
    for (int s = 0; s < N; s += 256) {
        if (s == HOLD) dsp.NoteOff(note);
        int n = std::min(256, N - s);
        dsp.ProcessBlock(nullptr, o, 2, n);
        for (int i = 0; i < n; i++) out[s + i] = l[i];
    }
    return out;
}

static vector<float> renderPT(const uint8_t *patch, int note)
{
    static juno_voice_t v;
    memset(&v, 0, sizeof v);
    juno_init();
    uint32_t acc = 0;
    int32_t buf[JUNO_N];
    vector<float> out(N);
    for (int s = 0; s < FSI / 4; s += JUNO_N) {           // (the noise generator settled, as KR-106's)
        acc += juno_tick_q16;
        int tick = acc >= 65536u;
        if (tick) acc -= 65536u;
        juno_block(tick);
    }
    juno_note_on(&v, patch, note, 4096);
    for (int s = 0; s < N; s += JUNO_N) {
        if (s == HOLD) juno_note_off(&v);
        acc += juno_tick_q16;
        int tick = acc >= 65536u;
        if (tick) acc -= 65536u;
        juno_block(tick);
        memset(buf, 0, sizeof buf);
        juno_render(&v, buf, tick);
        for (int i = 0; i < JUNO_N && s + i < N; i++) out[s + i] = buf[i] / 32768.f;
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ measures ----
enum { M_ATTACK, M_LEVEL, M_RELEASE, M_BRIGHT, M_PITCH, M_VIBDEPTH, M_VIBRATE, M_COUNT };
static const char *MNAME[M_COUNT] = {"attack (octaves of time)", "level while held (dB)", "release (octaves of time)",
    "brightness (octaves)", "pitch (cents)", "vibrato depth (cents)", "vibrato rate (octaves)"};
struct Fig { double m[M_COUNT]; double peakDb; };

// the level every 10 ms (dB), its power the mean over the 50 ms around (a low note's period is longer than 10 ms)
static vector<double> rmsDb(const vector<float> &x)
{
    const int W = 441;
    vector<double> p(x.size() / W), d(p.size());
    for (size_t k = 0; k < p.size(); k++) {
        double s = 0;
        for (int i = 0; i < W; i++) { double v = x[k * W + i]; s += v * v; }
        p[k] = s / W;
    }
    for (size_t k = 0; k < p.size(); k++) {
        double s = 0;
        int n = 0;
        for (int j = -2; j <= 2; j++) if ((int)k + j >= 0 && k + j < p.size()) { s += p[k + j]; n++; }
        d[k] = 10 * std::log10(s / n + 1e-20);
    }
    return d;
}
static void fft(vector<std::complex<double>> &a)
{
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2 * M_PI / len;
        std::complex<double> w(cos(ang), sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> c(1, 0);
            for (size_t j = 0; j < len / 2; j++) {
                auto u = a[i + j], t = a[i + j + len / 2] * c;
                a[i + j] = u + t;
                a[i + j + len / 2] = u - t;
                c *= w;
            }
        }
    }
}
// the spectrum's centroid (power-weighted, 20 Hz - 20 kHz: a floor far under the sound, as an integer engine's at a
// closed filter, does not move it) over 2048-sample frames of the held part
static vector<double> centroids(const vector<float> &x, vector<double> &lev)
{
    const int W = 2048;
    vector<double> c;
    lev.clear();
    for (int s = 0; s + W <= HOLD; s += 1024) {
        vector<std::complex<double>> a(W);
        double e = 0;
        for (int i = 0; i < W; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / W);
            a[i] = x[s + i] * w;
            e += x[s + i] * x[s + i];
        }
        fft(a);
        double num = 0, den = 0;
        for (int k = 1; k < W / 2; k++) {
            double f = k * (double)FSI / W;
            if (f < 20 || f > 20000) continue;
            double m = std::norm(a[k]);
            num += f * m;
            den += m;
        }
        c.push_back(den > 0 ? num / den : 0);
        lev.push_back(10 * std::log10(e / W + 1e-20));
    }
    return c;
}
// the oscillators' pitch every 10 ms over the held part, in cents from the note: the autocorrelation's peak within
// the patch's vibrato depth and a semitone of the note's period (so a filter ringing elsewhere, or noise, is not
// taken for it); frames whose peak is weak left out (NaN)
static vector<double> pitchTrack(const vector<float> &x, int note, double vibSt)
{
    double f0 = 440.0 * pow(2.0, (note - 69) / 12.0), P = FSI / f0, w = (vibSt + 1) / 12.0;
    int lo = (int)floor(P * pow(2.0, -w)), hi = (int)ceil(P * pow(2.0, w));
    int W = std::max(2 * hi, 256);
    vector<double> out, lev = rmsDb(x);
    double top = *std::max_element(lev.begin(), lev.begin() + HOLD / 441);
    for (int s = FSI / 10; s + W + hi <= HOLD; s += 441) {
        if (lev[s / 441] < top - 40) { out.push_back(NAN); continue; }   // (the sound all but gone: not tracked)
        double best = -2, bl = 0;
        vector<double> r(hi + 2, 0);
        double e0 = 0;
        for (int i = 0; i < W; i++) e0 += (double)x[s + i] * x[s + i];
        for (int L = lo - 1; L <= hi + 1; L++) {
            double num = 0, e1 = 0;
            for (int i = 0; i < W; i++) { num += (double)x[s + i] * x[s + i + L]; e1 += (double)x[s + i + L] * x[s + i + L]; }
            r[L] = num / sqrt(e0 * e1 + 1e-30);
        }
        for (int L = lo; L <= hi; L++) if (r[L] > best && r[L] >= r[L - 1] && r[L] >= r[L + 1]) { best = r[L]; bl = L; }
        if (best < 0.7) { out.push_back(NAN); continue; }
        int L = (int)bl;
        double a = r[L - 1], b = r[L], c = r[L + 1], d = (a - c) / (2 * (a - 2 * b + c) + 1e-30);
        double per = L + std::clamp(d, -0.5, 0.5);
        out.push_back(1200 * log2(P / per));
    }
    return out;
}
static double pct(vector<double> v, double p)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return std::isnan(x); }), v.end());
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    double i = p * (v.size() - 1);
    size_t a = (size_t)i;
    return a + 1 < v.size() ? v[a] + (i - a) * (v[a + 1] - v[a]) : v[a];
}
static double vibRate(const vector<double> &c, double med, double depth)
{
    int n = 0, state = 0, first = -1, last = -1;
    for (size_t i = 0; i < c.size(); i++) {
        if (std::isnan(c[i])) continue;
        double y = c[i] - med;
        int st = y > depth / 4 ? 1 : y < -depth / 4 ? -1 : 0;
        if (st && st != state) { if (state) { n++; if (first < 0) first = (int)i; last = (int)i; } state = st; }
    }
    if (n < 3 || last <= first) return NAN;
    return (n - 1) / 2.0 / ((last - first) * 0.01);
}

// the attack: the time (s) the sound's size (its largest sample over a period of the note, every ms) takes to come
// within 3 dB of its largest while held
static double attackTime(const vector<float> &x, int note)
{
    int W = std::max(88, (int)(FSI / (440.0 * pow(2.0, (note - 69) / 12.0))) + 1);
    vector<double> e;
    for (int s = 0; s + W <= HOLD; s += 44) {
        double m = 0;
        for (int i = 0; i < W; i++) m = std::max(m, (double)std::fabs(x[s + i]));
        e.push_back(m);
    }
    double top = *std::max_element(e.begin(), e.end());
    size_t k = 0;
    while (k < e.size() && e[k] < top * 0.7079) k++;
    return k * 44.0 / FSI;
}

// what is heard: the sound high-passed at 20 Hz, twice (a pulse's DC step through the coupling filters is not heard,
// and each engine shapes it its own way)
static vector<float> heard(const vector<float> &x)
{
    vector<float> y(x.size());
    const double g = exp(-2 * M_PI * 20.0 / FSI);
    double x1 = 0, y1 = 0, z1 = 0, w1 = 0;
    for (size_t i = 0; i < x.size(); i++) {
        y1 = g * (y1 + x[i] - x1); x1 = x[i];
        w1 = g * (w1 + y1 - z1); z1 = y1;
        y[i] = (float)w1;
    }
    return y;
}

static Fig compare(const vector<float> &a0, const vector<float> &b0, int note, double vibSt)
{
    Fig f;
    vector<float> a = heard(a0), b = heard(b0);
    vector<double> da = rmsDb(a), db = rmsDb(b);
    const int H = HOLD / 441;
    double pa = *std::max_element(da.begin(), da.begin() + H), pb = *std::max_element(db.begin(), db.begin() + H);
    f.peakDb = pa - pb;
    auto rel = [](double d, double p) { return std::max(d - p, -50.0); };
    f.m[M_ATTACK] = std::fabs(log2((attackTime(a, note) + 0.005) / (attackTime(b, note) + 0.005)));
    // the level while held, each against its own peak
    double s = 0;
    int n = 0;
    for (int k = 0; k < H; k++) {
        double x = rel(da[k], pa), y = rel(db[k], pb);
        if (x > -50 || y > -50) { s += std::fabs(x - y); n++; }
    }
    f.m[M_LEVEL] = n ? s / n : 0;
    // the release: the times each takes to fall 10, 20 and 30 dB from its level at the note's end (the whole tail, if
    // it never does), their ratios in octaves, the mean (a tail far down, where one release reaches silence and the
    // other fades on, counts no more than it is heard)
    auto fall = [&](const vector<double> &dd, double drop) {
        double from = dd[H - 1];
        for (size_t k = H; k < dd.size(); k++) if (dd[k] <= from - drop) return (k - H + 1) * 0.01;
        return (dd.size() - H) * 0.01;
    };
    s = 0;
    for (double drop : {10.0, 20.0, 30.0}) s += std::fabs(log2((fall(da, drop) + 0.01) / (fall(db, drop) + 0.01)));
    f.m[M_RELEASE] = s / 3;
    // brightness: the centroid where both sound (40 dB under their peaks at most)
    vector<double> la, lb, ca = centroids(a, la), cb = centroids(b, lb);
    double ma = *std::max_element(la.begin(), la.end()), mb = *std::max_element(lb.begin(), lb.end());
    s = 0; n = 0;
    for (size_t k = 0; k < ca.size(); k++)
        if (la[k] > ma - 40 && lb[k] > mb - 40 && ca[k] > 0 && cb[k] > 0) { s += std::fabs(log2(ca[k] / cb[k])); n++; }
    f.m[M_BRIGHT] = n ? s / n : 0;
    // pitch and vibrato
    vector<double> pa_ = pitchTrack(a, note, vibSt), pb_ = pitchTrack(b, note, vibSt);
    double meda = pct(pa_, 0.5), medb = pct(pb_, 0.5);
    double deptha = pct(pa_, 0.95) - pct(pa_, 0.05), depthb = pct(pb_, 0.95) - pct(pb_, 0.05);
    f.m[M_PITCH] = std::isnan(meda) || std::isnan(medb) ? 0 : std::fabs(meda - medb);
    f.m[M_VIBDEPTH] = std::isnan(deptha) || std::isnan(depthb) ? 0 : std::fabs(deptha - depthb);
    double ra = deptha > 20 && depthb > 20 ? vibRate(pa_, meda, deptha) : NAN, rb = deptha > 20 && depthb > 20 ? vibRate(pb_, medb, depthb) : NAN;
    f.m[M_VIBRATE] = std::isnan(ra) || std::isnan(rb) ? 0 : std::fabs(log2(ra / rb));
    return f;
}

// ------------------------------------------------------------------------------------------------ patches ----
struct P60 { string num; c60::Patch60 v; };
static vector<string> splitCsv(const string &l)
{
    vector<string> o;
    std::stringstream ss(l);
    string c;
    while (std::getline(ss, c, ',')) o.push_back(c);
    if (!l.empty() && l.back() == ',') o.push_back("");
    return o;
}
static vector<P60> load60(const string &path)
{
    std::ifstream f(path);
    string line;
    std::getline(f, line);
    vector<P60> out;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        vector<string> c = splitCsv(line);
        if (c.size() < 28) continue;
        bool empty = false;
        for (int i = 1; i < 28; i++) if (c[i].empty()) empty = true;
        if (empty) continue;                                // (a patch the tape lacks)
        int x[28];
        for (int i = 1; i < 28; i++) x[i] = std::stoi(c[i]);
        // the CSV's columns: VCF Env, VCF LFO, VCF KYBD, VCA Level, ENV A, R, D, S, VCF Freq, DCO Noise, DCO Sub, VCF Res,
        // DCO PWM, LFO Rate, DCO LFO, LFO Delay, VCF Polarity, DCO PWM Mode, HPF, VCA Type (0 GATE), LFO Trigger,
        // Chorus I, Square, Triangle (saw), Sub, Chorus II, Chorus Off
        c60::Patch60 v{x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[10], x[11], x[12], x[13], x[14], x[15], x[16],
                       x[17], x[18], x[19], x[20] ? 0 : 1, x[21], x[22], x[23], x[24], x[25], x[26], x[27]};
        out.push_back({c[0], v});
    }
    return out;
}
static Params params60(const c60::Patch60 &v)
{
    using q::slider;
    static const int HPFMODE[4] = {1, 0, 2, 3};             // (the tape's 122 Hz, FLAT, 269, 571 -> KR-106's J60 switch)
    return {{kLfoRate, slider(v.lfo_rate)}, {kLfoDelay, q::delaySlider(v.lfo_delay)}, {kDcoLfo, slider(v.dco_lfo)},
            {kDcoPwm, slider(v.dco_pwm)}, {kDcoSub, slider(v.dco_sub)}, {kDcoNoise, slider(v.dco_noise)}, {kHpfFreq, HPFMODE[v.hpf]},
            {kVcfFreq, slider(v.vcf_freq)}, {kVcfRes, slider(v.vcf_res)}, {kVcfEnv, slider(v.vcf_env)}, {kVcfLfo, slider(v.vcf_lfo)},
            {kVcfKbd, slider(v.vcf_kybd)}, {kVcaLevel, slider(v.vca_level)}, {kEnvA, q::attackSlider(v.env_a)},
            {kEnvD, slider(v.env_d)}, {kEnvS, slider(v.env_s)}, {kEnvR, slider(v.env_r)}, {kDcoPulse, (double)v.pulse},
            {kDcoSaw, (double)v.saw}, {kDcoSubSw, (double)v.sub_on}, {kPwmMode, (double)v.pwm_source},
            {kVcfEnvInv, (double)v.env_polarity}, {kVcaMode, (double)v.vca_gate}};
}
static Params params106(const uint8_t *p)
{
    auto s = [&](int i) { return p[i] / 127.0; };
    return {{kLfoRate, s(0)}, {kLfoDelay, s(1)}, {kDcoLfo, kr106::Voice<float>::dcoLfoDepth106_inverseTaper((float)s(2))},
            {kDcoPwm, std::min<int>(p[3], 105) / 105.0}, {kDcoNoise, s(4)}, {kVcfFreq, s(5)}, {kVcfRes, s(6)}, {kVcfEnv, s(7)},
            {kVcfLfo, s(8)}, {kVcfKbd, s(9)}, {kVcaLevel, s(10)}, {kEnvA, s(11)}, {kEnvD, s(12)}, {kEnvS, s(13)}, {kEnvR, s(14)},
            {kDcoSub, s(15)}, {kDcoSubSw, 1}, {kDcoPulse, (double)((p[16] >> 3) & 1)}, {kDcoSaw, (double)((p[16] >> 4) & 1)},
            {kHpfFreq, (double)(3 - ((p[17] >> 3) & 3))}, {kPwmMode, (double)(p[17] & 1)}, {kVcfEnvInv, (double)((p[17] >> 1) & 1)},
            {kVcaMode, (double)((p[17] >> 2) & 1)}};
}
static void at8(uint8_t *p) { p[16] = (uint8_t)((p[16] & ~7) | 2); }   // (RANGE 8')

static Fig worst(const vector<Fig> &fs)
{
    Fig w{};
    for (int m = 0; m < M_COUNT; m++) { w.m[m] = 0; for (auto &f : fs) w.m[m] = std::max(w.m[m], f.m[m]); }
    double s = 0;
    for (auto &f : fs) s += f.peakDb;
    w.peakDb = s / fs.size();
    return w;
}
static Fig figs60(const c60::Patch60 &v, const uint8_t *p)
{
    vector<Fig> fs;
    double vib = std::max(q::dcoLfo60(v.dco_lfo), q::dcoLfo106(p[2]));
    for (int n : NOTES) fs.push_back(compare(renderKR(true, params60(v), n), renderPT(p, n), n, vib));
    return worst(fs);
}

// ---------------------------------------------------------------------------------------------- the jobs ----
struct Data { vector<std::array<uint8_t, 18>> j106; vector<P60> j60; };
static Data loadAll(const string &kr)
{
    Data d;
    std::ifstream f(kr + "/tools/preset-gen/Factory_Patches.pat");
    string line;
    while (std::getline(f, line)) {
        std::array<uint8_t, 18> p{};
        std::stringstream ss(line.substr(0, line.find(',')));
        int k = 0;
        unsigned x;
        while (k < 18 && ss >> std::hex >> x) p[k++] = (uint8_t)x;
        if (k == 18) d.j106.push_back(p);
    }
    for (const char *g : {"/tools/juno60_group1.csv", "/tools/juno60_group2.csv"}) {
        vector<P60> part = load60(kr + g);
        for (auto &x : part) x.num = string(g[19] == '1' ? "A" : "B") + x.num;   // (A: the first tape, B: the second)
        d.j60.insert(d.j60.end(), part.begin(), part.end());
    }
    return d;
}
// a render pair to measure: Y the yardstick's patch i; C the Juno-60 patch i converted; H the same with the HPF's
// 122 Hz as FLAT (var 1) or 236 Hz (2); W with PWM SOURCE ENV's other choices (var 1-3)
struct Job { char kind; int i, var; };
static vector<Job> jobList(const Data &d, const c60::Tab &tab)
{
    vector<Job> J;
    for (size_t i = 0; i < d.j106.size(); i++) J.push_back({'Y', (int)i, 0});
    for (size_t i = 0; i < d.j60.size(); i++) J.push_back({'C', (int)i, 0});
    for (size_t i = 0; i < d.j60.size(); i++)
        if (d.j60[i].v.hpf == 0) for (int h = 1; h <= 2; h++) J.push_back({'H', (int)i, h});
    for (size_t i = 0; i < d.j60.size(); i++)
        if (c60::convert(tab, d.j60[i].v).pwmEnv) for (int a = 1; a <= 3; a++) J.push_back({'W', (int)i, a});
    return J;
}
// the patch a job plays on PT/FM's engine
static c60::Result jobPatch(const Job &j, const Data &d, const c60::Tab &tab)
{
    c60::Result r{};
    if (j.kind == 'Y') { memcpy(r.p, d.j106[j.i].data(), 18); at8(r.p); return r; }
    const c60::Patch60 &v = d.j60[j.i].v;
    r = c60::convert(tab, v);
    if (j.kind == 'H') r.p[17] = (uint8_t)((r.p[17] & ~0x18) | (3 - j.var) << 3);
    if (j.kind == 'W') {
        double w = v.dco_pwm / 255.0;
        if (j.var == 1) w *= std::max(v.env_s / 255.0, 0.001);
        r.p[3] = (uint8_t)std::lround(std::min(w, 1.0) * 105);
        if (j.var == 3) r.p[17] &= ~1;
    }
    return r;
}
static Fig runJob(const Job &j, const Data &d, const c60::Tab &tab)
{
    c60::Result r = jobPatch(j, d, tab);
    if (j.kind != 'Y') return figs60(d.j60[j.i].v, r.p);
    vector<Fig> fs;
    for (int n : NOTES) fs.push_back(compare(renderKR(false, params106(r.p), n), renderPT(r.p, n), n, q::dcoLfo106(r.p[2])));
    return worst(fs);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: check KR106_DIR figs SHARD SHARDS > PART.csv\n       check KR106_DIR report REPORT.md PART.csv...\n");
        return 2;
    }
    string kr = argv[1], mode = argv[2];
    static c60::Tab tab;
    c60::buildTab(tab);
    dx7_init();
    Data d = loadAll(kr);
    vector<Job> J = jobList(d, tab);
    if (mode == "wav") {                               // (a job's two renders at a note, to hear: KR-106's, PT/FM's)
        size_t k = std::stoul(argv[3]);
        int note = argc > 4 ? atoi(argv[4]) : 60;
        c60::Result r = jobPatch(J[k], d, tab);
        vector<float> a = J[k].kind == 'Y' ? renderKR(false, params106(r.p), note) : renderKR(true, params60(d.j60[J[k].i].v), note);
        vector<float> b = renderPT(r.p, note);
        for (int w = 0; w < 2; w++) {
            const vector<float> &x = w ? b : a;
            string name = string("build/juno60/job") + argv[3] + (w ? "_ptfm.wav" : "_kr106.wav");
            FILE *f = fopen(name.c_str(), "wb");
            uint32_t n = (uint32_t)x.size(), bytes = n * 2;
            uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                             0x44, 0xAC, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a'};
            uint32_t riff = 36 + bytes;
            memcpy(h + 4, &riff, 4);
            fwrite(h, 1, 40, f);
            fwrite(&bytes, 4, 1, f);
            double pk = 0, ss = 0;
            for (float v : x) { pk = std::max(pk, (double)std::fabs(v)); ss += v * v; }
            for (float v : x) { int16_t s = (int16_t)std::lround(std::clamp(v / (pk + 1e-9) * 0.9, -1.0, 1.0) * 32767); fwrite(&s, 2, 1, f); }
            fclose(f);
            printf("%s: peak %.4f rms %.4f (normalised to -1 dB in the file)\n", name.c_str(), pk, sqrt(ss / x.size()));
        }
        double vib = J[k].kind == 'Y' ? q::dcoLfo106(r.p[2]) : std::max(q::dcoLfo60(d.j60[J[k].i].v.dco_lfo), q::dcoLfo106(r.p[2]));
        Fig f = compare(a, b, note, vib);
        for (int m = 0; m < M_COUNT; m++) printf("%s %.3f\n", MNAME[m], f.m[m]);
        return 0;
    }
    if (mode == "sweep") {                             // (a job's patch with one byte swept: the figures for each)
        size_t k = std::stoul(argv[3]);
        int field = atoi(argv[4]), step = argc > 5 ? atoi(argv[5]) : 4;
        c60::Result r = jobPatch(J[k], d, tab);
        printf("job %zu, byte %d converted to %d\n", k, field, r.p[field]);
        for (int b = 0; b < 128; b += step) {
            c60::Result x = r;
            x.p[field] = (uint8_t)b;
            Fig f = J[k].kind == 'Y' ? runJob(J[k], d, tab) : figs60(d.j60[J[k].i].v, x.p);
            printf("%3d", b);
            for (int m = 0; m < M_COUNT; m++) printf("  %s %.3f", MNAME[m], f.m[m]);
            printf("\n");
        }
        return 0;
    }
    if (mode == "figs") {                              // (this shard's jobs: their figures, a line each)
        int shard = atoi(argv[3]), shards = argc > 4 ? atoi(argv[4]) : 1;
        for (size_t k = 0; k < J.size(); k++) {
            if ((int)(k % shards) != shard) continue;
            Fig f = runJob(J[k], d, tab);
            printf("%zu", k);
            for (int m = 0; m < M_COUNT; m++) printf(",%.4f", f.m[m]);
            printf(",%.3f\n", f.peakDb);
            fflush(stdout);
        }
        return 0;
    }
    std::map<size_t, Fig> got;
    for (int a = 4; a < argc; a++) {
        std::ifstream f(argv[a]);
        string line;
        while (std::getline(f, line)) {
            vector<string> c = splitCsv(line);
            if (c.size() != M_COUNT + 2) continue;
            Fig g;
            for (int m = 0; m < M_COUNT; m++) g.m[m] = std::stod(c[1 + m]);
            g.peakDb = std::stod(c[M_COUNT + 1]);
            got[std::stoul(c[0])] = g;
        }
    }
    if (got.size() != J.size()) { fprintf(stderr, "check: %zu of %zu jobs' figures\n", got.size(), J.size()); return 1; }
    auto figOf = [&](char kind, int i, int var) {
        for (size_t k = 0; k < J.size(); k++) if (J[k].kind == kind && J[k].i == i && J[k].var == var) return got[k];
        return Fig{};
    };
    const vector<P60> &j60 = d.j60;
    vector<Fig> yard;
    for (size_t i = 0; i < d.j106.size(); i++) yard.push_back(figOf('Y', (int)i, 0));
    double bar[M_COUNT], yardLevel;
    for (int m = 0; m < M_COUNT; m++) { vector<double> v; for (auto &f : yard) v.push_back(f.m[m]); bar[m] = pct(v, 0.95); }
    { vector<double> v; for (auto &f : yard) v.push_back(f.peakDb); yardLevel = pct(v, 0.5); }

    struct Row { string num; Fig f; bool pwmEnv; c60::Result r; };
    vector<Row> rows;
    for (size_t i = 0; i < j60.size(); i++) {
        c60::Result r = c60::convert(tab, j60[i].v);
        rows.push_back({j60[i].num, figOf('C', (int)i, 0), r.pwmEnv, r});
    }

    // ---- the HPF's 122 Hz both ways: FLAT (1), 236 Hz (2)
    double hpfErr[3] = {0, 0, 0};
    int hpfN = 0;
    vector<string> hpfRows;
    for (size_t i = 0; i < j60.size(); i++) {
        if (j60[i].v.hpf != 0) continue;
        hpfN++;
        string line = j60[i].num + ":";
        for (int h = 1; h <= 2; h++) {
            Fig f = figOf('H', (int)i, h);
            double e = f.m[M_BRIGHT] / bar[M_BRIGHT] + f.m[M_LEVEL] / bar[M_LEVEL] + f.m[M_RELEASE] / bar[M_RELEASE];
            hpfErr[h] += e;
            char b[96];
            snprintf(b, sizeof b, " %s brightness %.3f level %.2f", h == 1 ? "FLAT" : "236 Hz", f.m[M_BRIGHT], f.m[M_LEVEL]);
            line += b;
        }
        hpfRows.push_back(line);
    }

    // ---- PWM SOURCE ENV: the rule (the envelope's mean over a note's first second) against other choices
    struct Alt { const char *name; double sum[M_COUNT]; int pass; };
    Alt alts[4] = {{"the rule: MAN at the envelope's mean width over a note's first second", {}, 0},
                   {"MAN at the sustain's width", {}, 0}, {"MAN at the full width (the envelope's peak)", {}, 0},
                   {"LFO at the slider's width", {}, 0}};
    int pwmN = 0;
    vector<string> pwmRows;
    for (size_t i = 0; i < j60.size(); i++) {
        c60::Result r0 = c60::convert(tab, j60[i].v);
        if (!r0.pwmEnv) continue;
        pwmN++;
        string line = j60[i].num + ":";
        for (int a = 0; a < 4; a++) {
            c60::Result r = a == 0 ? r0 : jobPatch({'W', (int)i, a}, d, tab);
            Fig f = a == 0 ? rows[i].f : figOf('W', (int)i, a);
            bool ok = true;
            for (int m = 0; m < M_COUNT; m++) { alts[a].sum[m] += f.m[m]; if (f.m[m] > bar[m]) ok = false; }
            alts[a].pass += ok;
            char b[64];
            snprintf(b, sizeof b, " %d: PWM %d brightness %.3f", a, r.p[3], f.m[M_BRIGHT]);
            line += b;
        }
        pwmRows.push_back(line);
    }

    // ---- the report
    FILE *o = fopen(argv[3], "w");
    if (!o) { perror(argv[3]); return 1; }
    fprintf(o, "# The Juno-60 conversion's check\n\n");
    fprintf(o, "%zu Juno-60 factory patches (KR-106's Juno-60 mode) against their conversions (PT/FM's engine); the yardstick: "
               "%zu Juno-106 factory patches (KR-106's Juno-106 mode against PT/FM's engine). Notes C2, C4, C6, 2 s held, "
               "2.5 s of release; a patch's figure is the worst of its notes.\n\n", rows.size(), yard.size());
    fprintf(o, "| Measure | Yardstick (95th percentile) | Converted patches within it |\n|---|---|---|\n");
    int allPass = 0;
    vector<int> passM(M_COUNT, 0);
    for (auto &r : rows) {
        bool ok = true;
        for (int m = 0; m < M_COUNT; m++) { if (r.f.m[m] <= bar[m]) passM[m]++; else ok = false; }
        allPass += ok;
    }
    for (int m = 0; m < M_COUNT; m++) fprintf(o, "| %s | %.3f | %d of %zu |\n", MNAME[m], bar[m], passM[m], rows.size());
    fprintf(o, "\nWithin every measure: %d of %zu.\n\n", allPass, rows.size());
    {
        vector<double> lv;
        for (auto &r : rows) lv.push_back(r.f.peakDb - yardLevel);
        fprintf(o, "The overall level (reported, not counted): the converted patches' peak against KR-106's Juno-60, less the "
                   "yardstick's median offset: median %.1f dB, 5th to 95th percentile %.1f to %.1f dB.\n\n",
                pct(lv, 0.5), pct(lv, 0.05), pct(lv, 0.95));
    }
    // what a patch is: its filter self-oscillating (RES where KR-106's Juno-60 k passes 4, the Juno-106's top byte
    // range), no oscillator (its sound the filter's ringing or noise), its envelope inverted, PWM by ENV
    auto selfOsc60 = [](const c60::Patch60 &v) { return q::resK60(v.vcf_res) >= 4.0; };
    auto noOsc60 = [](const c60::Patch60 &v) { return !v.saw && !v.pulse && !v.sub_on; };
    auto past = [&](const Fig &f) { for (int m = 0; m < M_COUNT; m++) if (f.m[m] > bar[m]) return true; return false; };
    int pastN = 0, pastKind = 0;
    for (size_t i = 0; i < rows.size(); i++)
        if (past(rows[i].f)) { pastN++; pastKind += selfOsc60(j60[i].v) || noOsc60(j60[i].v); }
    int ypast = 0, ykind = 0;
    for (size_t i = 0; i < yard.size(); i++) {
        if (!past(yard[i])) continue;
        const uint8_t *p = d.j106[i].data();
        ypast++;
        ykind += q::resK106(p[6]) >= 4.0 || (!(p[16] & 0x18) && !p[15]);
    }
    fprintf(o, "## Past the yardstick\n\n%d patches; %d of them with a self-oscillating filter or no oscillator (the sound then "
               "the filter's ringing or noise: where the two models differ most, as the yardstick's own %d patches past its "
               "95th percentile in some measure show, %d of them of that kind).\n\n", pastN, pastKind, ypast, ykind);
    for (size_t i = 0; i < rows.size(); i++) {
        const Row &r = rows[i];
        string why, kind;
        for (int m = 0; m < M_COUNT; m++)
            if (r.f.m[m] > bar[m]) { char b[96]; snprintf(b, sizeof b, "%s%s %.3f", why.empty() ? "" : "; ", MNAME[m], r.f.m[m]); why += b; }
        if (why.empty()) continue;
        const c60::Patch60 &v = j60[i].v;
        if (selfOsc60(v)) kind += ", self-oscillating filter";
        if (noOsc60(v)) kind += ", no oscillator";
        if (v.env_polarity && v.vcf_env) kind += ", envelope inverted";
        if (v.vca_gate) kind += ", VCA GATE";
        if (r.pwmEnv) kind += ", PWM by ENV";
        fprintf(o, "- %s%s%s%s: %s\n", r.num.c_str(), kind.empty() ? "" : " (", kind.empty() ? "" : kind.c_str() + 2,
                kind.empty() ? "" : ")", why.c_str());
    }
    fprintf(o, "\n## HPF 122 Hz\n\n%d patches use it. Summed error (brightness, level, release, each over its yardstick): "
               "FLAT %.2f, 236 Hz %.2f: **%s** is closer.\n\n", hpfN, hpfErr[1], hpfErr[2], hpfErr[1] <= hpfErr[2] ? "FLAT" : "236 Hz");
    for (auto &l : hpfRows) fprintf(o, "- %s\n", l.c_str());
    fprintf(o, "\n## PWM SOURCE ENV\n\n%d patches use it with PULSE on and DCO PWM above 0.\n\n| Choice | Within every measure | "
               "Mean brightness | Mean level |\n|---|---|---|---|\n", pwmN);
    for (auto &a : alts) fprintf(o, "| %s | %d of %d | %.3f | %.2f |\n", a.name, a.pass, pwmN, pwmN ? a.sum[M_BRIGHT] / pwmN : 0, pwmN ? a.sum[M_LEVEL] / pwmN : 0);
    fprintf(o, "\n");
    for (auto &l : pwmRows) fprintf(o, "- %s\n", l.c_str());
    fprintf(o, "\n## The conversions\n\n");
    for (auto &r : rows) {
        fprintf(o, "- %s:", r.num.c_str());
        for (int k = 0; k < 18; k++) fprintf(o, " %02X", r.r.p[k]);
        fprintf(o, "\n");
    }
    fclose(o);
    printf("check: %d of %zu converted patches within every measure (report %s)\n", allPass, rows.size(), argv[3]);
    return 0;
}
