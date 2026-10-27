/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The UI on the PC (fw/core with a fake platform): the demo disk laid into a fake flash as records, the
 * app started, a script of presses, turns and waits on a fake clock, screenshots (PPM) at its shots, and
 * the checks: a frame sent in parts (the rows that changed) equals the frame drawn and sent whole; the
 * texts come back from the packed table as written.
 *
 *   ui_test DISK.ptfmdsk OUTDIR    OUTDIR/<shot>.ppm, and a line a check */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "app.h"
#include "store/store.h"
#include "store/crc.h"
#include "ui/ui_core.h"
#include "ui/ui_data.h"
#include "ui/gfx.h"
#include "ui/txt.h"
#include "ui/ui_priv.h"
#include "play/player.h"

void ui_render(void);

/* ------------------------------------------------------------- the fake FM-1 --- */
static uint8_t flash[1 << 20];
static uint16_t screen[240 * 240];
static uint32_t now_ms, rows_sent;
static plat_input_t in_now;
static uint8_t leds[LED_COUNT];
static int fails;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_us(void) { return now_ms * 1000u; }
void plat_input(plat_input_t *in)
{
    *in = in_now;
    memset(in_now.knob, 0, sizeof in_now.knob);
}
uint32_t plat_notes(void) { return in_now.notes; }
void plat_led(int id, uint8_t level) { if (id >= 0 && id < LED_COUNT) leds[id] = level; }
int plat_screen_lost(void) { return 0; }
void plat_lcd_rows(int y, int h, const uint16_t *px)
{
    memcpy(screen + y * 240, px, (size_t)h * 240 * 2);
    rows_sent += (uint32_t)h;
}
void plat_cpu(plat_cpu_t *c, int reset) { (void)reset; memset(c, 0, sizeof *c); c->half_us = 5804; c->c2_state = PLAT_C2_OFF; }
static int user(uint32_t a, uint32_t n)
{
    return (a >= STORE_U1_START && a + n <= STORE_U1_END) || (a >= STORE_U2_START && a + n <= STORE_U2_END);
}
const uint8_t *plat_flash_ptr(uint32_t a) { return user(a, 1) ? flash + a : 0; }
int plat_flash_read(uint32_t a, void *d, uint32_t n) { if (a + n > sizeof flash) return -1; memcpy(d, flash + a, n); return 0; }
int plat_flash_erase(uint32_t a) { if (a & 4095 || a >= sizeof flash) return -1; memset(flash + a, 0xFF, 4096); return 0; }
int plat_flash_prog(uint32_t a, const void *s, uint32_t n)
{
    if (n > 256 || (a & ~255u) != ((a + n - 1) & ~255u)) return -1;
    for (uint32_t i = 0; i < n; i++) flash[a + i] &= ((const uint8_t *)s)[i];
    return 0;
}
uint32_t plat_fw_end(void) { return 0x50000; }
int plat_sysex_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 0; }
int plat_battery(void) { return 50; }
void plat_output(int mono, int line_gain_db) { (void)mono; (void)line_gain_db; }
void plat_usb_gain(int db) { (void)db; }
void plat_idle(void) {}
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t was) { (void)was; }
void plat_fault(const char *why) { fprintf(stderr, "FAULT: %s\n", why); exit(2); }

/* ------------------------------------------------ the disk laid into the flash --- */
/* records as a save writes them (specs/files/store.md#a-record), one save, packed from the user area's
 * start, none crossing a block; audio after them, from a block of its own, packed */
static uint32_t at = STORE_U1_START;
static void put(const uint8_t *h12, const uint8_t *payload, uint32_t len, int audio, int last)
{
    uint32_t size = 16 + len;
    if (!audio && (at & 4095) + size > 4096) at = (at + 4095) & ~4095u;
    uint8_t hd[16];
    memcpy(hd, h12, 4);
    hd[4] = (uint8_t)len; hd[5] = (uint8_t)(len >> 8); hd[6] = (uint8_t)(len >> 16);
    hd[7] = (uint8_t)(last ? 1 : 0);
    hd[8] = 1; hd[9] = hd[10] = hd[11] = 0;             /* save 1 */
    uint32_t c = crc32_update(0, hd, 12);
    c = crc32_update(c, payload, len);
    hd[12] = (uint8_t)c; hd[13] = (uint8_t)(c >> 8); hd[14] = (uint8_t)(c >> 16); hd[15] = (uint8_t)(c >> 24);
    memcpy(flash + at, hd, 16);
    memcpy(flash + at + 16, payload, len);
    at = (at + size + 3) & ~3u;
}
static void lay_disk(const char *path)
{
    memset(flash, 0xFF, sizeof flash);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ui_test: no %s\n", path); exit(1); }
    static uint8_t d[1 << 20];
    size_t n = fread(d, 1, sizeof d, f);
    fclose(f);
    if (n < 16 || memcmp(d, "PTFMDISK", 8)) { fprintf(stderr, "ui_test: %s is no disk\n", path); exit(1); }
    uint32_t count = d[12] | d[13] << 8 | d[14] << 16 | (uint32_t)d[15] << 24, p;
    for (int pass = 0; pass < 2; pass++) {             /* records, then audio */
        if (pass) at = (at + 4095) & ~4095u;
        p = 16;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = d[p + 4] | d[p + 5] << 8 | d[p + 6] << 16 | (uint32_t)d[p + 7] << 24;
            int kind = d[p], audio = kind == REC_AUDIO;
            if (kind < 0xE0 && audio == pass) put(d + p, d + p + 12, len, audio, 0);
            p = (p + 12 + len + 3) & ~3u;
        }
    }
}
/* the save's last record needs its flag: find the last header written and set bit 0 of its flags */
static void flag_last(void)
{
    uint32_t last = 0;
    for (uint32_t a = STORE_U1_START; a < STORE_U1_END;) {
        if (flash[a] == 0xFF) { a = (a + 4096) & ~4095u; if (a >= at) break; continue; }
        uint32_t len = flash[a + 4] | flash[a + 5] << 8 | flash[a + 6] << 16;
        last = a;
        a = (a + 16 + len + 3) & ~3u;
        if (flash[a] == 0xFF) a = (a + 4095) & ~4095u;
    }
    if (!last) return;
    flash[last + 7] |= 1;
    uint32_t len = flash[last + 4] | flash[last + 5] << 8 | flash[last + 6] << 16;
    uint32_t c = crc32_update(0, flash + last, 12);
    c = crc32_update(c, flash + last + 16, len);
    flash[last + 12] = (uint8_t)c; flash[last + 13] = (uint8_t)(c >> 8);
    flash[last + 14] = (uint8_t)(c >> 16); flash[last + 15] = (uint8_t)(c >> 24);
}

/* --------------------------------------------------------------- the script --- */
static const char *out;
static void run(uint32_t ms)                            /* the main loop and the audio, ms of fake time */
{
    static int16_t lr[PLAT_HALF * 2];
    static uint32_t audio_at;
    for (uint32_t t = 0; t < ms; t++) {
        now_ms++;
        while (audio_at * 5804u / 1000u < now_ms) { app_audio(lr, PLAT_HALF); audio_at++; }
        app_poll();
    }
}
static void press(int b) { in_now.buttons |= 1u << b; run(30); }
static void release(int b) { in_now.buttons &= ~(1u << b); run(30); }
static void tap(int b) { press(b); release(b); run(200); }        /* past the grace period */
static void key(int n, int down) { if (down) in_now.notes |= 1u << n; else in_now.notes &= ~(1u << n); run(30); }
static void turn(int k, int clicks)
{
    for (int i = 0; i < (clicks < 0 ? -clicks : clicks); i++) { in_now.knob[k] += clicks < 0 ? -1 : 1; run(120); }
}
static void combo(int held, int b) { press(held); tap(b); release(held); run(200); }
static void home_key(int n) { press(B_HOME); key(n, 1); key(n, 0); release(B_HOME); run(200); }
static void ppm(const char *name)                       /* the screen as it is */
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", out, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint32_t c = screen[i];
        fputc((int)((c >> 11) * 255u / 31u), f);
        fputc((int)(((c >> 5) & 63u) * 255u / 63u), f);
        fputc((int)((c & 31u) * 255u / 31u), f);
    }
    fclose(f);
}
static void shot(const char *name)
{
    run(40);
    ui_render();                                        /* a frame now, sent in parts (the rows that changed) */
    ppm(name);
    /* the check: the screen as sent in parts == the frame drawn again at once (nothing played between) and
     * sent whole */
    static uint16_t parts[240 * 240];
    memcpy(parts, screen, sizeof parts);
    g_invalidate();
    ui_render();
    if (memcmp(parts, screen, sizeof parts)) { printf("FAIL %s: the parts sent differ from the whole frame\n", name); fails++; }
    else printf("shot %s: partial redraws == the whole frame\n", name);
}

void ui_render(void);
const char *ui_toast_now(void);
extern void (*ui_between_halves)(void);
void ui_player_bump_row(int d) __attribute__((weak));  /* ui_player.c's; absent with the real player */
static void bump_up(void) { ui_player_bump_row(1); }
static void check(const char *what, int ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: ui_test DISK.ptfmdsk OUTDIR\n"); return 1; }
    out = argv[2];
    if (!strcmp(argv[1], "-")) {
        /* a new FM-1: nothing uploaded, only the built-in instruments */
        memset(flash, 0xFF, sizeof flash);
        app_init();
        run(100);
        shot("ui_empty_status");
        home_key(1); shot("ui_empty_songs");
        tap(B_PLAY);
        check("PLAY with no song refuses", ui_toast_now() && !strcmp(ui_toast_now(), "NO SONG TO PLAY"));
        home_key(8); shot("ui_empty_playback");
        key(0, 1); key(0, 0);                           /* a key plays the current instrument: a built-in one */
        check("the current instrument is a built-in one", (ui_cur.inst >> 8) >= 0xF0);
        printf(fails ? "ui_test: %d FAILED\n" : "ui_test: all passed\n", fails);
        return fails ? 1 : 0;
    }
    lay_disk(argv[1]);
    flag_last();
    app_init();
    run(100);

    /* the packed texts: every one back as written (the plain source's T() and the table agree); text 0 the empty
     * one (a table's none) */
    char b[1024];
    int n = 0;
    for (int i = 0; i < TXT_COUNT; i++) {
        txt_unpack(i, b, sizeof b);
        if (!b[0] != !i) { printf("FAIL text %d %s\n", i, i ? "empty" : "not empty"); fails++; }
        n++;
    }
    printf("texts: %d unpacked\n", n);

    shot("01_status");
    /* PRESETS shows in the header as every knob (frame.md); turned back, so the shots below stay as they were */
    {
        uint16_t was = ui_cur.inst;
        char want[40], id[8], nm[16];
        turn(K_PRESETS, 1);
        ui_ref_text(id, ui_cur.inst);
        ui_inst_name(ui_cur.inst, nm);
        snprintf(want, sizeof want, "PRESETS %s %s", id, nm);
        check("PRESETS: `PRESETS <ID> <NAME>` in the header", ui_knob_now() && !strcmp(ui_knob_now(), want));
        turn(K_PRESETS, -1);
        press(B_SEL); turn(K_PRESETS, 1);
        ui_cart_name((uint8_t)(ui_cur.inst >> 8), nm);
        snprintf(want, sizeof want, "CARTRIDGE %02X %s", ui_cur.inst >> 8, nm);
        check("SEL + PRESETS: `CARTRIDGE <ID> <NAME>` in the header", ui_knob_now() && !strcmp(ui_knob_now(), want));
        turn(K_PRESETS, -1); release(B_SEL);
        ui_set_inst(was);
        run(1300);
        check("the knob's note gone after 1.2 s", !ui_knob_now());
    }
    combo(B_HOME, B_LFO); shot("02_help_status"); tap(B_HOME);
    home_key(1); shot("03_songs");
    check("PLAY's red dim at the start: PLAY works here", leds[B_PLAY] == LV_DIM && leds[LED_PLAY_G] == 0);
    check("SAVE dark with nothing to save", leds[B_SAVE] == 0);
    tap(B_SEQ); run(2100); shot("03b_songs_next"); tap(B_SEQ); tap(B_SEQ); run(2100);   /* REPEAT SONG, round to ONE PLAY */
    tap(B_EDIT); turn(K_ALG, 3); shot("04_songs_order");
    /* the order edited, then dropped (HOME): nothing unsaved */
    turn(K_K1, 1); shot("ui_songs_order_k1");
    check("an order edit is unsaved", ui_unsaved == 1);
    tap(B_HOME); run(2100);
    check("HOME drops the order's edits: nothing unsaved", ui_unsaved == 0);
    /* the properties: TEMPO up 5, then SAVE writes it into the flash */
    uint32_t len;
    uint8_t ver;
    const uint8_t *p0 = store_get(REC_SONG, ui_cur.song, 0, &len, &ver);
    int tempo0 = p0 ? p0[12] | p0[13] << 8 : -1;
    tap(B_GLO); turn(K_SELECT, 1); turn(K_K1, 5); shot("ui_song_props_tempo"); tap(B_GLO);
    check("SAVE dim while a change waits", leds[B_SAVE] == LV_DIM);
    const uint8_t *p1 = store_get(REC_SONG, ui_cur.song, 0, &len, &ver);
    check("GLO's TEMPO +5 is read back from the edit memory", p1 && (p1[12] | p1[13] << 8) == tempo0 + 5);
    tap(B_SAVE); run(1500);
    check("SAVE leaves nothing unsaved", ui_unsaved == 0);
    check("SAVE says SAVED", ui_toast_now() && !strcmp(ui_toast_now(), "SAVED"));
    store_mount();
    const uint8_t *p2 = store_get(REC_SONG, ui_cur.song, 0, &len, &ver);
    check("the saved TEMPO is in the flash after a mount", p2 && (p2[12] | p2[13] << 8) == tempo0 + 5);
    /* the current instrument: F0:00 at first; another one no change of its own, but SAVE keeps it for power-on */
    {
        check("the current instrument: F0:00 at first", ui_cur.inst == UI_FIRST_INST);
        turn(K_PRESETS, 1);
        uint16_t picked = ui_cur.inst;
        check("another instrument picked: no unsaved change", picked != UI_FIRST_INST && ui_unsaved == 0);
        tap(B_SAVE); run(1500);
        check("... SAVE writes it all the same (SAVED)", ui_toast_now() && !strcmp(ui_toast_now(), "SAVED") && !ui_unsaved);
        store_mount();
        ui_settings_load();
        check("... the settings record keeps it: the current one at power-on", ui_settings_inst() == picked);
        tap(B_SAVE);
        check("SAVE again: NOTHING TO SAVE", ui_toast_now() && !strcmp(ui_toast_now(), "NOTHING TO SAVE"));
        ui_set_inst(UI_FIRST_INST);                     /* (and kept again: the shots below as they were) */
        tap(B_SAVE); run(1500);
        store_mount();
        ui_settings_load();
        check("... back to F0:00, kept", ui_settings_inst() == UI_FIRST_INST);
    }
    /* REVERT in the order's menu (specs/ui/saving.md#revert): the song as saved, after its question */
    tap(B_EDIT); turn(K_K1, 1); tap(B_SEL); shot("ui_songs_order_menu");
    turn(K_SELECT, 10); tap(B_SEL);                     /* REVERT, the menu's last line while the song is unsaved */
    check("REVERT asks first", ui_top() && ui_top()->kind == OV_QUESTION && ui_unsaved == 1);
    tap(B_OCTP);                                        /* REVERT */
    check("REVERT takes the K1 change back (REVERTED)", ui_unsaved == 0 && ui_toast_now() && !strcmp(ui_toast_now(), "REVERTED"));
    tap(B_HOME); run(2100);
    home_key(8); shot("22_playback");                   /* nothing played yet: the song's first position */
    home_key(1); turn(K_ALG, 2); shot("ui_songs_alg");
    tap(B_PLAY); run(1500); shot("ui_songs_playing");
    check("PLAY green while playing, its red off", leds[LED_PLAY_G] >= 140 && leds[B_PLAY] == 0);
    home_key(8); shot("ui_playback8");
    tap(B_SEQ); run(2100); shot("ui_playback4");
    tap(B_SEQ); run(2100); shot("ui_playback2");
    tap(B_SEQ); run(2100); shot("ui_playback1");
    tap(B_SEQ); run(2100);
    tap(B_PLAY); run(300);                              /* paused */
    /* PLAYBACK's SELECT: a row at a time (the first step plays the row it stands on again) */
    player_state_t s0, s1;
    player_state(&s0, 0);
    turn(K_SELECT, 2); run(100);
    player_state(&s1, 0);
    check("PLAYBACK: SELECT steps a row (two clicks: the next row)", !s1.playing &&
          (s1.row == s0.row + 1 || (s1.row == 0 && s1.pos == s0.pos + 1)));
    /* a frame's halves read one snapshot: the row moved between them (as the audio would) changes nothing
     * in that frame (the stand-in player only: the real one moves when it plays) */
    if (ui_player_bump_row) {
        static uint16_t a[240 * 240];
        ui_between_halves = bump_up;
        g_invalidate();
        ui_render();
        ui_between_halves = 0;
        memcpy(a, screen, sizeof a);
        ui_player_bump_row(-1);
        g_invalidate();
        ui_render();
        check("a row moved between a frame's halves: both halves show the frame's start", !memcmp(a, screen, sizeof a));
    }
    /* the arpeggiator: ARP on, lit; off */
    tap(B_ARP);
    check("ARP on: lit, ARP UP", leds[B_ARP] == 255 && ui_toast_now() && !strcmp(ui_toast_now(), "ARP UP"));
    tap(B_ARP);
    check("ARP off: dim again", leds[B_ARP] == LV_DIM);
    home_key(20); shot("26_settings");
    tap(B_SEQ); shot("26b_settings_midi"); tap(B_SEQ);
    home_key(17); turn(K_SELECT, 1); shot("29_status_1");
    turn(K_SELECT, 1); shot("29_status_2");
    turn(K_SELECT, 1); shot("29_status_3");
    turn(K_SELECT, -3);
    press(B_HOME); run(600); shot("30_home_layer"); release(B_HOME); run(200);
    tap(B_HOME); shot("31_home_combo"); tap(B_HOME);
    press(B_SEL); run(600); shot("ui_sel_layer"); release(B_SEL); run(200);
    /* FX held: the punch-ins' layer; a white key runs one, lit full while it does */
    press(B_FX); run(600); key(0, 1); shot("ui_fx_layer_punch");
    check("a punch-in's key lights full", leds[LED_NOTE(0)] == 255);
    check("the other groups' first keys dim", leds[LED_NOTE(7)] == LV_DIM);
    key(0, 0); release(B_FX); run(200);
    /* HOME + a white key mutes its track; again: on */
    press(B_HOME); key(0, 1); key(0, 0); release(B_HOME); run(200);
    check("HOME + white key 1 mutes track 00", ui_muted(0));
    press(B_HOME); key(0, 1); key(0, 0); release(B_HOME); run(200);
    check("again: track 00 on", !ui_muted(0));
    /* HOME held + SELECT: the screens of the round, one a click */
    press(B_HOME); turn(K_SELECT, 1); release(B_HOME); run(200);
    check("HOME + SELECT from STATUS goes to SONGS", !strcmp(T(ui_screen_now()->name), "SONGS"));
    home_key(17);
    home_key(1); tap(B_GLO); shot("ui_song_props"); tap(B_GLO);
    /* SETTINGS: the theme changed, then put back (LFO) */
    home_key(20); turn(K_SELECT, 5); turn(K_K1, 1); shot("ui_settings_theme"); tap(B_LFO);
    /* TOOLS: CHECK REFERENCES (the demo disk has none missing), CLEAN UP's checklist (kept) */
    turn(K_SELECT, 20); turn(K_SELECT, -1);             /* TOOLS, before ABOUT at the end */
    tap(B_EDIT); run(800);
    check("CHECK REFERENCES: none missing on the demo disk", ui_toast_now() && !strcmp(ui_toast_now(), "NO MISSING REFERENCES"));
    turn(K_K1, 1); tap(B_EDIT); run(800);               /* CHECK STORAGE */
    check("CHECK STORAGE: nothing wrong", ui_toast_now() && !strcmp(ui_toast_now(), "STORAGE OK"));
    turn(K_K1, 1); tap(B_EDIT); shot("ui_clean_up"); tap(B_OCTM);
    /* MIDI CONTROLLER (the MIDI page's line after its items): asks first (BACK: nothing changes); then, with unsaved
     * changes, whether to save them (HOME: nothing happens). Its DROP, the tracker ending, is the test's last step */
    tap(B_SEQ); turn(K_SELECT, -20); turn(K_SELECT, S_MIDI_COUNT);
    tap(B_EDIT); shot("ui_controller_question");
    check("MIDI CONTROLLER asks first", ui_top() && ui_top()->kind == OV_QUESTION && !ui_controller_wanted());
    tap(B_OCTM);
    check("its BACK: the tracker goes on", !ui_top() && !ui_controller_wanted());
    tap(B_EDIT); tap(B_OCTP);
    check("SWITCH with unsaved changes: SAVE BEFORE SWITCHING?", ui_unsaved > 0 && ui_top() &&
          ui_top()->kind == OV_QUESTION && !ui_controller_wanted());
    shot("ui_controller_save");
    tap(B_HOME);
    check("HOME there: nothing happens", !ui_top() && !ui_controller_wanted());
    tap(B_SEQ);                                         /* (back to the list, on TOOLS) */
    turn(K_K1, -2); turn(K_SELECT, -20); turn(K_SELECT, 5);
    /* REVERT's question (SAVE + OCT-), answered no */
    turn(K_K1, 1); combo(B_SAVE, B_OCTM); shot("ui_revert_question"); tap(B_OCTM);
    tap(B_LFO);
    /* FWS holds nothing of the user's (specs/ui/saving.md#fws): SONGS' list menu is NEW, CLONE, MOVE, DELETE, RENAME
     * (REVERT while unsaved), no MOVE TO FWS */
    if (argv[1][0] != '-') {
        home_key(1);
        tap(B_SEL);
        int items = 0, fws = 0;
        turn(K_SELECT, -12);
        for (int i = 0; i < 12; i++) {
            const char *w = ui_overlay_words(ui_top());
            if (w && strstr(w, "FWS")) fws = 1;
            if (w) items++;
            turn(K_SELECT, 1);
        }
        check("SONGS' list menu: no MOVE TO FWS", ui_top() && !fws && items > 0);
        shot("ui_songs_menu");
        tap(B_HOME);
    }
    /* the last step, MIDI CONTROLLER's switch: a song playing, SWITCH (and DROP, if changes are unsaved): the sound
     * fades, then the tracker ends for good (the screens, the sound, its LEDs) and the controller's start shows */
    home_key(1); tap(B_PLAY); run(500);
    home_key(20); tap(B_SEQ); turn(K_SELECT, -20); turn(K_SELECT, S_MIDI_COUNT);
    tap(B_EDIT); tap(B_OCTP);
    if (ui_top() && ui_top()->kind == OV_QUESTION) tap(B_OCTM);   /* (SAVE BEFORE SWITCHING?: DROP) */
    check("MIDI CONTROLLER, SWITCH: the tracker to end", !ui_top() && ui_controller_wanted());
    run(200);
    static int16_t lr[PLAT_HALF * 2];
    int quiet = 1;
    for (int h = 0; h < 8; h++) { app_audio(lr, PLAT_HALF); for (int i = 0; i < PLAT_HALF * 2; i++) quiet &= lr[i] == 0; }
    tap(B_SEQ);                                         /* (a screen's key: nothing of the tracker answers) */
    int lit = 0;
    for (int i = 0; i < LED_COUNT; i++) lit += leds[i] != 0;
    check("the tracker ended: silent, the controller's start with REC and HOME lit, nothing else",
          quiet && lit == 2 && leds[B_REC] == 255 && leds[B_HOME] == 255);
    ppm("ui_controller_start");
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: all passed\n", fails);
    return fails ? 1 : 0;
}
