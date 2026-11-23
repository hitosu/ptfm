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
    uint32_t bus[8];                    /* the debug unit's culprit registers (fm1_irq.h FM1_DBG_ID): the write limits
                                         * core 1, core 2 and the DMAs hit, a bit a window (fm1_guard.h: 0-3); the DMA
                                         * that hit one; the bus masters of an invalid AXI read and write; the DMAs of
                                         * an MMU write and read error */
    uint32_t etm[4];                    /* the last branches traced */
    uint32_t usp, ssp, psr, icfg;       /* both stack pointers, PSR, ICFG as the fault found them */
    uint32_t up_s, late;                /* seconds since the start; halves late on core 1 */
    uint8_t stack;                      /* the stack check: 0 none, 1 the main loop's, 2 the interrupts' */
    uint8_t c2, c2_why;                 /* core 2: CS_C2_*; stopped, why (fm1_cpu1.h's FM1_C1_WHY_*) */
    uint8_t c2_pc_ok;                   /* c2_pc read: running, where the crash paused it; stopped by its own fault,
                                         * that fault's PC */
    uint32_t c2_pc;
    uint32_t c2_rets;                   /* stopped by its own fault: that fault's RETS (0: not known) */
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

/* CORE 2 FAULTED, at a start: core 2's own fault record from an earlier run, not shown yet (main.c's c2_fault_check):
 * its words named, two a line, then when it came (the run's seconds up) and the faults counted since the power came on */
typedef struct {
    uint32_t vec, pc, emu, dbg, rets, sp, up_s, count;
    uint32_t bad_d, bad_n;              /* a fill or copy refused (vec 0x100-0x103): its destination and length */
} c2_fault_info_t;
static CS_CALL void c2_fault_screen(const c2_fault_info_t *x)
{
    char s[48];
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    CS_DRAW(0, 6, 240, &FONT_S, "CORE 2 FAULTED", C_WHITE, 1);
    if (x->vec >= 0x100u && x->vec <= 0x103u) {   /* (a fill or copy refused, fm1_cpu1.h: no fault, no frame) */
        CS_DRAW(0, 26, 240, &FONT_S, x->vec & 1u ? "A BAD COPY STOPPED IT" : "A BAD FILL STOPPED IT", C_WHITE, 1);
        cs_pair(43, "TO   ", x->bad_d, "LEN  ", x->bad_n);
        cs_pair(60, "FROM ", x->pc, "DBG  ", x->dbg);
        cs_dec(cs_put(cs_dec(cs_put(s, "UP "), x->up_s % 10000000u), " S  FAULT "), x->count % 1000000u);
        cs_small(82, s);
        cs_small(94, x->vec & 2u ? "ON CORE 1 (CORE 2 OFF); NOT WRITTEN" : "NOT WRITTEN; CORE 1 WENT ON ALONE");
        cs_small(106, "FROM AN EARLIER RUN, SHOWN ONCE");
        return;
    }
    cs_pair(26, "VEC  ", x->vec, "PC   ", x->pc);
    cs_pair(43, "EMU  ", x->emu, "DBG  ", x->dbg);
    cs_pair(60, "RETS ", x->rets, "SP   ", x->sp);
    cs_dec(cs_put(cs_dec(cs_put(s, "UP "), x->up_s % 10000000u), " S  FAULT "), x->count % 1000000u);
    cs_small(82, s);
    cs_small(94, "FROM AN EARLIER RUN, SHOWN ONCE");
}

/* PT/FM CRASH: the record's words named, two a line, both stack pointers among them; then in the smaller font, a row
 * each 12 px: the culprit registers (two rows: the write limits hit by core 1, core 2 and the DMAs, core 1's window
 * named when it is one, and the DMA that hit one; the bus masters of a bad AXI write and read, the DMAs of an MMU
 * write and read error), the branches traced (two rows), PSR and ICFG (the interrupt it ran in), the time up and the
 * late halves, core 2 as the crash found it (where it was, paused), the stack check's line when it found one past its
 * end. Every line keeps 8 px clear of the screen's sides (the host test checks) */
static CS_CALL void crash_screen(const crash_info_t *x)
{
    static const char *const WR[4] = {"MAIN LOOP BAND", "INTERRUPT BAND", "TOP (VECTORS)", "CORE 2'S CODE"};
    static const char *const WHY[5] = {"", "NO ANSWER", "MISREAD", "STUCK", "FAULT"};
    uint32_t i, wr = x->bus[0] & 0xFFu;
    char s[44], *p;
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    CS_DRAW(0, 6, 240, &FONT_S, "PT/FM CRASH", C_WHITE, 1);
    cs_pair(26, "VEC  ", x->vec, "PC   ", x->pc);
    cs_pair(43, "EMU  ", x->emu, "DBG  ", x->dbg);
    cs_pair(60, "RETS ", x->rets, "SP   ", x->sp);
    cs_pair(77, "USP  ", x->usp, "SSP  ", x->ssp);
    p = cs_put(s, "WR");                 /* `WR 02 00 00  DMA 0000  INTERRUPT BAND` */
    for (i = 0; i < 3u; i++)
        p = cs_hex(cs_put(p, " "), x->bus[i] & 0xFFu, 2u);
    p = cs_hex(cs_put(p, "  DMA "), x->bus[3], 4u);
    for (i = 0; i < 4u; i++)
        if (wr == 1u << i)
            cs_put(cs_put(p, "  "), WR[i]);
    cs_small(94, s);
    p = cs_hex(cs_put(cs_hex(cs_put(s, "AXI W "), x->bus[5], 4u), " R "), x->bus[4], 4u);   /* `AXI W 0100 R 0000  MMU` */
    cs_hex(cs_put(cs_hex(cs_put(p, "  MMU W "), x->bus[6], 4u), " R "), x->bus[7], 4u);    /* `W 0000 R 0000` */
    cs_small(106, s);
    for (i = 0; i < 4u; i += 2u) {       /* (two branches a row, the second row under the first's) */
        p = cs_hex(cs_put(s, i ? "    " : "ETM "), x->etm[i], 8u);
        cs_hex(cs_put(p, " "), x->etm[i + 1u], 8u);
        cs_small(118u + 12u * (i / 2u), s);
    }
    cs_hex(cs_put(cs_hex(cs_put(s, "PSR "), x->psr, 8u), "  ICFG "), x->icfg, 8u);
    cs_small(142, s);
    cs_dec(cs_put(cs_put(cs_dec(cs_put(s, "UP "), x->up_s), " S"), "  LATE "), x->late);
    cs_small(154, s);
    if (x->c2 == CS_C2_BANNED)
        p = cs_put(s, "CORE 2 OFF: AFTER A CRASH");
    else if (x->c2 == CS_C2_RUNS)
        p = cs_put(s, "CORE 2 RUNNING");
    else if (x->c2 == CS_C2_OFF)
        p = cs_put(s, "CORE 2 OFF");
    else if (x->c2_why == 4u && x->c2_pc_ok && x->c2_rets)   /* (its own fault: its record's PC and RETS) */
        p = cs_hex(cs_put(cs_hex(cs_put(s, "CORE 2 FAULT AT "), x->c2_pc, 8u), " R "), x->c2_rets, 8u);
    else
        p = cs_put(cs_put(s, "CORE 2 STOPPED: "), x->c2_why < 5u ? WHY[x->c2_why] : "?");
    if (x->c2_pc_ok && (x->c2 == CS_C2_RUNS || (x->c2 == CS_C2_STOPPED && !(x->c2_why == 4u && x->c2_rets))))
        cs_hex(cs_put(p, " AT "), x->c2_pc, 8u);
    cs_small(166, s);
    if (x->stack)
        cs_small(178, x->stack == 2u ? "STACK: INTERRUPTS" : "STACK: MAIN LOOP");
}

/* PT/FM FROZE, at the start after a watchdog restart (main.c's freeze record): where the main loop stood, when its
 * last pass came, how the audio and input interrupts went on after it (and whether either was inside at the end),
 * core 2's queue. Blue, not red: nothing crashed, the FM-1 has restarted already */
typedef struct {
    const char *step;                   /* the main loop's step, named */
    uint32_t arg;                       /* a flash step's address (flash: shown) */
    uint8_t flash;
    uint32_t pass_ms;                   /* ms since the start at the main loop's last pass */
    uint32_t halves, ticks;             /* audio halves and input ticks begun after it */
    uint8_t in_audio, in_input;         /* inside either at the end */
    uint8_t c2;                         /* core 2 was running */
    uint32_t c2_posted, c2_done;        /* its queue: the last entry handed over, the last it ran */
    const char *astep;                  /* inside the audio interrupt at the end: where, named (0: not known) */
    uint8_t aread;                      /* ... a flash read: aarg its address */
    uint32_t aarg;
    uint8_t crash;                      /* a crash began in that run (its handler never drew): its VEC and PC */
    uint32_t crash_vec, crash_pc;
    const char *part;                   /* inside a Juno-106 voice's render: its part, named (0: not known), */
    uint32_t part_v, part_p;            /* ... the voice and its patch as the render began */
    uint8_t c2_why;                     /* core 2 stopped by a fallback in that run: why (fm1_cpu1.h's FM1_C1_WHY_*; 0
                                         * never), */
    uint32_t c2_s, c2_pc;               /* ... when (s), where it stood (0: not read) */
    uint16_t vdc14_mv, sysvdd_mv;       /* the core supplies as this start raised them */
    uint8_t raised;                     /* ... to the stock levels */
} freeze_info_t;
/* the small font's rows from y 98, 12 px each, the last at 170 (the version's row starts at 191) */
static CS_CALL uint32_t cs_row(uint32_t y, const char *s)
{
    if (y <= 170u)
        cs_small(y, s);
    return y + 12u;
}
static CS_CALL void freeze_screen(const freeze_info_t *x)
{
    static const char *const WHY[5] = {"", "NO ANSWER", "MISREAD", "STUCK", "FAULT"};
    char s[44], *p;
    uint32_t y = 98;
    lcd_fill(0, 0, 240, 240, RGB(0, 0, 140));
    CS_DRAW(0, 6, 240, &FONT_S, "PT/FM FROZE", C_WHITE, 1);
    CS_DRAW(0, 26, 240, &FONT_T, "THE WATCHDOG RESTARTED IT", C_WHITE, 1);
    CS_DRAW(0, 46, 240, &FONT_S, "THE MAIN LOOP STOOD AT", C_WHITE, 1);
    CS_DRAW(0, 63, 240, &FONT_S, x->step, C_WHITE, 1);
    if (x->flash) {
        cs_hex(cs_put(s, "ADDRESS "), x->arg, 8u);
        CS_DRAW(0, 80, 240, &FONT_S, s, C_WHITE, 1);
    }
    p = cs_dec(cs_put(s, "ITS LAST PASS AT "), x->pass_ms / 1000u);
    p = cs_put(p, ".");
    p[0] = (char)('0' + x->pass_ms % 1000u / 100u);
    p[1] = (char)('0' + x->pass_ms % 100u / 10u);
    p[2] = (char)('0' + x->pass_ms % 10u);
    cs_put(p + 3, " S");
    y = cs_row(y, s);
    cs_put(cs_dec(cs_put(s, "AUDIO: "), x->halves), x->in_audio ? " HALVES AFTER, IN" : " HALVES AFTER, OUT");
    y = cs_row(y, s);
    cs_put(cs_dec(cs_put(s, "INPUT: "), x->ticks), x->in_input ? " TICKS AFTER, IN" : " TICKS AFTER, OUT");
    y = cs_row(y, s);
    if (x->c2)
        cs_hex(cs_put(cs_hex(cs_put(s, "CORE 2 POSTED "), x->c2_posted, 8u), " DONE "), x->c2_done, 8u);
    else if (x->c2_why) {                /* (stopped earlier in that run: `CORE 2 STUCK AT 1234 S PC 01C78472`) */
        p = cs_put(cs_dec(cs_put(cs_put(cs_put(s, "CORE 2 "), x->c2_why < 5u ? WHY[x->c2_why] : "?"), " AT "),
                          x->c2_s % 100000u), " S");
        if (x->c2_pc)
            cs_hex(cs_put(p, " PC "), x->c2_pc, 8u);
    } else
        cs_put(s, "CORE 2 OFF");
    y = cs_row(y, s);
    if (x->in_audio && x->astep) {       /* (where in the audio interrupt: `IN THE AUDIO: A FLASH READ 000A3000`) */
        p = cs_put(cs_put(s, "IN THE AUDIO: "), x->astep);
        if (x->aread)
            cs_hex(cs_put(p, " "), x->aarg, 8u);
        y = cs_row(y, s);
        if (x->part) {                   /* (`FILTER 2/4 V 01C3A0B4 P 01C3A0A2`) */
            cs_hex(cs_put(cs_hex(cs_put(cs_put(s, x->part), " V "), x->part_v, 8u), " P "), x->part_p, 8u);
            y = cs_row(y, s);
        }
    }
    if (x->crash) {                      /* (a fault whose handler never drew: `A CRASH BEGAN: VEC 01 PC 0200B086`) */
        cs_hex(cs_put(cs_hex(cs_put(s, "A CRASH BEGAN: VEC "), x->crash_vec, 2u), " PC "), x->crash_pc, 8u);
        y = cs_row(y, s);
    }
    p = cs_put(cs_put(s, "SUPPLIES "), "0.00 V, 0.00 V");   /* (`SUPPLIES 1.60 V, 1.38 V`; not raised: said) */
    s[9] = (char)('0' + x->vdc14_mv / 1000u % 10u);
    s[11] = (char)('0' + x->vdc14_mv / 100u % 10u);
    s[12] = (char)('0' + x->vdc14_mv / 10u % 10u);
    s[17] = (char)('0' + x->sysvdd_mv / 1000u % 10u);
    s[19] = (char)('0' + x->sysvdd_mv / 100u % 10u);
    s[20] = (char)('0' + x->sysvdd_mv / 10u % 10u);
    if (!x->raised)
        cs_put(p, ": NOT RAISED");
    cs_row(y, s);
}

/* the lines both red screens (and PT/FM FROZE) end with: the version and the build's code, one row when they fit it
 * in the larger font, else two rows in the smaller (a debug build's stamp), then what to do */
static void crash_tail(const char *what)
{
    static const char vb[] = PTFM_VERSION "  BUILD " PTFM_BUILD;
    if (text_w(&FONT_S, vb) <= 224)
        CS_DRAW(0, 191, 240, &FONT_S, vb, C_WHITE, 1);
    else {
        CS_DRAW(0, 191, 240, &FONT_T, PTFM_VERSION, C_WHITE, 1);
        CS_DRAW(0, 204, 240, &FONT_T, "BUILD " PTFM_BUILD, C_WHITE, 1);
    }
    CS_DRAW(0, 220, 240, &FONT_S, what, C_WHITE, 1);
}
