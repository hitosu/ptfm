/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The commands as the screens show them (ui_cmds.h), as the playground's cmds.js does. Names in the
 * tables are TX() texts (txt.h), given out through T(). */
#include <string.h>
#include "ui_core.h"
#include "ui_cmds.h"
#include "ui_data.h"
#include "ui_grid.h"
#include "fxdef.h"
#include "ed_inst.h"                     /* (CHIP's tables' and waves' names) */

/* ------------------------------------------------------------------------ the list K2 turns --- */
#define EN(n, c, s, d) {n, c, s, d, 0}
const ui_entry_t UI_ENTRIES[] = {
    EN(TX("NO COMMAND"), -1, -1, 0),
    EN(TX("ARPEGGIO"), 0x00, -1, 2), EN(TX("SLIDE UP"), 0x01, -1, 2), EN(TX("SLIDE DOWN"), 0x02, -1, 2),
    EN(TX("SLIDE TO NOTE"), 0x03, -1, 2), EN(TX("VIBRATO"), 0x04, -1, 2), EN(TX("SLIDE TO NOTE + VOL"), 0x05, -1, 2),
    EN(TX("VIBRATO + VOL"), 0x06, -1, 2), EN(TX("TREMOLO"), 0x07, -1, 2), EN(TX("PAN"), 0x08, -1, 2),
    EN(TX("SAMPLE OFFSET"), 0x09, -1, 2), EN(TX("VOLUME SLIDE"), 0x0A, -1, 2), EN(TX("JUMP"), 0x0B, -1, 2),
    EN(TX("VOLUME"), 0x0C, -1, 2), EN(TX("BREAK"), 0x0D, -1, 2),
    EN(TX("FINE SLIDE UP"), 0x0E, 1, 1), EN(TX("FINE SLIDE DOWN"), 0x0E, 2, 1), EN(TX("GLISSANDO"), 0x0E, 3, 1),
    EN(TX("VIBRATO WAVE"), 0x0E, 4, 1), EN(TX("FINETUNE"), 0x0E, 5, 1), EN(TX("LOOP"), 0x0E, 6, 1),
    EN(TX("TREMOLO WAVE"), 0x0E, 7, 1), EN(TX("RETRIGGER"), 0x0E, 9, 1), EN(TX("FINE VOLUME UP"), 0x0E, 10, 1),
    EN(TX("FINE VOLUME DOWN"), 0x0E, 11, 1), EN(TX("NOTE CUT"), 0x0E, 12, 1), EN(TX("NOTE DELAY"), 0x0E, 13, 1),
    EN(TX("PATTERN DELAY"), 0x0E, 14, 1),
    EN(TX("TICKS / TEMPO"), 0x0F, -1, 2), EN(TX("GLOBAL VOLUME"), 0x10, -1, 2), EN(TX("GLOBAL VOL SLIDE"), 0x11, -1, 2),
    EN(TX("KEY OFF"), 0x14, -1, 2), EN(TX("PAN SLIDE"), 0x19, -1, 2), EN(TX("MULTI RETRIGGER"), 0x1B, -1, 2),
    EN(TX("TREMOR"), 0x1D, -1, 2), EN(TX("FINE VIBRATO"), 0x1E, -1, 2), EN(TX("EXTRA FINE UP"), 0x21, 1, 1),
    EN(TX("EXTRA FINE DOWN"), 0x21, 2, 1), EN(TX("PANBRELLO"), 0x22, -1, 2),
    EN(TX("VOL SLIDE DOWN"), 0x24, -1, 1), EN(TX("VOL SLIDE UP"), 0x25, -1, 1), EN(TX("FINE VOL DOWN"), 0x26, -1, 1),
    EN(TX("FINE VOL UP"), 0x27, -1, 1), EN(TX("VIBRATO SPEED"), 0x28, -1, 1), EN(TX("VIBRATO DEPTH"), 0x29, -1, 1),
    EN(TX("PAN"), 0x2A, -1, 1), EN(TX("PAN SLIDE LEFT"), 0x2B, -1, 1), EN(TX("PAN SLIDE RIGHT"), 0x2C, -1, 1),
    EN(TX("SLIDE TO NOTE"), 0x2D, -1, 1),
    EN(TX("ACCENT"), 0xC0, -1, 0), EN(TX("SLIDE"), 0xC1, -1, 0), EN(TX("ACCENT + SLIDE"), 0xC2, -1, 0),
    EN(TX("LEGATO"), 0xD3, -1, 0),
    {TX("LOOP 1/4"), 0xC3, -1, 2, 1}, {TX("LOOP 1/8"), 0xC4, -1, 2, 1}, {TX("LOOP 1/16"), 0xC5, -1, 2, 1},
    {TX("LOOP 1/32"), 0xC6, -1, 2, 1}, {TX("STUTTER 1/16T"), 0xC7, -1, 2, 1}, {TX("BIT CRUSH"), 0xC8, -1, 2, 1},
    {TX("GATE 1/16"), 0xC9, -1, 2, 1}, {TX("GATE 1/32"), 0xCA, -1, 2, 1}, {TX("LOW-PASS SWEEP"), 0xCB, -1, 2, 1},
    {TX("HIGH-PASS SWEEP"), 0xCC, -1, 2, 1}, {TX("ECHO THROW"), 0xCD, -1, 2, 1}, {TX("REVERB SWELL"), 0xCE, -1, 2, 1},
    {TX("REVERSE"), 0xCF, -1, 2, 1}, {TX("HALF SPEED"), 0xD0, -1, 2, 1}, {TX("TAPE STOP"), 0xD1, -1, 2, 1},
    {TX("VINYL STOP"), 0xD2, -1, 2, 1},
    EN(TX("PARAMETER"), CE_GROUP + CG_PARAM, -1, -1), EN(TX("MIXER"), CE_GROUP + CG_MIXER, -1, -1),
    EN(TX("FX"), CE_GROUP + CG_FX, -1, -1),
};
#undef EN
const int UI_NENTRIES = (int)(sizeof UI_ENTRIES / sizeof UI_ENTRIES[0]);

static int group_of(uint8_t c) { return ui_cmd_group(c); }
int ui_cmd_entry(uint8_t c, uint8_t v)
{
    int g = group_of(c);
    for (int i = 1; i < UI_NENTRIES; i++) {
        const ui_entry_t *e = &UI_ENTRIES[i];
        if (g ? e->code == CE_GROUP + g : e->code == c && (e->sub < 0 || e->sub == v >> 4)) return i;
    }
    return -1;
}
int ui_cmd_punch(uint8_t c) { return c >= 0xC3 && c <= 0xD2 ? c - 0xC3 : -1; }
int ui_cmd_is_global(uint8_t c, uint8_t v)
{
    return c == 0x0B || c == 0x0D || c == 0x0F || c == 0x10 || c == 0x11 || (c == 0x0E && ((v >> 4) == 6 || (v >> 4) == 0xE))
        || group_of(c) == CG_FX || ui_cmd_punch(c) >= 0;
}

/* ------------------------------------------------------------------------- the parameters --- */
enum { PW_HEX, PW_LIST, PW_SGN1, PW_SGN2, PW_XN, PW_FRAMES, PW_CENTS, PW_TAB, PW_WAVE };   /* (CHIP's: X1-X4; frames and
                                                    * cents stored halved; a table (off: its kind) or a wave by name) */
/* a parameter: its name a TX() text, or `made` (OP1 LEVEL, ENV R1: written into pname) */
typedef struct { txt_t name; uint8_t max, words; int16_t off; const txt_t *list; const char *made; } param_t;
static const txt_t OFFON[] = {TX("OFF"), TX("ON")};
static const txt_t JUNO_RANGE[] = {TX("16'"), TX("8'"), TX("4'")}, JUNO_CHO[] = {TX("OFF"), TX("I"), TX("II")};
static const txt_t JUNO_PWM[] = {TX("LFO"), TX("MAN")}, JUNO_POL[] = {TX("+"), TX("-")}, JUNO_VCA[] = {TX("ENV"), TX("GATE")};
static const txt_t DX_WAVE[] = {TX("TRI"), TX("SAW DOWN"), TX("SAW UP"), TX("SQUARE"), TX("SINE"), TX("S/H")};
static const txt_t SMP_LOOP[] = {TX("OFF"), TX("FORWARD"), TX("PING-PONG")};
static const txt_t TB_N[6] = {TX("WAVE"), TX("CUTOFF"), TX("RESONANCE"), TX("ENV MOD"), TX("DECAY"), TX("ACCENT")};
static const txt_t JUNO_N[16] = {TX("LFO RATE"), TX("LFO DELAY"), TX("DCO LFO"), TX("DCO PWM"), TX("DCO NOISE"), TX("VCF FREQ"),
                                 TX("VCF RES"), TX("VCF ENV"), TX("VCF LFO"), TX("VCF KYBD"), TX("VCA LEVEL"), TX("ENV A"),
                                 TX("ENV D"), TX("ENV S"), TX("ENV R"), TX("DCO SUB")};
static const param_t JUNO_X[8] = {{TX("RANGE"), 2, PW_LIST, 0, JUNO_RANGE}, {TX("PULSE"), 1, PW_LIST, 0, OFFON},
                                  {TX("SAW"), 1, PW_LIST, 0, OFFON}, {TX("CHORUS"), 2, PW_LIST, 0, JUNO_CHO},
                                  {TX("PWM SOURCE"), 1, PW_LIST, 0, JUNO_PWM}, {TX("ENV POLARITY"), 1, PW_LIST, 0, JUNO_POL},
                                  {TX("VCA MODE"), 1, PW_LIST, 0, JUNO_VCA}, {TX("HPF"), 3, PW_HEX, 0, 0}};
static const txt_t DX_OPF[8] = {TX("LEVEL"), TX("COARSE"), TX("FINE"), TX("DETUNE"), TX("R1"), TX("R2"), TX("R3"), TX("R4")};
static const param_t DX_V[11] = {{TX("ALGORITHM"), 31, PW_HEX, 0, 0}, {TX("FEEDBACK"), 7, PW_HEX, 0, 0},
                                 {TX("OSC KEY SYNC"), 1, PW_LIST, 0, OFFON}, {TX("TRANSPOSE"), 48, PW_SGN2, 24, 0},
                                 {TX("LFO SPEED"), 99, PW_HEX, 0, 0}, {TX("LFO DELAY"), 99, PW_HEX, 0, 0},
                                 {TX("LFO PITCH"), 99, PW_HEX, 0, 0}, {TX("LFO AMP"), 99, PW_HEX, 0, 0},
                                 {TX("LFO WAVE"), 5, PW_LIST, 0, DX_WAVE}, {TX("LFO KEY SYNC"), 1, PW_LIST, 0, OFFON},
                                 {TX("PITCH MOD SENS"), 7, PW_HEX, 0, 0}};
static const param_t SMP_P[6] = {{TX("START"), 255, PW_HEX, 0, 0}, {TX("END"), 255, PW_HEX, 0, 0},
                                 {TX("LOOP"), 2, PW_LIST, 0, SMP_LOOP}, {TX("LOOP START"), 255, PW_HEX, 0, 0},
                                 {TX("LOOP END"), 255, PW_HEX, 0, 0}, {TX("TUNE"), 255, PW_SGN2, 128, 0}};
static const txt_t C_OSC[] = {TX("PULSE"), TX("TRIANGLE"), TX("WAVE"), TX("BUZZ"), TX("NOISE")};
static const txt_t C_DUTY[] = {TX("12.5%"), TX("25%"), TX("50%"), TX("75%")}, C_BSH[] = {TX("SAW DN"), TX("SAW UP"), TX("TRI")};
static const txt_t C_STEPS[] = {TX("16"), TX("32")}, C_MODE[] = {TX("LONG"), TX("SHORT")};
static const txt_t C_CLOCK[] = {TX("BRK"), TX("8BIT"), TX("PSG 1.77"), TX("PSG 1.00"), TX("PSG 1.79"), TX("16BIT"), TX("EXACT")};
static const txt_t C_CURVE[] = {TX("BRK"), TX("8BIT"), TX("PSG"), TX("16BIT")}, C_FILT[] = {TX("BRK"), TX("8BIT"), TX("PSG"), TX("NONE")};
static const txt_t C_FPS[] = {TX("60 HZ"), TX("50 HZ")}, C_SWEEP[] = {TX("OFF"), TX("DOWN"), TX("UP")};
static const txt_t C_ENV[] = {TX("NONE"), TX("DOWN"), TX("UP"), TX("PSG")};
/* CHIP's 0x40-0x5B (0 where a code is free), then the tables' and speeds' 0x60-0x6C, the vibrato's 0x6D-0x6F */
static const param_t CHIP_P[28] = {
    {TX("OSC"), 4, PW_LIST, 0, C_OSC}, {TX("DUTY"), 3, PW_LIST, 0, C_DUTY}, {TX("BUZZ SHAPE"), 2, PW_LIST, 0, C_BSH},
    {TX("BUZZ STEPS"), 1, PW_LIST, 0, C_STEPS}, {TX("BUZZ GATE"), 1, PW_LIST, 0, OFFON}, {0}, {0}, {0},
    {TX("NOISE"), 1, PW_LIST, 0, OFFON}, {TX("NOISE MODE"), 1, PW_LIST, 0, C_MODE}, {TX("NOISE RATE"), 15, PW_HEX, 0, 0},
    {TX("NOISE KEY"), 1, PW_LIST, 0, OFFON}, {TX("CLOCK"), 6, PW_LIST, 0, C_CLOCK}, {TX("CURVE"), 3, PW_LIST, 0, C_CURVE},
    {TX("FILTER"), 3, PW_LIST, 0, C_FILT}, {TX("FRAMES"), 1, PW_LIST, 0, C_FPS}, {TX("SWEEP"), 2, PW_LIST, 0, C_SWEEP},
    {TX("SWEEP SHIFT"), 7, PW_HEX, 0, 0}, {TX("SWEEP PACE"), 7, PW_HEX, 0, 0}, {0}, {TX("VOL"), 15, PW_HEX, 0, 0},
    {TX("ENV"), 3, PW_LIST, 0, C_ENV}, {TX("ENV STEP"), 7, PW_HEX, 0, 0}, {TX("RELEASE"), 15, PW_HEX, 0, 0},
    {TX("PSG SHAPE"), 15, PW_HEX, 0, 0}, {TX("PSG PERIOD HI"), 255, PW_HEX, 0, 0}, {TX("PSG PERIOD LO"), 255, PW_HEX, 0, 0},
    {TX("WAVE"), 15, PW_WAVE, 0, 0}};
static const txt_t C_TABN[5] = {TX("VOL TABLE"), TX("ARP TABLE"), TX("PITCH TABLE"), TX("DUTY TABLE"), TX("NOISE TABLE")};
static const txt_t C_SPDN[5] = {TX("VOL SPEED"), TX("ARP SPEED"), TX("PITCH SPEED"), TX("DUTY SPEED"), TX("NOISE SPEED")};
static const param_t CHIP_V[3] = {{TX("VIB DELAY"), 31, PW_FRAMES, 0, 0}, {TX("VIB SPEED"), 3, PW_XN, 0, 0},
                                  {TX("VIB DEPTH"), 31, PW_CENTS, 0, 0}};
static char pname[4][20];
static int pname_at;
/* the parameter a code names for an engine: 0 none */
static int param(int engine, uint8_t c, param_t *p)
{
    memset(p, 0, sizeof *p);
    if (engine == ENG_TB303) { if (c >= 0x40 && c <= 0x45) { p->name = TB_N[c - 0x40]; p->max = 127; return 1; } return 0; }
    if (engine == ENG_JUNO) {
        if (c >= 0x40 && c <= 0x4F) { p->name = JUNO_N[c - 0x40]; p->max = 127; return 1; }
        if (c >= 0x50 && c <= 0x57) { *p = JUNO_X[c - 0x50]; return 1; }
        return 0;
    }
    if (engine == ENG_DX7) {
        if (c >= 0x40 && c <= 0x6F) {
            int op = (c - 0x40) / 8 + 1, k = (c - 0x40) % 8;
            char *b = pname[pname_at++ & 3];
            ui_fmt(b, 20, T("OP%d %s"), op, T(DX_OPF[k]));
            p->made = b;
            p->max = k == 0 || k == 2 || k >= 4 ? 99 : k == 1 ? 31 : 14;
            if (k == 3) { p->words = PW_SGN1; p->off = 7; }
            return 1;
        }
        if (c >= 0x70 && c <= 0x7A) { *p = DX_V[c - 0x70]; return 1; }
        return 0;
    }
    if (engine == ENG_CHIP) {
        if (c >= 0x40 && c <= 0x5B) { *p = CHIP_P[c - 0x40]; return p->max != 0; }
        if (c >= 0x60 && c <= 0x64) { p->name = C_TABN[c - 0x60]; p->max = 63; p->words = PW_TAB; p->off = (int16_t)(c - 0x60); return 1; }
        if (c >= 0x68 && c <= 0x6C) { p->name = C_SPDN[c - 0x68]; p->max = 3; p->words = PW_XN; return 1; }
        if (c >= 0x6D && c <= 0x6F) { *p = CHIP_V[c - 0x6D]; return 1; }
        return 0;
    }
    if (engine == ENG_SAMPLER) {
        if (c >= 0x40 && c <= 0x45) { *p = SMP_P[c - 0x40]; return 1; }
        if (c >= 0x48 && c <= 0x4F) {
            char *b = pname[pname_at++ & 3];
            ui_fmt(b, 20, T("ENV %c%d"), c < 0x4C ? 'R' : 'L', (c & 3) + 1);
            p->made = b;
            p->max = 99;
            return 1;
        }
    }
    return 0;
}
static const char *pname_of(const param_t *p) { return p->made ? p->made : T(p->name); }
const char *ui_cmd_param_name(int engine, uint8_t c) { param_t p; return param(engine, c, &p) ? pname_of(&p) : 0; }
int ui_cmd_param_max(int engine, uint8_t c) { param_t p; return param(engine, c, &p) ? p.max : 255; }
static void sgn(char *o, int max, int v, int w)
{
    char s[2] = {v > 0 ? '+' : v < 0 ? '-' : 0, 0};
    ui_fmt(o, max, w == 2 ? "%s%02X" : "%s%X", s, v < 0 ? -v : v);
}
static void param_value(const param_t *p, int v, char *o, int max)
{
    if (v > p->max) v = p->max;
    if (p->words == PW_LIST) ui_fmt(o, max, "%s", T(p->list[v]));
    else if (p->words == PW_SGN1) sgn(o, max, v - p->off, 1);
    else if (p->words == PW_SGN2) sgn(o, max, v - p->off, 2);
    else if (p->words == PW_XN) ui_fmt(o, max, "X%d", v + 1);
    else if (p->words == PW_FRAMES) ui_fmt(o, max, T("%d FRAMES"), v * 2);
    else if (p->words == PW_CENTS) ui_fmt(o, max, T("%d CENTS"), v * 2);
    else if (p->words == PW_WAVE) ui_fmt(o, max, "%02X %s", v, T(CHIP_WAVE_NAMES[v & 15]));
    else if (p->words == PW_TAB) {
        if (!v) ui_fmt(o, max, "--");
        else if (v <= CHIP_TAB_N[p->off]) ui_fmt(o, max, "%02X %s", v, T(CHIP_TAB_NAMES[p->off][v - 1]));
        else ui_fmt(o, max, "%02X", v);
    } else ui_fmt(o, max, "%02X", v);
}

static const txt_t MIXER_N[5] = {TX("LEVEL"), TX("CHORUS SEND"), TX("DELAY SEND"), TX("REVERB SEND"), TX("DUCK AMOUNT")};
static const txt_t MIXER_S[5] = {TX("LEVEL"), TX("CHO SEND"), TX("DLY SEND"), TX("REV SEND"), TX("DUCK")};

/* ------------------------------------------------------------------------------- the FX --- */
/* the effects and their knobs are the FX screen's (fxdef.h); an FX command's value is the knob's step
 * from the bottom of its range (specs/files/commands.md) */
#define FXI_N FE_N
static int fx_code(uint8_t c, int *fx, int *knob)
{
    for (int f = 0; f < FE_N; f++)
        for (int k = 0; k < FX_EFF[f].nk; k++)
            if (FX_EFF[f].code && fxd_code(f, k) == c) { *fx = f; *knob = k; return 1; }
    return 0;
}
int ui_cmd_fx(uint8_t c, int *fx, int *knob) { return fx_code(c, fx, knob); }
static int fx_knob_value(int f, int k, int v)
{
    const fx_knob_t *kn = &FX_EFF[f].k[k];
    if (kn->list) return v < kn->n - 1 ? v : kn->n - 1;
    int x = kn->min + v * (kn->step ? kn->step : 1);
    return x > kn->max ? kn->max : x;
}
static int fx_steps(int f, int k)
{
    const fx_knob_t *kn = &FX_EFF[f].k[k];
    return kn->list ? kn->n - 1 : (kn->max - kn->min) / (kn->step ? kn->step : 1);
}

/* ------------------------------------------------------------------------------- in words --- */
static void vol_slide(int v, char *o, int max)
{
    int x = v >> 4, y = v & 15;
    if (x == 15 && y) ui_fmt(o, max, T("FINE -%X"), y);
    else if (y == 15 && x) ui_fmt(o, max, T("FINE +%X"), x);
    else if (x) ui_fmt(o, max, "+%X", x);
    else ui_fmt(o, max, "-%X", y);
}
static const txt_t WAVES[4] = {TX("SINE"), TX("SAW"), TX("SQUARE"), TX("RANDOM")};
void ui_cmd_words(uint8_t c, uint8_t v, int engine, char *o, int max)
{
    char t[24];
    int g = group_of(c), x = v >> 4, y = v & 15;
    if (g == CG_PARAM) {
        param_t p;
        if (!param(engine, c, &p)) { ui_fmt(o, max, T("PARAMETER %02X %02X"), c, v); return; }
        param_value(&p, v, t, sizeof t);
        ui_fmt(o, max, "%s %s", pname_of(&p), t);
        return;
    }
    if (g == CG_MIXER) {
        if (c <= 0x84) ui_fmt(o, max, "%s %02X", T(MIXER_N[c - 0x80]), v);
        else ui_fmt(o, max, T("MIXER %02X %02X"), c, v);
        return;
    }
    if (g == CG_FX) {
        int f, k;
        if (c == 0xAE || c == 0xAF) {
            const char *w = c == 0xAE ? T("ON") : T("OFF");
            if (v < FXI_N) ui_fmt(o, max, "%s %s", T(FX_EFF[v].name), w); else ui_fmt(o, max, T("FX %s"), w);
            return;
        }
        if (!fx_code(c, &f, &k)) { ui_fmt(o, max, T("FX %02X %02X"), c, v); return; }
        char kv[32];
        fxd_text(f, k, fx_knob_value(f, k, v), kv);
        ui_fmt(o, max, "%s %s %s", T(FX_EFF[f].name), T(FX_EFF[f].k[k].name), kv);
        return;
    }
    int pi = ui_cmd_punch(c);
    if (pi >= 0) { ui_fmt(o, max, "%s %d%s", T(UI_ENTRIES[ui_cmd_entry(c, v)].name), v, v == 1 ? T(" ROW") : T(" ROWS")); return; }
    switch (c) {
    case 0x00: ui_fmt(o, max, T("ARPEGGIO 0 %X %X"), x, y); return;
    case 0x01: ui_fmt(o, max, T("SLIDE UP %02X"), v); return;
    case 0x02: ui_fmt(o, max, T("SLIDE DOWN %02X"), v); return;
    case 0x03: ui_fmt(o, max, T("SLIDE TO NOTE %02X"), v); return;
    case 0x04: ui_fmt(o, max, T("VIBRATO %X %X"), x, y); return;
    case 0x05: vol_slide(v, t, sizeof t); ui_fmt(o, max, T("TO NOTE, VOL %s"), t); return;
    case 0x06: vol_slide(v, t, sizeof t); ui_fmt(o, max, T("VIBRATO, VOL %s"), t); return;
    case 0x07: ui_fmt(o, max, T("TREMOLO %X %X"), x, y); return;
    case 0x08:
        if (v == 128) ui_fmt(o, max, "%s", T("PAN MIDDLE"));
        else ui_fmt(o, max, T("PAN %s %02X"), v < 128 ? T("LEFT") : T("RIGHT"), v < 128 ? 128 - v : v - 128);
        return;
    case 0x09: ui_fmt(o, max, T("START AT %02X00"), v); return;
    case 0x0A: vol_slide(v, t, sizeof t); ui_fmt(o, max, T("VOLUME %s"), t); return;
    case 0x0B: ui_fmt(o, max, T("JUMP TO POS %02X"), v); return;
    case 0x0C: ui_fmt(o, max, T("VOLUME %02X"), v > 64 ? 64 : v); return;
    case 0x0D: ui_fmt(o, max, T("BREAK TO ROW %02X"), v); return;
    case 0x0E:
        switch (x) {
        case 3: ui_fmt(o, max, T("GLISSANDO %s"), y ? T("ON") : T("OFF")); return;
        case 4: case 7: ui_fmt(o, max, "%s %s", x == 4 ? T("VIBRATO WAVE") : T("TREMOLO WAVE"), T(WAVES[y & 3])); return;
        case 5: sgn(t, sizeof t, y - 8, 1); ui_fmt(o, max, T("FINETUNE %s"), t); return;
        case 6: if (y) ui_fmt(o, max, T("LOOP %d TIMES"), y); else ui_fmt(o, max, "%s", T("LOOP FROM HERE")); return;
        case 9: ui_fmt(o, max, T("RETRIGGER EVERY %d"), y); return;
        case 12: ui_fmt(o, max, T("NOTE CUT AT TICK %X"), y); return;
        case 13: ui_fmt(o, max, T("NOTE DELAY %d TICKS"), y); return;
        case 14: ui_fmt(o, max, T("ROW DELAY %d ROWS"), y); return;
        default: {
            int e = ui_cmd_entry(c, v);
            if (e > 0) ui_fmt(o, max, "%s %X", T(UI_ENTRIES[e].name), y); else ui_cmd_text(c, v, o);
            return;
        }
        }
    case 0x0F:
        if (!v) ui_fmt(o, max, "%s", T("THE SONG TEMPO"));
        else if (v < 0x20) ui_fmt(o, max, T("TICKS A ROW %d"), v);
        else ui_fmt(o, max, T("TEMPO %02X"), v);
        return;
    case 0x10: ui_fmt(o, max, T("GLOBAL VOLUME %02X"), v > 64 ? 64 : v); return;
    case 0x11: vol_slide(v, t, sizeof t); ui_fmt(o, max, T("GLOBAL VOL %s"), t); return;
    case 0x14: ui_fmt(o, max, T("NOTE OFF AT TICK %02X"), v); return;
    case 0x19: if (x) ui_fmt(o, max, T("PAN RIGHT %X"), x); else ui_fmt(o, max, T("PAN LEFT %X"), y); return;
    case 0x1B: ui_fmt(o, max, T("RETRIGGER %d, VOL %X"), y, x); return;
    case 0x1D: ui_fmt(o, max, T("TREMOR ON %d OFF %d"), x, y); return;
    case 0x1E: ui_fmt(o, max, T("FINE VIBRATO %X %X"), x, y); return;
    case 0x21: if (x == 1 || x == 2) { ui_fmt(o, max, T("EXTRA FINE %s %X"), x == 1 ? T("UP") : T("DOWN"), y); return; } break;
    case 0x22: ui_fmt(o, max, T("PANBRELLO %X %X"), x, y); return;
    case 0xC0: ui_fmt(o, max, "%s", T("ACCENT")); return;
    case 0xC1: ui_fmt(o, max, "%s", T("SLIDE")); return;
    case 0xC2: ui_fmt(o, max, "%s", T("ACCENT + SLIDE")); return;
    case 0xD3: ui_fmt(o, max, "%s", T("LEGATO")); return;
    default: break;
    }
    if (c >= 0x24 && c <= 0x2D) {
        if (c == 0x2A) ui_fmt(o, max, T("PAN %02X"), (v & 15) * 17);
        else ui_fmt(o, max, "%s %X", T(UI_ENTRIES[ui_cmd_entry(c, v)].name), v & 15);
        return;
    }
    ui_cmd_text(c, v, o);
}

/* a command's name in 9 characters at most, for the 1-track view (its value is in its 3 characters) */
static const txt_t SHORT[][2] = {
    {TX("SLIDE DOWN"), TX("SLIDE DN")}, {TX("SLIDE TO NOTE"), TX("TO NOTE")}, {TX("SLIDE TO NOTE + VOL"), TX("NOTE+VOL")},
    {TX("VIBRATO + VOL"), TX("VIB+VOL")}, {TX("SAMPLE OFFSET"), TX("OFFSET")}, {TX("VOLUME SLIDE"), TX("VOL SLIDE")},
    {TX("FINE SLIDE UP"), TX("FINE UP")}, {TX("FINE SLIDE DOWN"), TX("FINE DN")}, {TX("VIBRATO WAVE"), TX("VIB WAVE")},
    {TX("TREMOLO WAVE"), TX("TREM WAVE")}, {TX("FINE VOLUME UP"), TX("FINE VOL+")}, {TX("FINE VOLUME DOWN"), TX("FINE VOL-")},
    {TX("NOTE DELAY"), TX("NOTE DLY")}, {TX("PATTERN DELAY"), TX("ROW DELAY")}, {TX("GLOBAL VOLUME"), TX("GLOB VOL")},
    {TX("GLOBAL VOL SLIDE"), TX("GLOB SLD")}, {TX("MULTI RETRIGGER"), TX("RETRIG+V")}, {TX("FINE VIBRATO"), TX("FINE VIB")},
    {TX("EXTRA FINE UP"), TX("XFINE UP")}, {TX("EXTRA FINE DOWN"), TX("XFINE DN")}, {TX("VOL SLIDE DOWN"), TX("VOL DOWN")},
    {TX("VOL SLIDE UP"), TX("VOL UP")}, {TX("FINE VOL DOWN"), TX("FINE VOL-")}, {TX("FINE VOL UP"), TX("FINE VOL+")},
    {TX("VIBRATO SPEED"), TX("VIB SPEED")}, {TX("VIBRATO DEPTH"), TX("VIB DEPTH")}, {TX("PAN SLIDE LEFT"), TX("PAN LEFT")},
    {TX("PAN SLIDE RIGHT"), TX("PAN RIGHT")}, {TX("ACCENT + SLIDE"), TX("ACC+SLIDE")}, {TX("STUTTER 1/16T"), TX("STUTTER")},
    {TX("HALF SPEED"), TX("HALF SPD")}, {TX("LOW-PASS SWEEP"), TX("LP SWEEP")}, {TX("HIGH-PASS SWEEP"), TX("HP SWEEP")},
    {TX("ECHO THROW"), TX("ECHO THRW")}, {TX("REVERB SWELL"), TX("REV SWELL")}, {TX("VINYL STOP"), TX("VINYL STP")},
    {TX("OSC KEY SYNC"), TX("OSC SYNC")}, {TX("LFO KEY SYNC"), TX("LFO SYNC")}, {TX("PITCH MOD SENS"), TX("PITCH MOD")},
    {TX("PWM SOURCE"), TX("PWM SRC")}, {TX("ENV POLARITY"), TX("ENV POL")}, {TX("LOOP START"), TX("LOOP STRT")},
    {TX("BUZZ SHAPE"), TX("BUZ SHAPE")}, {TX("BUZZ STEPS"), TX("BUZ STEPS")}, {TX("NOISE MODE"), TX("NSE MODE")},
    {TX("NOISE RATE"), TX("NSE RATE")}, {TX("SWEEP SHIFT"), TX("SWP SHIFT")}, {TX("SWEEP PACE"), TX("SWP PACE")},
    {TX("PSG PERIOD HI"), TX("PERIOD HI")}, {TX("PSG PERIOD LO"), TX("PERIOD LO")}, {TX("PITCH TABLE"), TX("PIT TABLE")},
    {TX("DUTY TABLE"), TX("DTY TABLE")}, {TX("NOISE TABLE"), TX("NSE TABLE")}, {TX("PITCH SPEED"), TX("PIT SPEED")},
    {TX("DUTY SPEED"), TX("DTY SPEED")}, {TX("NOISE SPEED"), TX("NSE SPEED")},
};
static const txt_t FX_SHORT[FXI_N] = {TX("GATE"), TX("FILT"), TX("DRV"), TX("LOFI"), TX("CHO"), TX("DLY"), TX("REV"),
                                      TX("LIM"), TX("DUCK"), TX("METRO")};
static const txt_t KNOB_SHORT[][2] = {{TX("DEPTH"), TX("DPTH")}, {TX("SMOOTH"), TX("SMTH")}, {TX("PATTERN"), TX("PATT")},
                                      {TX("FEEDBACK"), TX("FDBK")}, {TX("PING-PONG"), TX("PING")}, {TX("PRE-DELAY"), TX("PRE")},
                                      {TX("CEILING"), TX("CEIL")}, {TX("RELEASE"), TX("REL")}, {TX("SOURCE"), TX("SRC")},
                                      {TX("ATTACK"), TX("ATK")}, {TX("DECAY"), TX("DEC")}, {TX("SUSTAIN"), TX("SUS")}};
/* o: the name n (a text of its own: T() turns its buffers while the list is read), or its short form in the list */
static void shorten(const txt_t (*list)[2], unsigned n_list, const char *n, char *o, int max)
{
    char name[24];
    ui_fmt(name, sizeof name, "%s", n);
    for (unsigned i = 0; i < n_list; i++) if (!strcmp(T(list[i][0]), name)) { ui_fmt(o, max, "%s", T(list[i][1])); return; }
    ui_fmt(o, max, "%s", name);
}
static void replace_word(char *s, const char *from, const char *to)
{
    char *p = strstr(s, from);
    if (!p) return;
    char tail[24];
    ui_fmt(tail, sizeof tail, "%s", p + strlen(from));
    ui_fmt(p, 24, "%s%s", to, tail);
}
const char *ui_cmd_short(uint8_t c, uint8_t v, int engine)
{
    static char b[4][24];
    static int at;
    char *o = b[at++ & 3];
    int g = group_of(c);
    if (g == CG_PARAM) {
        param_t p;
        if (!param(engine, c, &p)) ui_fmt(o, 24, T("PARAM %02X"), c);
        else {
            shorten(SHORT, sizeof SHORT / sizeof SHORT[0], pname_of(&p), o, 24);
            replace_word(o, T("COARSE"), T("CRS"));
            replace_word(o, T("DETUNE"), T("DTUNE"));
        }
    } else if (g == CG_MIXER) {
        if (c <= 0x84) ui_fmt(o, 24, "%s", T(MIXER_S[c - 0x80])); else ui_fmt(o, 24, T("MIXER %02X"), c);
    } else if (g == CG_FX) {
        int f, k;
        if (c == 0xAE || c == 0xAF) ui_fmt(o, 24, "%s %s", v < FXI_N ? T(FX_SHORT[v]) : T("FX"), c == 0xAE ? T("ON") : T("OFF"));
        else if (!fx_code(c, &f, &k)) ui_fmt(o, 24, T("FX %02X"), c);
        else {
            char kn[12];
            shorten(KNOB_SHORT, sizeof KNOB_SHORT / sizeof KNOB_SHORT[0], T(FX_EFF[f].k[k].name), kn, sizeof kn);
            ui_fmt(o, 24, "%s %s", T(FX_SHORT[f]), kn);
        }
    } else if (c == 0x0F) ui_fmt(o, 24, "%s", !v ? T("SONG BPM") : v < 0x20 ? T("TICKS") : T("TEMPO"));
    else {
        int e = ui_cmd_entry(c, v);
        if (e > 0) shorten(SHORT, sizeof SHORT / sizeof SHORT[0], T(UI_ENTRIES[e].name), o, 24); else ui_cmd_text(c, v, o);
    }
    o[9] = 0;
    return o;
}

/* ------------------------------------------------------------------------ the three groups --- */
int ui_cmd_codes(int group, int engine, uint8_t *codes, int max)
{
    int n = 0;
    param_t p;
    if (group == CG_PARAM) { for (int c = 0x40; c <= 0x7F && n < max; c++) if (param(engine, (uint8_t)c, &p)) codes[n++] = (uint8_t)c; }
    else if (group == CG_MIXER) { for (int c = 0x80; c <= 0x84 && n < max; c++) codes[n++] = (uint8_t)c; }
    else if (group == CG_FX) {
        for (int c = 0x90; c <= 0xAC && n < max; c++) {
            codes[n++] = (uint8_t)c;
            if (c == 0x95)                              /* (FILTER's envelope after its CUT, RESO) */
                for (int d = 0xB0; d <= 0xB5 && n < max; d++) codes[n++] = (uint8_t)d;
        }
        if (n < max) codes[n++] = 0xAE;
        if (n < max) codes[n++] = 0xAF;
    }
    return n;
}
void ui_cmd_code_name(uint8_t c, int engine, char *o, int max)
{
    int g = group_of(c), f, k;
    if (g == CG_PARAM) { const char *n = ui_cmd_param_name(engine, c); if (n) ui_fmt(o, max, "%s", n); else ui_fmt(o, max, "%02X", c); }
    else if (g == CG_MIXER) ui_fmt(o, max, "%s", c <= 0x84 ? T(MIXER_N[c - 0x80]) : T("MIXER"));
    else if (c == 0xAE || c == 0xAF) ui_fmt(o, max, T("FX %s"), c == 0xAE ? T("ON") : T("OFF"));
    else if (fx_code(c, &f, &k)) ui_fmt(o, max, "%s %s", T(FX_EFF[f].name), T(FX_EFF[f].k[k].name));
    else ui_fmt(o, max, T("FX %02X"), c);
}
int ui_cmd_max(uint8_t c, int engine)
{
    int g = group_of(c), f, k;
    if (g == CG_PARAM) return ui_cmd_param_max(engine, c);
    if (g == CG_MIXER) return 127;
    if (c == 0xAE || c == 0xAF) return FXI_N - 1;
    if (fx_code(c, &f, &k)) return fx_steps(f, k);
    return 255;
}
