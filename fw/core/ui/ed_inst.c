/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Editing instruments (ed_inst.h). A slot's bits (specs/files/instruments.md), LSB first: the DX7's six
 * operators (OP6 first, 113 bits each), its voice (106), name (60) and LEVEL (8); the Juno-106's 16
 * sliders (7 each), switches, name and LEVEL; the TB-303's six knobs; a sample instrument's fields; a
 * FUSION slot's pattern. The edit pages follow the playground's scr-instr.js. */
#include <string.h>
#include "ui_core.h"
#include "ui_data.h"
#include "ed_inst.h"
#include "ed_pat.h"
#include "../play/rec.h"
#include "../store/store.h"

/* ---------------------------------------------------------------------------- the pages --- */
static const txt_t OFFON[] = {TX("OFF"), TX("ON")};
static const txt_t DXWAVE[] = {TX("TRI"), TX("SAW-"), TX("SAW+"), TX("SQR"), TX("SINE"), TX("S+H")};
static const txt_t JRANGE[] = {TX("16'"), TX("8'"), TX("4'")}, JPWM[] = {TX("LFO"), TX("MAN")}, JPOL[] = {TX("+"), TX("-")};
static const txt_t JMODE[] = {TX("ENV"), TX("GATE")}, JCHO[] = {TX("OFF"), TX("I"), TX("II")};
static const txt_t LOOP_MODES[] = {TX("OFF"), TX("FWD"), TX("PING")};
#define N(n, b, w, mx) {n, 0, b, w, PK_NUM, 0, mx, 0}
#define L(n, b, w, lab, c) {n, 0, b, w, PK_LIST, 0, (c) - 1, lab}
#define LEVEL_P(b) {TX("LEVEL"), 0, b, 8, PK_LEVEL, -128, 127, 0}
#define OPB(i) ((5 - (i)) * 113)
#define VB 678
/* an operator's groups, named as written (`OP1`, `OP1 RATE`, `OP1 LVL`) */
#define OPG(i, nm) {nm, (i) - 1, 0, 0, 4, {N(TX("LEVEL"), OPB((i) - 1) + 89, 7, 99), N(TX("COARSE"), OPB((i) - 1) + 97, 5, 31), \
                    N(TX("FINE"), OPB((i) - 1) + 102, 7, 99), {TX("DETUNE"), 0, OPB((i) - 1) + 109, 4, PK_SGN, -7, 7, 0}}}
#define OPR(i, nm) {nm, (i) - 1, 0, 0, 4, {N(TX("R1"), OPB((i) - 1), 7, 99), N(TX("R2"), OPB((i) - 1) + 7, 7, 99), \
                    N(TX("R3"), OPB((i) - 1) + 14, 7, 99), N(TX("R4"), OPB((i) - 1) + 21, 7, 99)}}
#define OPL(i, nm) {nm, (i) - 1, 0, 0, 4, {N(TX("L1"), OPB((i) - 1) + 28, 7, 99), N(TX("L2"), OPB((i) - 1) + 35, 7, 99), \
                    N(TX("L3"), OPB((i) - 1) + 42, 7, 99), N(TX("L4"), OPB((i) - 1) + 49, 7, 99)}}
#define NAME_G {TX("NAME"), -1, 1, 0, 0, {{0}}}
static const ins_grp_t DX_OPS[] = {OPG(1, TX("OP1")), OPG(2, TX("OP2")), OPG(3, TX("OP3")), OPG(4, TX("OP4")), OPG(5, TX("OP5")),
                                   OPG(6, TX("OP6"))};
static const ins_grp_t DX_ENV[] = {OPR(1, TX("OP1 RATE")), OPL(1, TX("OP1 LVL")), OPR(2, TX("OP2 RATE")), OPL(2, TX("OP2 LVL")),
                                   OPR(3, TX("OP3 RATE")), OPL(3, TX("OP3 LVL")), OPR(4, TX("OP4 RATE")), OPL(4, TX("OP4 LVL")),
                                   OPR(5, TX("OP5 RATE")), OPL(5, TX("OP5 LVL")), OPR(6, TX("OP6 RATE")), OPL(6, TX("OP6 LVL"))};
static const ins_grp_t DX_PLFO[] = {
    {TX("P RATE"), -1, 0, 0, 4, {N(TX("R1"), VB, 7, 99), N(TX("R2"), VB + 7, 7, 99), N(TX("R3"), VB + 14, 7, 99), N(TX("R4"), VB + 21, 7, 99)}},
    {TX("P LEVEL"), -1, 0, 0, 4, {N(TX("L1"), VB + 28, 7, 99), N(TX("L2"), VB + 35, 7, 99), N(TX("L3"), VB + 42, 7, 99),
                                  N(TX("L4"), VB + 49, 7, 99)}},
    {TX("LFO"), -1, 0, 0, 4, {N(TX("SPEED"), VB + 65, 7, 99), N(TX("DELAY"), VB + 72, 7, 99), N(TX("PM DEPTH"), VB + 79, 7, 99),
                              N(TX("AM DEPTH"), VB + 86, 7, 99)}},
    {TX("LFO 2"), -1, 0, 0, 3, {L(TX("WAVE"), VB + 94, 3, DXWAVE, 6), L(TX("SYNC"), VB + 93, 1, OFFON, 2), N(TX("P SENS"), VB + 97, 3, 7)}},
};
static const ins_grp_t DX_VOICE[] = {
    NAME_G,
    {TX("VOICE"), -1, 0, 0, 4, {{TX("ALGORITHM"), 0, VB + 56, 5, PK_ALG, 1, 32, 0}, N(TX("FEEDBACK"), VB + 61, 3, 7),
                                L(TX("OSC SYNC"), VB + 64, 1, OFFON, 2), {TX("TRANSPOSE"), 0, VB + 100, 6, PK_SGN, -24, 24, 0}}},
    {TX("LEVEL"), -1, 0, 0, 1, {LEVEL_P(844)}},
};
static const ins_page_t DX_PAGES[] = {{TX("OPERATORS"), 6, DX_OPS, 0}, {TX("ENVELOPES"), 12, DX_ENV, 0},
                                      {TX("PITCH / LFO"), 4, DX_PLFO, 0}, {TX("VOICE"), 3, DX_VOICE, 0}};

static const ins_grp_t J_DCO[] = {
    {TX("DCO"), -1, 0, 0, 4, {N(TX("LFO"), 14, 7, 127), N(TX("PWM"), 21, 7, 127), N(TX("NOISE"), 28, 7, 127), N(TX("SUB"), 105, 7, 127)}},
    {TX("WAVES"), -1, 0, 0, 4, {L(TX("RANGE"), 112, 2, JRANGE, 3), L(TX("PULSE"), 114, 1, OFFON, 2), L(TX("SAW"), 115, 1, OFFON, 2),
                                L(TX("PWM BY"), 118, 1, JPWM, 2)}},
};
static const ins_grp_t J_VCF[] = {
    {TX("VCF"), -1, 0, 0, 4, {N(TX("FREQ"), 35, 7, 127), N(TX("RES"), 42, 7, 127), N(TX("ENV"), 49, 7, 127), N(TX("LFO"), 56, 7, 127)}},
    {TX("VCF 2"), -1, 0, 0, 3, {N(TX("KYBD"), 63, 7, 127), L(TX("ENV +/-"), 119, 1, JPOL, 2), N(TX("HPF"), 121, 2, 3)}},
};
static const ins_grp_t J_ENV[] = {
    {TX("ENV"), -1, 0, 0, 4, {N(TX("A"), 77, 7, 127), N(TX("D"), 84, 7, 127), N(TX("S"), 91, 7, 127), N(TX("R"), 98, 7, 127)}},
    {TX("VCA"), -1, 0, 0, 3, {N(TX("LEVEL"), 70, 7, 127), L(TX("MODE"), 120, 1, JMODE, 2), L(TX("CHORUS"), 116, 2, JCHO, 3)}},
    {TX("LFO"), -1, 0, 0, 2, {N(TX("RATE"), 0, 7, 127), N(TX("DELAY"), 7, 7, 127)}},
};
static const ins_grp_t J_VOICE[] = {NAME_G, {TX("LEVEL"), -1, 0, 0, 1, {LEVEL_P(183)}}};
static const ins_page_t J_PAGES[] = {{TX("DCO"), 2, J_DCO, 0}, {TX("FILTERS"), 2, J_VCF, 0}, {TX("ENVELOPE"), 3, J_ENV, 0},
                                     {TX("VOICE"), 2, J_VOICE, 0}};

static const ins_grp_t TB_KNOBS[] = {
    {TX("KNOBS"), -1, 0, 0, 4, {N(TX("WAVE"), 0, 7, 127), N(TX("CUTOFF"), 7, 7, 127), N(TX("RESO"), 14, 7, 127),
                                N(TX("ENV MOD"), 21, 7, 127)}},
    {TX("KNOBS 2"), -1, 0, 0, 2, {N(TX("DECAY"), 28, 7, 127), N(TX("ACCENT"), 35, 7, 127)}},
    NAME_G,
    {TX("LEVEL"), -1, 0, 0, 1, {LEVEL_P(102)}},
};
static const ins_page_t TB_PAGES[] = {{TX("KNOBS"), 4, TB_KNOBS, 0}};

static const ins_grp_t S_SAMPLE[] = {
    {TX("SAMPLE"), -1, 0, 0, 3, {{TX("SAMPLE"), 0, 0, 6, PK_SAMPLE, 0, 63, 0}, {TX("START"), 0, 6, 20, PK_POS, 0, 0, 0},
                                 {TX("END"), 0, 26, 20, PK_POS, 0, 0, 0}}},
    {TX("LOOP"), -1, 0, 0, 4, {L(TX("LOOP"), 86, 2, LOOP_MODES, 3), {TX("FROM"), TX("LOOP START"), 46, 20, PK_POS, 0, 0, 0},
                               {TX("TO"), TX("LOOP END"), 66, 20, PK_POS, 0, 0, 0}, {TX("RELEASE"), 0, 139, 7, PK_RELEASE, 0, 99, 0}}},
    {TX("PITCH"), -1, 0, 0, 4, {{TX("BASE NOTE"), 0, 88, 7, PK_NOTE, 12, 127, 0}, {TX("TUNE"), 0, 95, 8, PK_TUNE, -128, 127, 0},
                                N(TX("VOLUME"), 111, 7, 64), LEVEL_P(103)}},
    NAME_G,
};
static const ins_grp_t S_ENV[] = {
    {TX("RATES"), -1, 0, 0, 4, {N(TX("R1"), 118, 7, 99), N(TX("R2"), 125, 7, 99), N(TX("R3"), 132, 7, 99), N(TX("R4"), 139, 7, 99)}},
    {TX("LEVELS"), -1, 0, 0, 4, {N(TX("L1"), 146, 7, 99), N(TX("L2"), 153, 7, 99), N(TX("L3"), 160, 7, 99), N(TX("L4"), 167, 7, 99)}},
};
static const ins_page_t S_PAGES[] = {{TX("SAMPLE"), 4, S_SAMPLE, 0}, {TX("ENVELOPE"), 2, S_ENV, 0}};

static const ins_grp_t F_FUS[] = {
    {TX("PATTERN"), -1, 0, 1, 1, {{TX("PATTERN"), 0, 0, 8, PK_PATTERN, 0, 255, 0}}},
    NAME_G,
    {TX("LEVEL"), -1, 0, 0, 1, {LEVEL_P(68)}},
};
static const ins_page_t F_PAGES[] = {{TX("FUSION"), 3, F_FUS, 0}};

/* CHIP (specs/proposals/202610251559-chip-engine.md; fw/core/eng/chip/chip.h's CHIP_B_* the bits): four pages, the
 * TONE page's OSC row the oscillator's own knobs (a TONE page an oscillator kind) */
static const txt_t COSC[] = {TX("PULSE"), TX("TRIANGLE"), TX("WAVE"), TX("BUZZ"), TX("NOISE")};
static const txt_t CDUTY[] = {TX("12.5%"), TX("25%"), TX("50%"), TX("75%")}, CBSH[] = {TX("SAW DN"), TX("SAW UP"), TX("TRI")};
static const txt_t CSTEPS[] = {TX("16"), TX("32")}, CMODE[] = {TX("LONG"), TX("SHORT")}, CKEY[] = {TX("KEY OFF"), TX("KEY ON")};
static const txt_t CCLOCK[] = {TX("BRK"), TX("8BIT"), TX("PSG 1.77"), TX("PSG 1.00"), TX("PSG 1.79"), TX("16BIT"), TX("EXACT")};
static const txt_t CCURVE[] = {TX("BRK"), TX("8BIT"), TX("PSG"), TX("16BIT")}, CFILT[] = {TX("BRK"), TX("8BIT"), TX("PSG"), TX("NONE")};
static const txt_t CFPS[] = {TX("60 HZ"), TX("50 HZ")}, CSWEEP[] = {TX("OFF"), TX("DOWN"), TX("UP")};
static const txt_t CENV[] = {TX("NONE"), TX("DOWN"), TX("UP"), TX("PSG")};
static const txt_t PSGPIC[16] = {TX("\\___"), TX("\\___"), TX("\\___"), TX("\\___"), TX("/___"), TX("/___"), TX("/___"),
                                 TX("/___"), TX("\\\\\\\\"), TX("\\___"), TX("\\/\\/"), TX("\\```"), TX("////"), TX("/```"),
                                 TX("/\\/\\"), TX("/___")};
#define C_OSC L(TX("OSC"), 0, 3, COSC, 5)
/* the TONE page: its OSC row TRIANGLE's and NOISE's (the oscillator alone); PULSE, WAVE and BUZZ put theirs in its
 * place (ins_page_t.g0) */
static const ins_grp_t C_TONE[] = {
    {TX("OSC"), OPL_OSC, 0, 0, 1, {C_OSC}},
    {TX("NOISE"), -1, 0, 0, 4, {L(TX("NOISE"), 9, 1, OFFON, 2), L(TX("MODE"), 10, 1, CMODE, 2), N(TX("RATE"), 11, 4, 15),
                                L(TX("KEY"), 15, 1, CKEY, 2)}},
    {TX("CLOCK"), OPL_WIDE, 0, 0, 4, {L(TX("CLOCK"), 16, 3, CCLOCK, 7), L(TX("CURVE"), 19, 2, CCURVE, 4),
                                      L(TX("FILTER"), 21, 2, CFILT, 4), L(TX("FRAMES"), 23, 1, CFPS, 2)}},
    {TX("SWEEP"), -1, 0, 0, 3, {L(TX("SWEEP"), 24, 2, CSWEEP, 3), N(TX("SHIFT"), 26, 3, 7), N(TX("PACE"), 29, 3, 7)}},
    {TX("LEVEL"), -1, 0, 0, 1, {LEVEL_P(181)}}, NAME_G,
};
static const ins_grp_t C_OSC_ROW[3] = {
    {TX("OSC"), OPL_OSC, 0, 0, 2, {C_OSC, L(TX("DUTY"), 3, 2, CDUTY, 4)}},
    {TX("OSC"), OPL_OSC, 0, 0, 2, {C_OSC, L(TX("WAVE"), 65, 4, CHIP_WAVE_NAMES, 16)}},
    {TX("OSC"), OPL_OSC, 0, 0, 4, {C_OSC, L(TX("SHAPE"), 5, 2, CBSH, 3), L(TX("STEPS"), 7, 1, CSTEPS, 2), L(TX("GATE"), 8, 1, OFFON, 2)}},
};
static const ins_grp_t C_VOLUME[] = {
    {TX("VOLUME"), -1, 0, 0, 4, {N(TX("VOL"), 32, 4, 15), L(TX("ENV"), 36, 2, CENV, 4), N(TX("STEP"), 38, 3, 7),
                                 N(TX("RELEASE"), 41, 4, 15)}},
    /* (PERIOD: K2 its first two hex digits, FINE its last two) */
    {TX("PSG ENV"), OPL_WIDE, 0, 0, 3, {{TX("SHAPE"), 0, 45, 4, PK_PSGSHAPE, 0, 15, PSGPIC},
                                        {TX("PERIOD"), 0, 57, 8, PK_PERIOD, 0, 255, 0}, N(TX("FINE"), 49, 8, 255)}},
    {TX("LEVEL"), -1, 0, 0, 1, {LEVEL_P(181)}},
};
#define C_TAB(nm, k) {nm, OPL_TABLE - (k), 0, 0, 2, {{TX("TABLE"), 0, 69 + 8 * (k), 6, PK_CHTAB, k, 63, 0},             \
                                                     {TX("SPEED"), 0, 75 + 8 * (k), 2, PK_XN, 0, 3, 0}}}
static const ins_grp_t C_TABLES[] = {
    C_TAB(TX("VOL"), 0), C_TAB(TX("ARP"), 1), C_TAB(TX("PITCH"), 2),
    {TX("VIBRATO"), OPL_VIB, 0, 0, 3, {{TX("DELAY"), 0, 109, 5, PK_HALF, 0, 31, 0}, {TX("SPEED"), 0, 114, 2, PK_XN, 0, 3, 0},
                                       {TX("DEPTH"), 0, 116, 5, PK_HALF, 0, 31, 0}}},
    C_TAB(TX("DUTY"), 3), C_TAB(TX("NOISE"), 4),
};
static const ins_grp_t C_WAVE[] = {{TX("TABLE"), OPL_LONG, 0, 0, 1, {{TX("TABLE"), 0, 65, 4, PK_WAVE, 0, 15, CHIP_WAVE_NAMES}}}};
int ins_layout(const ins_grp_t *g, ins_lay_t *l)
{
    static const ins_lay_t OSC = {{60, 105, 150, 195}, {8, 10, 7, 7}, 0}, WIDE = {{60, 110, 160, 210}, {8, 8, 8, 5}, 0},
                           VIB = {{60, 86, 112, 0}, {3, 3, 3, 0}, 6}, LONG = {{60, 0, 0, 0}, {13, 0, 0, 0}, 0};
    if (!g || g->op > OPL_OSC) return 0;
    *l = g->op == OPL_OSC ? OSC : g->op == OPL_WIDE ? WIDE : g->op == OPL_VIB ? VIB : LONG;
    if (g->op <= OPL_TABLE) *l = (ins_lay_t){{60, 156, 0, 0}, {13, 2, 0, 0}, (uint8_t)(OPL_TABLE - g->op + 1)};
    return 1;
}
#define C_PAGES(osc) {{TX("TONE"), 6, C_TONE, osc}, {TX("VOLUME"), 3, C_VOLUME, 0}, {TX("TABLES"), 6, C_TABLES, 0}, \
                      {TX("WAVE"), 1, C_WAVE, 0}}
static const ins_page_t C_PAGES_OSC[4][4] = {C_PAGES(&C_OSC_ROW[0]), C_PAGES(&C_OSC_ROW[1]), C_PAGES(&C_OSC_ROW[2]), C_PAGES(0)};
#undef N
#undef L

const ins_grp_t *ins_grp(const ins_page_t *p, int i) { return !i && p->g0 ? p->g0 : &p->g[i]; }

int ins_pages(int kind, const ins_page_t **pages)
{
    switch (kind) {
    case REC_DX7: *pages = DX_PAGES; return 4;
    case REC_JUNO: *pages = J_PAGES; return 4;
    case REC_TB303: *pages = TB_PAGES; return 1;
    case REC_SAMPLER: *pages = S_PAGES; return 2;
    case REC_FUSION: *pages = F_PAGES; return 1;
    case REC_CHIP: *pages = C_PAGES_OSC[3]; return 4;
    default: *pages = 0; return 0;
    }
}
int ins_pages_of(int kind, const uint8_t *slot, const ins_page_t **pages)
{
    if (kind != REC_CHIP || !slot) return ins_pages(kind, pages);
    uint32_t osc = rec_bits(slot, 0, 3);                /* (PULSE, WAVE, BUZZ: their knobs; TRIANGLE, NOISE: none) */
    *pages = C_PAGES_OSC[osc == 0 ? 0 : osc == 2 ? 1 : osc == 3 ? 2 : 3];
    return 4;
}

/* ------------------------------------------------------------------------------ the bits --- */
static void put_bits(uint8_t *p, uint32_t pos, int n, uint32_t v)
{
    for (int i = 0; i < n; i++, pos++) {
        uint8_t m = (uint8_t)(1u << (pos & 7));
        if (v >> i & 1) p[pos >> 3] |= m; else p[pos >> 3] &= (uint8_t)~m;
    }
}
int ins_get(const uint8_t *s, const ins_prm_t *p)
{
    int raw = (int)rec_bits(s, p->bit, p->bits);
    switch (p->kind) {
    case PK_SGN: return raw + p->min;
    case PK_LEVEL: return (int8_t)raw;
    case PK_ALG: return raw + 1;
    case PK_TUNE: return raw - 128;
    default: return raw;
    }
}
void ins_set(uint8_t *s, const ins_prm_t *p, int v)
{
    int raw = v;
    switch (p->kind) {
    case PK_SGN: raw = v - p->min; break;
    case PK_LEVEL: raw = (uint8_t)(int8_t)ui_clamp(v, -128, 127); break;
    case PK_ALG: raw = v - 1; break;
    case PK_TUNE: raw = v + 128; break;
    default: break;
    }
    if (raw < 0) raw = 0;
    if (raw > (int)((1u << p->bits) - 1)) raw = (int)((1u << p->bits) - 1);
    put_bits(s, p->bit, p->bits, (uint32_t)raw);
}
int ins_name_pos(int kind) { return kind == REC_DX7 ? 784 : kind == REC_JUNO ? 123 : kind == REC_TB303 ? 42 : kind == REC_SAMPLER ? 174 : kind == REC_CHIP ? 121 : 8; }
int ins_level_pos(int kind) { return kind == REC_DX7 ? 844 : kind == REC_JUNO ? 183 : kind == REC_TB303 ? 102 : kind == REC_SAMPLER ? 103 : kind == REC_CHIP ? 181 : 68; }
void ins_name_set(uint8_t *p, int bit, int chars, const char *name)
{
    static const char SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-?|/\\*#$@%";
    int end = 0;
    for (int i = 0; i < chars; i++) {
        if (!end && !name[i]) end = 1;                  /* (nothing read past the name's end) */
        char ch = end ? ' ' : name[i];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        const char *at = strchr(SET, ch);
        put_bits(p, (uint32_t)(bit + 6 * i), 6, at && ch ? (uint32_t)(at - SET) : 0);
    }
}

/* RELEASE: R4 as a time, ms from full to -60 dB at each R4, measured on the envelope; 0: it never fades
 * (R4 under 7). Neighbouring rates share a time */
const uint32_t RELEASE_MS[100] = {
    0, 0, 0, 0, 0, 0, 0, 118480, 94784, 94784, 78987, 67704, 67704, 59240, 59240, 47392, 39494, 39494,
    33852, 29620, 29620, 23696, 19747, 19747, 16926, 14810, 14810, 11848, 11848, 9874, 8464, 8464, 7406, 5924, 5924, 4937,
    4232, 4232, 3704, 3704, 2962, 2469, 2469, 2116, 1852, 1852, 1482, 1235, 1235, 1058, 926, 926, 742, 742, 618, 530, 530,
    463, 372, 372, 309, 266, 266, 232, 186, 186, 155, 155, 134, 116, 116, 93, 78, 78, 67, 58, 58, 46, 46, 39, 33, 33, 29,
    23, 23, 20, 17, 17, 15, 12, 12, 10, 10, 9, 7, 7, 6, 6, 6, 4};
static void ms_text(uint32_t ms, char *o, int max)
{
    if (!ms) ui_fmt(o, max, "%s", T("ENDLESS"));
    else if (ms < 1000) ui_fmt(o, max, "%u MS", ms);
    else if (ms < 10000) { uint32_t t = (ms + 50) / 100; ui_fmt(o, max, "%u.%u S", t / 10, t % 10); }
    else ui_fmt(o, max, "%u S", (ms + 500) / 1000);
}
void ins_text(const uint8_t *s, const ins_prm_t *p, uint8_t cart, char *o, int max)
{
    int v = ins_get(s, p);
    switch (p->kind) {
    case PK_LIST: ui_fmt(o, max, "%s", T(p->labels[ui_clamp(v, 0, p->max)])); return;
    case PK_LEVEL: ui_fmt(o, max, "%s%d%s DB", v > 0 ? "+" : v < 0 ? "-" : "", (v < 0 ? -v : v) / 2, (v & 1) ? ".5" : ""); return;
    case PK_ALG: ui_fmt(o, max, "%02X", v - 1); return;
    case PK_RELEASE: ms_text(RELEASE_MS[ui_clamp(v, 0, 99)], o, max); return;
    case PK_NOTE: ui_note_name(o, v); return;
    case PK_POS: ui_fmt(o, max, "%X", v); return;
    case PK_SAMPLE: {
        rec_cart_t c;
        rec_sample_t sm;
        if (rec_cart(cart, &c) || rec_sample(&c, (uint8_t)v, &sm)) ui_fmt(o, max, "--");
        else ui_fmt(o, max, "%02X", v);
        return;
    }
    case PK_PATTERN:
        if (v == 0xFF) ui_fmt(o, max, "--");
        else ui_fmt(o, max, ui_pattern_rows((uint8_t)v) ? "%02X" : T("%02X MISSING"), v);
        return;
    case PK_XN: ui_fmt(o, max, "X%d", v + 1); return;
    case PK_HALF: ui_fmt(o, max, "%d", v * 2); return;
    case PK_CHTAB:
        if (!v) ui_fmt(o, max, "--");
        else if (v <= CHIP_TAB_N[p->min]) ui_fmt(o, max, "%02X %s", v, T(CHIP_TAB_NAMES[p->min][v - 1]));
        else ui_fmt(o, max, "%02X", v);                 /* (a later firmware's table: none here) */
        return;
    case PK_WAVE: ui_fmt(o, max, "%02X %s", v, T(CHIP_WAVE_NAMES[v & 15])); return;
    case PK_PSGSHAPE: ui_fmt(o, max, "%X %s", v, T(p->labels[v & 15])); return;
    case PK_PERIOD: {                                   /* (its first byte turned: the whole period shown, 0 read as 1) */
        uint32_t w = rec_bits(s, p->bit - 8u, 16);
        ui_fmt(o, max, "%04X", w ? w : 1u);
        return;
    }
    case PK_SGN: case PK_TUNE:                          /* (hex, two digits at least, signed ones with their sign) */
        ui_fmt(o, max, "%s%02X", v > 0 ? "+" : v < 0 ? "-" : "", v < 0 ? -v : v);
        return;
    default: ui_fmt(o, max, "%02X", v); return;
    }
}

/* ------------------------------------------------------------------------------- INIT --- */
static void setb(uint8_t *s, int bit, int bits, int v) { put_bits(s, (uint32_t)bit, bits, (uint32_t)v); }
void ins_init(int kind, uint8_t *s)
{
    memset(s, 0, (size_t)rec_slot_size((uint8_t)kind));
    if (kind == REC_DX7) {
        for (int i = 0; i < 6; i++) {                   /* OP1 at the record's last place */
            int b = OPB(i);
            for (int k = 0; k < 4; k++) setb(s, b + 7 * k, 7, 99);
            setb(s, b + 28, 7, 99); setb(s, b + 35, 7, 99); setb(s, b + 42, 7, 99); setb(s, b + 49, 7, 0);
            setb(s, b + 56, 7, 39);                     /* BREAK POINT */
            setb(s, b + 89, 7, i ? 0 : 99);
            setb(s, b + 97, 5, 1);                      /* COARSE 1 */
            setb(s, b + 109, 4, 7);                     /* DETUNE 0 */
        }
        for (int k = 0; k < 4; k++) { setb(s, VB + 7 * k, 7, 99); setb(s, VB + 28 + 7 * k, 7, 50); }
        setb(s, VB + 64, 1, 1);                         /* OSC KEY SYNC */
        setb(s, VB + 65, 7, 35);                        /* LFO SPEED */
        setb(s, VB + 93, 1, 1);                         /* LFO KEY SYNC */
        setb(s, VB + 97, 3, 3);                         /* PITCH MOD SENS */
        setb(s, VB + 100, 6, 24);                       /* TRANSPOSE 0 */
    } else if (kind == REC_JUNO) {
        setb(s, 0, 7, 40);                              /* LFO RATE */
        setb(s, 35, 7, 127);                            /* VCF FREQ */
        setb(s, 70, 7, 64);                             /* VCA LEVEL */
        setb(s, 91, 7, 127); setb(s, 98, 7, 10);        /* ENV S, R */
        setb(s, 112, 2, 1);                             /* 8' */
        setb(s, 115, 1, 1);                             /* SAW */
        setb(s, 118, 1, 1);                             /* PWM: MAN */
        setb(s, 121, 2, 1);                             /* HPF 1 */
    } else if (kind == REC_TB303) {
        for (int k = 1; k < 6; k++) setb(s, 7 * k, 7, 64);
    } else if (kind == REC_SAMPLER) {
        setb(s, 88, 7, 60);                             /* BASE NOTE C-4 */
        setb(s, 95, 8, 128);                            /* TUNE 0 */
        setb(s, 111, 7, 64);                            /* VOLUME */
        setb(s, 118, 7, 99); setb(s, 125, 7, 99); setb(s, 132, 7, 99); setb(s, 139, 7, 80);
        setb(s, 146, 7, 99); setb(s, 153, 7, 99); setb(s, 160, 7, 99);
    } else if (kind == REC_FUSION) setb(s, 0, 8, 0xFF);                         /* no pattern yet */
    else if (kind == REC_CHIP) {                         /* the engine's reference: PULSE 50 %, CLOCK EXACT, FILTER NONE */
        setb(s, 3, 2, 2);                               /* DUTY 50 % */
        setb(s, 11, 4, 4);                              /* NOISE RATE 4 */
        setb(s, 16, 3, 6);                              /* CLOCK EXACT */
        setb(s, 21, 2, 3);                              /* FILTER NONE */
        setb(s, 26, 3, 2); setb(s, 29, 3, 2);           /* SWEEP SHIFT 2, PACE 2 */
        setb(s, 32, 4, 15);                             /* VOL F */
        setb(s, 45, 4, 9);                              /* PSG SHAPE 9 */
        setb(s, 49, 16, 800);                           /* PSG PERIOD 0320 */
    }
    ins_name_set(s, ins_name_pos(kind), 10, T("INIT"));
}

/* ------------------------------------------------------------------------- the records --- */
#define rb ui_scratch                   /* a cartridge's record copied to change it (at most 3,595 B) */
int ins_kind(uint8_t cart) { rec_cart_t c; return rec_cart(cart, &c) ? 0 : c.kind; }
int ins_builtin(uint8_t cart)           /* 1 built in (F0-FF) as the firmware has it, 0 the user area's or edited */
{
    store_rec_t r;
    int k = ins_kind(cart);
    if (!k || !store_info((uint8_t)k, cart, 0, &r)) return cart >= 0xF0;
    return r.area == STORE_AREA_BUILTIN;
}
int ins_slot_bytes(uint8_t cart, int slot, uint8_t *out)
{
    rec_cart_t c;
    if (rec_cart(cart, &c)) return 0;
    const uint8_t *s = rec_slot(&c, (uint8_t)slot);
    if (!s) return 0;
    memcpy(out, s, (size_t)rec_slot_size(c.kind));
    return 1;
}
static int empty_slot(int kind, const uint8_t *s) { return rec_bits(s, (uint32_t)ins_name_pos(kind), 6) == REC_NAME_EMPTY; }
int ins_cart_copy(uint8_t cart, rec_cart_t *c, uint8_t *dst, uint32_t *len, uint8_t *ver)
{
    uint8_t v;
    if (rec_cart(cart, c)) return -1;
    return store_edit_build(c->kind, cart, 0, dst, UI_SCRATCH, len, ver ? ver : &v) ? -1 : 0;
}
int ins_put_slot(uint8_t cart, int slot, const uint8_t *bytes)
{
    rec_cart_t c;
    uint32_t clen;
    uint8_t tab[64 * 16];                               /* a sampler's samples' table, after its instruments */
    if (ins_cart_copy(cart, &c, rb, &clen, 0)) return -1;
    int kind = c.kind, size = rec_slot_size(c.kind), samp = kind == REC_SAMPLER, head = samp ? 11 : 10;
    int n = c.nslots, ns = samp ? c.nsamples : 0, nn = bytes && slot + 1 > n ? slot + 1 : n;
    if (ns > 64 || head + nn * size + ns * 16 > UI_SCRATCH) return -1;
    memcpy(tab, rb + head + n * size, (size_t)ns * 16);
    for (int k = n; k < nn; k++) {                      /* new empty slots up to it */
        memset(rb + head + k * size, 0, (size_t)size);
        put_bits(rb + head + k * size, (uint32_t)ins_name_pos(kind), 6, REC_NAME_EMPTY);
    }
    if (slot < nn) {
        uint8_t *d = rb + head + slot * size;
        if (bytes) memcpy(d, bytes, (size_t)size);
        else { memset(d, 0, (size_t)size); put_bits(d, (uint32_t)ins_name_pos(kind), 6, REC_NAME_EMPTY); }
    }
    while (nn > 0 && empty_slot(kind, rb + head + (nn - 1) * size)) nn--;     /* stored up to the last one used */
    rb[9] = (uint8_t)nn;
    uint32_t len = (uint32_t)(head + nn * size);
    if (samp) {
        memcpy(rb + len, tab, (size_t)ns * 16);
        len += (uint32_t)ns * 16;
    }
    return ui_edit_put((uint8_t)kind, cart, 0, 1, rb, len);
}
int ins_new_cart(uint8_t num, int kind, const char *name)
{
    memset(rb, 0, 11);
    ins_name_set(rb, 0, 12, name);
    return ui_edit_put((uint8_t)kind, num, 0, 1, rb, kind == REC_SAMPLER ? 11 : 10);
}
int ins_rename_cart(uint8_t num, const char *name)
{
    rec_cart_t c;
    uint32_t len;
    if (ins_cart_copy(num, &c, rb, &len, 0)) return -1;
    ins_name_set(rb, 0, 12, name);
    return ui_edit_put(c.kind, num, 0, 1, rb, len);
}
int ins_copy_cart(uint8_t from, uint8_t to, const char *name)
{
    rec_cart_t c;
    uint32_t len;
    if (ins_cart_copy(from, &c, rb, &len, 0)) return -1;
    ins_name_set(rb, 0, 12, name);
    return ui_edit_put(c.kind, to, 0, 1, rb, len);
}
int ins_delete_cart(uint8_t num)
{
    rec_cart_t c;
    if (rec_cart(num, &c)) return -1;
    ui_edit_del(c.kind, num, 0);
    if (c.kind == REC_SAMPLER) for (int k = 0; k < c.nsamples; k++) ui_edit_del(REC_AUDIO, num, (uint8_t)k);
    return 0;
}
int ins_free_cart(void)
{
    uint8_t nums[256];
    int n = ui_carts(nums, 256), last = -1;
    for (int i = 0; i < n; i++) if (nums[i] <= 0xEF && nums[i] > last) last = nums[i];
    if (last < 0xEF) return last + 1;
    for (int k = 0; k <= 0xEF; k++) {
        int used = 0;
        for (int i = 0; i < n; i++) if (nums[i] == k) used = 1;
        if (!used) return k;
    }
    return -1;
}

/* the DX7's algorithms as MSFA keeps them: per operator, OP6 first, the bus it writes (bits 0-1, 0 the
 * output), the one it reads (bits 4-5), feedback 0xC0 */
const uint8_t DX7_ALG[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04},
};
