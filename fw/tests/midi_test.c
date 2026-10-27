/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* MIDI on the PC (specs/connections.md): all of fw/core with a fake platform whose MIDI in is a queue the
 * test fills and whose MIDI out is a log; the demo disk laid into its flash as records. Notes as the keys
 * (velocity, the keys' range and past it, IN CHANNEL, the sustain pedal, program change, bank select), a
 * press and its release between two passes, MIDI OUT (OFF, KEYS, ALL; no echo of MIDI's own notes; every
 * note sent ended at STOP), MIDI learn (knobs absolute and relative, pads momentary and toggle, MASTER, HOME
 * cancelling, a control learned again, 64 at most), the MIDI map record through SAVE and a power-on, its
 * mappings deleted and SEL's REVERT, the SETTINGS MIDI page and LEARN's box (screenshots), the clock in (the tempo
 * followed, START / STOP / CONTINUE, the song position, the clock lost) and out (START, clocks at the
 * tempo, STOP).
 *
 *   midi_test DISK.ptfmdsk OUTDIR    OUTDIR/<shot>.ppm, and a line a check */
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
#include "play/player.h"
#include "play/dj.h"
#include "midi/midi.h"

void ui_render(void);
const char *ui_toast_now(void);

/* ------------------------------------------------------------- the fake FM-1 --- */
static uint8_t flash[1 << 20];
static uint16_t screen[240 * 240];
static uint32_t now_ms;
static plat_input_t in_now;
static int fails;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_us(void) { return now_ms * 1000u; }
void plat_input(plat_input_t *in)
{
    *in = in_now;
    memset(in_now.knob, 0, sizeof in_now.knob);
}
uint32_t plat_notes(void) { return in_now.notes; }
void plat_led(int id, uint8_t level) { (void)id; (void)level; }
void plat_lcd_rows(int y, int h, const uint16_t *px) { memcpy(screen + y * 240, px, (size_t)h * 240 * 2); }
int plat_screen_lost(void) { return 0; }
void plat_cpu(plat_cpu_t *c, int reset) { (void)reset; memset(c, 0, sizeof *c); c->half_us = 5804; c->c2_state = PLAT_C2_OFF; }
void plat_output(int mono, int line_gain_db) { (void)mono; (void)line_gain_db; }
void plat_usb_gain(int db) { (void)db; }
static int user(uint32_t a, uint32_t n)
{
    return (a >= STORE_U1_START && a + n <= STORE_U1_END) || (a >= STORE_U2_START && a + n <= STORE_U2_END);
}
const uint8_t *plat_flash_ptr(uint32_t a) { return user(a, 1) ? flash + a : 0; }
int plat_flash_read(uint32_t a, void *d, uint32_t n) { if (a + n > sizeof flash) return -1; memcpy(d, flash + a, n); return 0; }
int plat_flash_erase(uint32_t a) { if (a & 4095 || !user(a, 4096)) return -1; memset(flash + a, 0xFF, 4096); return 0; }
int plat_flash_prog(uint32_t a, const void *s, uint32_t n)
{
    if (!n || n > 256 || (a & ~255u) != ((a + n - 1) & ~255u) || !user(a, n)) return -1;
    for (uint32_t i = 0; i < n; i++) flash[a + i] &= ((const uint8_t *)s)[i];
    return 0;
}
uint32_t plat_fw_end(void) { return 0x50000; }
static uint8_t sx_last[640];            /* the last SysEx sent (the state's answer) */
static uint32_t sx_last_n, usb_resets;
int plat_sysex_send(const uint8_t *m, uint32_t n)
{
    if (n <= sizeof sx_last) { memcpy(sx_last, m, n); sx_last_n = n; }
    return 0;
}
uint32_t plat_usb_resets(void) { return usb_resets; }
int plat_battery(void) { return 50; }
void plat_idle(void) {}
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t was) { (void)was; }
void plat_fault(const char *why) { fprintf(stderr, "FAULT: %s\n", why); exit(2); }

/* MIDI: in, a queue the test fills; out, a log */
static plat_midi_t mq[512];
static uint32_t mq_w, mq_r;
static uint8_t tx[65536][3];
static uint32_t ntx;
static int master_set = -1;
int plat_midi_rx(plat_midi_t *e)
{
    if (mq_r == mq_w) return 0;
    *e = mq[mq_r++ % 512];
    return 1;
}
int plat_midi_tx(const uint8_t m[3])
{
    if (ntx < 65536) memcpy(tx[ntx++], m, 3);
    return 0;
}
/* the audio context's, timed: the frame each clock is heard at, counted from the first half (as the FM-1 sends it) */
static uint32_t audio_at;                               /* halves made so far */
static uint32_t clk_frame[8192], nclk;
int plat_midi_tx_at(const uint8_t m[3], int frame)
{
    if (m[0] == 0xF8 && nclk < 8192) clk_frame[nclk++] = audio_at * PLAT_HALF + (uint32_t)frame;
    return plat_midi_tx(m);
}
void plat_master_set(uint16_t v) { master_set = v; }
static void midi(uint8_t a, uint8_t b, uint8_t c)
{
    plat_midi_t *e = &mq[mq_w++ % 512];
    e->m[0] = a; e->m[1] = b; e->m[2] = c;
    e->src = PLAT_MIDI_USB;
    e->ms = now_ms;
}

/* ------------------------------------------------ the disk laid into the flash --- */
static uint32_t at = STORE_U1_START;
static void put(const uint8_t *h12, const uint8_t *payload, uint32_t len, int audio)
{
    uint32_t size = 16 + len;
    if (!audio && (at & 4095) + size > 4096) at = (at + 4095) & ~4095u;
    uint8_t hd[16];
    memcpy(hd, h12, 4);
    hd[4] = (uint8_t)len; hd[5] = (uint8_t)(len >> 8); hd[6] = (uint8_t)(len >> 16);
    hd[7] = 0;
    hd[8] = 1; hd[9] = hd[10] = hd[11] = 0;             /* save 1 */
    uint32_t c = crc32_update(0, hd, 12);
    c = crc32_update(c, payload, len);
    hd[12] = (uint8_t)c; hd[13] = (uint8_t)(c >> 8); hd[14] = (uint8_t)(c >> 16); hd[15] = (uint8_t)(c >> 24);
    memcpy(flash + at, hd, 16);
    memcpy(flash + at + 16, payload, len);
    at = (at + size + 3) & ~3u;
}
static uint32_t last_hdr;
static void lay_disk(const char *path)
{
    memset(flash, 0xFF, sizeof flash);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "midi_test: no %s\n", path); exit(1); }
    static uint8_t d[1 << 20];
    size_t n = fread(d, 1, sizeof d, f);
    fclose(f);
    if (n < 16 || memcmp(d, "PTFMDISK", 8)) { fprintf(stderr, "midi_test: %s is no disk\n", path); exit(1); }
    uint32_t count = d[12] | d[13] << 8 | d[14] << 16 | (uint32_t)d[15] << 24, p;
    for (int pass = 0; pass < 2; pass++) {             /* records, then audio */
        if (pass) at = (at + 4095) & ~4095u;
        p = 16;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = d[p + 4] | d[p + 5] << 8 | d[p + 6] << 16 | (uint32_t)d[p + 7] << 24;
            int kind = d[p], audio = kind == REC_AUDIO;
            if (kind < 0xE0 && audio == pass) { last_hdr = at; put(d + p, d + p + 12, len, audio); }
            p = (p + 12 + len + 3) & ~3u;
        }
    }
    /* (a record laid after audio may have moved `at` to a fresh block: last_hdr is the last header put) */
    flash[last_hdr + 7] |= 1;                           /* the save's last record */
    uint32_t len = flash[last_hdr + 4] | flash[last_hdr + 5] << 8 | flash[last_hdr + 6] << 16;
    uint32_t c = crc32_update(0, flash + last_hdr, 12);
    c = crc32_update(c, flash + last_hdr + 16, len);
    flash[last_hdr + 12] = (uint8_t)c; flash[last_hdr + 13] = (uint8_t)(c >> 8);
    flash[last_hdr + 14] = (uint8_t)(c >> 16); flash[last_hdr + 15] = (uint8_t)(c >> 24);
}

/* --------------------------------------------------------------- the script --- */
static const char *out;
/* SYNC PULSE's check: the left side's clicks (where each starts, counted in frames), anything else there, the right */
static int pulse_scan;
static uint32_t pulse_at[512], npulse, pulse_other;
static double right_sq;
static int16_t last_left;
static void run(uint32_t ms)                            /* the main loop and the audio, ms of fake time */
{
    static int16_t lr[PLAT_HALF * 2];
    for (uint32_t t = 0; t < ms; t++) {
        now_ms++;
        while (audio_at * 5804u / 1000u < now_ms) {
            app_audio(lr, PLAT_HALF);
            if (pulse_scan)
                for (int i = 0; i < PLAT_HALF; i++) {
                    int16_t l = lr[2 * i];
                    if (l == 32767 && last_left != 32767 && npulse < 512) pulse_at[npulse++] = audio_at * PLAT_HALF + (uint32_t)i;
                    if (l != 32767 && l != 0) pulse_other++;
                    last_left = l;
                    right_sq += (double)lr[2 * i + 1] * lr[2 * i + 1];
                }
            audio_at++;
        }
        app_poll();
    }
}
static void press(int b) { in_now.buttons |= 1u << b; run(30); }
static void release(int b) { in_now.buttons &= ~(1u << b); run(30); }
static void tap(int b) { press(b); release(b); run(200); }
static void key(int n, int down) { if (down) in_now.notes |= 1u << n; else in_now.notes &= ~(1u << n); run(30); }
static void turn(int k, int clicks)
{
    for (int i = 0; i < (clicks < 0 ? -clicks : clicks); i++) { in_now.knob[k] += clicks < 0 ? -1 : 1; run(120); }
}
static void shot(const char *name)
{
    run(40);
    g_invalidate();
    ui_render();
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
    printf("shot %s\n", name);
}
static void check(const char *what, int ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}
static int toast_is(const char *t) { const char *s = ui_toast_now(); return s && !strcmp(s, t); }
static int unpack7(const uint8_t *p, uint32_t n, uint8_t *out)  /* PT/FM's 7-in-8 packing undone */
{
    int k = 0;
    for (uint32_t i = 0; i < n; i += 8)
        for (uint32_t j = 0; j < 7 && i + 1 + j < n; j++) out[k++] = (uint8_t)(p[i + 1 + j] | (p[i] >> j & 1) << 7);
    return k;
}

/* the voice of the keys sounding this note, if any */
static int key_voice(int note)
{
    pl_voice_info_t v[PL_VOICES];
    int n = player_voices_now(v, PL_VOICES);
    for (int i = 0; i < n; i++)
        if (v[i].on && v[i].track == 0xFF && v[i].note == note) return 1;
    return 0;
}
/* what went out since `from`: messages with this status (a channel's: its high nibble alone when lo is 1) */
static int sent(uint32_t from, uint8_t st, int nibble)
{
    int k = 0;
    for (uint32_t i = from; i < ntx; i++)
        if (nibble ? (tx[i][0] & 0xF0) == st : tx[i][0] == st) k++;
    return k;
}
static int sent_note(uint32_t from, uint8_t st, uint8_t note, uint8_t vel)
{
    for (uint32_t i = from; i < ntx; i++)
        if (tx[i][0] == st && tx[i][1] == note && (vel == 0xFF || tx[i][2] == vel)) return 1;
    return 0;
}

static void learn_cc(int ctl_knob, uint8_t cc, const uint8_t *vals, int n)
{
    midi_learn_start();
    in_now.knob[ctl_knob] += 1;
    run(5);
    for (int i = 0; i < n; i++) { midi(0xB0, cc, vals[i]); run(20); }
    run(1100);                                          /* (the second passes: decided by now) */
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: midi_test DISK.ptfmdsk OUTDIR\n"); return 1; }
    out = argv[1 + 1];
    lay_disk(argv[1]);
    app_init();
    run(200);
    ui_go_name("SONGS");
    run(100);

    /* ---- notes as the keys ---- */
    int k0 = ui_midi(0);
    uint32_t t0 = ntx;
    midi(0x90, (uint8_t)(k0 + 5), 100);
    run(10);
    check("a MIDI note on a key's note: the key held", ui_keys_held() == 1);
    run(20);
    check("it sounds the current instrument", key_voice(k0 + 5));
    midi(0x80, (uint8_t)(k0 + 5), 0);
    run(10);
    check("its note off lets the key go", ui_keys_held() == 0);
    check("MIDI's own notes are never sent back out", sent(t0, 0x90, 1) == 0 && sent(t0, 0x80, 1) == 0);
    /* a press and its release between two passes: a press, then a release (and still MIDI's: not sent out) */
    t0 = ntx;
    midi(0x90, (uint8_t)(k0 + 2), 90);
    midi(0x90, (uint8_t)(k0 + 2), 0);                   /* (a note on at velocity 0: a note off) */
    run(1);
    check("a note on and off between two passes: pressed in the first", ui_keys_held() == 1);
    run(1);
    check("... and let go in the next", ui_keys_held() == 0);
    run(300);
    check("... and not sent back out either", sent(t0, 0x90, 1) == 0);
    midi(0x90, 20, 100);                                /* past the keys' range: plays, as the keys would */
    run(30);
    check("a note past the keys' range plays the current instrument", key_voice(20) && ui_keys_held() == 0);
    midi(0x80, 20, 0);
    run(300);
    /* the sustain pedal */
    midi(0xB0, 64, 127);
    midi(0x90, (uint8_t)(k0 + 7), 100);
    run(10);
    midi(0x80, (uint8_t)(k0 + 7), 0);
    run(30);
    check("the sustain pedal holds a note let go", ui_keys_held() == 1);
    midi(0xB0, 64, 0);
    run(30);
    check("... until the pedal is let go", ui_keys_held() == 0);
    /* IN CHANNEL */
    ui_midi_set[S_MIDI_IN_CH] = 2;
    midi(0x90, (uint8_t)(k0 + 4), 100);
    run(20);
    check("MIDI IN CHANNEL 2: a note on channel 1 ignored", ui_keys_held() == 0);
    midi(0x91, (uint8_t)(k0 + 4), 100);
    run(20);
    check("... a note on channel 2 plays", ui_keys_held() == 1);
    midi(0x81, (uint8_t)(k0 + 4), 0);
    run(20);
    ui_midi_set[S_MIDI_IN_CH] = 0;
    /* program change, bank select */
    {
        uint8_t carts[256];
        int nc = ui_carts(carts, 256), used[64], nu = 0;
        uint8_t c = (uint8_t)(ui_cur.inst >> 8);
        char nm[12];
        for (int s = 0; s < ui_cart_slots(c) && nu < 64; s++) if (ui_inst_name((uint16_t)(c << 8 | s), nm)) used[nu++] = s;
        midi(0xC0, 2, 0);
        run(10);
        check("program change 2: the cartridge's third used slot", nu > 2 && ui_cur.inst == (uint16_t)(c << 8 | used[2]));
        midi(0xB0, 0, 3);
        run(10);
        check("bank select 3: the fourth cartridge", nc > 3 && (ui_cur.inst >> 8) == carts[3]);
        midi(0xB0, 0, 0);
        run(10);
    }
    midi(0xE0, 0, 127);                                 /* pitch bend up: no harm (heard, not checked here) */
    midi(0xE0, 0, 64);
    run(10);

    /* ---- MIDI OUT ---- */
    t0 = ntx;
    key(0, 1);
    check("MIDI OUT KEYS: an FM-1 key goes out on MIDI OUT CHANNEL at KEYS VOLUME",
          sent_note(t0, 0x90, (uint8_t)k0, (uint8_t)ui_set[S_KEYS_VOLUME]));
    key(0, 0);
    check("... and its end", sent_note(t0, 0x80, (uint8_t)k0, 0xFF));
    ui_midi_set[S_MIDI_OUT_CH] = 5;
    t0 = ntx;
    key(1, 1); key(1, 0);
    check("MIDI OUT CHANNEL 5", sent_note(t0, 0x94, (uint8_t)(k0 + 1), 0xFF) && sent_note(t0, 0x84, (uint8_t)(k0 + 1), 0xFF));
    ui_midi_set[S_MIDI_OUT_CH] = 1;
    ui_midi_set[S_MIDI_OUT] = 0;
    t0 = ntx;
    key(2, 1); key(2, 0);
    check("MIDI OUT OFF: nothing", ntx == t0);
    ui_midi_set[S_MIDI_OUT] = 2;                        /* ALL: the tracks' notes too, track t on channel t + 1 */
    t0 = ntx;
    ui_play_song(ui_cur.song, 0);
    run(3000);
    {
        int chans = 0, on[16][128];
        memset(on, 0, sizeof on);
        for (int c = 0; c < 16; c++) chans += sent(t0, (uint8_t)(0x90 | c), 0) > 0;
        check("MIDI OUT ALL: a playing song's tracks go out, each on its channel", chans >= 2);
        player_pause();
        run(100);
        for (uint32_t i = t0; i < ntx; i++) {
            int c = tx[i][0] & 15;
            if ((tx[i][0] & 0xF0) == 0x90) on[c][tx[i][1]]++;
            else if ((tx[i][0] & 0xF0) == 0x80 && on[c][tx[i][1]]) on[c][tx[i][1]]--;
        }
        int hanging = 0;
        for (int c = 0; c < 16; c++) for (int n = 0; n < 128; n++) hanging += on[c][n];
        check("... and STOP ends every note sent", hanging == 0);
        printf("     (%d channels, %u messages)\n", chans, ntx - t0);
    }
    ui_midi_set[S_MIDI_OUT] = 1;

    /* ---- CLOCK OUT ---- */
    ui_midi_set[S_CLOCK_OUT] = 1;
    t0 = ntx;
    uint32_t c0 = nclk;
    ui_play_song(ui_cur.song, 0);
    run(2000);
    {
        player_state_t s;
        player_state(&s, 0);
        int want = (int)(s.bpm10 * 24u * 2u / 600u), got = sent(t0, 0xF8, 0);
        double gap = 44100.0 * 600.0 / (s.bpm10 * 24.0), worst = 0;   /* (frames between two clocks) */
        for (uint32_t i = c0 + 10; i + 1 < nclk; i++) {
            double e = (double)(clk_frame[i + 1] - clk_frame[i]) - gap;
            worst = e > worst ? e : -e > worst ? -e : worst;
        }
        check("CLOCK OUT: each clock at its own frame, evenly (within a frame of its place)", nclk - c0 > 40 && worst <= 1.0);
        printf("     (%u clocks, %.1f frames apart, the most off %.2f)\n", nclk - c0, gap, worst);
        uint32_t first = t0;
        while (first < ntx && tx[first][0] == 0xF8) first++;   /* (clocks run before START too) */
        check("CLOCK OUT: START as a song starts from its start", sent(t0, 0xFA, 0) == 1);
        check("CLOCK OUT: 24 clocks a beat at the song's tempo", got >= want - 3 && got <= want + 3);
        printf("     (%d clocks in 2 s at %u.%u BPM, %d expected)\n", got, s.bpm10 / 10, s.bpm10 % 10, want);
        t0 = ntx;
        player_pause();
        run(100);
        check("CLOCK OUT: STOP as it stops", sent(t0, 0xFC, 0) == 1);
    }
    ui_midi_set[S_CLOCK_OUT] = 0;
    run(100);

    /* ---- OUTPUT SYNC PULSE L: a click at every half beat on the left, nothing else there; the music on the right ---- */
    ui_set[S_OUTPUT] = 2;
    run(20);
    pulse_scan = 1;
    ui_play_song(ui_cur.song, 0);
    run(3000);
    pulse_scan = 0;
    {
        player_state_t s;
        player_state(&s, 0);
        double half = 44100.0 * 60.0 / (s.bpm10 / 10.0) / 2.0, worst = 0;
        for (uint32_t i = 1; i + 1 < npulse; i++) {
            double e = (double)(pulse_at[i + 1] - pulse_at[i]) - half;
            worst = e > worst ? e : -e > worst ? -e : worst;
        }
        check("SYNC PULSE L: a click every half beat on the left (with the rows: within a 64-frame block), nothing else "
              "there, the music right", npulse >= 10 && worst <= 64.0 && pulse_other == 0 && right_sq > 0);
        printf("     (%u clicks, %.1f frames apart, the most off %.2f; %u other values on the left)\n", npulse, half, worst,
               pulse_other);
        player_pause();
    }
    ui_set[S_OUTPUT] = 0;
    run(100);

    /* ---- the clock in ---- */
    ui_midi_set[S_SYNC] = 1;
    player_to_start();
    run(50);
    {
        double t = now_ms;
        midi(0xFA, 0, 0);                               /* START */
        for (int i = 0; i < 24 * 8; i++) {              /* 8 beats at 120 BPM */
            t += 60000.0 / 120.0 / 24.0;
            while (now_ms + 1 < t) run(1);
            midi(0xF8, 0, 0);
        }
        run(1);
        player_state_t s;
        player_state(&s, 0);
        check("SYNC EXTERNAL: START plays", s.playing);
        check("SYNC EXTERNAL: the tempo follows the clock (120.0 BPM)", s.bpm10 >= 1195 && s.bpm10 <= 1205);
        printf("     (%u.%u BPM)\n", s.bpm10 / 10, s.bpm10 % 10);
        uint32_t r0 = s.rows_played;
        for (int i = 0; i < 24 * 4; i++) {              /* 4 beats more at 90 BPM */
            t += 60000.0 / 90.0 / 24.0;
            while (now_ms + 1 < t) run(1);
            midi(0xF8, 0, 0);
        }
        run(1);
        player_state(&s, 0);
        check("... and changes with it (90.0 BPM)", s.bpm10 >= 895 && s.bpm10 <= 905);
        printf("     (%u.%u BPM, %u rows in 4 beats)\n", s.bpm10 / 10, s.bpm10 % 10, s.rows_played - r0);
        midi(0xFC, 0, 0);                               /* STOP */
        run(20);
        player_state(&s, 0);
        check("STOP stops", !s.playing);
        midi(0xF2, 16, 0);                              /* the song position: 16 sixteenths, then CONTINUE */
        midi(0xFB, 0, 0);
        run(30);
        player_state(&s, 0);
        check("the song position, then CONTINUE: plays from there (row 16 at ROWS A BEAT 4)", s.playing && s.pos == 0 && s.row >= 16 && s.row <= 17);
        printf("     (position %u, row %u)\n", s.pos, s.row);
        run(700);                                       /* no clock for 0.7 s */
        check("the clock lost: CLOCK LOST", toast_is("CLOCK LOST"));
        player_state(&s, 0);
        check("... and it plays on at the last tempo", s.playing && s.bpm10 >= 895 && s.bpm10 <= 905);
        player_pause();
    }
    /* locked to the clock: a master a little off any tempo PT/FM shows (120.04 BPM: 0.1 BPM steps would drift 30 ms
     * in the 90 s), its clocks arriving up to 1 ms late at random: each row's start against its clock's time, the
     * mean over 10 s at the start and at the end; and START waits for the first clock */
    {
        rec_song_t rs;
        int rpb = !rec_song(ui_cur.song, &rs) && rs.rpb ? rs.rpb : 4;
        double per = 60000.0 / 120.04 / 24.0, t, first = 0, a = 0, b = 0;
        uint32_t seed = 2463534242u, last_r = 0, rows_end = 0;
        int na = 0, nb = 0, beats = 180;
        player_state_t s;
        player_to_start();
        run(300);
        midi(0xFA, 0, 0);                               /* START, then no clock for 0.3 s */
        run(300);
        player_state(&s, 0);
        check("SYNC EXTERNAL: START waits for the first clock", s.playing && s.pos == 0 && s.row == 0);
        rows_end = (uint32_t)(beats * rpb);
        t = now_ms;
        for (int i = 0; i < 24 * beats; i++) {
            t += per;
            while (now_ms + 1 < t) {
                run(1);
                player_state(&s, 0);
                if (s.rows_played != last_r && first) {
                    last_r = s.rows_played;
                    double off = now_ms - (first + (double)(s.rows_played - 1u) * per * 24.0 / rpb);
                    if (s.rows_played > 40u && s.rows_played <= 120u) { a += off; na++; }
                    if (s.rows_played > rows_end - 80u && s.rows_played <= rows_end) { b += off; nb++; }
                }
            }
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            if (seed & 1u) run(1);                      /* (a clock arriving 1 ms late) */
            midi(0xF8, 0, 0);
            if (!first) first = now_ms;
        }
        a = na ? a / na : 0;
        b = nb ? b / nb : 1e9;
        check("locked to the clock: no drift over 90 s (the rows' starts against the clocks', within 3 ms)",
              na >= 60 && nb >= 60 && b - a < 3.0 && a - b < 3.0);
        printf("     (rows against their clocks: %.1f ms in the first stretch, %.1f ms in the last, %d / %d rows)\n",
               a, b, na, nb);
        midi(0xFC, 0, 0);
        run(50);
    }
    /* on DJ the master's START, STOP and CONTINUE are the main deck's: from its cue point, paused, on */
    {
        dj_deck_state_t d;
        ui_go_name("DJ");
        run(200);
        key(8, 1);                                      /* OP4: LOAD on the left (the song under the cursor) */
        key(8, 0);
        run(400);
        check("DJ: a song on the left deck", dj_deck(0)[DK_SONG] != DJ_NO_SONG);
        midi(0xFA, 0, 0);
        run(60);
        check("DJ, SYNC EXTERNAL: START plays the main deck", dj_deck_state(0, &d) && d.playing);
        midi(0xFC, 0, 0);
        run(60);
        check("... STOP pauses it", dj_deck_state(0, &d) && !d.playing);
        midi(0xFB, 0, 0);
        run(60);
        check("... CONTINUE plays it on", dj_deck_state(0, &d) && d.playing);
        midi(0xFC, 0, 0);
        run(60);
        ui_go_name("SONGS");
        run(200);
    }
    ui_midi_set[S_SYNC] = 0;
    run(100);
    {
        player_state_t s;
        player_state(&s, 0);
        check("SYNC INTERNAL again: the song's own tempo", s.bpm10 < 895 || s.bpm10 > 905);
    }

    /* ---- MIDI learn ---- */
    ui_go_name("SETTINGS");
    run(50);
    tap(B_SEQ);                                         /* the MIDI page */
    turn(K_SELECT, S_MIDI_COUNT);                       /* MIDI CONTROLLER, after the items */
    {
        uint8_t col;
        const char *f = ui_screen_now()->footer(&col);
        check("the MIDI page: MIDI CONTROLLER after its items, EDIT: SWITCH", f && !strcmp(f, "EDIT: SWITCH"));
    }
    turn(K_SELECT, 1);                                  /* to LEARN */
    shot("midi_page_empty");
    tap(B_EDIT);
    check("EDIT on LEARN: step 1", midi_learning() == 1);
    shot("midi_learn_1");
    in_now.knob[K_K1] += 1;                             /* touch K1 */
    run(30);
    check("K1 turned: step 2, K1 picked", midi_learning() == 2 && midi_learn_ctl() == MC_KNOB + K_K1);
    shot("midi_learn_2");
    {
        static const uint8_t v[6] = {64, 65, 66, 67, 68, 69};
        for (int i = 0; i < 6; i++) { midi(0xB0, 74, v[i]); run(20); }
    }
    run(300);
    midi_map_t m;
    check("a knob's values by steps: K1 learned as CC 74, ABSOLUTE", midi_learning() == 0 && midi_map_count() == 1 &&
          !midi_map_at(0, &m) && m.ctl == MC_KNOB + K_K1 && !m.note && m.num == 74 && m.ch == 0 && m.kind == MK_ABS);
    check("the toast says what", toast_is("K1: CC 74, CH 1"));
    shot("midi_page_one");
    {   /* an endless encoder, two's complement */
        static const uint8_t v[6] = {1, 1, 1, 1, 2, 1};
        learn_cc(K_K2, 20, v, 6);
        check("an encoder's 1s: K2 learned RELATIVE (two's complement)", midi_map_count() == 2 && !midi_map_at(1, &m) &&
              m.ctl == MC_KNOB + K_K2 && m.kind == MK_REL_2C);
        static const uint8_t w[6] = {65, 65, 63, 65, 63, 63};
        learn_cc(K_K3, 21, w, 6);
        check("65 / 63: K3 learned RELATIVE (offset 64)", midi_map_count() == 3 && !midi_map_at(2, &m) && m.kind == MK_REL_64);
        static const uint8_t x[6] = {1, 1, 65, 65, 1, 1};
        learn_cc(K_K4, 22, x, 6);
        check("1 / 65: K4 learned RELATIVE (sign and magnitude)", midi_map_count() == 4 && !midi_map_at(3, &m) && m.kind == MK_REL_SM);
    }
    /* a momentary pad: a note's press and release; a toggle pad: one CC a press */
    midi_learn_start();
    press(B_PLAY);
    check("PLAY pressed: step 2, PLAY picked (and nothing played)", midi_learning() == 2 && midi_learn_ctl() == MC_BUTTON + B_PLAY);
    release(B_PLAY);
    midi(0x99, 36, 100); run(80); midi(0x89, 36, 0); run(50);
    check("a note pressed and let go: PLAY learned as NOTE 36 CH 10, MOMENTARY", midi_map_count() == 5 &&
          !midi_map_at(4, &m) && m.note && m.num == 36 && m.ch == 9 && m.kind == MK_MOMENTARY);
    midi_learn_start();
    key(0, 1); key(0, 0);
    midi(0xB0, 50, 127);
    run(1100);
    check("one CC a press: key F-3 learned TOGGLE", midi_map_count() == 6 && !midi_map_at(5, &m) && m.ctl == MC_KEY && m.kind == MK_TOGGLE);
    midi_learn_start();
    in_now.master = (uint16_t)(in_now.master > 512 ? 100 : 900);
    run(30);
    check("MASTER moved: picked", midi_learning() == 2 && midi_learn_ctl() == MC_MASTER);
    midi(0xB0, 7, 100);
    run(30);
    check("MASTER learns a CC at once, ABSOLUTE", midi_map_count() == 7 && !midi_map_at(6, &m) && m.ctl == MC_MASTER && m.kind == MK_ABS);
    /* HOME cancels at either step */
    midi_learn_start();
    tap(B_HOME);
    check("HOME cancels step 1", midi_learning() == 0 && midi_map_count() == 7);
    midi_learn_start();
    turn(K_K1, 1);
    tap(B_HOME);
    check("HOME cancels step 2", midi_learning() == 0 && midi_map_count() == 7);
    /* a MIDI control learned again: its old mapping goes */
    {
        static const uint8_t v[6] = {10, 11, 12, 13, 14, 15};
        learn_cc(K_SELECT, 74, v, 6);
        check("CC 74 learned again (SELECT): K1's mapping gone, 7 still", midi_map_count() == 7 && !midi_map_at(6, &m) &&
              m.ctl == MC_KNOB + K_SELECT && m.num == 74);
    }
    shot("midi_page_mappings");

    /* ---- the learned controls act as the FM-1's ---- */
    ui_go_name("SETTINGS");
    run(50);
    if (ui_screen_now() != ui_screen_named("SETTINGS")) printf("     (not on SETTINGS)\n");
    {
        int before = ui_set[S_KEYS_VOLUME];
        /* K2 is the two's-complement encoder (CC 20): on SETTINGS' list K1 changes; turn K2? no: map K1 again */
        static const uint8_t v[6] = {1, 1, 1, 1, 1, 1};
        learn_cc(K_K1, 30, v, 6);                       /* K1 from CC 30, relative */
        tap(B_SEQ);                                     /* back to the list */
        turn(K_SELECT, -30);
        turn(K_SELECT, S_KEYS_VOLUME);
        midi(0xB0, 30, 1); run(30); midi(0xB0, 30, 1); run(30); midi(0xB0, 30, 127); run(30);
        check("a learned encoder turns K1: KEYS VOLUME up 2, down 1", ui_set[S_KEYS_VOLUME] == before + 1 ||
              (before >= 127 && ui_set[S_KEYS_VOLUME] == before - 1));
        printf("     (KEYS VOLUME %d -> %d)\n", before, ui_set[S_KEYS_VOLUME]);
    }
    {
        player_state_t s;
        ui_go_name("SONGS");
        run(50);
        player_state(&s, 0);
        int was = s.playing;
        midi(0x99, 36, 100); run(40); midi(0x89, 36, 0); run(300);
        player_state(&s, 0);
        check("the learned pad on PLAY plays (or stops) as PLAY does", s.playing != was);
        printf("     (playing %d -> %d)\n", was, s.playing);
        player_pause();
        run(50);
        midi(0xB0, 7, 64);
        run(10);
        check("the learned knob on MASTER sets MASTER", master_set == 64 * 1023 / 127);
    }

    /* ---- the record: SAVE, a power-on, delete, SEL's REVERT ---- */
    {
        uint32_t len = 0;
        uint8_t ver = 0;
        const uint8_t *p = store_get(REC_MIDIMAP, 0, 0, &len, &ver);
        int n = midi_map_count();
        check("the MIDI map record: 4 B, then 4 B a mapping", p && ver == 1 && len == 4u + 4u * (uint32_t)n && p[0] == n &&
              !p[1] && !p[2] && !p[3]);
        int ok = 1;
        for (int i = 0; p && i < n; i++) {
            midi_map_at(i, &m);
            const uint8_t *e = p + 4 + 4 * i;
            if (e[0] != (m.ch | m.note << 4) || e[1] != m.num || e[2] != m.ctl || e[3] != m.kind) ok = 0;
        }
        check("... each: channel and message, number, the FM-1 control, the kind", ok);
        ui_go_name("SONGS");
        run(50);
        tap(B_SAVE);
        run(3000);
        check("SAVE wrote it: nothing unsaved", ui_edit_count() == 0);
        store_mount();
        player_store_changed();
        run(50);
        check("after a power-on (a mount): the same mappings", midi_map_count() == n);
        ui_go_name("SETTINGS");
        run(50);
        tap(B_SEQ);
        turn(K_SELECT, -30);
        turn(K_SELECT, S_MIDI_COUNT + 2);               /* the first mapping (after MIDI CONTROLLER, LEARN) */
        tap(B_LFO);
        check("LFO on a mapping deletes it", midi_map_count() == n - 1);
        printf("     (toast: %s)\n", ui_toast_now() ? ui_toast_now() : "-");
        tap(B_SEL);                                     /* the MIDI page's menu: REVERT (the map unsaved) */
        tap(B_SEL);
        check("SEL's REVERT asks first (REVERT THE MIDI MAP TO SAVED?)", ui_top() && ui_overlay_words(ui_top()) &&
              !strcmp(ui_overlay_words(ui_top()), "REVERT THE MIDI MAP TO SAVED?"));
        tap(B_OCTP);                                    /* REVERT */
        run(20);
        check("REVERT brings it back", midi_map_count() == n && !ui_edit_unsaved(REC_MIDIMAP, 0, 0));
        tap(B_SEL);
        check("nothing unsaved on the page: SEL refused, NOTHING TO REVERT", !ui_top() && toast_is("NOTHING TO REVERT"));
    }
    {   /* 64 mappings: LEARN refuses */
        static uint8_t rec[4 + 4 * 64];
        memset(rec, 0, sizeof rec);
        rec[0] = 64;
        for (int i = 0; i < 64; i++) { rec[4 + 4 * i] = 1; rec[5 + 4 * i] = (uint8_t)i; rec[6 + 4 * i] = MC_KNOB + K_K1; rec[7 + 4 * i] = MK_ABS; }
        ui_edit_put(REC_MIDIMAP, 0, 0, 1, rec, sizeof rec);
        run(20);
        ui_go_name("SETTINGS");
        run(50);
        if (!ui_screen_now()->sub || !ui_screen_now()->sub()) tap(B_SEQ);     /* (to the MIDI page, if not there) */
        turn(K_SELECT, -80);
        turn(K_SELECT, S_MIDI_COUNT + 1);               /* (LEARN, after MIDI CONTROLLER) */
        tap(B_EDIT);
        check("64 mappings: EDIT on LEARN refused", midi_learning() == 0 && toast_is("64 MAPPINGS: DELETE ONE FIRST"));
        shot("midi_page_full");
        ui_edit_revert(REC_MIDIMAP, 0, 0);
        run(20);
    }

    /* ---- MIRROR (specs/connections.md#mirror-the-panel-from-a-page): the controller's hello, then channel 16
     * from USB is the panel; the state a page asks for ---- */
    {
        static const uint8_t on[10] = {0xF0, 0x7D, 0x50, 0x54, 0x43, 0x02, 0x00, 0x02, 0x0F, 0xF7};
        static const uint8_t off[10] = {0xF0, 0x7D, 0x50, 0x54, 0x43, 0x02, 0x01, 0x02, 0x00, 0xF7};
        static const uint8_t ask[6] = {0xF0, 0x7D, 0x50, 0x54, 0x40, 0xF7};
        static const uint8_t question[7] = {0xF0, 0x7D, 0x50, 0x54, 0x43, 0x01, 0xF7};
        uint8_t st[400];
        int ch = -1;
        ui_go_name("SONGS");
        run(100);
        sx_last_n = 0;
        app_sysex(question, 7);
        run(20);
        check("MIRROR: the controller's question goes unanswered (PT/FM is no controller)", sx_last_n == 0 && !midi_mirror(0));
        midi(0xBF, 102 + B_HOME, 127);                  /* (before the hello: channel 16 is no panel) */
        run(20);
        check("MIRROR: before the hello, channel 16's CC 110 is nothing", !ui_held(B_HOME));
        midi(0xBF, 102 + B_HOME, 0);
        app_sysex(on, 10);
        run(20);
        check("MIRROR: the hello switches it on, channel 16, MIRROR ON", midi_mirror(&ch) && ch == 15 && toast_is("MIRROR ON"));
        midi(0xBF, 102 + B_HOME, 127);                  /* HOME + OP5 (key 10): DJ, as a hand would */
        run(30);
        check("MIRROR: CC 102 + 8 holds HOME", ui_held(B_HOME));
        midi(0x9F, 53 + 10, 127);
        run(30);
        midi(0x8F, 53 + 10, 0);
        run(30);
        midi(0xBF, 102 + B_HOME, 0);
        run(300);
        check("MIRROR: HOME + note 63 (OP5): the DJ screen", !strcmp(T(ui_screen_now()->name), "DJ"));
        midi(0xBF, 102 + B_HOME, 127);
        midi(0x9F, 53 + 1, 127);                        /* (a press and its release between two passes) */
        midi(0x9F, 53 + 1, 0);
        midi(0xBF, 102 + B_HOME, 0);
        run(400);
        check("MIRROR: a combination pressed and let go within a pass still acts (SONGS)", !strcmp(T(ui_screen_now()->name), "SONGS"));
        uint8_t song0 = ui_cur.song;
        midi(0xBF, 20 + K_SELECT, 1);                   /* SELECT a click clockwise, then one back (127) */
        run(200);
        check("MIRROR: CC 20 + 0 = 1, SELECT a click: the next song", ui_cur.song != song0);
        midi(0xBF, 20 + K_SELECT, 127);
        run(200);
        check("MIRROR: CC 20 + 0 = 127, a click back", ui_cur.song == song0);
        /* a click and a press in one pass, in the order they came: a click, then SEL, is a step and SEL's tap (its
         * menu); SEL, then a click, is SEL + SELECT (on SONGS a step, no menu) */
        midi(0xBF, 20 + K_SELECT, 1);
        midi(0xBF, 102 + B_SEL, 127);
        run(30);
        midi(0xBF, 102 + B_SEL, 0);
        run(400);
        check("MIRROR: a click, then SEL, in one pass: a step, then SEL's tap (its menu)",
              ui_cur.song != song0 && ui_top() && ui_top()->kind == OV_MENU);
        if (ui_top() && ui_top()->kind == OV_MENU) {     /* (the menu closed) */
            midi(0xBF, 102 + B_HOME, 127);
            run(30);
            midi(0xBF, 102 + B_HOME, 0);
            run(300);
        }
        song0 = ui_cur.song;
        midi(0xBF, 102 + B_SEL, 127);
        midi(0xBF, 20 + K_SELECT, 127);
        run(30);
        midi(0xBF, 102 + B_SEL, 0);
        run(400);
        check("MIRROR: SEL, then a click, in one pass: SEL + SELECT, no menu",
              ui_cur.song != song0 && !(ui_top() && ui_top()->kind == OV_MENU));
        if (ui_top() && ui_top()->kind == OV_MENU) { midi(0xBF, 102 + B_HOME, 127); run(30); midi(0xBF, 102 + B_HOME, 0); run(300); }
        master_set = -1;
        midi(0xBF, 7, 127);
        run(10);
        check("MIRROR: CC 7, MASTER", master_set == 1023);
        uint32_t t0 = ntx;
        midi(0x9F, 53 + 0, 127);                        /* key 0 held: it plays as the FM-1's own key */
        run(30);
        check("MIRROR: note 53 holds key 0; not sent out again", ui_keys_held() == 1 && sent(t0, 0x9F, 1) == 0);
        midi(0xBF, 102 + B_OCTP, 127);                  /* held when the hello comes again: let go */
        run(20);
        app_sysex(on, 10);
        run(30);
        check("MIRROR: a hello lets go of what MIRROR holds", ui_keys_held() == 0 && !ui_held(B_OCTP));
        midi(0x90, (uint8_t)(ui_midi(0) + 5), 100);     /* other channels: notes as ever */
        run(30);
        check("MIRROR: other channels are MIDI in as ever (a note on channel 1)", ui_keys_held() == 1);
        midi(0x80, (uint8_t)(ui_midi(0) + 5), 0);
        run(30);
        /* the state */
        sx_last_n = 0;
        app_sysex(ask, 6);
        run(10);
        int n = sx_last_n > 6 ? unpack7(sx_last + 5, sx_last_n - 6, st) : 0;
        check("STATE: 40 asked, 41 answered with its bytes", sx_last_n > 6 && sx_last[4] == 0x41 && n >= 301 && st[0] == 1);
        check("STATE: MIRROR on, channel 16; the screen SONGS; its cursor the current song",
              (st[1] & 2) && st[2] == 15 && !strcmp((const char *)st + 26, "SONGS") && st[68] == ui_cur.song && st[42] == 0);
        check("STATE: the settings (HOLD DELAY, KEYS OCTAVE, VOICES) and the keys' instrument",
              (st[15] | st[16] << 8) == ui_set[S_HOLD_DELAY] && (int8_t)st[21] == ui_set[S_KEYS_OCTAVE] && st[11] == 12 &&
              st[19] == ui_cur.inst >> 8 && st[20] == (ui_cur.inst & 255));
        /* an unsaved change made through MIRROR (SETTINGS' KEYS OCTAVE, K1), then REVERT as the autotests do it:
         * SETTINGS -> TOOLS -> REVERT ALL, EDIT, OCT+ (SAVE never pressed); presses 60 ms apart, as a hand's */
#define MBTN(b, d) do { midi(0xBF, (uint8_t)(102 + (b)), (d) ? 127 : 0); run(60); } while (0)
#define MKNOB(k, c) do { midi(0xBF, (uint8_t)(20 + (k)), (uint8_t)((c) & 127)); run(60); } while (0)
        MBTN(B_HOME, 1);
        midi(0x9F, 53 + 20, 127); run(60);              /* HOME + MONO: SETTINGS */
        midi(0x8F, 53 + 20, 0); run(60);
        MBTN(B_HOME, 0);
        run(300);
        app_sysex(ask, 6);
        run(10);
        unpack7(sx_last + 5, sx_last_n - 6, st);
        check("MIRROR: HOME + MONO: SETTINGS, on its MIDI page as left (the state's cursor)",
              !strcmp((const char *)st + 26, "SETTINGS") && st[68] == 1);
        MKNOB(K_ALG, -1);                               /* ALGORITHM back: the list's page */
        app_sysex(ask, 6);
        run(10);
        unpack7(sx_last + 5, sx_last_n - 6, st);
        check("MIRROR: ALGORITHM a click back: the list's page", st[68] == 0);
        for (int i = 0; i < 40 && st[69] != S_KEYS_OCTAVE; i++) {   /* seek the line, a click at a time */
            MKNOB(K_SELECT, st[69] < S_KEYS_OCTAVE ? 1 : -1);
            app_sysex(ask, 6);
            run(10);
            unpack7(sx_last + 5, sx_last_n - 6, st);
        }
        MKNOB(K_K1, 1);                                 /* KEYS OCTAVE +1: an unsaved change */
        run(100);
        app_sysex(ask, 6);
        run(10);
        unpack7(sx_last + 5, sx_last_n - 6, st);
        check("STATE: seeking SETTINGS' line by its cursor; K1 changed KEYS OCTAVE: 1 unsaved",
              st[69] == S_KEYS_OCTAVE && (int8_t)st[21] == 1 && (st[13] | st[14] << 8) == 1);
        for (int i = 0; i < 40 && st[69] != S_TOOLS; i++) {
            MKNOB(K_SELECT, st[69] < S_TOOLS ? 1 : -1);
            app_sysex(ask, 6);
            run(10);
            unpack7(sx_last + 5, sx_last_n - 6, st);
        }
        for (int i = 0; i < 8 && st[70] != 4; i++) {   /* the tool: REVERT ALL */
            MKNOB(K_K1, st[70] < 4 ? 1 : -1);
            app_sysex(ask, 6);
            run(10);
            unpack7(sx_last + 5, sx_last_n - 6, st);
        }
        MBTN(B_EDIT, 1);
        MBTN(B_EDIT, 0);
        run(100);
        app_sysex(ask, 6);
        run(10);
        unpack7(sx_last + 5, sx_last_n - 6, st);
        check("STATE: TOOLS' REVERT ALL, EDIT: the question (its overlay, its words), still 1 unsaved",
              st[42] == 2 && !strcmp((const char *)st + 44, "REVERT TO SAVED?") && (st[13] | st[14] << 8) == 1);
        MBTN(B_OCTP, 1);                                /* OCT+: REVERT */
        MBTN(B_OCTP, 0);
        run(1500);
#undef MBTN
#undef MKNOB
        app_sysex(ask, 6);
        run(10);
        unpack7(sx_last + 5, sx_last_n - 6, st);
        check("STATE: OCT+ reverted: nothing unsaved, no overlay, KEYS OCTAVE back", st[42] == 0 && !(st[13] | st[14]) &&
              ui_set[S_KEYS_OCTAVE] == 0);
        usb_resets++;                                   /* a USB bus reset: MIRROR ends */
        run(20);
        check("MIRROR: a USB bus reset switches it off (MIRROR OFF)", !midi_mirror(0) && toast_is("MIRROR OFF"));
        app_sysex(on, 10);
        run(20);
        app_sysex(off, 10);
        run(20);
        check("MIRROR: the general mode's hello switches it off", !midi_mirror(0));
        midi(0xBF, 102 + B_HOME, 127);
        run(20);
        check("MIRROR: off, channel 16's CC 110 is nothing again", !ui_held(B_HOME));
        midi(0xBF, 102 + B_HOME, 0);
        run(20);
    }

    printf(fails ? "midi_test: %d FAILED\n" : "midi_test: all passed\n", fails);
    return fails ? 1 : 0;
}
