/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Host test of the update loader's screen (fw/loader/ldr_screen.c): each screen drawn into a 240x240 frame as the
 * panel would show it, saved as a PPM to look at, and its bar checked against the share it says.
 *   ldr_screen_test OUTDIR   (ldr_font.h from fw/build.sh's build/fw/loader on the include path) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../stub/stub_core.h"
#include "ldr_font.h"

static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    for (uint32_t j = y; j < y + h && j < 240; j++)
        for (uint32_t i = x; i < x + w && i < 240; i++)
            fb[j * 240 + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)   /* (pixels byte-swapped) */
{
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++) {
            uint16_t p = px[j * w + i];
            fb[(y + j) * 240 + x + i] = (uint16_t)((p >> 8) | (p << 8));
        }
}
#include "../loader/ldr_screen.c"

static const char *dir;
static void save(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static uint32_t bar_px(void)                          /* the bar's cyan pixels on its middle row */
{
    uint32_t n = 0;
    for (uint32_t x = 0; x < 240; x++) n += fb[(BAR_Y + BAR_H / 2) * 240 + x] == LDR_CYAN;
    return n;
}
static int lit(uint32_t y0, uint32_t y1)              /* any pixel not black in rows y0..y1 */
{
    for (uint32_t i = y0 * 240; i < y1 * 240; i++) if (fb[i]) return 1;
    return 0;
}
static int check(const char *what, int ok) { printf("%-60s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

int main(int argc, char **argv)
{
    int bad = 0;
    dir = argc > 1 ? argv[1] : ".";
    ldr_connecting();
    save("ldr_connecting");
    bad += check("CONNECTING: a title and a line, no bar", lit(40, 72) && lit(100, 116) && bar_px() == 0);
    ldr_update_mode(STUB_W_OCT);
    save("ldr_update_oct");
    bad += check("UPDATE MODE (OCT- held): the title, two lines, why", lit(40, 72) && lit(110, 146) && lit(176, 192));
    ldr_update_mode(STUB_W_REQ);
    save("ldr_update_req");
    ldr_update_mode(STUB_W_BODY);
    save("ldr_update_body");
    ldr_installing();
    ldr_shown(1, 48, 143);
    save("ldr_checking");
    bad += check("CHECKING 33%: the bar a third full", bar_px() == 48u * (BAR_W - 4u) / 143u);
    ldr_shown(2, 1, 143);
    bad += check("WRITING: the bar starts again", bar_px() == 1u * (BAR_W - 4u) / 143u);
    ldr_shown(2, 109, 143);
    save("ldr_writing");
    bad += check("WRITING 76%: the bar three quarters", bar_px() == 109u * (BAR_W - 4u) / 143u);
    ldr_shown(2, 143, 143);
    ldr_ended(0);
    save("ldr_done");
    bad += check("DONE: the bar full", bar_px() == BAR_W - 4u);
    ldr_installing();
    ldr_shown(2, 20, 143);
    ldr_ended(-12);
    save("ldr_stopped");
    ldr_installing();
    ldr_ended(-14);
    save("ldr_flash");
    bad += check("UNKNOWN FLASH: its line fits the screen", lit(100, 116));
    printf("%s\n", bad ? "LOADER SCREEN TEST FAILED" : "loader screen test passed");
    return bad != 0;
}
