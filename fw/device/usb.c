/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev */
/* USB full-speed device on USB0: a class-compliant USB-MIDI interface.
 * All SIE traffic happens in usb_poll(), called from the TIMER5 ISR at 2 kHz,
 * so INDEX is never shared and no USB IRQ is needed. MIDI in goes to a ring
 * the TIMER5 ISR drains (the controller's ctl_isr); MIDI out comes from a ring
 * it fills.
 * SysEx F0 22 24 35 7D F7 (soft key) asks the main loop to enter UBOOT.
 * VID 0x1209 / PID 0x0001 is the pid.codes test id.
 * The update loader builds from this file too (FW_LOADER), without the audio
 * input: its bytes must stay those the installers know (tools/build.py checks).
 * PT/FM's firmware is "FM-1 PT/FM" (the installers look for "FM-1"); it sets
 * USB_BCD_DEVICE, MQO (the out ring) and FW_UAC.
 * FW_UAC=1: the USB audio input (specs/connections.md#usb-audio), UAC1,
 * class compliant, 16-bit stereo at 44.1 or 48 kHz (the host's SET_CUR), on an
 * isochronous IN endpoint (EP4) (the audio streaming interface first in the
 * audio control header: older macOS);
 * the stream is PT/FM's (uac.h): the mix resampled to the host's clock, every
 * 1 ms packet the host's exact frames. The audio interrupt produces
 * (usb_audio_start, usb_audio_in, usb_audio_run), uac_service in usb_poll sends. */
#include "../hal/fm1_usb.h"   /* registers; relative, so the loader and the host tests find it too */
#ifndef FW_UAC
#define FW_UAC 0
#endif
enum { S_FADDR = 0, S_POWER = 1, S_INTRTX1 = 2, S_INTRTX2 = 3, S_INTRRX1 = 4, S_INTRRX2 = 5, S_INTRUSB = 6,
       S_INTRTX1E = 7, S_INTRTX2E = 8, S_INTRRX1E = 9, S_INTRRX2E = 10, S_INTRUSBE = 11, S_FRAME1 = 12,
       S_FRAME2 = 13, S_INDEX = 14,
       S_TXMAXP = 16, S_CSR0 = 17, S_TXCSR1 = 17, S_TXCSR2 = 18, S_RXMAXP = 19, S_RXCSR1 = 20, S_RXCSR2 = 21,
       S_COUNT0 = 22, S_RXCOUNT1 = 22, S_RXCOUNT2 = 23 };

static uint8_t ep0buf[64 + 4] __attribute__((aligned(4)));
static uint8_t ep1tx[64] __attribute__((aligned(4)));
static uint8_t ep1rx[64 + 4] __attribute__((aligned(4)));
#if FW_UAC
#include "uac.h"                 /* the stream: the ring, the resampler, the steering */
#define UAC_AS_IF 2u             /* the audio streaming interface */
#define UAC_MAXP (UA_MAXF * 4u)
static uint32_t ep4tx[UA_MAXF] __attribute__((aligned(4)));
static struct {
    volatile uint8_t alt;        /* the streaming interface's setting: 1 the host records */
    volatile uint8_t flowing;    /* the host takes the packets (it may set alt 1 long before it reads) */
    uint8_t queued;              /* a packet queued since the stream (re)started */
    uint8_t wait;                /* polls the queued packet has waited */
    uint8_t e0_rx;               /* SET_CUR sampling frequency: its data stage pending */
    uint32_t starts, stalls, missed, rate_sets;   /* figures */
} uac;
#endif

static struct {
    uint8_t up, config, pend_addr, has_pend_addr, e0_tx, e0_zlp;
    const uint8_t *e0_src;
    uint16_t e0_left;
    uint32_t resets, setups, rx_pkts, tx_pkts, sof_seen, timeouts, no_sof;
    uint32_t suspends, max_gap, retries, frame_stalls;   /* diagnostics: suspend events, longest run of polls without SOF */
    uint16_t frame, frame_same;  /* last SOF frame number and how long it has not moved (x 10 ms) */
    uint8_t detached;            /* usb_detach() was called: no retry */
    volatile uint8_t suspended;  /* bus idle > 3 ms (unplugged or host asleep): the UI hides "USB" */
    uint8_t last_setup[8];
    uint8_t sysex[16];
    uint8_t sx_len, sx_on;
    volatile uint8_t uboot_req;
    volatile uint8_t ota_req;    /* F0 22 24 35 7F F7: M-UPGRADE upgrade command (FW_OTA) */
    uint8_t rx_pend;             /* EP1 OUT packet seen, not yet taken (the MIDI ring was too full) */
    uint32_t rx_held, rx_bad;    /* packets held back (NAK) for room, malformed events ignored */
} usb;

#if FW_OTA
/* M-UPGRADE SysEx (ota.c): one received frame at a time (7-bit bytes between
 * F0 and F7; frames arriving while one is pending are dropped, the host
 * retries), and a TX ring of SysEx event packets sent before any MIDI. */
static uint8_t sx_frame[640];
static uint32_t sx_pos;
static volatile uint32_t sx_frame_len;
static volatile uint8_t sx_ready, sx_collect, sx_busy;
#define SXQ 64u
static uint32_t sx_out_q[SXQ];
static volatile uint32_t so_w, so_r;
#ifndef FW_LOADER
/* PT/FM's: a short message (a page's question: STATE?, FIGURES?) begun while a frame waits for the main loop is kept
 * in a slot of its own (usb.sysex's bytes between F0 and F7) and served after it; a frame begun while that slot waits
 * too is dropped, as before */
static uint8_t sx_short[sizeof usb.sysex - 2u];
static volatile uint32_t sx_short_len;
static volatile uint8_t sx_short_ready, sx_waited, sx_from_short;
#endif
#endif
#ifndef FW_LOADER
volatile uint16_t usb_sx_lost[2];   /* SysEx messages lost since power-on: [0] begun while frames waited (none kept),
                                     * [1] cut short by another message's status (the debug build's FIGURES 2) */
#endif

/* MIDI rings: 4-byte USB-MIDI event packets */
#define MQ 64u
#ifndef MQO
#define MQO MQ                   /* the out ring (the controller: 256) */
#endif
static uint32_t midi_in_q[MQ], midi_out_q[MQO];
static volatile uint32_t mi_w, mi_r, mo_w, mo_r;
#ifndef FW_LOADER
static uint32_t ep1_last, ep1_wait;                 /* events in the IN packet not taken yet; polls waited */
static volatile uint32_t ep1_dropped;               /* events flushed: no program read them (ep1_stale) */
#endif
static int midi_out_event(uint32_t pkt)             /* from the TIMER5 ISR; 0: not queued (USB down, full) */
{
    if (usb.config && mo_w - mo_r < MQO) {
        midi_out_q[mo_w % MQO] = pkt;
        RING_PUBLISH();
        mo_w++;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------- descriptors --- */
/* interfaces: 0 audio control, 1 MIDI streaming, 2 audio streaming (FW_UAC). bcdDevice: hosts cache
 * descriptors per version */
#define CFG_LEN (101 + 77 * FW_UAC)
#define USB_NIF (2 + FW_UAC)
#ifndef FW_USB_PID
#define FW_USB_PID 0x0001   /* the update loader is 0x0002 */
#endif
#ifndef USB_BCD_DEVICE
#define USB_BCD_DEVICE 0x0300u   /* 3.00: a MIDI-only device (the update loader's) */
#endif
/* PT/FM's firmware has a serial number (string 3): Windows names a device without one when it first sees its
 * ID on a port and keeps that name, so on a computer where the FM-1 ran another firmware with the same ID
 * (1209:0001) PT/FM's audio input kept that firmware's name. With a serial number it is a device of its own
 * there, named from its own strings, on any port. The update loader has none (its bytes stay as they are) */
#ifdef FW_LOADER
#define USB_ISERIAL 0
#else
#define USB_ISERIAL 3
#endif
static const uint8_t DEV_DESC[18] = {18, 1, 0x10, 0x01, 0, 0, 0, 64, 0x09, 0x12, FW_USB_PID & 0xFF,
                                     FW_USB_PID >> 8, USB_BCD_DEVICE & 0xFF, USB_BCD_DEVICE >> 8,
                                     1, 2, USB_ISERIAL, 1};
static const uint8_t CFG_DESC[] = {
    9, 2, CFG_LEN & 0xFF, CFG_LEN >> 8, USB_NIF, 1, 0, 0x80, 50,
    9, 4, 0, 0, 0, 1, 1, 0, 0,
#if FW_UAC
    /* the AC header's collection: the audio streaming interface first, then the MIDI one. The kernel AppleUSBAudio
     * (macOS up to 15) drops a MIDI streaming interface from the collection while walking it with the count from
     * bInCollection: listed first, the walk runs past the shortened list and no audio device appears (MIDI still
     * works); listed last it is dropped at the end. The class gives the order no meaning */
    10, 0x24, 1, 0x00, 0x01, 31, 0, 2, UAC_AS_IF, 1,    /* AC header 1.00: audio + MIDI streaming */
    12, 0x24, 2, 1, 0x03, 0x06, 0, 2, 0x03, 0x00, 0, 0, /* input terminal 1: line, 2 ch (L R) */
    9, 0x24, 3, 2, 0x01, 0x01, 0, 1, 0,                 /* output terminal 2: USB streaming, from 1 */
#else
    9, 0x24, 1, 0x00, 0x01, 9, 0, 1, 1,
#endif
    9, 4, 1, 0, 2, 1, 3, 0, 0,
    7, 0x24, 1, 0x00, 0x01, 0x41, 0x00,
    6, 0x24, 2, 1, 1, 0,
    6, 0x24, 2, 2, 2, 0,
    9, 0x24, 3, 1, 3, 1, 2, 1, 0,
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,
    9, 5, 0x01, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 1,
    9, 5, 0x81, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 3,
#if FW_UAC
    9, 4, UAC_AS_IF, 0, 0, 1, 2, 0, 0,                  /* audio streaming, alt 0: no bandwidth */
    9, 4, UAC_AS_IF, 1, 1, 1, 2, 0, 0,                  /* alt 1: the stream */
    7, 0x24, 1, 2, 1, 0x01, 0x00,                       /* AS general: terminal 2, delay 1, PCM */
    14, 0x24, 2, 1, 2, 2, 16, 2, 0x44, 0xAC, 0x00, 0x80, 0xBB, 0x00,   /* type I: 2 ch, 16 bit, 44100 / 48000 */
    9, 5, 0x84, 0x05, UAC_MAXP & 0xFF, UAC_MAXP >> 8, 1, 0, 0,        /* EP4 IN isochronous async, 1 ms */
    7, 0x25, 1, 0x01, 0, 0, 0,                          /* CS endpoint: sampling frequency control */
#endif
};
typedef char cfg_len_ok[sizeof CFG_DESC == CFG_LEN ? 1 : -1];
static const uint8_t STR0[4] = {4, 3, 0x09, 0x04};
#ifdef FW_LOADER
static const uint8_t STR1[] = {42, 3, 'H', 0, 0xFC, 0, 'g', 0, 'e', 0, 'l', 0, 't', 0, 'o', 0, 'n', 0, ' ', 0, 'I', 0,
                               'n', 0, 's', 0, 't', 0, 'r', 0, 'u', 0, 'm', 0, 'e', 0, 'n', 0, 't', 0, 's', 0};
static const uint8_t STR2[] = {30, 3, 'F', 0, 'e', 0, 'l', 0, 'u', 0, 'c', 0, 'c', 0, 'a', 0, ' ', 0, 'U', 0, 'p', 0,
                               'd', 0, 'a', 0, 't', 0, 'e', 0};
#else
static const uint8_t STR1[] = {12, 3, 'P', 0, 'T', 0, '/', 0, 'F', 0, 'M', 0};
static const uint8_t STR2[] = {22, 3, 'F', 0, 'M', 0, '-', 0, '1', 0, ' ', 0, 'P', 0, 'T', 0, '/', 0, 'F', 0, 'M', 0};
static const uint8_t STR3[] = {10, 3, 'P', 0, 'T', 0, 'F', 0, 'M', 0};   /* the serial number (USB_ISERIAL) */
#endif

static int get_desc(uint32_t wvalue, const uint8_t **d, uint16_t *l)
{
    switch (wvalue >> 8) {
    case 1:
        *d = DEV_DESC;
        *l = sizeof DEV_DESC;
        return 1;
    case 2:
        *d = CFG_DESC;
        *l = sizeof CFG_DESC;
        return 1;
    case 3:
        switch (wvalue & 0xFFu) {
        case 0:
            *d = STR0;
            *l = sizeof STR0;
            return 1;
        case 1:
            *d = STR1;
            *l = sizeof STR1;
            return 1;
        case 2:
            *d = STR2;
            *l = sizeof STR2;
            return 1;
#ifndef FW_LOADER
        case 3:
            *d = STR3;
            *l = sizeof STR3;
            return 1;
#endif
        default:
            return 0;
        }
    default:
        return 0;
    }
}

/* ---------------------------------------------------------- the SIE --- */
static void sie_wr(uint32_t r, uint32_t v)
{
    uint32_t n = 20000;
    if (!fm1_usb_sie_on())
        return;
    fm1_usb_sie_wr_start(r, v);
    while (!fm1_usb_sie_done() && --n)
        ;
    if (n)
        usb.timeouts = 0;                               /* consecutive failures only */
    else if (++usb.timeouts > 50u)
        usb.up = 0;                                     /* SIE dead (no clock?): stop polling it */
}

static uint32_t sie_rd(uint32_t r)
{
    uint32_t n = 20000;
    if (!fm1_usb_sie_on())
        return 0;
    fm1_usb_sie_rd_start(r);
    while (!fm1_usb_sie_done())
        if (!--n) {
            if (++usb.timeouts > 50u)
                usb.up = 0;
            return 0;
        }
    usb.timeouts = 0;
    return fm1_usb_sie_data();
}

#if FW_UAC
static void uac_ep4_reset(void)                         /* drop a queued packet; isochronous mode */
{
    sie_wr(S_INDEX, 4);
    sie_wr(S_TXCSR1, 0x48);                             /* ClrDataTog + FlushFIFO (SDK usb_g_ep_config) */
    sie_wr(S_TXCSR2, 0);
    sie_wr(S_TXCSR1, 0);
    sie_wr(S_TXCSR2, 0x40);                             /* ISO */
}

static void uac_stream(uint32_t alt)                    /* SET_INTERFACE, configuration, bus reset */
{
    ua.go = 0;                                          /* the next half primes the ring again ... */
    uac.flowing = 0;                                    /* ... once the host has taken a packet */
    uac.queued = 0;
    uac.wait = 0;
    RING_PUBLISH();
    uac.alt = (uint8_t)alt;
    ua.acc = 0;
    ua.last = 0;
    if (alt)
        uac.starts++;
}

static void uac_rate_set(uint32_t hz)                   /* SET_CUR (a bus reset, a configuration: 44.1) */
{
    uac_rs_rate(hz);
}
#endif

static void ep1_config(void)
{
    fm1_usb_ep_txbuf(1, ep1tx);
    fm1_usb_ep_rxbuf(1, ep1rx);
    sie_wr(S_INDEX, 1);
    sie_wr(S_TXMAXP, 0xFF);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_TXCSR2, 0);
    sie_wr(S_RXMAXP, 0xFF);
    sie_wr(S_RXCSR1, 0x90);
    sie_wr(S_RXCSR2, 0);
    sie_wr(S_INTRRX1E, 0x02);
    fm1_usb_ep_enable(1u << 1);
#if FW_UAC
    fm1_usb_ep4_txbuf(ep4tx);
    sie_wr(S_INDEX, 4);
    sie_wr(S_TXMAXP, 0xFF);
    uac_ep4_reset();
    fm1_usb_ep_enable(1u << 4);
    uac_stream(0);
#endif
}

static void e0_chunk(void)
{
    uint32_t n = usb.e0_left > 64u ? 64u : usb.e0_left, i;
    int last;
    for (i = 0; i < n; i++)
        ep0buf[i] = usb.e0_src[i];
    fm1_usb_ep0_send(ep0buf, n);
    usb.e0_src += n;
    usb.e0_left = (uint16_t)(usb.e0_left - n);
    last = usb.e0_left == 0 && !(n == 64u && usb.e0_zlp);
    if (usb.e0_left == 0 && n == 64u)
        usb.e0_zlp = 0;
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, last ? 0x0A : 0x02);
    if (last)
        usb.e0_tx = 0;
}

static void e0_send(const uint8_t *p, uint16_t len, uint16_t wlen)
{
    if (len > wlen)
        len = wlen;
    usb.e0_src = p;
    usb.e0_left = len;
    usb.e0_zlp = (len < wlen) && (len % 64u == 0);
    usb.e0_tx = 1;
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, 0x40);
    e0_chunk();
}

static void ep0_service(void)
{
    static const uint8_t zero2[2];
    uint32_t csr, i;
    uint16_t wvalue, wlength;
    uint8_t *s = usb.last_setup;
    if (usb.has_pend_addr) {
        sie_wr(S_FADDR, usb.pend_addr);
        usb.has_pend_addr = 0;
    }
    sie_wr(S_INDEX, 0);
    csr = sie_rd(S_CSR0);
    if (csr & 0x04u) {                                  /* SentStall */
        sie_wr(S_CSR0, 0);
        usb.e0_tx = 0;
#if FW_UAC
        uac.e0_rx = 0;
#endif
        return;
    }
    if (csr & 0x10u) {                                  /* SetupEnd: the host abandoned the transfer */
        sie_wr(S_CSR0, 0x80);
        usb.e0_tx = 0;
#if FW_UAC
        uac.e0_rx = 0;
#endif
    }
    if (usb.e0_tx) {
        if (!(csr & 0x02u))
            e0_chunk();
        return;
    }
    if (!(csr & 0x01u))
        return;
    fm1_usb_rx_sync();
#if FW_UAC
    if (uac.e0_rx) {                                    /* SET_CUR sampling frequency: 3 bytes, Hz */
        uac.rate_sets++;
        if (sie_rd(S_COUNT0) >= 3u)
            uac_rate_set(ep0buf[0] | (uint32_t)ep0buf[1] << 8 | (uint32_t)ep0buf[2] << 16);
        uac.e0_rx = 0;
        goto ack;
    }
#endif
    for (i = 0; i < 8u; i++)
        s[i] = ep0buf[i];
    usb.setups++;
    wvalue = (uint16_t)(s[2] | s[3] << 8);
    wlength = (uint16_t)(s[6] | s[7] << 8);
    switch ((uint32_t)s[0] << 8 | s[1]) {
    case 0x0005:                                        /* SET_ADDRESS */
        usb.pend_addr = s[2] & 0x7Fu;
        usb.has_pend_addr = 1;
        goto ack;
    case 0x8006: {                                      /* GET_DESCRIPTOR */
        const uint8_t *d;
        uint16_t l;
        if (get_desc(wvalue, &d, &l)) {
            if (!wlength)
                goto ack;
            e0_send(d, l, wlength);
            return;
        }
        goto stall;
    }
    case 0x0009:                                        /* SET_CONFIGURATION: 0 or 1 only */
        if (s[2] > 1u)
            goto stall;
        usb.config = s[2];
        if (usb.config == 1u)
            ep1_config();
        else
            sie_wr(S_INTRRX1E, 0);
#if FW_UAC
        if (usb.config != 1u)
            uac_stream(0);
        uac_rate_set(44100u);                           /* a new configuration starts at 44.1 */
#endif
        goto ack;
    case 0x8008:
        e0_send(&usb.config, 1, wlength);
        return;
    case 0x8000:
    case 0x8100:
    case 0x8200:
        e0_send(zero2, 2, wlength);
        return;
    case 0x010B:                                        /* SET_INTERFACE: alt 0 (alt 1 for the audio stream) */
#if FW_UAC
        if (s[4] == UAC_AS_IF && wvalue <= 1u && usb.config) {
            uac_ep4_reset();
            uac_stream(wvalue);
            goto ack;
        }
#endif
        if (wvalue == 0)
            goto ack;
        goto stall;
    case 0x810A:
#if FW_UAC
        if (s[4] == UAC_AS_IF) {
            static uint8_t alt;
            alt = uac.alt;
            e0_send(&alt, 1, wlength);
            return;
        }
#endif
        e0_send(zero2, 1, wlength);
        return;
    case 0x0201: {                                      /* CLEAR_FEATURE(ENDPOINT_HALT): data toggle reset */
        uint32_t ep = s[4] & 0x0Fu, last = 1u;
#if FW_UAC
        if (wvalue == 0 && s[4] == 0x84u)
            goto ack;                                   /* isochronous: no halt, no toggle */
#endif
        if (wvalue != 0 || ep > last)
            goto stall;                                 /* not an endpoint we have */
        if (ep) {                                       /* (EP0 has no halt to clear) */
            sie_wr(S_INDEX, ep);
            if (s[4] & 0x80u)
                sie_wr(S_TXCSR1, 0x40);
            else
                sie_wr(S_RXCSR1, 0x80);
        }
        goto ack;
    }
#if FW_UAC
    case 0x2201:                                        /* SET_CUR, endpoint: sampling frequency */
        if (s[4] != 0x84u || s[3] != 1u)
            goto stall;
        if (wlength) {
            uac.e0_rx = 1;
            sie_wr(S_INDEX, 0);
            sie_wr(S_CSR0, 0x40);                       /* the 3 bytes follow */
            return;
        }
        goto ack;
    case 0xA281:                                        /* GET_CUR / MIN / MAX: the rate / the range */
    case 0xA282:
    case 0xA283: {
        static const uint8_t RATE44[3] = {44100u & 0xFF, (44100u >> 8) & 0xFF, 44100u >> 16};
        static const uint8_t RATE48[3] = {48000u & 0xFF, (48000u >> 8) & 0xFF, 48000u >> 16};
        if (s[4] != 0x84u || s[3] != 1u)
            goto stall;
        e0_send(s[1] == 0x83u || (s[1] == 0x81u && ua.r48) ? RATE48 : RATE44, 3, wlength);
        return;
    }
#endif
    default:
        goto stall;
    }
ack:
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, 0x48);
    return;
stall:
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, 0x60);
}

/* SysEx assembly: only short commands matter here */
static void sysex_byte(uint8_t b)
{
    static const uint8_t UBOOT_KEY[6] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
    if (b >= 0xF8u)
        return;                                        /* realtime may occur anywhere in SysEx */
    if ((b & 0x80u) && b != 0xF0u && b != 0xF7u) {
#ifndef FW_LOADER
        if (usb.sx_on)
            usb_sx_lost[1]++;
#endif
        usb.sx_on = 0;
#if FW_OTA
        sx_collect = 0;
#endif
        return;                                        /* another status aborts the unfinished frame */
    }
    if (b == 0xF0) {
        usb.sx_on = 1;
        usb.sx_len = 0;
#if FW_OTA
#ifdef FW_LOADER
        sx_collect = !sx_ready;
#else
        sx_waited = sx_ready || sx_short_ready;          /* (a frame waits: this one goes to the short slot, if any) */
        sx_collect = !sx_waited;
#endif
        sx_pos = 0;
#endif
    }
    if (!usb.sx_on)
        return;
#if FW_OTA
    if (sx_collect && b != 0xF0 && b != 0xF7) {
        if (sx_pos < sizeof sx_frame)
            sx_frame[sx_pos++] = b;
        else
            sx_collect = 0;                             /* too long: not ours */
    }
#endif
    if (usb.sx_len < sizeof usb.sysex)
        usb.sysex[usb.sx_len++] = b;
    if (b == 0xF7) {
        uint32_t i, ok = usb.sx_len == 6u;
        for (i = 0; ok && i < 6u; i++)
            ok = usb.sysex[i] == UBOOT_KEY[i];
        if (ok)
            usb.uboot_req = 1;
#if FW_OTA
        else if (usb.sx_len == 6u && usb.sysex[1] == 0x22 && usb.sysex[2] == 0x24 && usb.sysex[3] == 0x35 &&
                 usb.sysex[4] == 0x7F)
            usb.ota_req = 1;
        else if (sx_collect && sx_pos) {
            sx_frame_len = sx_pos;
            RING_PUBLISH();
            sx_ready = 1;                               /* main loop: the handshake, then ota_take() */
        }
#ifndef FW_LOADER
        else if (sx_waited) {                           /* begun while a frame waited: a short one kept apart */
            if (!sx_short_ready && usb.sx_len > 2u && usb.sysex[usb.sx_len - 1u] == 0xF7u) {
                for (i = 0; i + 2u < usb.sx_len; i++)
                    sx_short[i] = usb.sysex[i + 1u];
                sx_short_len = usb.sx_len - 2u;
                RING_PUBLISH();
                sx_short_ready = 1;
            } else {
                usb_sx_lost[0]++;
            }
        }
        sx_waited = 0;
#endif
        sx_collect = 0;
#endif
        usb.sx_on = 0;
    }
}

/* one USB-MIDI event packet: SysEx bytes to sysex_byte, channel voice messages and the clock /
 * transport realtime ones to the MIDI ring; a malformed packet (status not matching its CIN, a data
 * byte with bit 7) is ignored */
static void midi_in_event(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (cin >= 4u && cin <= 7u) {                      /* SysEx */
        uint32_t k, nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (k = 0; k < nb; k++)
            sysex_byte((uint8_t)(pkt >> (8u * (k + 1u))));
        return;
    }
    if (st >= 0x80u && st < 0xF8u) {                   /* channel / system common also ends a SysEx */
        usb.sx_on = 0;
#if FW_OTA
        sx_collect = 0;
#endif
    }
#ifndef FW_LOADER
    if (cin == 3u && st == 0xF2u && !((pkt >> 16) & 0x80u) && !(pkt & 0x80000000u)) {   /* PT/FM: the song position */
        if (mi_w - mi_r < MQ) {
            midi_in_q[mi_w % MQ] = pkt;
            RING_PUBLISH();
            mi_w++;
        }
        return;
    }
#endif
    if (cin == 0xFu && (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)) {   /* clock, transport */
        if (mi_w - mi_r < MQ) {
            midi_in_q[mi_w % MQ] = 0xFu | st << 8;
            RING_PUBLISH();
            mi_w++;
        }
        return;
    }
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && !((pkt >> 16) & 0x80u) &&
        (cin == 0xCu || cin == 0xDu || !(pkt & 0x80000000u))) {
        if (mi_w - mi_r < MQ) {
            midi_in_q[mi_w % MQ] = pkt;
            RING_PUBLISH();
            mi_w++;
        }
        return;
    }
    if (cin >= 8u)
        usb.rx_bad++;
}

/* one EP1 OUT packet (<= 16 events) into the MIDI ring, or 0: fewer than 16 + 8 slots free, so the
 * packet stays (the host is NAKed) and is taken on a later poll: a burst from a DAW never drops a
 * note-off (dropping what did not fit would leave notes hanging), and 8 slots stay for the TRS input,
 * which shares the ring and cannot wait. The update loader never drains the ring: no back-pressure there. */
#define EP1_ROOM (16u + 8u)
static int ep1_take(const uint8_t *b, uint32_t n)
{
    uint32_t i;
#ifndef FW_LOADER
    if (MQ - (mi_w - mi_r) < EP1_ROOM)
        return 0;
#endif
    for (i = 0; i + 3u < n; i += 4u)
        midi_in_event((uint32_t)b[i] | (uint32_t)b[i + 1] << 8 | (uint32_t)b[i + 2] << 16 | (uint32_t)b[i + 3] << 24);
    return 1;
}

static void ep1_rx(void)                                /* leaves the packet (NAK) while the ring is too full */
{
    uint32_t csr, n;
    sie_wr(S_INDEX, 1);
    csr = sie_rd(S_RXCSR1) | (sie_rd(S_RXCSR2) << 8);
    if (!(csr & 1u)) {
        usb.rx_pend = 0;
        return;
    }
    n = sie_rd(S_RXCOUNT1) | (sie_rd(S_RXCOUNT2) << 8);
    if (n > 64u)
        n = 64u;
    fm1_usb_rx_sync();
    if (!ep1_take(ep1rx, n)) {
        usb.rx_held++;
        return;                                         /* rx_pend stays: taken on a later poll */
    }
    usb.rx_pend = 0;
    usb.rx_pkts++;
    csr = (csr & ~0x164u) | 0x10u;
    sie_wr(S_RXCSR1, csr & 0xFFu);
    sie_wr(S_RXCSR2, csr >> 8);
}

#if FW_OTA
/* ---- SysEx frames for the main loop (ota.c's hooks, and the controller's handshake; controller.c and
 * the update loader supply ota_now_ms / ota_idle) ---- */
static uint32_t ota_now_ms(void);
static void ota_idle(void);

#ifndef FW_LOADER
/* A message the host stopped taking for 200 ms is cut short with part of it queued (the ring holds 64 packets,
 * a message up to 214): its end, a lone F7 (CIN 5), is owed, so the host never reads it run into the next one.
 * Notes stay held meanwhile (sx_busy). sx_end queues the end when there is room: 1 when nothing is owed. The
 * main loop's, as ota_wire_send: usb_sx_poll pays it between messages, ota_wire_send before its own. */
static volatile uint8_t sx_cut;
static int sx_end(void)
{
    if (!sx_cut)
        return 1;
    if (so_w - so_r >= SXQ)
        return 0;
    sx_out_q[so_w % SXQ] = 0x05u | 0xF7u << 8;
    RING_PUBLISH();
    so_w++;
    sx_cut = 0;
    return 1;
}
static void usb_sx_poll(void)                           /* the main loop: an owed end out, then notes again */
{
    if (sx_cut && sx_end())
        sx_busy = 0;
}
#endif

static int ota_wire_send(const uint8_t *p, uint32_t n)   /* F0..F7 -> USB-MIDI SysEx packets */
{
    uint32_t i = 0, t0 = ota_now_ms();
    sx_busy = 1;
#ifndef FW_LOADER
    while (!sx_end()) {                                 /* a message cut short before: its end first */
        if (!*(volatile uint8_t *)&usb.config || ota_now_ms() - t0 > 200u)
            return -1;                                  /* (still owed: notes still held) */
        ota_idle();
    }
    sx_busy = 1;                                        /* (a bus reset meanwhile may have let it go) */
#endif
    while (i < n) {
        uint32_t k = n - i >= 3u ? 3u : n - i, pkt;
        uint32_t cin = k == 3u && i + 3u < n ? 4u : k == 3u ? 7u : 4u + k;   /* 4 continues; 5/6/7 end */
        pkt = cin | (uint32_t)p[i] << 8 | (k > 1u ? (uint32_t)p[i + 1] << 16 : 0u) |
              (k > 2u ? (uint32_t)p[i + 2] << 24 : 0u);
        while (so_w - so_r >= SXQ) {
            if (!*(volatile uint8_t *)&usb.config || ota_now_ms() - t0 > 200u) {
#ifndef FW_LOADER
                if (i && *(volatile uint8_t *)&usb.config) {
                    sx_cut = 1;                         /* part of it queued: its end owed, notes held till then */
                    return -1;
                }
#endif
                sx_busy = 0;
                return -1;
            }
            ota_idle();
        }
        sx_out_q[so_w % SXQ] = pkt;
        RING_PUBLISH();
        so_w++;
        i += k;
    }
    sx_busy = 0;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!sx_ready) {
#ifndef FW_LOADER
        if (sx_short_ready) {                           /* (the short one kept while a frame waited: after it) */
            RING_PUBLISH();
            *p = sx_short;
            *n = sx_short_len;
            sx_from_short = 1;
            return 1;
        }
#endif
        return 0;
    }
    RING_PUBLISH();                                     /* read the frame only after the flag */
    *p = sx_frame;
    *n = sx_frame_len;
#ifndef FW_LOADER
    sx_from_short = 0;
#endif
    return 1;
}
static void ota_frame_done(void)
{
    RING_PUBLISH();                                     /* done with the frame before the ISR may refill it */
#ifndef FW_LOADER
    if (sx_from_short) {
        sx_from_short = 0;
        sx_short_ready = 0;
        return;
    }
#endif
    sx_ready = 0;
}
#endif

static void ep1_tx(void)
{
    uint32_t csr, n = 0;
#if FW_OTA
    if (mo_w == mo_r && so_w == so_r)
        return;
#else
    if (mo_w == mo_r)
        return;
#endif
    sie_wr(S_INDEX, 1);
    csr = sie_rd(S_TXCSR1);
    if (csr & 0x01u)
        return;                                         /* previous packet still pending */
    if (csr & 0x80u)
        sie_wr(S_TXCSR1, csr & ~0x80u);
#if FW_OTA
    while (so_r != so_w && n < 64u) {                   /* SysEx first, never split by notes */
        uint32_t pkt = sx_out_q[so_r % SXQ];
        ep1tx[n] = (uint8_t)pkt;
        ep1tx[n + 1] = (uint8_t)(pkt >> 8);
        ep1tx[n + 2] = (uint8_t)(pkt >> 16);
        ep1tx[n + 3] = (uint8_t)(pkt >> 24);
        n += 4u;
        so_r++;
    }
    while (!sx_busy && so_r == so_w && mo_r != mo_w && n < 64u) {
#else
    while (mo_r != mo_w && n < 64u) {
#endif
        uint32_t pkt;
        RING_PUBLISH();                                 /* a producer in another ISR could preempt us: */
        pkt = midi_out_q[mo_r % MQO];                   /* slot read strictly between the index checks */
        ep1tx[n] = (uint8_t)pkt;
        ep1tx[n + 1] = (uint8_t)(pkt >> 8);
        ep1tx[n + 2] = (uint8_t)(pkt >> 16);
        ep1tx[n + 3] = (uint8_t)(pkt >> 24);
        n += 4u;
        RING_PUBLISH();
        mo_r++;
    }
    if (!n)
        return;
    fm1_usb_ep_send(1, ep1tx, n);
    sie_wr(S_TXCSR1, sie_rd(S_TXCSR1) | 0x01u);
    usb.tx_pkts++;
#ifndef FW_LOADER
    ep1_last = n / 4u;
#endif
}

#ifndef FW_LOADER
/* The controller: an IN packet no program takes for 100 ms (nothing has the FM-1's MIDI port open) is
 * flushed, with everything queued behind it (ep1_dropped counts the events), so a program opening the
 * port later never gets a stale backlog. A page asks for the hello when it opens the port anyway. */
static void ep1_stale(void)
{
    if (!ep1_last)
        return;
    sie_wr(S_INDEX, 1);
    if (!(sie_rd(S_TXCSR1) & 0x01u)) {                  /* taken */
        ep1_last = ep1_wait = 0;
        return;
    }
    if (++ep1_wait < 200u)                              /* polls at 2 kHz */
        return;
    sie_wr(S_TXCSR1, 0x08);                             /* FlushFIFO (TxPktRdy with it) */
    ep1_dropped += ep1_last + (mo_w - mo_r);
    ep1_last = ep1_wait = 0;
    mo_r = mo_w;                                        /* (the producer runs in this same interrupt) */
    if (!sx_busy)
        so_r = so_w;                                    /* a SysEx being queued stays whole */
}
#endif

#if FW_UAC
/* ---- the audio input: the audio interrupt produces (usb_audio_start, then each block), TIMER5 consumes ---- */
static void usb_audio_start(void)                       /* the audio interrupt, before a half renders */
{
    uac_rs_start(uac.alt && usb.config && uac.flowing);  /* (fed only while the host reads) */
}
static UAC_C2 int16_t *usb_audio_in(void) { return uac_rs_in(); }   /* a block's job: where its frames go (the */
static UAC_C2 void usb_audio_run(uint32_t n) { uac_rs_run(n); }      /* host's values; 0: none wanted), then in */

/* TIMER5, 2 kHz, also nested in the render: the host takes one packet per 1 ms frame; the next is queued as
 * soon as the last has gone (TxPktRdy clear). The ring is fed only while the host reads: it often selects alt 1
 * long before its first IN token, and a recorder may stop reading without going back to alt 0 (a packet
 * waiting > 20 ms). */
static void uac_service(void)
{
    uint32_t csr, n;
    if (!usb.up || !usb.config || !uac.alt)
        return;
    sie_wr(S_INDEX, 4);
    csr = sie_rd(S_TXCSR1);
    if (csr & 0x01u) {                                  /* still queued */
        if (uac.wait < 255u && ++uac.wait == 40u && uac.flowing) {
            uac.flowing = 0;
            uac.stalls++;
        }
        return;
    }
    if (uac.queued) {
        if (csr & 0x04u)
            uac.missed++;                               /* UnderRun: an IN token found nothing queued */
        uac.flowing = 1;
    }
    uac.queued = 1;
    uac.wait = 0;
    n = uac_rs_packet(ep4tx);
    fm1_usb_ep4_send(ep4tx, n * 4u);
    sie_wr(S_TXCSR1, 0x01);                             /* TxPktRdy (and UnderRun cleared) */
}
#endif

static void usb_poll(void)                              /* TIMER5 ISR, 2 kHz */
{
    uint32_t iu, it, ir;
    if (!usb.up)
        return;
    if (fm1_usb_sof_take()) {                           /* SOF pending: not a reliable "host is there", */
                                                        /* it keeps firing with the cable out */
        usb.sof_seen++;
        usb.no_sof = 0;
    } else if (++usb.no_sof > usb.max_gap) {
        usb.max_gap = usb.no_sof;
    }
    if (usb.config) {                                   /* the frame number only advances with a real host */
        static uint32_t tick;
        if (++tick >= 20u) {                            /* every 20 polls = 10 ms */
            uint16_t f;
            tick = 0;
            f = (uint16_t)(sie_rd(S_FRAME1) | (sie_rd(S_FRAME2) & 7u) << 8);
            if (f != usb.frame) {
                usb.frame = f;
                usb.frame_same = 0;
                usb.suspended = 0;
            } else if (++usb.frame_same >= 10u && !usb.suspended) {   /* 100 ms frozen: unplugged / host asleep */
                usb.suspended = 1;
                usb.frame_stalls++;
            }
        }
    }
    iu = sie_rd(S_INTRUSB);
#ifdef FW_LOADER
    it = sie_rd(S_INTRTX1) | sie_rd(S_INTRTX2) << 8;    /* the update loader reads both */
    ir = sie_rd(S_INTRRX1) | sie_rd(S_INTRRX2) << 8;
#else
    it = sie_rd(S_INTRTX1);                             /* EP0..7 only: skipping INTR*2 saves */
    ir = sie_rd(S_INTRRX1);                             /* 2 SIE round trips per poll */
#endif
    if (iu & 0x01u) {                                   /* suspend: the bus went idle */
        usb.suspends++;
        usb.suspended = 1;
    }
    if (iu & 0x02u)                                     /* resume */
        usb.suspended = 0;
    if (iu & 0x04u) {                                   /* bus reset */
        usb.resets++;
        sie_wr(S_FADDR, 0);
        usb.config = 0;
        usb.suspended = 0;
        mo_r = mo_w;                                    /* nothing stale for the next host */
#ifndef FW_LOADER
        ep1_last = ep1_wait = 0;
#endif
        usb.sx_on = 0;
#if FW_OTA
        so_r = so_w;
        sx_collect = 0;
#ifndef FW_LOADER
        if (sx_cut) {                                   /* the message cut short went with the host: nothing owed */
            sx_cut = 0;
            sx_busy = 0;
        }
#endif
#endif
        usb.e0_tx = 0;
        usb.has_pend_addr = 0;
        usb.rx_pend = 0;
#if FW_UAC
        uac.e0_rx = 0;
        uac_stream(0);
        uac_rate_set(44100u);
#endif
        fm1_usb_ep0_buf(ep0buf);
        sie_wr(S_INTRUSBE, 0x07);
        sie_wr(S_INTRTX1E, 0x01);
        sie_wr(S_INTRRX1E, 0);
    }
    if (it & 0x01u)
        ep0_service();
    if (ir & 0x02u)
        usb.rx_pend = 1;                                /* (the flag reads once: remember the packet) */
    if (usb.rx_pend)
        ep1_rx();
#ifndef FW_LOADER
    if (usb.config)
        ep1_stale();
#endif
    if (usb.config)
        ep1_tx();
#if FW_UAC
    uac_service();
#endif
}

static void usb_start(void)                             /* boot, or main-loop retry while usb.up == 0 */
{
    usb.timeouts = 0;
    fm1_usb_reset();                                    /* reset whatever the ROM left */
    fm1_delay_ms(25);
    fm1_usb_attach(ep0buf);
    sie_wr(S_POWER, 0x60);
    sie_wr(S_INTRUSBE, 0x07);                           /* suspend, resume, reset (polled) */
    sie_wr(S_INTRTX1E, 0x01);
    sie_wr(S_INTRTX2E, 0);
    sie_wr(S_INTRRX1E, 0);
    sie_wr(S_INTRRX2E, 0);
    fm1_usb_ep_enable(0x1Fu);
    usb.up = usb.timeouts < 3u;                         /* a dead SIE leaves USB off */
}

/* Powered on without a cable, the SIE does not answer and usb_start leaves
 * USB off: retry once a second from the main loop so a cable plugged in
 * later still enumerates. */
static void usb_retry(uint32_t now_ms)
{
    static uint32_t t;
    if (usb.up || usb.detached || now_ms - t < 1000u)
        return;
    t = now_ms;
    usb.retries++;
    usb_start();
}

static void usb_detach(void)
{
    usb.detached = 1;
    usb.up = 0;
    fm1_usb_off();
}
