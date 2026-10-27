/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The bring-up app (fw/build.sh --bringup): the device layer alone, through plat.h as the real app
 * uses it, for trying the hardware side on the FM-1. Never released.
 *   the screen: a box a note key and a button, lit while held; a bar a knob (its clicks), MASTER's
 *               bar, the audio's time a half against the half's length (CPU), the LATE halves in red;
 *   the sound:  a triangle a note key held, at the key's pitch;
 *   the LEDs:   every control lit while held; PLAY's green while a key is held, the others dim;
 *   SysEx:      a PT/FM message comes back as it came (F0 7D 50 54 ... F7), so a page can see the path.
 * HOME + REC: OUTPUT mono / stereo; HOME + OCT- / OCT+: LINE GAIN down / up by 3 dB. */
#include <stdint.h>
#include "plat.h"
#include "app.h"

static const uint32_t STEP[27] = {
    17005939u, 18017165u, 19088521u, 20223584u, 21426141u, 22700205u, 24050030u, 25480119u, 26995246u,
    28600467u, 30301139u, 32102938u, 34011878u, 36034330u, 38177043u, 40447168u, 42852281u, 45400411u,
    48100060u, 50960238u, 53990491u, 57200933u, 60602278u, 64205876u, 68023757u, 72068660u, 76354085u};
static uint32_t phase[27];
static int32_t env[27];                  /* 0 .. 4096, a short attack and release */

void app_audio(int16_t *lr, int frames)
{
    uint32_t held = plat_notes();
    int i, k;
    for (i = 0; i < frames; i++) {
        int32_t s = 0;
        for (k = 0; k < 27; k++) {
            int32_t tri;
            if (held & (1u << k)) {
                if (env[k] < 4096)
                    env[k] += 16;
            } else if (env[k] > 0) {
                env[k] -= 4;
            }
            if (!env[k])
                continue;
            phase[k] += STEP[k];
            tri = (int32_t)(phase[k] >> 16) - 32768;          /* saw, -32768 .. 32767 */
            tri = (tri < 0 ? -tri : tri) * 2 - 32768;           /* triangle */
            s += (tri * env[k]) >> 15;                          /* a quarter of full scale, -12 dB */
        }
        if (s > 32767)
            s = 32767;
        if (s < -32768)
            s = -32768;
        lr[2 * i] = (int16_t)s;
        lr[2 * i + 1] = (int16_t)s;
    }
}

static uint16_t strip[30 * PLAT_W] PLAT_POOL;
static int32_t knob_pos[K_COUNT];
static plat_input_t in;
static int mono, gain_db;
static uint32_t t_frame;

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

static void rect(int y0, int x, int y, int w, int h, uint16_t c)   /* clipped to the strip at y0 */
{
    int r, i;
    for (r = y; r < y + h; r++) {
        if (r < y0 || r >= y0 + 30)
            continue;
        for (i = x; i < x + w && i < PLAT_W; i++)
            if (i >= 0)
                strip[(r - y0) * PLAT_W + i] = c;
    }
}

static void frame(void)
{
    plat_cpu_t cpu;
    int y0, k;
    plat_cpu(&cpu, 0);
    for (y0 = 0; y0 < PLAT_H; y0 += 30) {
        rect(y0, 0, y0, PLAT_W, 30, RGB(8, 12, 16));
        rect(y0, 4, 4, 232, 6, RGB(132, 232, 196));                    /* a mint line: it draws */
        for (k = 0; k < 27; k++)                                        /* the note keys */
            rect(y0, 4 + (k % 14) * 16, 20 + (k / 14) * 22, 14, 18,
                 (in.notes >> k) & 1u ? RGB(244, 244, 236) : RGB(42, 59, 54));
        for (k = 0; k < B_COUNT; k++)                                   /* the buttons */
            rect(y0, 4 + k * 16, 70, 14, 18, (in.buttons >> k) & 1u ? RGB(240, 224, 116) : RGB(42, 46, 51));
        for (k = 0; k < K_COUNT; k++) {                                 /* the knobs: their clicks */
            int p = (int)(((knob_pos[k] % 232) + 232) % 232);
            rect(y0, 4, 100 + k * 14, 232, 10, RGB(28, 31, 35));
            rect(y0, 4 + p, 100 + k * 14, 4, 10, RGB(140, 154, 248));
        }
        rect(y0, 4, 202, 232, 8, RGB(28, 31, 35));                    /* MASTER */
        rect(y0, 4, 202, (int)(in.master * 232u / 1023u), 8, RGB(116, 212, 131));
        rect(y0, 4, 216, 232, 8, RGB(28, 31, 35));                    /* the audio's time a half */
        rect(y0, 4, 216, (int)(cpu.last_us * 232u / (cpu.half_us ? cpu.half_us : 1u)), 8, RGB(234, 164, 88));
        rect(y0, 4, 230, (int)(cpu.late > 58u ? 232u : cpu.late * 4u), 6, RGB(248, 124, 116));
        rect(y0, 236 - 8, 216, 8, 8, mono ? RGB(240, 224, 116) : RGB(28, 31, 35));
        plat_lcd_rows(y0, 30, strip);
    }
}

void app_init(void)
{
    plat_output(0, 0);
    frame();
}

void app_poll(void)
{
    plat_input_t now;
    uint32_t pressed;
    int k, id;
    plat_input(&now);
    pressed = now.buttons & ~in.buttons;
    for (k = 0; k < K_COUNT; k++)
        knob_pos[k] += now.knob[k];
    if (now.buttons & (1u << B_HOME)) {
        if (pressed & (1u << B_REC))
            mono = !mono;
        if (pressed & (1u << B_OCTM))
            gain_db = gain_db - 3 < -24 ? -24 : gain_db - 3;
        if (pressed & (1u << B_OCTP))
            gain_db = gain_db + 3 > 12 ? 12 : gain_db + 3;
        plat_output(mono, gain_db);
    }
    in = now;
    for (id = 0; id < B_COUNT; id++)
        plat_led(id, (in.buttons >> id) & 1u ? 255 : 64);
    for (k = 0; k < 27; k++)
        plat_led(LED_NOTE(k), (in.notes >> k) & 1u ? 255 : 0);
    plat_led(LED_PLAY_G, in.notes ? 255 : 0);
    if (plat_screen_lost() || plat_ms() - t_frame >= 50u) {
        t_frame = plat_ms();
        frame();
    }
}

void app_sysex(const uint8_t *msg, uint32_t n)
{
    plat_sysex_send(msg, n);             /* back as it came */
}
