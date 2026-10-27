/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* MIDI CONTROLLER, the PT/FM controller (fw/core/ctlmode.c, specs/controller.md) on a fake FM-1: started
 * (as SETTINGS' MIDI CONTROLLER starts it), its start screen (REC mirror, HOME general, SELECT the channel; a key
 * held since before it picks nothing), then the controller's messages over the fake USB-MIDI, the page's LEDs
 * back, the handshake; the screen's shots.
 *   ctlmode_test mirror|general SHOTS_DIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "ctlmode.h"

static uint16_t screen[240 * 240];
static uint32_t now_ms;
static plat_input_t in_now;
static uint8_t leds[LED_COUNT];
static int fails, checks;
static uint8_t sent[64][16];            /* what went out: channel messages (3 B) and SysEx */
static uint32_t sent_n[64], nsent;
static plat_midi_t inq[16];             /* what comes in */
static uint32_t inq_r, inq_w;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_us(void) { return now_ms * 1000u; }
void plat_input(plat_input_t *in) { *in = in_now; memset(in_now.knob, 0, sizeof in_now.knob); }
uint32_t plat_notes(void) { return in_now.notes; }
void plat_led(int id, uint8_t level) { if (id >= 0 && id < LED_COUNT) leds[id] = level; }
void plat_lcd_rows(int y, int h, const uint16_t *px) { memcpy(screen + y * 240, px, (size_t)h * 240 * 2); }
int plat_screen_lost(void) { return 0; }
uint32_t plat_usb_resets(void) { return 0; }
int plat_usb_up(void) { return 1; }
static void keep(const uint8_t *m, uint32_t n)
{
    if (nsent < 64 && n <= 16) { memcpy(sent[nsent], m, n); sent_n[nsent++] = n; }
}
int plat_sysex_send(const uint8_t *m, uint32_t n) { keep(m, n); return 0; }
int plat_midi_tx(const uint8_t m[3]) { keep(m, 3); return 0; }
int plat_midi_rx(plat_midi_t *e)
{
    if (inq_r == inq_w) return 0;
    *e = inq[inq_r++ % 16];
    return 1;
}
void plat_fault(const char *why) { fprintf(stderr, "FAULT: %s\n", why); exit(2); }
void ui_toast(const char *s) { (void)s; }                /* (ui_fmt.c's ui_toastf: the tracker's, unused here) */
uint8_t *store_scratch(void)                            /* (the store's scratch: the controller's memory) */
{
    static uint32_t s[1024];
    return (uint8_t *)s;
}

/* "fast": the platform's 1 kHz tick (plat_fast, as the FM-1's) runs the controller, the main loop only every 25 ms
 * (as while the screen is drawn) */
static int fast_mode;
static void (*fast)(const plat_input_t *in, uint32_t ms);
int plat_fast(void (*fn)(const plat_input_t *in, uint32_t ms)) { if (!fast_mode) return 0; fast = fn; return 1; }
static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t++) {
        now_ms++;
        if (fast) {
            plat_input_t i = in_now;
            memset(in_now.knob, 0, sizeof in_now.knob);
            fast(&i, now_ms);
        }
        if (!fast || now_ms % 25u == 0) ctlmode_poll();
    }
}
static void check(int ok, const char *what)
{
    checks++;
    printf("%-66s %s\n", what, ok ? "ok" : "WRONG");
    fails += !ok;
}
static int sent_has(const uint8_t *m, uint32_t n)   /* that message went out since the last clear */
{
    for (uint32_t i = 0; i < nsent; i++) if (sent_n[i] == n && !memcmp(sent[i], m, n)) return 1;
    return 0;
}
static void shot(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
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

int main(int argc, char **argv)
{
    int general = argc > 1 && !strcmp(argv[1], "general");
    const char *dir = argc > 2 ? argv[2] : ".";
    fast_mode = argc > 3 && !strcmp(argv[3], "fast");
    int key = general ? B_HOME : B_REC;
    in_now.buttons = 1u << key;                         /* (held as it starts: no pick, it has to be pressed) */
    ctlmode_start();
    run(50);
    check(leds[B_REC] == 255 && leds[B_HOME] == 255 && leds[B_PLAY] == 0 && nsent == 0,
          "the start: REC and HOME lit, nothing sent; a key held since before picks nothing");
    in_now.buttons = 0;
    run(20);
    for (int i = 0; i < 3; i++) { in_now.knob[K_SELECT] = 1; run(40); }
    if (!general) shot(dir, "ctl_start");
    check(nsent == 0, "SELECT turns the channel: nothing sent yet");
    in_now.buttons = 1u << key;                         /* pressed: that mode */
    run(20);
    in_now.buttons = 0;
    run(30);
    const uint8_t hello[10] = {0xF0, 0x7D, 0x50, 0x54, 0x43, 0x02, (uint8_t)general, 0x02, 0x02, 0xF7};
    check(sent_has(hello, 10), general ? "HOME: general mode on channel 3 (SELECT 3 clicks), its hello sent"
                                       : "REC: mirror mode on channel 3 (SELECT 3 clicks), its hello sent");
    nsent = 0;
    uint8_t ch = 2;
    in_now.notes = 1u << 7;                             /* the key C4 (F3 + 7) */
    run(10);
    const uint8_t on[3] = {(uint8_t)(0x90 | ch), general ? 60 : 53 + 7, general ? 100 : 127};
    check(sent_has(on, 3), "a key: its Note On");
    in_now.notes = 0;
    run(10);
    in_now.knob[K_K1] = 2;
    run(10);
    const uint8_t k1m[3] = {(uint8_t)(0xB0 | ch), 22, 2}, k1s[3] = {(uint8_t)(0xB0 | ch), 74, 72};
    check(sent_has(general ? k1s : k1m, 3), general ? "K1 two clicks at once: CC 74, 64 + 2 x 4 (fast clicks: 4 a click)"
                                                    : "K1 two clicks: CC 22 +2 (relative)");
    in_now.buttons = 1u << B_FX;
    run(10);
    const uint8_t fx[3] = {(uint8_t)(0xB0 | ch), 104, 127};
    check(sent_has(fx, 3), "FX: CC 104, 127");
    in_now.buttons = 0;
    run(10);
    if (fast_mode) {                                    /* a 2 ms tap, the main loop away: both ends went out */
        nsent = 0;
        in_now.notes = 1u << 9;
        run(2);
        in_now.notes = 0;
        run(2);
        const uint8_t on9[3] = {(uint8_t)(0x90 | ch), general ? 62 : 53 + 9, general ? 100 : 127};
        const uint8_t off9a[3] = {(uint8_t)(0x80 | ch), general ? 62 : 53 + 9, 0};
        check(sent_has(on9, 3) && (sent_has(off9a, 3) || nsent >= 2),
              "the 1 kHz tick: a 2 ms tap's Note On and Off, the main loop away");
        run(30);
    }
    if (!general) {                                     /* the page's LEDs and its question */
        inq[inq_w++ % 16] = (plat_midi_t){{(uint8_t)(0x90 | ch), 53 + 7, 127}, PLAT_MIDI_USB, now_ms};
        inq[inq_w++ % 16] = (plat_midi_t){{(uint8_t)(0xB0 | ch), 102 + B_PLAY, 32}, PLAT_MIDI_USB, now_ms};
        run(10);
        check(leds[LED_NOTE(7)] == 255 && leds[B_PLAY] == 64, "mirror: the page's LEDs (a key lit, PLAY dim)");
        nsent = 0;
        const uint8_t ask[7] = {0xF0, 0x7D, 0x50, 0x54, 0x43, 0x01, 0xF7};
        ctlmode_sysex(ask, 7);
        run(10);
        check(sent_has(hello, 10), "the page's question: the answer");
    } else {
        in_now.buttons = 1u << B_OCTP;
        run(10);
        in_now.buttons = 0;
        run(10);
        check(leds[B_OCTP] == 64, "general: OCT+ an octave up, its LED dim");
    }
    run(100);
    shot(dir, general ? "ctl_monitor_general" : "ctl_monitor_mirror");
    printf("ctlmode %s: %d checks, %s\n", general ? "general" : "mirror", checks, fails ? "FAILED" : "all as controller.md says");
    return fails != 0;
}
