/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The settings (specs/ui/screens/settings.md): their values, the settings record (specs/files/settings.md)
 * read at power-on and written by SAVE, and where each acts. */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "../play/player.h"
#include "../play/dj.h"
#include "../store/store.h"
#include "../fx/fx.h"

/* OUTPUT: SYNC PULSE L / R put a click on that side at every half beat, the music in mono on the other
 * (specs/connections.md#clock) */
static const txt_t L_OUTPUT[4] = {TX("STEREO"), TX("MONO"), TX("SYNC PULSE L"), TX("SYNC PULSE R")};
static const txt_t L_VOICES[3] = {TX("8"), TX("12"), TX("16")};   /* (16: 10 on core 1, 6 on core 2: PL_VOICES_MOST) */
static const uint8_t VOICES_N[3] = {8, 12, 16};
static const txt_t L_ON_OFF[2] = {TX("ON"), TX("OFF")};
static const txt_t L_THEME[G_THEMES] = {TX("DEFAULT"), TX("EGA"), TX("PASTEL"), TX("NEON"), TX("SUNSET"), TX("ICE"),
                                        TX("CLEAR"), TX("GREEN"), TX("AMBER"), TX("WHITE"), TX("DAYLIGHT"), TX("PAPER"),
                                        TX("MANUAL"), TX("SILVER"), TX("LCD"), TX("COMMANDER")};
static const txt_t L_SPEED[3] = {TX("OFF"), TX("LOW"), TX("HIGH")};
static const txt_t L_SCROLL[2] = {TX("ROWS"), TX("PAGES")};
static const txt_t L_COUNT_IN[4] = {TX("OFF"), TX("1 BAR"), TX("2 BARS"), TX("KEY")};
/* QUANTIZE: where the EDITOR's takes put a note; the record keeps 0 ROW, 1-3 2-4 ROWS, 4 REAL TIME (5-7 read as ROW) */
static const txt_t L_QUANTIZE[5] = {TX("2 ROWS"), TX("3 ROWS"), TX("4 ROWS"), TX("ROW"), TX("REAL TIME")};
static const uint8_t QUANT_REC[5] = {1, 2, 3, 0, 4}, QUANT_OF_REC[8] = {3, 0, 1, 2, 4, 3, 3, 3};
static const txt_t L_RHYTHM[10] = {TX("#"), TX("#_"), TX("#__"), TX("#___"), TX("##_"), TX("###_"), TX("##__"), TX("#_##_"),
                                   TX("#_______"), TX("#_______________")};
static const txt_t L_PATTERN[5] = {TX("UP"), TX("DOWN"), TX("UP-DOWN"), TX("RANDOM"), TX("AS PLAYED")};
static const txt_t L_LATCH[9] = {TX("OFF"), TX("1 NOTE"), TX("2 NOTES"), TX("3 NOTES"), TX("4 NOTES"), TX("5 NOTES"),
                                 TX("6 NOTES"), TX("7 NOTES"), TX("8 NOTES")};
const txt_t UI_POWER_ON[11] = {TX("STATUS"), TX("LAST"), TX("SONGS"), TX("EDITOR"), TX("INSTRUMENTS"), TX("PLAYBACK"),
                               TX("FX"), TX("VISUAL"), TX("SETTINGS"), TX("LOOPS"), TX("DJ")};
static const txt_t L_TOOLS[6] = {TX("CHECK REFERENCES"), TX("CHECK STORAGE"), TX("CLEAN UP"), TX("AUTO LEVEL ALL"),
                                 TX("REVERT ALL"), TX("FACTORY RESET")};
static const txt_t L_ABOUT[1] = {TX("VERSION, LICENCES")};
static const txt_t L_MIDI_IN[17] = {TX("ALL"), TX("1"), TX("2"), TX("3"), TX("4"), TX("5"), TX("6"), TX("7"), TX("8"),
                                    TX("9"), TX("10"), TX("11"), TX("12"), TX("13"), TX("14"), TX("15"), TX("16")};
static const txt_t L_MIDI_OUT[3] = {TX("OFF"), TX("KEYS"), TX("ALL")};
static const txt_t L_SYNC[2] = {TX("INTERNAL"), TX("EXTERNAL")};
static const txt_t L_OFF_ON[2] = {TX("OFF"), TX("ON")};
const txt_t UI_KEYS_FX[2] = {TX("THROUGH FX"), TX("DRY")};   /* KEYS: the keys heard through the effects, or past them */

const ui_setting_t UI_SETTINGS[S_COUNT] = {
    {TX("OUTPUT"), L_OUTPUT, 4, 0, 0, 1, 0, SF_LIST},
    {TX("LINE GAIN"), 0, 0, -24, 12, 1, 0, SF_DB},           /* the analog out's level, with MASTER */
    {TX("USB GAIN"), 0, 0, -24, 12, 1, 0, SF_DB},            /* USB audio's, without it */
    {TX("VOICES"), L_VOICES, 3, 0, 0, 1, 2, SF_LIST},
    {TX("SECOND CORE"), L_ON_OFF, 2, 0, 0, 1, 0, SF_LIST},  /* the effects and the output stage on core 2, or not */
    {TX("THEME"), L_THEME, G_THEMES, 0, 0, 1, 0, SF_LIST},
    {TX("KEYS VOLUME"), 0, 0, 1, 127, 1, 0x60, SF_HEX},
    {TX("KEYS GAIN"), 0, 0, -24, 12, 1, 0, SF_DB},          /* a gain on every note the keys start */
    {TX("KEYS"), UI_KEYS_FX, 2, 0, 0, 1, 0, SF_LIST},       /* THROUGH FX, or DRY: past every effect */
    {TX("KEYS OCTAVE"), 0, 0, -4, 4, 1, 0, SF_SIGNED},
    {TX("KNOB SPEED"), L_SPEED, 3, 0, 0, 1, 1, SF_LIST},
    {TX("HOLD DELAY"), 0, 0, 300, 1500, 50, 500, SF_MS},
    {TX("COMBO GRACE"), 0, 0, 0, 250, 10, 100, SF_MS},
    {TX("PLAYBACK SCROLL"), L_SCROLL, 2, 0, 0, 1, 0, SF_LIST},
    {TX("COUNT-IN"), L_COUNT_IN, 4, 0, 0, 1, 1, SF_LIST},
    {TX("QUANTIZE"), L_QUANTIZE, 5, 0, 0, 1, 3, SF_LIST},  /* (LOOPS has its own) */
    {TX("ARP RHYTHM"), L_RHYTHM, 10, 0, 0, 1, 3, SF_LIST},
    {TX("ARP PATTERN"), L_PATTERN, 5, 0, 0, 1, 0, SF_LIST},
    {TX("ARP LATCH"), L_LATCH, 9, 0, 0, 1, 4, SF_LIST},
    {TX("POWER-ON SCREEN"), UI_POWER_ON, 11, 0, 0, 1, 0, SF_LIST},
    {TX("TUNING"), 0, 0, 430, 450, 1, 440, SF_HZ},
    {TX("TOOLS"), L_TOOLS, 6, 0, 0, 1, 0, SF_LIST},
    {TX("ABOUT"), L_ABOUT, 1, 0, 0, 1, 0, SF_LIST},
};
const ui_setting_t UI_MIDI_SETTINGS[S_MIDI_COUNT] = {
    {TX("MIDI IN CHANNEL"), L_MIDI_IN, 17, 0, 0, 1, 0, SF_LIST},
    {TX("MIDI OUT"), L_MIDI_OUT, 3, 0, 0, 1, 1, SF_LIST},
    {TX("MIDI OUT CHANNEL"), 0, 0, 1, 16, 1, 1, SF_NUM},
    {TX("SYNC"), L_SYNC, 2, 0, 0, 1, 0, SF_LIST},
    {TX("CLOCK OUT"), L_OFF_ON, 2, 0, 0, 1, 0, SF_LIST},
};

int16_t ui_set[S_COUNT];
int16_t ui_midi_set[S_MIDI_COUNT];
static uint8_t last_screen;             /* the screen at the last SAVE (POWER-ON SCREEN = LAST) */
static uint16_t saved_inst = UI_FIRST_INST;     /* the current instrument at the last SAVE */
static uint8_t saved_pat = UI_EDITOR_PATTERN;   /* the EDITOR's pattern at the last SAVE (FE: its own) */
static uint8_t editor_pattern(void)             /* the EDITOR's pattern now: its own when the one picked is gone */
{
    return ui_pattern_rows(ui_cur.pattern) ? ui_cur.pattern : UI_EDITOR_PATTERN;
}
static uint8_t gates[96];               /* the user's 16 GATE patterns, kept as read */

void ui_setting_text(const ui_setting_t *s, int v, char *dst)
{
    switch (s->fmt) {
    case SF_LIST: ui_fmt(dst, 24, "%s", v >= 0 && v < s->n ? T(s->list[v]) : "?"); break;
    case SF_DB: ui_fmt(dst, 24, "%s%d DB", v > 0 ? "+" : "", v); break;
    case SF_HEX: ui_fmt(dst, 24, "%02X", v); break;
    case SF_SIGNED: ui_fmt(dst, 24, "%s%X", v > 0 ? "+" : v < 0 ? "-" : "", v < 0 ? -v : v); break;
    case SF_MS: ui_fmt(dst, 24, "%d MS", v); break;
    case SF_HZ: ui_fmt(dst, 24, "%d HZ", v); break;
    default: ui_fmt(dst, 24, "%d", v); break;
    }
}

/* the record's bytes (specs/files/settings.md) */
static void from_record(const uint8_t *p, uint32_t len)
{
    if (len < 13) return;
    ui_set[S_OUTPUT] = p[0] <= 3 ? p[0] : 1;           /* (SYNC PULSE L / R as 2 / 3; an older record's below) */
    ui_set[S_VOICES] = p[1] == 8 ? 0 : p[1] == 12 ? 1 : 2;   /* (anything else: 16, the default) */
    ui_set[S_THEME] = p[2] < G_THEMES ? p[2] : 0;
    ui_set[S_KEYS_VOLUME] = (int16_t)ui_clamp(p[3], 1, 127);
    ui_set[S_KEYS_OCTAVE] = (int16_t)ui_clamp((int8_t)p[4], -4, 4);
    ui_set[S_KNOB_SPEED] = p[5] < 3 ? p[5] : 1;
    ui_set[S_HOLD_DELAY] = (int16_t)ui_clamp(p[6] | p[7] << 8, 300, 1500);
    ui_set[S_COMBO_GRACE] = (int16_t)ui_clamp(p[8], 0, 250);
    ui_set[S_PLAYBACK_SCROLL] = p[9] ? 1 : 0;
    ui_set[S_POWER_ON] = p[10] < 11 ? p[10] : 0;
    last_screen = p[11];
    ui_set[S_TUNING] = (int16_t)(430 + ui_clamp(p[12], 0, 200) / 10);
    if (len >= 109) memcpy(gates, p + 13, sizeof gates);
    if (len >= 115) {
        ui_set[S_COUNT_IN] = p[109] & 3;
        ui_set[S_QUANTIZE] = QUANT_OF_REC[p[109] >> 2 & 7];   /* (bits 4-2: ROW in a record from before it) */
        ui_set[S_ARP_RHYTHM] = p[110] < 10 ? p[110] : 3;
        ui_set[S_LINE_GAIN] = (int16_t)(ui_clamp(p[111], 0, 36) - 24);
        ui_set[S_USB_GAIN] = (int16_t)(ui_clamp(p[112], 0, 36) - 24);
        ui_set[S_ARP_PATTERN] = p[113] < 5 ? p[113] : 0;
        ui_set[S_ARP_LATCH] = p[114] < 9 ? p[114] : 4;
    }
    if (len >= 120) {
        ui_midi_set[S_MIDI_IN_CH] = p[115] <= 16 ? p[115] : 0;
        ui_midi_set[S_MIDI_OUT] = p[116] == 1 ? 0 : p[116] == 2 ? 2 : 1;      /* the record: 0 KEYS, 1 OFF, 2 ALL */
        ui_midi_set[S_MIDI_OUT_CH] = (int16_t)(1 + (p[117] & 15));
        ui_midi_set[S_SYNC] = p[118] ? 1 : 0;
        ui_midi_set[S_CLOCK_OUT] = p[119] ? 1 : 0;
    }
    if (len >= 121 && ui_set[S_OUTPUT] < 2 && (p[120] == 1 || p[120] == 2))
        ui_set[S_OUTPUT] = (int16_t)(1 + p[120]);       /* (a record from when SYNC PULSE was the MIDI page's) */
    if (len >= 124 && (p[123] & 1))
        saved_inst = (uint16_t)(p[121] << 8 | p[122]);
    if (len >= 124)
        ui_set[S_SECOND_CORE] = p[123] & 2 ? 1 : 0;    /* (bit 1 OFF: ON in a record from before it) */
    if (len >= 125)
        ui_set[S_KEYS_GAIN] = (int16_t)ui_clamp((int8_t)p[124], -24, 12);   /* (0 dB in a record from before it) */
    if (len >= 126)
        ui_set[S_KEYS_FX] = p[125] == 1 ? 1 : 0;        /* (THROUGH FX in a record from before it) */
    if (len >= 128 && (p[126] & 1))
        saved_pat = p[127];                             /* (the EDITOR's own in a record from before it) */
    if (len >= 133) {                                   /* the keys' strip, 0 its default (in a record from before it) */
        ui_keys_mix[RM_PAN] = (uint8_t)(p[128] ^ 0x80);
        ui_keys_mix[RM_DUCK] = p[129] & 0x7F;
        for (int s = 0; s < 3; s++) ui_keys_mix[RM_CHORUS + s] = (uint8_t)(0x7F - (p[130 + s] & 0x7F));
    }
}
#define SET_LEN 133                     /* the record's length (specs/files/settings.md) */
static void to_record(uint8_t *p)
{
    memset(p, 0, SET_LEN);
    p[0] = (uint8_t)ui_set[S_OUTPUT];
    p[1] = VOICES_N[ui_set[S_VOICES]];
    p[2] = (uint8_t)ui_set[S_THEME];
    p[3] = (uint8_t)ui_set[S_KEYS_VOLUME];
    p[4] = (uint8_t)(int8_t)ui_set[S_KEYS_OCTAVE];
    p[5] = (uint8_t)ui_set[S_KNOB_SPEED];
    p[6] = (uint8_t)(ui_set[S_HOLD_DELAY] & 255);
    p[7] = (uint8_t)(ui_set[S_HOLD_DELAY] >> 8);
    p[8] = (uint8_t)ui_set[S_COMBO_GRACE];
    p[9] = (uint8_t)ui_set[S_PLAYBACK_SCROLL];
    p[10] = (uint8_t)ui_set[S_POWER_ON];
    const ui_screen_t *s = ui_screen_now();
    for (int i = 0; i < 11; i++) if (s && !strcmp(T(UI_POWER_ON[i]), T(s->name))) last_screen = (uint8_t)i;
    p[11] = last_screen;
    p[12] = (uint8_t)((ui_set[S_TUNING] - 430) * 10);
    memcpy(p + 13, gates, sizeof gates);
    p[109] = (uint8_t)(ui_set[S_COUNT_IN] | QUANT_REC[ui_set[S_QUANTIZE]] << 2);   /* COUNT-IN, QUANTIZE */
    p[110] = (uint8_t)ui_set[S_ARP_RHYTHM];
    p[111] = (uint8_t)(ui_set[S_LINE_GAIN] + 24);
    p[112] = (uint8_t)(ui_set[S_USB_GAIN] + 24);
    p[113] = (uint8_t)ui_set[S_ARP_PATTERN];
    p[114] = (uint8_t)ui_set[S_ARP_LATCH];
    p[115] = (uint8_t)ui_midi_set[S_MIDI_IN_CH];
    p[116] = ui_midi_set[S_MIDI_OUT] == 0 ? 1 : ui_midi_set[S_MIDI_OUT] == 2 ? 2 : 0;
    p[117] = (uint8_t)(ui_midi_set[S_MIDI_OUT_CH] - 1);
    p[118] = (uint8_t)ui_midi_set[S_SYNC];
    p[119] = (uint8_t)ui_midi_set[S_CLOCK_OUT];
    p[120] = (uint8_t)(ui_set[S_OUTPUT] >= 2 ? ui_set[S_OUTPUT] - 1 : 0);    /* (SYNC PULSE's side, as it was kept) */
    p[121] = (uint8_t)(ui_cur.inst >> 8);               /* the current instrument, kept */
    p[122] = (uint8_t)ui_cur.inst;
    p[123] = (uint8_t)(1 | (ui_set[S_SECOND_CORE] ? 2 : 0));   /* bit 0 the instrument kept, bit 1 SECOND CORE OFF */
    p[124] = (uint8_t)(int8_t)ui_set[S_KEYS_GAIN];      /* KEYS GAIN, dB */
    p[125] = (uint8_t)ui_set[S_KEYS_FX];                /* KEYS: 0 THROUGH FX, 1 DRY */
    saved_pat = editor_pattern();                       /* the EDITOR's pattern: a numbered one (bit 0), or its own */
    p[126] = saved_pat != UI_EDITOR_PATTERN;
    p[127] = saved_pat != UI_EDITOR_PATTERN ? saved_pat : 0;
    p[128] = (uint8_t)(ui_keys_mix[RM_PAN] ^ 0x80);     /* the keys' strip, 0 its default: PAN (80), DUCK, the sends (7F) */
    p[129] = ui_keys_mix[RM_DUCK];
    for (int s = 0; s < 3; s++) p[130 + s] = (uint8_t)(0x7F - ui_keys_mix[RM_CHORUS + s]);
    saved_inst = ui_cur.inst;
}

void ui_settings_load(void)
{
    for (int i = 0; i < S_COUNT; i++) ui_set[i] = UI_SETTINGS[i].def;
    for (int i = 0; i < S_MIDI_COUNT; i++) ui_midi_set[i] = UI_MIDI_SETTINGS[i].def;
    memset(gates, 0, sizeof gates);
    gates[0] = 0x49; gates[1] = 0x92; gates[2] = 0x24; gates[3] = 0x49; gates[4] = 0x92; gates[5] = 0x24;   /* MY GATE 10: all on */
    memcpy(ui_keys_mix, UI_KEYS_MIX_DEF, sizeof ui_keys_mix);
    last_screen = 0;
    saved_inst = UI_FIRST_INST;
    saved_pat = UI_EDITOR_PATTERN;
    uint32_t len;
    uint8_t ver;
    const uint8_t *p = store_get(REC_SETTINGS, 0, 0, &len, &ver);
    if (p) from_record(p, len);
    ui_gates_to_sound();                /* the user's GATE patterns to the effects */
}
/* the voices notes take: SETTINGS' VOICES, 8, 12 or 16, everywhere (DJ too: ui/screens/dj.md#voices; 16 counts as 12
 * while core 2 does not run: the player's); looked at every main loop pass, the player told of a change only */
static uint8_t voices_now;
void ui_voices_poll(void)
{
    uint8_t n = VOICES_N[ui_set[S_VOICES]];
    if (n == voices_now) return;
    voices_now = n;
    player_voice_count(n);
}
/* SYNC PULSE's side as it acts (OUTPUT's SYNC PULSE L / R): 1 left, 2 right; 0 off, or SYNC EXTERNAL (as CLOCK OUT,
 * only on PT/FM's own tempo) */
int ui_sync_pulse(void) { return ui_midi_set[S_SYNC] == 0 && ui_set[S_OUTPUT] >= 2 ? ui_set[S_OUTPUT] - 1 : 0; }
void ui_settings_apply(int k)
{
    if (k == S_OUTPUT || k == S_LINE_GAIN)      /* the analog out: mono for the speaker (SYNC PULSE's: the music mono
                                                 * on the other side, the player's), its level */
        plat_output(ui_set[S_OUTPUT] == 1, ui_set[S_LINE_GAIN]);
    if (k == S_USB_GAIN) plat_usb_gain(ui_set[S_USB_GAIN]);     /* USB audio's level, MASTER ignored */
    if (k == S_KEYS_GAIN) {                     /* the keys' notes, all of them: their level, and FX MIXER's KEYS */
        player_keys_gain(ui_set[S_KEYS_GAIN]);
        player_keys_mix(ui_keys_mix);
    }
    if (k == S_KEYS_FX) player_keys_dry(ui_set[S_KEYS_FX]);        /* the keys heard past every effect, or not */
    if (k == S_THEME) g_theme(ui_set[S_THEME]);
    if (k == S_VOICES) ui_voices_poll();
    if (k == S_SECOND_CORE) plat_c2_enable(!ui_set[S_SECOND_CORE]);  /* core 2 from the next half, or core 1 alone */
    if (k == S_KEYS_OCTAVE) ui_cur.octave = (int8_t)ui_set[S_KEYS_OCTAVE];
    if (k == S_ARP_RHYTHM || k == S_ARP_PATTERN || k == S_ARP_LATCH) ui_arp_refresh();
}

/* (DJ's decks standing otherwise than written count as one change at once, with DJ's record unless that is changed
 * already: specs/files/dj.md#the-stand) */
void ui_unsaved_refresh(void)
{
    ui_unsaved = (uint32_t)ui_edit_count() + (ui_dj_stand_moved() && !store_edit_has(REC_DJ, 0, 0) ? 1u : 0u);
}
/* the current instrument as the settings record keeps it (F0:00 with none): the current one at power-on */
uint16_t ui_settings_inst(void) { return saved_inst; }
uint8_t ui_settings_pattern(void) { return saved_pat; }
/* SAVE keeps the current state in the settings record: the current instrument, the EDITOR's pattern and, with POWER-ON
 * SCREEN = LAST, the screen. The record is put (an edit, saved with the rest) only when they differ from what it holds */
void ui_settings_keep_state(void)
{
    const ui_screen_t *s = ui_screen_now();
    int scr = -1;
    for (int i = 0; i < 11; i++) if (s && !strcmp(T(UI_POWER_ON[i]), T(s->name))) scr = i;
    if (saved_inst == ui_cur.inst && saved_pat == editor_pattern() && (ui_set[S_POWER_ON] != 1 || scr < 0 || scr == last_screen))
        return;
    static uint8_t rec[SET_LEN];                        /* (straight into the edit memory: no question when it is
                                                         * full, the save goes on) */
    to_record(rec);
    store_edit_put(REC_SETTINGS, 0, 0, 1, rec, sizeof rec);
    ui_unsaved_refresh();
}
/* a setting changed: the settings record, as an edit (SEL's REVERT takes it back, SAVE keeps it) */
void ui_settings_changed(void)
{
    static uint8_t rec[SET_LEN];
    to_record(rec);
    ui_edit_put(REC_SETTINGS, 0, 0, 1, rec, sizeof rec);
}
/* FX MIXER's KEYS column (fx.md#mixer): the keys' strip as a track's mixer bytes (RM_*), its LEVEL KEYS GAIN (dB, not
 * kept here); one for every setup, in the settings record */
uint8_t ui_keys_mix[RM_N];
const uint8_t UI_KEYS_MIX_DEF[RM_N] = {0x7F, 0x80, 0, 0x7F, 0x7F, 0x7F};
void ui_keys_mix_set(int key, int v)
{
    if (key == RM_LEVEL) ui_set[S_KEYS_GAIN] = (int16_t)ui_clamp(v, -24, 12);
    else ui_keys_mix[key] = (uint8_t)ui_clamp(v, 0, key == RM_PAN ? 255 : 127);
    ui_settings_changed();
    ui_settings_apply(S_KEYS_GAIN);
}
/* the user's 16 GATE patterns (specs/files/settings.md bytes 13-108: 16 steps of 3 bits, 6 B each, LSB
 * first; 0 OFF, 1 ON, 2-4 the ratchets): read, changed (into the record), and given to the sound */
void ui_gate_get(int i, uint8_t *steps)
{
    const uint8_t *g = gates + 6 * (i & 15);
    for (int k = 0; k < 16; k++) {
        int bit = 3 * k, v = (g[bit / 8] | (bit / 8 + 1 < 6 ? g[bit / 8 + 1] << 8 : 0)) >> (bit % 8) & 7;
        steps[k] = (uint8_t)(v > 4 ? 0 : v);
    }
}
static void gate_pack(int i, const uint8_t *steps)
{
    uint8_t *g = gates + 6 * (i & 15);
    memset(g, 0, 6);
    for (int k = 0; k < 16; k++) {
        int bit = 3 * k, v = steps[k] > 4 ? 0 : steps[k];
        g[bit / 8] |= (uint8_t)(v << (bit % 8));
        if (bit % 8 > 5) g[bit / 8 + 1] |= (uint8_t)(v >> (8 - bit % 8));
    }
}
void ui_gate_set(int i, const uint8_t *steps)
{
    gate_pack(i, steps);
    fx_gate_pattern(16 + (i & 15), steps);
    ui_settings_changed();
}
void ui_gates_to_sound(void)
{
    uint8_t st[16];
    for (int i = 0; i < 16; i++) { ui_gate_get(i, st); fx_gate_pattern(16 + i, st); }
}
/* REVERT, REVERT ALL: the settings as the record now says, applied where they act */
void ui_settings_reload(void)
{
    ui_settings_load();
    for (int k = 0; k < S_COUNT; k++) ui_settings_apply(k);
}
/* SAVE: every change as one save; store_poll writes it, ui.c shows its progress. Not fitting in the user
 * area, it writes nothing and asks: CLEAN UP (its checklist), then SAVE again (specs/ui/saving.md) */
static void clean_then_save(int a) { if (a == 1) ui_clean_up(ui_save_again); }
void ui_ask_storage_full(void)                  /* (specs/ui/saving.md#storage-full: the user area's figures) */
{
    char more[24], frees[28];
    ui_fmt(more, sizeof more, "%s", ui_txt_v(T("# KB MORE NEEDED"), ui_dec((int)((store_save_short() + 1023) / 1024))));
    ui_fmt(frees, sizeof frees, "%s", ui_txt_v(T("CLEAN UP FREES # KB"), ui_dec((int)((ui_clean_up_frees() + 1023) / 1024))));
    const char *l[3] = {T("NOT ENOUGH STORAGE TO SAVE"), more, frees};
    ui_ask(l, 3, T("CLEAN UP"), T("BACK"), clean_then_save);
}
int ui_save_all(void)
{
    int r = ui_edit_save();
    if (r == STORE_ERR_FULL) ui_ask_storage_full();
    else if (r != STORE_OK) ui_toast(r == STORE_ERR_STATE ? T("SAVE: THE STORE IS BUSY") : T("SAVE FAILED"));
    return r == STORE_OK ? 0 : -1;
}
/* REVERT ALL (specs/ui/saving.md): playback pauses first; the changes dropped, all read again, DJ's stand too
 * (specs/files/dj.md#the-stand: as at power-on, decks that are live handed to the player again, paused as written) */
void ui_revert_all(void)
{
    int dj = dj_live();
    player_pause();
    ui_edit_drop();
    player_store_changed();
    ui_settings_reload();
    if (dj) dj_on(0);
    dj_load();
    if (dj) dj_on(1);
    ui_unsaved_refresh();
}
const ui_screen_t *ui_power_on_screen(void)
{
    int k = ui_set[S_POWER_ON];
    const char *name = T(UI_POWER_ON[k == 1 ? (last_screen < 11 && last_screen != 1 ? last_screen : 0) : k]);
    const ui_screen_t *s = ui_screen_named(name);
    return s ? s : ui_screen_named("STATUS");
}
