/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The red screens' drawing (../../specs/hardware.md, Updates and rescue): PT/FM CRASH from what the fault found, and
 * the lines both red screens end with. main.c includes it after gfx.c and fills crash_info_t; the host test
 * (../tests/crash_screen_test.c) draws it on the PC, its CS_DRAW checking that every line fits its box. */
#ifndef PTFM_VERSION
#define PTFM_VERSION "DEV"               /* the version and the build's code (tools/build.py passes them; */
#endif
#ifndef PTFM_BUILD                       /* tools/buildcode.py reads the code back) */
#define PTFM_BUILD "--------"
#endif
#ifndef CS_DRAW
#define CS_DRAW draw_text_box
#endif

enum { CS_C2_RUNS, CS_C2_OFF, CS_C2_BANNED, CS_C2_STOPPED };
typedef struct {
    uint32_t vec, pc, emu, dbg, rets, sp;   /* the crash record's (fm1_irq.h) */
    uint32_t wr;                        /* the write limits core 1 hit, a bit a window (fm1_guard.h: 0-3) */
    uint32_t etm[4];                    /* the last branches traced */
    uint32_t usp, ssp, psr, icfg;       /* both stack pointers, PSR, ICFG as the fault found them */
    uint32_t up_s, late;                /* seconds since the start; halves late on core 1 */
    uint8_t stack;                      /* the stack check: 0 none, 1 the main loop's, 2 the interrupts' */
    uint8_t c2, c2_why;                 /* core 2: CS_C2_*; stopped, why (fm1_cpu1.h's FM1_C1_WHY_*) */
    uint8_t c2_pc_ok;                   /* stopped by its own fault: c2_pc its PC */
    uint32_t c2_pc;
} crash_info_t;

/* its lines put together: each returns the line's end (the small font's line is 39 characters at most). Calls, never
 * inlined: the screen is drawn once, its room counts */
#define CS_CALL __attribute__((noinline))
static CS_CALL char *cs_put(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}
static CS_CALL char *cs_hex(char *p, uint32_t v, uint32_t digits)
{
    uint32_t i;
    for (i = 0; i < digits; i++)
        p[i] = "0123456789ABCDEF"[(v >> (4u * (digits - 1u - i))) & 15u];
    p[digits] = 0;
    return p + digits;
}
static CS_CALL char *cs_dec(char *p, uint32_t v)
{
    char t[10];
    uint32_t n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *p++ = t[--n];
    *p = 0;
    return p;
}
/* two of the record's words a line, each named: `VEC  00000001  PC   0200A0F4` */
static void cs_pair(uint32_t y, const char *a, uint32_t va, const char *b, uint32_t vb)
{
    char s[32], *p = cs_hex(cs_put(s, a), va, 8u);
    cs_hex(cs_put(cs_put(p, "  "), b), vb, 8u);
    CS_DRAW(8, y, 224, &FONT_S, s, C_WHITE, 0);
}
static CS_CALL void cs_small(uint32_t y, const char *s) { CS_DRAW(8, y, 224, &FONT_T, s, C_WHITE, 0); }

/* PT/FM CRASH: the record's words named, two a line, both stack pointers among them; then in the smaller font the
 * write limit hit (named when it is one), the branches traced (two rows), PSR and ICFG (the interrupt it ran in), the
 * time up and the late halves, core 2 as the crash found it, the stack check's line when it found one past its end.
 * Every line keeps 8 px clear of the screen's sides (the host test checks) */
static CS_CALL void crash_screen(const crash_info_t *x)
{
    static const char *const WR[4] = {"MAIN LOOP'S STACK BAND", "INTERRUPTS' STACK BAND", "THE TOP (VECTORS)",
                                      "CORE 2'S CODE"};
    static const char *const WHY[5] = {"", "NO ANSWER", "MISREAD", "STUCK", "FAULT"};
    uint32_t i, wr = x->wr & 0xFFu;
    char s[44], *p;
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    CS_DRAW(0, 6, 240, &FONT_S, "PT/FM CRASH", C_WHITE, 1);
    cs_pair(26, "VEC  ", x->vec, "PC   ", x->pc);
    cs_pair(43, "EMU  ", x->emu, "DBG  ", x->dbg);
    cs_pair(60, "RETS ", x->rets, "SP   ", x->sp);
    cs_pair(77, "USP  ", x->usp, "SSP  ", x->ssp);
    p = cs_hex(cs_put(s, "WRITE "), wr, 2u);
    for (i = 0; i < 4u; i++)
        if (wr == 1u << i)
            cs_put(cs_put(p, ": "), WR[i]);
    cs_small(97, s);
    for (i = 0; i < 4u; i += 2u) {       /* (two branches a row, the second row under the first's) */
        p = cs_hex(cs_put(s, i ? "    " : "ETM "), x->etm[i], 8u);
        cs_hex(cs_put(p, " "), x->etm[i + 1u], 8u);
        cs_small(110u + 13u * (i / 2u), s);
    }
    cs_hex(cs_put(cs_hex(cs_put(s, "PSR "), x->psr, 8u), "  ICFG "), x->icfg, 8u);
    cs_small(136, s);
    cs_dec(cs_put(cs_put(cs_dec(cs_put(s, "UP "), x->up_s), " S"), "  LATE "), x->late);
    cs_small(149, s);
    if (x->c2 == CS_C2_BANNED)
        cs_put(s, "CORE 2 OFF: AFTER A CRASH");
    else if (x->c2 == CS_C2_RUNS)
        cs_put(s, "CORE 2 RUNNING");
    else if (x->c2 == CS_C2_OFF)
        cs_put(s, "CORE 2 OFF");
    else {
        p = cs_put(cs_put(s, "CORE 2 STOPPED: "), x->c2_why < 5u ? WHY[x->c2_why] : "?");
        if (x->c2_pc_ok)
            cs_hex(cs_put(p, " AT "), x->c2_pc, 8u);
    }
    cs_small(162, s);
    if (x->stack)
        cs_small(175, x->stack == 2u ? "STACK: INTERRUPTS" : "STACK: MAIN LOOP");
}

/* the lines both red screens end with: the version and the build's code, one row when they fit it in the larger font,
 * else two rows in the smaller (a debug build's stamp), then what to do */
static void crash_tail(void)
{
    static const char vb[] = PTFM_VERSION "  BUILD " PTFM_BUILD;
    if (text_w(&FONT_S, vb) <= 224)
        CS_DRAW(0, 191, 240, &FONT_S, vb, C_WHITE, 1);
    else {
        CS_DRAW(0, 191, 240, &FONT_T, PTFM_VERSION, C_WHITE, 1);
        CS_DRAW(0, 204, 240, &FONT_T, "BUILD " PTFM_BUILD, C_WHITE, 1);
    }
    CS_DRAW(0, 220, 240, &FONT_S, "PRESS A BUTTON TO RESTART", C_WHITE, 1);
}
