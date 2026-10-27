/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev */
/* MIDI IN on the TRS jack: PH8 -> input channel 1 -> UART1 RX, 31250 baud,
 * RX DMA into a 128-byte ring, polled from the TIMER5 ISR (no UART IRQ).
 * Messages go into midi_in_q next to USB's, as USB-MIDI packets with cable 1
 * (the jack; USB's are cable 0): channel messages (CIN = status >> 4), the song
 * position (F2, CIN 3), clock and transport (F8 FA FB FC, CIN F); other
 * realtime, other system common and SysEx are dropped. (PT/FM: cable 1 for every
 * message, and the song position.)
 *
 * The ring is read by content, not by the DMA count: every slot the parser has
 * not yet been given holds UM_EMPTY, and the reader takes bytes until it meets
 * one. The count (HRXCNT, latched every poll) can miss a byte while the byte
 * itself is in the ring: the reader then stays one byte behind, so each message
 * ends only with the next byte (a note-off held until the next note). Following
 * the content cannot drift. (The missed byte showed on an FM-1 driven by an
 * Akai MPC Sample.) */
#include "fm1_uart.h"

#define UM_RING 128u
#define UM_EMPTY 0xFDu         /* undefined MIDI realtime byte: never sent, marks unwritten slots */
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));
static struct {
    uint32_t rd, bytes, drops, msgs;
    uint8_t st, need, got, d0, sysex;
} um;

static void uart_midi_init(void)                   /* before the TIMER5 scan starts: PORTH RMW */
{
    uint32_t i;
    for (i = 0; i < UM_RING; i++)
        um_ring[i] = UM_EMPTY;
    fm1_uart1_midi_init(um_ring, UM_RING);
}

static uint32_t um_len(uint32_t s)                 /* data bytes of a channel status */
{
    return (s & 0xE0u) == 0xC0u ? 1u : 2u;         /* Cx Dx: 1, else 2 */
}

static void um_put(uint32_t pkt)
{
    if (mi_w - mi_r < MQ) {
        midi_in_q[mi_w % MQ] = pkt;
        RING_PUBLISH();
        mi_w++;
        um.msgs++;
    } else {
        um.drops++;
    }
}

static void um_byte(uint32_t b)
{
    if (b >= 0xF8u) {                              /* realtime: the state is kept; clock and transport */
        if (b == 0xF8u || b == 0xFAu || b == 0xFBu || b == 0xFCu)
            um_put(0x1Fu | b << 8);
        return;
    }
    if (b & 0x80u) {
        um.sysex = b == 0xF0u;
        um.st = b < 0xF0u || b == 0xF2u ? (uint8_t)b : 0;   /* other system common / SysEx cancel running status */
        um.need = (uint8_t)(b == 0xF2u ? 2u : um_len(b));
        um.got = 0;
        return;
    }
    if (um.sysex || !um.st)
        return;
    if (um.need == 2u && !um.got) {
        um.d0 = (uint8_t)b;
        um.got = 1;
        return;
    }
    {
        uint32_t d1 = um.need == 2u ? um.d0 : b, d2 = um.need == 2u ? b : 0u;
        uint32_t cin = um.st == 0xF2u ? 3u : (uint32_t)um.st >> 4;
        um.got = 0;
        um_put(0x10u | cin | (uint32_t)um.st << 8 | d1 << 16 | d2 << 24);
        if (um.st == 0xF2u)
            um.st = 0;                             /* (system common: no running status after it) */
    }
}

/* the bytes the DMA has written since the last call; a received UM_EMPTY (line
 * noise) is passed over once the byte after it has landed */
static void um_drain(void)
{
    uint32_t n;
    for (n = 0; n < UM_RING; n++) {
        uint32_t b = um_ring[um.rd];
        if (b == UM_EMPTY) {
            if (um_ring[(um.rd + 1u) & (UM_RING - 1u)] == UM_EMPTY)
                break;
            b = um_ring[um.rd];                    /* the DMA writes in order: this one has landed too */
        }
        um_ring[um.rd] = UM_EMPTY;
        um.rd = (um.rd + 1u) & (UM_RING - 1u);
        um.bytes++;
        um_byte(b);                                /* a UM_EMPTY is an unknown realtime byte: ignored */
    }
}

static void uart_midi_poll(void)                   /* TIMER5 ISR, same context as usb_poll */
{
    fm1_uart1_rx_take();                           /* acknowledges the pendings; the count is not used */
    um_drain();
}
