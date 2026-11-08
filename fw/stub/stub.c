/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The start check (stub_core.h; specs/hardware.md#updates-and-rescue): the slot's first code, run by the chip's boot
 * at every start, before the firmware. Interrupts off throughout, the watchdog armed (~8 s), the CPU at the boot's
 * clock. It reads the chip's reset reasons (a power-on sets the box anew: RAM can outlast a short power-off, measured
 * on the FM-1), OCT- (about 48 ms of scans: PA2 driven low first, the matrix reads only then), the firmware's request
 * in the box, the mark, then the body's CRC-32 in place (26.5 ms on the FM-1), and starts the firmware, the update
 * loader kept (its head read by a direct read: a RAM routine, copied to RAMTEXT first; at the flash's end, then in
 * the staging between the user area's regions), or the chip's own USB mode.
 *
 * Its bytes are the same in every build and pinned (stub.lock: build.py refuses others): it is built from this file,
 * stub_core.h, crt0_stub.S, stub.ld and the HAL headers below. A change to any of them that changes its bytes is a
 * change of the slot's first block, which the next install writes with nothing to fall back on for ~44 ms. Its RAM:
 * its stack and variables in the main loop's stack (cleared by nobody: the firmware sets it up again), RAMTEXT for
 * the read routine, the box; nothing else (the debug build reads the restart band, 0x01C07F00, as a restart left it). */
#include <stdint.h>
#define FL_READ_ONLY 1
#define FL_RANGE_OK(off, n) 0
#include "fm1_cc.h"
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_lcd_hw.h"
#include "fm1_input.h"
#include "fm1_flash.h"
#include "stub_core.h"

extern uint32_t _sbss[], _ebss[], _srt[], _ert[], _lrt[];

static int oct_held(void)
{
    uint32_t start = fm1_ticks();
    fm1_lcd_backlight_on();
    fm1_input_init();
    do
        fm1_input_scan();
    while ((uint32_t)(fm1_ticks() - start) < 48u * 1000u * FM1_TICKS_PER_US);
    return (fm1_in.buttons & 3u) == 1u;          /* OCT- alone (OCT+ with it: no) */
}

void stub_main(void)
{
    static const uint32_t PLACES[STUB_NPLACES] = STUB_LOADER_PLACES;
    volatile stub_box_t *BOX = fm1_stub_box();
    const uint8_t *mk = (const uint8_t *)STUB_MARK_XIP;
    uint32_t t[256], m[8], h[8], *p, *q, why = 0, out, i, t0, at = 0;
    int d, ldr = 0;
    for (p = _sbss; p < _ebss; p++)
        *p = 0;
    for (p = _srt, q = _lrt; p < _ert; p++, q++)
        *p = *q;
    fm1_reset_reason();                          /* (P33's reasons, read only: the firmware reads them again) */
    fm1_wdt_arm(0x0D);
    fm1_time_init();
    if (BOX->magic != STUB_BOX_MAGIC || stub_power_on(fm1_boot.p3_rst)) {   /* a power-on: the box anew */
        BOX->magic = STUB_BOX_MAGIC;
        BOX->req = BOX->tries = BOX->last = BOX->why = 0;
        BOX->starts = 0;
    }
    BOX->starts++;
    BOX->p3 = fm1_boot.p3_rst;                   /* (the firmware's boot guard reads them from here too) */
    BOX->check_us = 0;
    if (oct_held())
        why |= STUB_W_OCT;
    if (BOX->req) {
        why |= STUB_W_REQ;
        BOX->req = 0;
    }
    if (!why) {
        stub_crc_table(t);
        for (i = 0; i < 8u; i++)
            m[i] = stub_rd32(mk + 4u * i);
        if (!stub_mark_ok(t, (const uint8_t *)m)) {
            why |= STUB_W_MARK;
        } else {
            t0 = fm1_ticks();
            if (stub_crc32(t, 0, (const uint8_t *)STUB_APP_XIP, STUB_BODY_LEN) != m[2])
                why |= STUB_W_BODY;
            BOX->check_us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
        }
    }
    for (i = 0; why && !ldr && i < STUB_NPLACES; i++) {   /* the loader kept: the first whole head */
        at = PLACES[i];
        ldr = !FL_FAR(fl_read_ram)(at, (uint8_t *)h, 32u) && stub_loader_ok((const uint8_t *)h, at);
    }
    d = stub_decide(why, ldr, BOX->tries, &out);
    BOX->last = (uint8_t)d;
    BOX->why = (uint8_t)out;
    if (d == STUB_APP) {
        BOX->tries = 0;
        ((void (*)(void))STUB_APP_XIP)();        /* the firmware's own entry: it sets its stacks, never returns */
    }
    if (d == STUB_LOADER) {
        uint8_t parm[112];
        BOX->tries++;
        stub_parm(parm, at);
        fm1_enter_update(parm);                  /* the chip's boot runs the loader kept */
    }
    BOX->tries = 0;
    fm1_enter_uboot();                           /* the chip's own USB mode */
}
