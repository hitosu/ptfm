/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The effects' knobs and the FX setup's bytes (fxdef.h). */
#include <string.h>
#include "fxdef.h"
#include "gfx.h"
#include "fxm.h"
#include "ui_core.h"
#include "../fx/fx.h"

static const txt_t L_GATE_RATE[] = {TX("1/8"), TX("1/16"), TX("1/32"), TX("1/8T"), TX("1/16T")};
static const txt_t L_CHO_MODE[] = {TX("I"), TX("II"), TX("I+II")};
static const txt_t L_TIME[] = {TX("1/32"), TX("1/16T"), TX("1/16"), TX("1/8T"), TX("1/16."), TX("1/8"), TX("1/4T"), TX("1/8."),
                               TX("1/4"), TX("1/2T"), TX("1/4."), TX("1/2")};
static const txt_t L_OFFON[] = {TX("OFF"), TX("ON")};
static const txt_t L_SOUND[] = {TX("CLICK"), TX("BEEP"), TX("WOOD"), TX("COWBELL")};

#define PCT(nm, d) {nm, 0, 0, 0, 100, d, 1, KF_PCT}
static const fx_knob_t K_GATE[] = {{TX("PATTERN"), 0, 0, 0, 31, 0, 1, KF_HEX}, {TX("RATE"), L_GATE_RATE, 5, 0, 4, 1, 1, KF_NUM},
                                   PCT(TX("DEPTH"), 100), PCT(TX("SMOOTH"), 20)};
static const fx_knob_t K_FILTER[] = {{TX("CUT"), 0, 0, -64, 63, 0, 1, KF_CUT}, PCT(TX("RESO"), 30), PCT(TX("ENV"), 0),
                                     {TX("SOURCE"), 0, 0, 0, 21, 0, 1, KF_ESRC}, {TX("ATTACK"), 0, 0, 0, 127, 0, 1, KF_ATK},
                                     {TX("DECAY"), 0, 0, 0, 127, 75, 1, KF_DREL}, PCT(TX("SUSTAIN"), 0),
                                     {TX("RELEASE"), 0, 0, 0, 127, 75, 1, KF_DREL}};
static const fx_knob_t K_DRIVE[] = {PCT(TX("DRIVE"), 30), PCT(TX("TONE"), 50)};
static const fx_knob_t K_LOFI[] = {{TX("BITS"), 0, 0, 2, 16, 8, 1, KF_NUM}, {TX("RATE"), 0, 0, 1, 44, 22, 1, KF_UNIT},
                                   PCT(TX("DUST"), 20)};
static const fx_knob_t K_CHORUS[] = {{TX("MODE"), L_CHO_MODE, 3, 0, 2, 0, 1, KF_NUM}, PCT(TX("RATE"), 30), PCT(TX("DEPTH"), 50),
                                     PCT(TX("MIX"), 50)};
static const fx_knob_t K_DELAY[] = {{TX("TIME"), L_TIME, 12, 0, 11, 7, 1, KF_NUM}, PCT(TX("FEEDBACK"), 40), PCT(TX("TONE"), 60),
                                    {TX("PING-PONG"), L_OFFON, 2, 0, 1, 0, 1, KF_NUM}, PCT(TX("MIX"), 30)};
static const fx_knob_t K_REVERB[] = {PCT(TX("SIZE"), 60), PCT(TX("DAMP"), 40), {TX("PRE-DELAY"), 0, 0, 0, 100, 10, 1, KF_UNIT},
                                     PCT(TX("MIX"), 25)};
static const fx_knob_t K_LIMITER[] = {{TX("CEILING"), 0, 0, -12, 0, -1, 1, KF_UNIT},
                                      {TX("RELEASE"), 0, 0, 10, 1000, 100, 10, KF_UNIT}};
static const fx_knob_t K_DUCK[] = {{TX("SOURCE"), 0, 0, 0, 16, 0, 1, KF_TRACK}, PCT(TX("DEPTH"), 60),   /* (16: the keys) */
                                   {TX("RELEASE"), 0, 0, 20, 1000, 150, 10, KF_UNIT}};
static const fx_knob_t K_METRO[] = {{TX("TEMPO"), 0, 0, 29, 300, 29, 1, KF_TEMPO}, {TX("BEATS A BAR"), 0, 0, 1, 16, 4, 1, KF_NUM},
                                    {TX("SOUND"), L_SOUND, 4, 0, 3, 1, 1, KF_NUM}, PCT(TX("LEVEL"), 60)};

const fx_eff_t FX_EFF[FE_N] = {
    {TX("GATE"), C_LBLUE, 0x90, 4, K_GATE, {0}, 4},
    {TX("FILTER"), C_CYAN, 0x94, 8, K_FILTER, {0}, 12},
    {TX("DRIVE"), C_LRED, 0x96, 2, K_DRIVE, {0}, 8},
    {TX("LO-FI"), C_YELLOW, 0x98, 3, K_LOFI, {0, " KHZ"}, 5},
    {TX("CHORUS"), C_LCYAN, 0x9B, 4, K_CHORUS, {0}, 22},
    {TX("DELAY"), C_LGREEN, 0x9F, 5, K_DELAY, {0}, 14},
    {TX("REVERB"), C_LMAGENTA, 0xA4, 4, K_REVERB, {0, 0, " MS"}, 75},
    {TX("LIMITER"), C_WHITE, 0xA8, 2, K_LIMITER, {" DB", " MS"}, 6},
    {TX("DUCK"), C_BROWN, 0xAA, 3, K_DUCK, {0, 0, " MS"}, 3},
    {TX("METRONOME"), C_GREEN, 0, 4, K_METRO, {0}, 1},
};
const txt_t FX_SHORT_NAME[FE_N] = {TX("GATE"), TX("FILTER"), TX("DRIVE"), TX("LO-FI"), TX("CHORUS"), TX("DELAY"), TX("REVERB"),
                                   TX("LIMITER"), TX("DUCK"), TX("METRO")};

uint8_t fxd_code(int e, int k)
{
    if (e == FE_FILTER && k >= 2) return (uint8_t)(0xB0 + k - 2);   /* (its envelope's: knobs added later take free
                                                                     * codes, specs/files/commands.md) */
    return FX_EFF[e].code ? (uint8_t)(FX_EFF[e].code + k) : 0;
}
int fxd_byte(int e, int k)
{
    if (e == FE_METRONOME) return k == 0 ? 9 : 10 + k;  /* TEMPO 9-10, BEATS 11, SOUND 12, LEVEL 13 */
    return 16 + fxd_code(e, k) - 0x90;
}
int fxd_get(const uint8_t *s, int e, int k)
{
    const fx_knob_t *kn = &FX_EFF[e].k[k];
    int b = fxd_byte(e, k), v;
    if (e == FE_METRONOME) {
        if (k == 0) { v = s[9] | s[10] << 8; return v ? ui_clamp(v, 30, 300) : 29; }
        v = s[b];
        return kn->list ? ui_clamp(v, 0, kn->n - 1) : ui_clamp(v, kn->min, kn->max);
    }
    v = s[b];
    if (kn->list) return ui_clamp(v, 0, kn->n - 1);
    return ui_clamp(kn->min + v * kn->step, kn->min, kn->max);
}
void fxd_set(uint8_t *s, int e, int k, int v)
{
    const fx_knob_t *kn = &FX_EFF[e].k[k];
    int b = fxd_byte(e, k);
    if (kn->list) v = ui_clamp(v, 0, kn->n - 1);
    else v = ui_clamp(v, kn->min, kn->max);
    if (e == FE_METRONOME) {
        if (k == 0) { int t = v <= 29 ? 0 : v; s[9] = (uint8_t)t; s[10] = (uint8_t)(t >> 8); return; }
        s[b] = (uint8_t)v;
        return;
    }
    s[b] = (uint8_t)(kn->list ? v : (v - kn->min) / kn->step);
}
int fxd_on(const uint8_t *s, int e) { return (s[7] | s[8] << 8) >> e & 1; }
void fxd_set_on(uint8_t *s, int e, int on)
{
    unsigned w = (unsigned)(s[7] | s[8] << 8);
    w = on ? w | 1u << e : w & ~(1u << e);
    s[7] = (uint8_t)w; s[8] = (uint8_t)(w >> 8);
}
int fxd_sends(const uint8_t *s) { return s[0] != 0; }
int fxd_chain(const uint8_t *s, uint8_t *items)
{
    int n = 0;
    for (int i = 0; i < 12; i++) {
        int v = s[1 + i / 2] >> (4 * (i % 2)) & 15;
        if (v == 15) break;
        items[n++] = (uint8_t)v;
    }
    return n;
}
void fxd_set_chain(uint8_t *s, const uint8_t *items, int n)
{
    memset(s + 1, 0xFF, 6);
    for (int i = 0; i < n && i < 12; i++) {
        int b = 1 + i / 2, sh = 4 * (i % 2);
        s[b] = (uint8_t)((s[b] & ~(15 << sh)) | (items[i] & 15) << sh);
    }
}
static int is_send(int e) { return e == FE_CHORUS || e == FE_DELAY || e == FE_REVERB; }
void fxd_set_routing(uint8_t *s, int sends)
{
    uint8_t c[12], o[12];
    int n = fxd_chain(s, c), m = 0;
    if (!!sends == fxd_sends(s)) return;
    if (!sends) {                       /* GLOBAL: the three where SENDS is */
        for (int i = 0; i < n; i++) {
            if (is_send(c[i])) continue;
            o[m++] = c[i];
            if (c[i] == FE_SENDS) { o[m++] = FE_CHORUS; o[m++] = FE_DELAY; o[m++] = FE_REVERB; }
        }
    } else {                            /* SENDS: the SENDS item where the first of them is */
        int put = 0;
        for (int i = 0; i < n; i++) {
            if (c[i] == FE_SENDS) continue;
            if (is_send(c[i]) && !put) { o[m++] = FE_SENDS; put = 1; }
            o[m++] = c[i];
        }
        if (!put) o[m++] = FE_SENDS;
    }
    fxd_set_chain(s, o, m);
    s[0] = (uint8_t)(sends ? 1 : 0);
}
void fxd_reset(uint8_t *s, int e)
{
    for (int k = 0; k < FX_EFF[e].nk; k++) fxd_set(s, e, k, FX_EFF[e].k[k].def);
}
void fxd_default(uint8_t *s)
{
    static const uint8_t CHAIN[9] = {FE_GATE, FE_FILTER, FE_LOFI, FE_SENDS, FE_CHORUS, FE_DELAY, FE_REVERB, FE_DRIVE,
                                     FE_LIMITER};
    memset(s, 0, FX_SETUP_LEN);
    fxd_set_chain(s, CHAIN, 9);
    for (int e = 0; e < FE_N; e++) fxd_reset(s, e);
    fxd_set_on(s, FE_LIMITER, 1);
}

int fxd_filter_hz(int cut)                /* x 10, as the DSP's curve: LP 20 kHz down to 100 Hz, HP 20 Hz up to 8 kHz */
{
    if (!cut) return 0;
    if (cut < 0) return (int)(200000ULL * fxm_exp2((int32_t)(-500948LL * -cut / 64)) >> 16);
    return (int)(200ULL * fxm_exp2((int32_t)(566484LL * cut / 63)) >> 16);
}
void fxd_hz_short(char *dst, int hz10)
{
    if (hz10 >= 100000) { ui_fmt(dst, 12, "%dK", (hz10 + 5000) / 10000); return; }
    if (hz10 >= 10000) { int t = (hz10 + 500) / 1000; ui_fmt(dst, 12, "%d.%dK", t / 10, t % 10); return; }
    ui_fmt(dst, 12, "%d", (hz10 + 5) / 10);
}
void fxd_time(char *dst, int t10)        /* 0.1 ms: `2.0 MS` under 10 ms, `296 MS` under a second, then `1.22 S` */
{
    if (t10 < 100) { ui_fmt(dst, 12, "%d.%d MS", t10 / 10, t10 % 10); return; }
    int ms = (t10 + 5) / 10, cs = (t10 + 50) / 100;
    if (ms < 1000) ui_fmt(dst, 12, "%d MS", ms);
    else ui_fmt(dst, 12, "%d.%d%d S", cs / 100, cs / 10 % 10, cs % 10);
}
void fxd_text(int e, int k, int v, char *out)
{
    const fx_knob_t *kn = &FX_EFF[e].k[k];
    if (kn->list) { strcpy(out, T(kn->list[ui_clamp(v, 0, kn->n - 1)])); return; }
    switch (kn->fmt) {
    case KF_PCT: ui_fmt(out, 12, "%d%%", v); return;
    case KF_HEX: strcpy(out, ui_hex2(v)); return;
    case KF_TRACK: if (v >= 16) strcpy(out, T("KEYS")); else ui_fmt(out, 12, T("TRACK %s"), ui_hex2(v)); return;
    case KF_TEMPO: if (v <= 29) strcpy(out, T("FOLLOW")); else ui_fmt(out, 12, "%d BPM", v); return;
    case KF_ESRC: {
        static const txt_t BEATS[5] = {TX("4 BEATS"), TX("2 BEATS"), TX("BEAT"), TX("1/2 BEAT"), TX("1/4 BEAT")};
        if (v <= 0) strcpy(out, T("KEYS"));
        else if (v <= 16) ui_fmt(out, 12, T("TRACK %s"), ui_hex2(v - 1));
        else strcpy(out, T(BEATS[v > 21 ? 4 : v - 17]));
        return;
    }
    case KF_ATK: case KF_DREL: fxd_time(out, (int)fx_env_t10(kn->fmt == KF_DREL, v)); return;
    case KF_UNIT: ui_fmt(out, 12, "%d%s", v, FX_EFF[e].unit[k] ? FX_EFF[e].unit[k] : ""); return;
    case KF_CUT: {
        if (!v) { strcpy(out, T("OFF")); return; }
        char h[12];
        fxd_hz_short(h, fxd_filter_hz(v));
        ui_fmt(out, 12, "%s %s", v < 0 ? "LP" : "HP", h);
        return;
    }
    default: ui_fmt(out, 12, "%d", v); return;
    }
}
static const txt_t KNOB6[][2] = {{TX("PATTERN"), TX("PATTRN")}, {TX("FEEDBACK"), TX("FEEDBK")}, {TX("PRE-DELAY"), TX("PRE-DL")},
                                 {TX("CEILING"), TX("CEIL")}, {TX("RELEASE"), TX("RELEAS")}, {TX("SUSTAIN"), TX("SUSTN")},
                                 {TX("BEATS A BAR"), TX("BEATS")}, {TX("PING-PONG"), TX("PINGPG")}};
const char *fxd_knob_short(int e, int k)
{
    static char b[16];
    ui_fmt(b, sizeof b, "%s", T(FX_EFF[e].k[k].name));
    for (unsigned i = 0; i < sizeof KNOB6 / sizeof KNOB6[0]; i++) if (!strcmp(T(KNOB6[i][0]), b)) return T(KNOB6[i][1]);
    b[6] = 0;
    return b;
}

static const txt_t GATE_NAMES[16] = {TX("STRAIGHT"), TX("OFFBEAT"), TX("TRANCE"), TX("HALF"), TX("GALLOP"), TX("SKIP"),
                                     TX("SPARSE"), TX("ROLL"), TX("STUTTER"), TX("DOTTED"), TX("SYNCOPE"), TX("PULSE"),
                                     TX("RAMP"), TX("BREAK"), TX("TRIPS"), TX("CHOP")};
const char *fxd_gate_name(int i, char *buf)
{
    if (i < 16) return T(GATE_NAMES[i & 15]);
    ui_fmt(buf, 16, T("MY GATE %s"), ui_hex2(i));
    return buf;
}
