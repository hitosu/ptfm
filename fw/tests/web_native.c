/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The emulator's C (emulator/plat_web.c with fw/core) built natively and run through a session
 * (web_session.txt), as web_test.js runs it on the WebAssembly: the two print the same lines when the
 * same C does the same in both (specs/emulator.md, Testing). A fake clock (each read 1 us later) makes
 * the CPU figures the same too.
 *
 *   web_native SESSION FLASH OUT     FLASH: a 1 MiB image to start from, or "-" (a new FM-1);
 *                                    OUT_<n>.ppm: the screens at the session's shots */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double fake_us;
double web_clock_us(void) { return fake_us += 1.0; }

/* plat_web.c's calls (WEB_NATIVE: no exports) */
uint8_t *web_flash_buf(void);
uint16_t *web_fb_buf(void);
uint8_t *web_leds_buf(void);
int32_t *web_out_buf(void);
uint32_t web_now_ms(void);
void web_flash_reset(void);
void web_init(void);
void web_render(uint32_t n);
void web_button(uint32_t id, uint32_t down);
void web_note(uint32_t n, uint32_t down);
void web_knob(uint32_t id, int32_t step);
void web_master_set(int32_t v);

static uint32_t fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

static void ppm(const char *out, int n)
{
    char path[512];
    const uint16_t *fb = web_fb_buf();
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s_%d.ppm", out, n);
    f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint32_t c = fb[i];
        fputc((int)((c >> 11) * 255u / 31u), f);
        fputc((int)(((c >> 5) & 63u) * 255u / 63u), f);
        fputc((int)((c & 31u) * 255u / 31u), f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    FILE *s, *f;
    char line[256], cmd[32];
    uint32_t audio = 2166136261u, frames = 0, shot = 0;
    int32_t peak = 0;
    if (argc != 4) {
        fprintf(stderr, "usage: web_native SESSION FLASH|- OUT\n");
        return 2;
    }
    web_flash_reset();
    if (strcmp(argv[2], "-")) {
        f = fopen(argv[2], "rb");
        if (!f || fread(web_flash_buf(), 1, 1u << 20, f) != 1u << 20) {
            fprintf(stderr, "%s: not a 1 MiB flash image\n", argv[2]);
            return 2;
        }
        fclose(f);
    }
    s = fopen(argv[1], "r");
    if (!s) {
        perror(argv[1]);
        return 2;
    }
    web_init();
    printf("power on\n");
    while (fgets(line, sizeof line, s)) {
        long at;
        int a = 0, b = 0;
        if (line[0] == '#' || sscanf(line, "%ld %31s %d %d", &at, cmd, &a, &b) < 2)
            continue;
        while ((uint64_t)frames * 1000u < (uint64_t)at * 44100u) {   /* the worklet's quanta of 128 frames */
            const int32_t *o = web_out_buf();
            int k;
            web_render(128);
            audio = fnv(audio, o, 128u * 8u);
            for (k = 0; k < 256; k++)
                if ((o[k] < 0 ? -o[k] : o[k]) > peak)
                    peak = o[k] < 0 ? -o[k] : o[k];
            frames += 128u;
        }
        if (!strcmp(cmd, "btn"))
            web_button((uint32_t)a, (uint32_t)b);
        else if (!strcmp(cmd, "note"))
            web_note((uint32_t)a, (uint32_t)b);
        else if (!strcmp(cmd, "knob"))
            web_knob((uint32_t)a, b);
        else if (!strcmp(cmd, "master"))
            web_master_set(a);
        else if (!strcmp(cmd, "shot")) {
            const uint8_t *leds = web_leds_buf();
            int i;
            printf("shot %u at %ld ms (device %u): audio %08x peak %d screen %08x leds ", shot, at, web_now_ms(),
                   audio, (int)peak, fnv(2166136261u, web_fb_buf(), 240u * 240u * 2u));
            peak = 0;
            for (i = 0; i < 42; i++)
                printf("%02x", leds[i]);
            printf("\n");
            ppm(argv[3], (int)shot++);
        }
    }
    fclose(s);
    return 0;
}
