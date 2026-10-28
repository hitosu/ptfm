/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* STATUS (specs/ui/screens/status.md): the start screen PT/FM, PERFORMANCE, STORAGE, DEVICE. */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "../play/player.h"
#include "../play/ibxm/ibxm.h"
#include "../store/store.h"
#include "../fx/fx.h"
#include "../eng/dx7/dx7.h"
#include "../spare.h"

#ifndef PTFM_VERSION
#define PTFM_VERSION "DEV"              /* the build gives the stamp: `2026-10-12 03:00`, ` DEV` after it in a dev build */
#endif
/* the version's stamp, and whether it is a dev build (its ` DEV` drawn apart) */
static void version_parts(char *stamp, int *dev)
{
    const char *v = PTFM_VERSION;
    int n = (int)strlen(v);
    *dev = n >= 3 && !strcmp(v + n - 3, "DEV");
    if (*dev) n = n >= 4 && v[n - 4] == ' ' ? n - 4 : n - 3;
    memcpy(stamp, v, (size_t)n);
    stamp[n] = 0;
}

#define NPAGES 6
static const txt_t PAGES[NPAGES] = {TX("PT/FM"), TX("PERFORMANCE"), TX("STORAGE"), TX("USER FLASH MAP"), TX("RAM MAP"), TX("DEVICE")};
static int page;
/* the CPU, each core's mean over each half second: the last 30 s, and the average since the counts were zeroed; core
 * 2's sample NO_C2 while it did not run (no mark), and its average over the samples it ran in alone */
#define NO_C2 0xFF
static uint8_t graph[60], graph2[60];
static int ngraph;
static uint32_t sample_at, avg_sum, avg2_sum, avg_n, avg2_n, cpu_sum0, cpu_n0, cpu2_sum0, cpu2_n0;
/* the engines' voices on PERFORMANCE, counted again every 100 ms or a little more (specs/ui/screens/status.md) */
static int eng_count[PL_ENG_CHIP + 1];   /* by PL_ENG_* */
static uint32_t eng_at;
static uint32_t up0;

static int cpu_col(int v) { return v < 50 ? C_GREEN : v < 80 ? C_YELLOW : C_RED; }
static int free_col(uint32_t free, uint32_t size) { return free * 10 < size ? C_RED : free * 4 < size ? C_YELLOW : C_GREEN; }
static void kb(char *d, uint32_t b)
{
    if (b < 1024) ui_fmt(d, 12, "%u B", b);
    else if (b < 10240) { uint32_t t = (b * 10 + 512) / 1024; ui_fmt(d, 12, "%u.%uK", t / 10, t % 10); }
    else ui_fmt(d, 12, "%uK", (b + 512) / 1024);
}
static int pct(uint32_t us, uint32_t half) { return half ? (int)((us * 100 + half / 2) / half) : 0; }
static void sample(void)
{
    uint32_t now = ui_now();
    if (now - sample_at < 500) return;
    int gap = !sample_at || now - sample_at > 1000;     /* (STATUS not drawn meanwhile: the sums only taken) */
    sample_at = now;
    int v = ui_cpu_mean(&cpu_sum0, &cpu_n0), v2 = ui_cpu2_mean(&cpu2_sum0, &cpu2_n0);
    if (gap || v < 0) return;
    if (v > 100) v = 100;
    v2 = ui_clamp(v2, 0, 100);
    plat_cpu_t c;
    plat_cpu(&c, 0);
    if (ngraph == 60) { memmove(graph, graph + 1, 59); memmove(graph2, graph2 + 1, 59); ngraph--; }
    graph[ngraph] = (uint8_t)v;
    graph2[ngraph++] = c.c2_state == PLAT_C2_ON ? (uint8_t)v2 : NO_C2;
    avg_sum += (uint32_t)v;
    avg_n++;
    if (c.c2_state == PLAT_C2_ON) {
        avg2_sum += (uint32_t)v2;
        avg2_n++;
    }
}
/* a figure: its name cyan, its value right-aligned in w */
static void fig(int x, int y, const char *name, const char *value, int col, int w)
{
    g_text(x, y, &FONT_T, name, C_CYAN);
    g_text(x + w - g_tw(&FONT_T, value), y, &FONT_T, value, col);
}

static void enter(const ui_screen_t *from) { (void)from; if (!up0) up0 = 1; }
static const char *sub(void) { return T(PAGES[page]); }
static void knob(int e, int s, int coarse)
{
    (void)coarse;
    if (e == K_SELECT || e == K_ALG) page = ui_clamp(page + (s > 0 ? 1 : -1), 0, NPAGES - 1);
}
static void list_play(int song) { ui_cur.song = (uint8_t)song; ui_play_song((uint8_t)song, 0); ui_go_name("SONGS"); }
static int tap(int b)
{
    if (b == B_LFO) {
        plat_cpu_t c;
        plat_cpu(&c, 1);
        player_state_t st;
        player_state(&st, 1);
        avg_sum = avg2_sum = avg_n = avg2_n = 0;
        ui_toast(T("COUNTS ZEROED"));
        return 1;
    }
    if (b == B_ENV) {                                   /* the song list: PLAY plays the highlighted song */
        player_state_t st;
        ui_player_state(&st);
        ui_song_list(T("SONGS"), st.what == PL_SONG ? st.song : ui_cur.song, 0, list_play);
        return 1;
    }
    if (b == B_EDIT) { ui_go_name("SETTINGS"); return 1; }    /* (as HOME + MONO: a plain switch) */
    return 0;
}
static const char *footer(uint8_t *col)
{
    static char b[24];
    store_usage_t u;
    store_usage(&u);
    char k[12];
    kb(k, u.user_free);
    ui_fmt(b, sizeof b, "%s", ui_txt_v(T("FREE #"), k));
    *col = (uint8_t)free_col(u.user_free, u.user_bytes);
    return b;
}
static int kind_at(uint8_t kind) { for (int i = 0; i < STORE_NKINDS; i++) if (store_kinds[i] == kind) return i; return -1; }

static void draw_ptfm(void)
{
    int y0 = BODY_Y + 8;
    g_run_t name[5] = {{T("P"), C_LRED}, {T("T"), C_LCYAN}, {"/", C_YELLOW}, {T("F"), C_LGREEN}, {T("M"), C_LMAGENTA}};
    g_runs(g_centre(&FONT_L, T("PT/FM"), 0, 240), y0, &FONT_L, name, 5);
    const char *full = T("POCKET TRACKER / FUSION MACHINE");
    g_text(g_centre(&FONT_T, full, 0, 240), y0 + 34, &FONT_T, full, C_LGREY);
    char by[64], stamp[40], st2[44];
    int dev;
    version_parts(stamp, &dev);
    ui_fmt(st2, sizeof st2, "%s%s", stamp[0] ? "  " : "", stamp);
    ui_fmt(by, sizeof by, T("BY HITOSU%s%s"), st2, dev ? T(" DEV") : "");
    g_run_t r[4] = {{T("BY "), C_LGREY}, {T("HITOSU"), C_WHITE}, {st2, C_DGREY}, {dev ? T(" DEV") : "", C_YELLOW}};
    g_runs(g_centre(&FONT_T, by, 0, 240), y0 + 46, &FONT_T, r, 4);
    g_run_t hl[3] = {{T("PRESS "), C_LGREY}, {T("HOME + LFO"), C_WHITE}, {T(" FOR HELP"), C_LGREY}};
    g_runs(g_centre(&FONT_S, T("PRESS HOME + LFO FOR HELP"), 0, 240), y0 + 64, &FONT_S, hl, 3);
    int kx = (240 - 126) >> 1, ky = y0 + 82;
    g_text(kx, ky, &FONT_T, T("SELECT"), C_WHITE);
    g_updown(kx + 39, ky + 1, C_LGREY);
    g_text(kx + 60, ky, &FONT_T, T("ALGORITHM"), C_WHITE);
    g_leftright(kx + 117, ky + 3, C_LGREY);

    plat_cpu_t c;
    plat_cpu(&c, 0);
    player_state_t st;
    ui_player_state(&st);
    store_usage_t u;
    store_usage(&u);
    int y = y0 + 100, L = 0, R = 126;
    char a[16], b[16], k[12], e[12];
    for (int n = 0; n < 2; n++) {                       /* `CPU 1  23% MAX  41%`, then core 2's: `CPU 2  OFF` while it
                                                         * does not run (OFF in the share's 4 places) */
        const char *core = n ? T("CPU 2") : T("CPU 1");
        if (n && c.c2_state != PLAT_C2_ON) {
            g_run_t off[3] = {{core, C_CYAN}, {"  ", C_DGREY}, {T("OFF"), C_DGREY}};
            g_runs(R, y, &FONT_T, off, 3);
            break;
        }
        int cpu = n ? ui_cpu2_now() : ui_cpu_now(), mx = pct(n ? c.c2_max_us : c.max_us, c.half_us);
        ui_fmt(a, sizeof a, " %3d%% ", cpu);
        ui_fmt(b, sizeof b, " %3d%%", mx);
        g_run_t fr[4] = {{core, C_CYAN}, {a, (uint8_t)cpu_col(cpu)}, {T("MAX"), C_CYAN}, {b, (uint8_t)cpu_col(mx)}};
        g_runs(n ? R : L, y, &FONT_T, fr, 4);
    }
    ui_fmt(a, sizeof a, "%4u", c.late);
    g_run_t l1[2] = {{T("LATE "), C_CYAN}, {a, c.late ? C_RED : C_DGREY}};
    g_runs(L, y + 12, &FONT_T, l1, 2);
    ui_fmt(b, sizeof b, "%3u", st.steals);
    g_run_t r1[2] = {{T("STEALS "), C_CYAN}, {b, st.steals ? C_RED : C_DGREY}};
    g_runs(R, y + 12, &FONT_T, r1, 2);
    ui_fmt(a, sizeof a, "%02d/%02d", st.voices_short, st.voice_count);   /* (the most over the last ~0.1 s) */
    ui_fmt(b, sizeof b, "%02d", st.voices_max);
    g_run_t l2[4] = {{T("VOICES "), C_CYAN}, {a, (uint8_t)ui_voices_col(st.voices_short, st.voice_count)}, {T(" MAX "), C_CYAN},
                     {b, (uint8_t)ui_voices_col(st.voices_max, st.voice_count)}};
    g_runs(L, y + 24, &FONT_T, l2, 4);
    ui_fmt(a, sizeof a, "%4u US", c.max_us);            /* (core 1's longest audio interrupt; the frames: on DEVICE) */
    g_run_t r2[2] = {{T("HALF "), C_CYAN}, {a, (uint8_t)cpu_col(pct(c.max_us, c.half_us))}};
    g_runs(R, y + 24, &FONT_T, r2, 2);
    uint32_t ed = store_edit_used();                   /* (the edit memory: specs/files/ram.md) */
    kb(e, STORE_EDIT_MAX - ed);
    g_run_t l3[2] = {{T("EDITS FREE "), C_CYAN}, {e, (uint8_t)free_col(STORE_EDIT_MAX - ed, STORE_EDIT_MAX)}};
    g_runs(L, y + 36, &FONT_T, l3, 2);
    ui_fmt(b, sizeof b, "%u", ui_unsaved);
    g_run_t r3[2] = {{T("UNSAVED "), C_CYAN}, {b, ui_unsaved ? C_YELLOW : C_DGREY}};
    g_runs(R, y + 36, &FONT_T, r3, 2);
    kb(k, u.user_free);
    g_run_t l4[2] = {{T("FLASH FREE "), C_CYAN}, {k, (uint8_t)free_col(u.user_free, u.user_bytes)}};
    g_runs(L, y + 48, &FONT_T, l4, 2);
    int bat = plat_battery(), usb = bat < 0 || plat_usb_power(), bars = bat >= 67 ? 3 : bat >= 34 ? 2 : bat >= 10 ? 1 : 0;   /* (USB: charging, as the header's) */
    if (usb) ui_fmt(a, sizeof a, "%s", T("USB")); else ui_fmt(a, sizeof a, "%d/3", bars);
    g_run_t l5[2] = {{T("BATTERY "), C_CYAN}, {a, usb ? C_LBLUE : bars >= 2 ? C_GREEN : bars == 1 ? C_YELLOW : C_RED}};
    g_runs(L, y + 60, &FONT_T, l5, 2);
    uint32_t up = ui_now() / 1000;
    ui_fmt(b, sizeof b, "%02u:%02u:%02u", up / 3600, up / 60 % 60, up % 60);
    g_run_t r5[2] = {{T("UP "), C_CYAN}, {b, C_LGREY}};
    g_runs(R, y + 60, &FONT_T, r5, 2);
    ui_song_t s;
    int song = st.what == PL_SONG ? st.song : ui_cur.song;
    g_text(0, y + 82, &FONT_T, T("SONG"), C_CYAN);
    g_text(30, y + 80, &FONT_S, ui_song((uint8_t)song, &s) ? s.name : "-", C_WHITE);
}
static void draw_performance(void)
{
    plat_cpu_t c;
    plat_cpu(&c, 0);
    player_state_t st;
    ui_player_state(&st);
    int y = BODY_Y + 2, w = 116, mx = pct(c.max_us, c.half_us);
    char v[24];
    /* each core's row under the head: its share now, its average (the graph's samples since the counts were zeroed;
     * core 2's, those it ran in), its most (its longest half over a half's length); core 2 not running: OFF under
     * NOW, the rest blank */
    static const uint8_t HEAD_X[3] = {96, 156, 210};
    const char *head[3] = {T("NOW"), T("AVERAGE"), T("MOST")};
    for (int i = 0; i < 3; i++) g_text(HEAD_X[i] - g_tw(&FONT_T, head[i]), y, &FONT_T, head[i], C_CYAN);
    for (int n = 0; n < 2; n++) {
        int cy = y + 12 + 12 * n;
        g_text(0, cy, &FONT_T, n ? T("CPU 2") : T("CPU 1"), C_CYAN);
        if (n && c.c2_state != PLAT_C2_ON) {
            const char *off = T("OFF");
            g_text(HEAD_X[0] - g_tw(&FONT_T, off), cy, &FONT_T, off, C_DGREY);
            break;
        }
        uint32_t sum = n ? avg2_sum : avg_sum, cnt = n ? avg2_n : avg_n;
        int f3[3] = {n ? ui_cpu2_now() : ui_cpu_now(), cnt ? (int)((sum + cnt / 2) / cnt) : 0,
                     pct(n ? c.c2_max_us : c.max_us, c.half_us)};
        for (int i = 0; i < 3; i++) {
            ui_fmt(v, sizeof v, "%d%%", f3[i]);
            g_text(HEAD_X[i] - g_tw(&FONT_T, v), cy, &FONT_T, v, cpu_col(f3[i]));
        }
    }
    ui_fmt(v, sizeof v, "%u US", c.max_us); fig(0, y + 36, T("WORST HALF"), v, cpu_col(mx), w);
    ui_fmt(v, sizeof v, "%u", c.late); fig(124, y + 36, T("LATE"), v, c.late ? C_RED : C_DGREY, w);
    {   /* used / most / limit: the sounding count in its colour, the most and the limit in the most's */
        char vu[8], vm[12];
        ui_fmt(vu, sizeof vu, "%02d/", st.voices_short);
        ui_fmt(vm, sizeof vm, "%02d/%02d", st.voices_max, st.voice_count);
        ui_fmt(v, sizeof v, "%s%s", vu, vm);
        g_text(0, y + 48, &FONT_T, T("VOICES"), C_CYAN);
        g_run_t r[2] = {{vu, (uint8_t)ui_voices_col(st.voices_short, st.voice_count)},
                        {vm, (uint8_t)ui_voices_col(st.voices_max, st.voice_count)}};
        g_runs(w - g_tw(&FONT_T, v), y + 48, &FONT_T, r, 2);
    }
    ui_fmt(v, sizeof v, "%u", st.steals); fig(124, y + 48, T("STEALS"), v, st.steals ? C_RED : C_DGREY, w);
    /* the engines: voices x the share of one (CHIP's: a triangle at A-4, measured on the FM-1) */
    static const struct { uint8_t pl, eng; char cost[6]; } ENGS[5] = {
        {PL_ENG_DX7, ENG_DX7, "5.6%"}, {PL_ENG_JUNO, ENG_JUNO, "4.4%"}, {PL_ENG_TB303, ENG_TB303, "2.8%"},
        {PL_ENG_SAMPLER, ENG_SAMPLER, "1.6%"}, {PL_ENG_CHIP, ENG_CHIP, "2.5%"}};
    uint32_t now = ui_now();
    if (!eng_at || (int32_t)(now - eng_at) >= 100) {   /* (| 1: maybe 1 ms ahead) */
        pl_voice_info_t vi[PL_VOICES];
        int nv = ui_player_voices(vi, PL_VOICES);
        memset(eng_count, 0, sizeof eng_count);
        for (int i = 0; i < nv; i++) if (vi[i].on && vi[i].engine >= 1 && vi[i].engine <= PL_ENG_CHIP) eng_count[vi[i].engine]++;
        eng_at = now | 1;
    }
    for (int i = 0; i < 5; i++) {                       /* two a line: DX7 JUNO, TB-303 SAMPLE, CHIP */
        int n = eng_count[ENGS[i].pl], eng = ENGS[i].eng;
        ui_fmt(v, sizeof v, "%d X %s", n, ENGS[i].cost);
        fig((i % 2) * 124, y + 64 + (i / 2) * 12, T(ENG_SHORT[eng]), v, n ? ENG_COL[eng] : C_DGREY, w);
    }
    fig(124, y + 88, T("EFFECTS"), "0.0%", C_LGREY, w);
    int gy = y + 106, gh = 100;
    g_box(0, gy, 240, gh, C_DGREY);
    g_hline(1, gy + gh - 50, 238, C_DEEP + C_YELLOW);
    g_hline(1, gy + gh - 80, 238, C_DEEP + C_RED);
    for (int i = 0; i < ngraph; i++) {                  /* core 1's bars, core 2's marks (white) at its share's height */
        int h = (graph[i] * (gh - 2) + 50) / 100, x = 239 - ngraph * 2 + i * 2;
        g_vline(x, gy + gh - 1 - h, h, cpu_col(graph[i]));
        if (graph2[i] != NO_C2) g_rect(x, gy + gh - 1 - (graph2[i] * (gh - 2) + 50) / 100, 2, 1, C_WHITE);
    }
    g_text(2, gy + 2, &FONT_T, T("CPU, LAST 30 S"), C_DGREY);
}
static void bar(int y, const char *name, uint32_t size, uint32_t used, int col, int fcol)
{
    char f[16], k[12];
    g_text(0, y, &FONT_T, name, C_LGREY);
    g_box(60, y, 120, 10, C_DGREY);
    uint32_t w = size ? (used >= size ? 118 : used * 118 / size) : 0;
    g_rect(61, y + 1, (int)w, 8, col);
    kb(k, size > used ? size - used : 0);
    ui_fmt(f, sizeof f, "%s", ui_txt_v(T("# FREE"), k));
    g_text(186, y, &FONT_T, f, fcol);
}
static void draw_storage(void)
{
    store_usage_t u;
    store_usage(&u);
    int y = BODY_Y + 2, ip = kind_at(REC_PATTERN), is = kind_at(REC_SONG), ia = kind_at(REC_AUDIO);
    char v[16];
    g_text(0, y, &FONT_T, T("FLASH"), C_CYAN);
    /* (the user area alone: nothing of the user's is ever in FWS; the room kept for compaction and the EDITOR's own
     * pattern counted as used, as store_usage's free leaves them out) */
    bar(y + 14, T("USER"), u.user_bytes, u.user_bytes - u.user_free, C_GREEN, free_col(u.user_free, u.user_bytes));
    g_text(0, y + 32, &FONT_T, T("FILES BY TYPE"), C_CYAN);
    uint32_t inst = 0, carts = 0;
    for (int i = 0; i < STORE_NKINDS; i++)
        if (store_kinds[i] >= REC_DX7 && store_kinds[i] <= REC_CART_LAST) { inst += u.bytes[i]; carts += u.count[i]; }
    kb(v, ip >= 0 ? u.bytes[ip] : 0); fig(0, y + 46, T("PATTERNS"), v, C_LGREY, 116);
    fig(124, y + 46, T("COUNT"), ui_dec(ip >= 0 ? u.count[ip] : 0), C_LGREY, 116);
    kb(v, inst); fig(0, y + 58, T("INSTRUMENTS"), v, C_LGREY, 116);
    fig(124, y + 58, T("CARTRIDGES"), ui_dec((int)carts), C_LGREY, 116);
    kb(v, ia >= 0 ? u.bytes[ia] : 0); fig(0, y + 70, T("SAMPLES"), v, C_LGREY, 116);
    fig(124, y + 70, T("SONGS"), ui_dec(is >= 0 ? u.count[is] : 0), C_LGREY, 116);
    g_text(0, y + 88, &FONT_T, T("NOT SAVED"), C_CYAN);
    fig(0, y + 102, T("CHANGES"), ui_dec((int)ui_unsaved), ui_unsaved ? C_YELLOW : C_DGREY, 116);
    uint32_t used = store_edit_used();                  /* (the edit memory: specs/files/ram.md) */
    kb(v, used); fig(124, y + 102, T("THEIR RAM"), v, ui_unsaved ? C_YELLOW : C_DGREY, 116);
    kb(v, STORE_EDIT_MAX - used); fig(0, y + 114, T("EDITS FREE"), v, free_col(STORE_EDIT_MAX - used, STORE_EDIT_MAX), 116);
}

/* ---- USER FLASH MAP and RAM MAP: a 4 x 4 square a KB, 48 a line from (0, 33); a legend under it ---- */
#define MAP_Y (BODY_Y + 16)
static int map_square(int i, int col)
{
    g_rect((i % 48) * 5, MAP_Y + (i / 48) * 5, 4, 4, col);
    return MAP_Y + (i / 48 + 1) * 5;
}
static void legend(int x, int y, int col, const char *name, const char *size, int size_col, int right)
{
    g_rect(x, y + 3, 6, 6, col);
    g_text(x + 10, y, &FONT_T, name, C_LGREY);
    if (right) g_text(right - g_tw(&FONT_T, size), y, &FONT_T, size, size_col);
    else g_text(x + 10 + g_tw(&FONT_T, name) + 6, y, &FONT_T, size, size_col);
}
static uint32_t kind_bytes(const store_usage_t *u, uint8_t lo, uint8_t hi)
{
    uint32_t n = 0;
    for (int i = 0; i < STORE_NKINDS; i++) if (store_kinds[i] >= lo && store_kinds[i] <= hi) n += u->bytes[i];
    return n;
}
static void draw_flash_map(void)
{
    uint8_t *kb_at = ui_scratch;                        /* (the user area's KB: 412 of its 4 KB, while drawing) */
    static const uint8_t COL[8] = {C_DEEP + C_LGREY, C_DEEP + C_LGREY, C_YELLOW, C_LBLUE, C_CYAN, C_GREEN, C_LGREEN, C_LGREY};
    store_usage_t u;
    store_usage(&u);
    char v[32], k[12];
    kb(k, u.user_bytes);
    ui_fmt(v, sizeof v, "%s", ui_txt_v(T("FLASH #, A SQUARE 1K"), k));
    g_text(0, BODY_Y + 2, &FONT_T, v, C_CYAN);
    int n = store_kb_map(kb_at, UI_SCRATCH), nfree = 0, y = MAP_Y;
    for (int i = 0; i < n; i++) nfree += kb_at[i] == STORE_KB_FREE;
    for (int i = 0, seen = 0; i < n; i++)               /* (KEPT FOR SAVING: an amount, drawn as the last free KB) */
        y = map_square(i, kb_at[i] == STORE_KB_FREE && ++seen > nfree - 40 ? C_MAGENTA : COL[kb_at[i]]);
    y += 8;
    static const struct { uint8_t lo, hi, col; } KINDS[6] = {
        {REC_AUDIO, REC_AUDIO, C_YELLOW}, {REC_DX7, REC_CART_LAST, C_LBLUE}, {REC_PATTERN, REC_PATTERN, C_CYAN},
        {REC_SONG, REC_SONG, C_GREEN}, {REC_LOOPS, REC_LOOPS, C_LGREEN}, {REC_DJ, 0xFF, C_LGREY}};
    const char *name[6] = {T("SAMPLES"), T("INSTRUMENTS"), T("PATTERNS"), T("SONGS"), T("LOOPS"), T("OTHER")};
    for (int i = 0; i < 6; i++, y += 12) {
        kb(k, kind_bytes(&u, KINDS[i].lo, KINDS[i].hi));
        legend(0, y, KINDS[i].col, name[i], k, C_WHITE, 150);
    }
    kb(k, (STORE_MIN_FREE + STORE_EDITOR_ROOM) * STORE_BLOCK);
    legend(0, y, C_MAGENTA, T("KEPT FOR SAVING"), k, C_WHITE, 150);
    kb(k, u.user_free);                                 /* (the footer's figure) */
    legend(0, y + 12, C_DEEP + C_LGREY, T("FREE"), k, free_col(u.user_free, u.user_bytes), 150);
}

void ui_ram(ui_ram_t *r)
{
    plat_ram_t p;
    plat_ram(&p);
    memset(r, 0, sizeof *r);
    r->total = p.total;
    r->ram_text = p.ram_text;
    r->effects = fx_ram();
    r->edits = STORE_EDIT_MAX;
    r->edits_used = store_edit_used();
    r->screen = g_ram() + p.screen;
    r->dx7 = dx7_ram();
    r->sound = player_ram() + ibxm_arena_size();
    r->loops = 0;                                       /* (LOOPS has no RAM of its own: its changes are the edits') */
    r->pool_system = store_ram_index() + UI_SCRATCH;
    r->variables = p.variables;
    r->spare = spare_size();
    r->dj = spare_lent();
    uint32_t parts = r->effects + r->edits + r->screen + r->dx7 + r->sound + r->loops + r->variables + r->spare;
    r->system = r->total > parts ? r->total - parts : 0;
}
static void draw_ram_map(void)
{
    ui_ram_t r;
    ui_ram(&r);
    char v[32], k[12], k2[12];
    kb(k, r.total);
    ui_fmt(v, sizeof v, "%s", ui_txt_v(T("RAM #, A SQUARE 1K"), k));
    g_text(0, BODY_Y + 2, &FONT_T, v, C_CYAN);
    uint32_t sys_rest = r.system > r.ram_text + r.pool_system ? r.system - r.ram_text - r.pool_system : 0;
    const struct { uint32_t bytes; uint8_t col; } SEG[13] = {   /* in the order they lie */
        {r.ram_text, C_MAGENTA}, {r.effects, C_GREEN}, {r.edits_used, C_YELLOW},
        {r.edits - (r.edits_used < r.edits ? r.edits_used : r.edits), C_DEEP + C_YELLOW}, {r.screen, C_LBLUE},
        {r.dx7, C_LRED}, {r.sound, C_LCYAN}, {r.loops, C_LGREEN}, {r.pool_system, C_MAGENTA}, {r.variables, C_LGREY},
        {r.spare - (r.dj < r.spare ? r.dj : r.spare), C_DEEP + C_LGREY}, {r.dj < r.spare ? r.dj : r.spare, C_BROWN},
        {sys_rest, C_MAGENTA}};
    int y = MAP_Y, s = 0;
    uint32_t end = SEG[0].bytes;
    for (int i = 0; i * 1024u < r.total && i < 1024; i++) {   /* a square the part its KB's middle lies in */
        uint32_t mid = (uint32_t)i * 1024u + 512u;
        while (s < 12 && mid >= end) end += SEG[++s].bytes;
        y = map_square(i, SEG[s].col);
    }
    y += 8;
    kb(k, r.edits_used); kb(k2, r.edits);
    char ev[24];
    ui_fmt(ev, sizeof ev, "%s", ui_txt_v(ui_txt_v(T("# OF #"), k), k2));
    static const uint8_t COL[10] = {C_GREEN, C_YELLOW, C_LBLUE, C_LRED, C_LCYAN, C_LGREEN, C_LGREY, C_DEEP + C_LGREY,
                                    C_BROWN, C_MAGENTA};
    const uint32_t bytes[10] = {r.effects, 0, r.screen, r.dx7, r.sound, r.loops, r.variables, r.spare, r.dj, r.system};
    for (int c = 0; c < 2; c++) {                       /* (a column's names at once: T's buffers take 6) */
        const char *name[5];
        if (!c) { name[0] = T("EFFECTS"); name[1] = T("EDITS"); name[2] = T("SCREEN"); name[3] = T("DX7"); name[4] = T("SOUND"); }
        else { name[0] = T("LOOPS"); name[1] = T("VARIABLES"); name[2] = T("SPARE"); name[3] = T("DJ"); name[4] = T("SYSTEM"); }
        for (int i = 0; i < 5; i++) {
            int j = c * 5 + i;
            kb(k, bytes[j]);
            legend(c * 120, y + i * 12, COL[j], name[i], j == 1 ? ev : k, C_WHITE, 0);
        }
    }
    g_text(0, y + 5 * 12 + 4, &FONT_T, T("DIM: ITS ROOM NOT IN USE NOW"), C_DGREY);
}
/* DEVICE's SECOND CORE (specs/second-core.md#start-switch-fallback): its state, or why it stopped for the rest of this
 * start */
static const char *c2_text(const plat_cpu_t *c)
{
    if (c->c2_state == PLAT_C2_STARTING) return T("STARTING");
    if (c->c2_state == PLAT_C2_ON) return T("ON");
    if (c->c2_state == PLAT_C2_OFF) return T("OFF");
    switch (c->c2_why) {
    case PLAT_C2_WHY_NO_ANSWER: return T("STOPPED: NO ANSWER");
    case PLAT_C2_WHY_MISREAD: return T("STOPPED: MISREAD");
    case PLAT_C2_WHY_STUCK: return T("STOPPED: STUCK");
    case PLAT_C2_WHY_CRASH: return T("STOPPED: AFTER A CRASH");
    default: return T("STOPPED: FAULT");              /* (its fault record; a why not given counts as one) */
    }
}
static void draw_device(void)
{
    int y = BODY_Y + 2, bat = plat_battery(), usb = bat < 0 || plat_usb_power(), bars = bat >= 67 ? 3 : bat >= 34 ? 2 : bat >= 10 ? 1 : 0;
    char v[32];
    if (usb) ui_fmt(v, sizeof v, "%s", T("USB POWER")); else ui_fmt(v, sizeof v, "%d/3 BARS", bars);
    fig(0, y, T("BATTERY"), v, usb ? C_LBLUE : bars >= 2 ? C_GREEN : bars == 1 ? C_YELLOW : C_RED, 236);
    fig(0, y + 14, T("FIRMWARE"), PTFM_VERSION, C_LGREY, 236);
    char o[24];
    ui_setting_text(&UI_SETTINGS[S_OUTPUT], ui_set[S_OUTPUT], o);
    fig(0, y + 28, T("OUTPUT"), o, C_LGREY, 236);
    ui_fmt(v, sizeof v, "%d%%", (ui_master * 100 + 511) / 1023);
    fig(0, y + 42, T("MASTER"), v, C_LGREY, 236);
    ui_fmt(v, sizeof v, "%2u%s  %2u%s", ui_fps(), T(" FPS"), ui_frame_ms(), T(" MS"));
    fig(0, y + 56, T("SCREEN FRAME"), v, C_LGREY, 236);
    uint32_t up = ui_now() / 1000;
    ui_fmt(v, sizeof v, "%02u:%02u:%02u", up / 3600, up / 60 % 60, up % 60);
    fig(0, y + 70, T("UP"), v, C_LGREY, 236);
    ui_setting_text(&UI_SETTINGS[S_VOICES], ui_set[S_VOICES], o);
    fig(0, y + 84, T("VOICES"), o, C_LGREY, 236);
    fig(0, y + 98, T("THEME"), g_theme_name(ui_set[S_THEME]), C_LGREY, 236);
    plat_cpu_t c;
    plat_cpu(&c, 0);
    fig(0, y + 112, T("SECOND CORE"), c2_text(&c), c.c2_state == PLAT_C2_STOPPED ? C_RED : C_LGREY, 236);
}
static void draw(void)
{
    sample();
    switch (page) {
    case 0: draw_ptfm(); break;
    case 1: draw_performance(); break;
    case 2: draw_storage(); break;
    case 3: draw_flash_map(); break;
    case 4: draw_ram_map(); break;
    default: draw_device(); break;
    }
}

const ui_screen_t SCR_STATUS = {
    .name = TX("STATUS"), .group = G_HEAR,
    .does = BIT(B_LFO) | BIT(B_ENV) | BIT(B_EDIT),
    .enter = enter, .draw = draw, .sub = sub, .knob = knob, .tap = tap, .footer = footer,
};
