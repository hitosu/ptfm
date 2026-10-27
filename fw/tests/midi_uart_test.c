/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The MIDI IN jack's parser (fw/device/midi_uart.c) on the PC: bytes laid into its DMA ring as the UART
 * would, read by content, out as USB-MIDI packets on cable 1: running status, realtime in the middle of a
 * message, the song position, SysEx dropped (and running status with it), one-byte messages, line noise. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MQ 64u
static uint32_t midi_in_q[MQ];
static volatile uint32_t mi_w, mi_r;
#define RING_PUBLISH() ((void)0)
#include "../device/midi_uart.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

/* the bytes, as the DMA lands them in the ring, then a poll */
static uint32_t wr;
static void feed(const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) { um_ring[wr] = b[i]; wr = (wr + 1u) & (UM_RING - 1u); }
    uart_midi_poll();
}
static int got(const uint32_t *want, int n)
{
    int ok = (int)(mi_w - mi_r) == n;
    for (int i = 0; ok && i < n; i++) ok = midi_in_q[(mi_r + (uint32_t)i) % MQ] == want[i];
    if (!ok) {
        printf("     got:");
        for (uint32_t i = mi_r; i != mi_w; i++) printf(" %08X", midi_in_q[i % MQ]);
        printf("\n");
    }
    mi_r = mi_w;
    return ok;
}
#define PKT(cin, a, b, c) (0x10u | (cin) | (uint32_t)(a) << 8 | (uint32_t)(b) << 16 | (uint32_t)(c) << 24)

int main(void)
{
    uart_midi_init();
    { static const uint8_t b[] = {0x90, 0x3C, 0x64, 0x3E, 0x64, 0x40, 0x00};
      static const uint32_t w[] = {PKT(9, 0x90, 0x3C, 0x64), PKT(9, 0x90, 0x3E, 0x64), PKT(9, 0x90, 0x40, 0x00)};
      feed(b, sizeof b); check("running status: three notes from one status", got(w, 3)); }
    { static const uint8_t b[] = {0x91, 0x3C, 0xF8, 0x64};
      static const uint32_t w[] = {PKT(0xF, 0xF8, 0, 0), PKT(9, 0x91, 0x3C, 0x64)};
      feed(b, sizeof b); check("a clock in the middle of a note: the clock first, the note whole", got(w, 2)); }
    { static const uint8_t b[] = {0xFA, 0xFB, 0xFC, 0xFE};
      static const uint32_t w[] = {PKT(0xF, 0xFA, 0, 0), PKT(0xF, 0xFB, 0, 0), PKT(0xF, 0xFC, 0, 0)};
      feed(b, sizeof b); check("START, CONTINUE, STOP; active sensing dropped", got(w, 3)); }
    { static const uint8_t b[] = {0xF2, 0x10, 0x01, 0x3C, 0x64};
      static const uint32_t w[] = {PKT(3, 0xF2, 0x10, 0x01)};
      feed(b, sizeof b); check("the song position; no running status after it", got(w, 1)); }
    { static const uint8_t b[] = {0x92, 0x3C, 0x64, 0xF0, 0x7D, 0x50, 0x54, 0x01, 0xF7, 0x3E, 0x64, 0x82, 0x3C, 0x00};
      static const uint32_t w[] = {PKT(9, 0x92, 0x3C, 0x64), PKT(8, 0x82, 0x3C, 0x00)};
      feed(b, sizeof b); check("SysEx dropped, and running status with it", got(w, 2)); }
    { static const uint8_t b[] = {0xC3, 0x05, 0x06, 0xD3, 0x40};
      static const uint32_t w[] = {PKT(0xC, 0xC3, 0x05, 0), PKT(0xC, 0xC3, 0x06, 0), PKT(0xD, 0xD3, 0x40, 0)};
      feed(b, sizeof b); check("program change and pressure: one data byte (running status too)", got(w, 3)); }
    { static const uint8_t b[] = {0xB0, 0x40, 0x7F, 0xE0, 0x00, 0x40};
      static const uint32_t w[] = {PKT(0xB, 0xB0, 0x40, 0x7F), PKT(0xE, 0xE0, 0x00, 0x40)};
      feed(b, sizeof b); check("a control change, a pitch bend", got(w, 2)); }
    {   /* a byte the DMA has written, the count not yet: read by content, nothing lost */
        static const uint8_t b[] = {0x94, 0x30};
        static const uint32_t w[] = {PKT(9, 0x94, 0x30, 0x50)};
        feed(b, sizeof b);
        um_ring[wr] = 0x50; wr = (wr + 1u) & (UM_RING - 1u);
        uart_midi_poll();
        check("a message across two polls", got(w, 1));
    }
    {   /* around the ring's end: 200 notes */
        int ok = 1;
        for (int i = 0; i < 200 && ok; i++) {
            uint8_t b[3] = {0x95, (uint8_t)(i & 127), 0x40};
            uint32_t w[1] = {PKT(9, 0x95, i & 127, 0x40)};
            feed(b, 3);
            ok = got(w, 1);
        }
        check("200 notes round the ring", ok);
    }
    printf(fails ? "midi_uart_test: %d FAILED\n" : "midi_uart_test: all passed\n", fails);
    return fails ? 1 : 0;
}
