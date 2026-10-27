/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* DJ's decks through player.h and play/dj.c (specs/ui/screens/dj.md), on the demo disk's loops: both decks
 * heard together and each through the crossfader and its VOLUME; a synced deck starting on the main's bar line
 * and staying in phase; REPEAT PATTERN, a free loop, a ROLL landing where the deck would have been; the stops
 * (silent once stopped, ECHO OUT's echoes ringing on, landing in time, staying stopped); the LFOs (the
 * crossfader's, a filter's heard and its preview, the FX LFO); KEY; the loops on tracks 16-31; the stand
 * through the store and back; SWAP, nothing heard changing.
 *   dj_test DISK */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../core/play/player.h"
#include "../../core/play/dj.h"
#include "../../core/store/store.h"
#include "../../core/spare.h"

int disk_load(const char *path);
void disk_put(uint8_t kind, uint8_t num, uint8_t sub, const uint8_t *p, uint32_t len);
static int fails;
static double rms;
static uint32_t zc;                      /* zero crossings of the left channel */
static player_dj_t mix;

static void run(double seconds)          /* that long on: the output's RMS (dB), its zero crossings */
{
    static int16_t half[512];
    int n = (int)(seconds * 44100 / 256), i, k;
    double s = 0;
    zc = 0;
    for (k = 0; k < n; k++) {
        player_audio(half, 256);
        for (i = 0; i < 512; i++)
            s += (double)half[i] * half[i];
        for (i = 2; i < 512; i += 2)
            zc += (half[i] >= 0) != (half[i - 2] >= 0);
    }
    rms = 10 * log10(s / (512.0 * (n ? n : 1)) / (32768.0 * 32768.0) + 1e-20);
}
static player_state_t st(void)
{
    player_state_t s;
    player_state(&s, 0);
    return s;
}
static void check(const char *what, int ok)
{
    printf("%-62s %s\n", what, ok ? "ok" : "WRONG");
    fails += !ok;
}
static void set(void) { player_dj_set(&mix); }
static void fresh(void)                  /* both decks paused, the mix as at first */
{
    memset(&mix, 0, sizeof mix);
    mix.xf = 16;
    mix.reso = 30;
    mix.sends_filtered = 1;
    mix.mix_set[0] = 50;
    mix.mix_set[1] = 30;
    mix.mix_set[2] = 35;
    for (int i = 0; i < PL_NLFO; i++) {
        mix.lfo[i].shape = 2;
        mix.lfo[i].len = 6;
    }
    set();
    for (int d = 0; d < 2; d++) {
        player_deck_stop(d, 0, 0, 0);
        player_deck_loop(d, PL_DLOOP_NONE, 0, 0, 0, 0);
        player_deck_play(d, 0, 0, 0);
        player_deck_tempo(d, 65536, 0);
    }
    run(0.3);
}
static int track_voices(int lo, int hi)  /* song voices sounding on tracks lo-hi */
{
    pl_voice_info_t v[PL_VOICES];
    int n = player_voices_now(v, PL_VOICES), c = 0;
    for (int i = 0; i < n; i++)
        c += v[i].on && v[i].track != 0xFF && v[i].track >= lo && v[i].track <= hi;
    return c;
}
/* TECHNO LOOP's (song 08) settings and one of its patterns, looping: the loops' source */
static int src_song(rec_song_t *s)
{
    static uint8_t order[1];
    if (rec_song(8, s))
        return -1;
    order[0] = s->order[s->len > 1 ? 1 : 0];
    s->order = order;
    s->len = 1;
    return 0;
}
static int src_pattern(uint8_t num, rec_pat_t *pt) { return rec_pattern(num, pt); }
static int src_inst(int track, pl_inst_t *in) { (void)track; (void)in; return 0; }
static const pl_source_t SRC = {src_song, src_pattern, src_inst, 0};

int main(int argc, char **argv)
{
    double r0, r1, rb, rmid;
    player_state_t s;
    int i, ok, n;
    if (argc != 2 || disk_load(argv[1])) {
        fprintf(stderr, "dj_test DISK\n");
        return 2;
    }
    player_init();
    dj_record_default(DJ_REC);
    {                                    /* (the second deck's memory, lent as dj.c has it lent) */
        void *mem = spare_borrow(player_dj_mem_size());
        check("DJ's memory: lent from the spare zone's top (under 16 KB)", mem && player_dj_mem_size() < 16 * 1024 &&
              (uint8_t *)mem + ((player_dj_mem_size() + 3u) & ~3u) == spare_zone() + spare_size());
        printf("  the second deck takes %u B while DJ plays\n", (unsigned)player_dj_mem_size());
        player_dj_mem(mem);
    }
    /* ---- DJ's first start: what plays stops and its sound is cut (the voices, the effects' tails): DJ starts dry ---- */
    player_play_song(0x00, 0, 0);
    run(2);
    s = st();
    printf("  before: what %d, playing %d, %d voices, %.1f dB\n", s.what, s.playing, track_voices(0, 15), rms);
    check("a song plays before DJ starts", s.what == PL_SONG && s.playing && rms > -40);
    player_dj(1, DJ_FX);
    run(0.05);
    s = st();
    check("DJ's first start: the song stops (PL_DJ, no deck playing)", s.what == PL_DJ && !s.playing && !s.deck[0].playing);
    run(0.5);
    printf("  after the cut: %.1f dB\n", rms);
    check("... its sound cut: no voice, silent (no DELAY or REVERB tail)", track_voices(0, 31) == 0 && rms < -100);
    fresh();

    /* ---- both decks heard ---- */
    player_deck_load(0, 0x08);           /* TECHNO LOOP */
    player_deck_load(1, 0x0A);           /* DISCO LOOP */
    player_deck_play(0, 1, 1, 0);
    player_deck_play(1, 1, 1, 0);
    mix.vol[1] = DJ_VOL_OFF;
    set();
    run(0.3);
    run(3);
    r0 = rms;
    mix.vol[1] = 0;
    mix.vol[0] = DJ_VOL_OFF;
    set();
    run(0.3);
    run(3);
    r1 = rms;
    mix.vol[0] = 0;
    set();
    run(0.3);
    run(3);
    rb = rms;
    s = st();
    printf("  the left alone %.1f dB, the right alone %.1f, both %.1f\n", r0, r1, rb);
    check("DJ: the decks play together (what: DJ, both playing)", s.what == PL_DJ && s.deck[0].playing && s.deck[1].playing);
    check("each deck alone is heard", r0 > -45 && r1 > -45);
    check("both together: louder than either alone", rb > (r0 > r1 ? r0 : r1) + 0.8);
    check("both decks' voices sound at once (tracks 0-15 and 16-31)", track_voices(0, 15) > 0 && track_voices(16, 31) > 0);
    mix.xf = 0;
    set();
    run(0.3);
    run(3);
    printf("  the crossfader at L %.1f dB (the middle gives each deck -3 dB)\n", rms);
    check("the crossfader at L: the left deck alone, 3 dB up on the middle", fabs(rms - r0 - 3.0) < 1.5);
    mix.xf = 32;
    set();
    run(0.3);
    run(3);
    printf("  the crossfader at R %.1f dB\n", rms);
    check("the crossfader at R: the right deck alone, 3 dB up on the middle", fabs(rms - r1 - 3.0) < 1.5);
    mix.xf = 16;
    mix.vol[0] = -12;
    mix.vol[1] = DJ_VOL_OFF;
    set();
    run(0.3);
    run(3);
    rmid = rms;
    mix.vol[0] = 0;
    set();
    run(0.3);
    run(3);
    printf("  the left at -12 dB %.1f, at 0 dB %.1f\n", rmid, rms);
    check("VOLUME -12 dB: about 12 dB quieter", fabs(rms - rmid - 12) < 2);

    /* ---- KEY (on the bass alone: track 03 of every DJ loop; DJ's DELAY and REVERB off, no tails) ---- */
    DJ_FX[7] &= (uint8_t)~0x60u;
    player_dj(1, DJ_FX);
    run(3);
    {                                    /* (each from the same place: the same notes) */
        uint32_t z0, z1;
        player_deck_mutes(0, 0xFFFFu & ~(1u << 2));
        player_deck_play(0, 1, 1, 0);
        run(2);
        z0 = zc;
        mix.key[0] = 6;
        set();
        player_deck_play(0, 1, 1, 0);
        run(2);
        z1 = zc;
        mix.key[0] = 0;
        set();
        player_deck_mutes(0, 0);
        printf("  zero crossings at KEY 0 %u, at KEY +6 %u\n", (unsigned)z0, (unsigned)z1);
        check("KEY +6: the deck higher (its bass's zero crossings up)", z1 > z0 * 1.15 && z1 < z0 * 1.7);
    }

    /* ---- the DJ FILTER, its slide ---- */
    {
        uint32_t z0, z1;
        player_deck_play(0, 1, 1, 0);
        run(2);
        z0 = zc;
        mix.filt[0] = -56;               /* a low LP */
        mix.filt_on[0] = 1;
        set();
        player_deck_play(0, 1, 1, 0);
        run(2);
        z1 = zc;
        printf("  zero crossings unfiltered %u, LP -56 %u\n", (unsigned)z0, (unsigned)z1);
        check("the DJ FILTER on, a low LP: duller", z1 < z0 * 0.8);
        mix.filt_on[0] = 0;
        mix.filt_slide[0] = 1;           /* the key held: sliding to OFF */
        set();
        run(0.25);
        s = st();
        check("its key held: the filter slides (half way at 0.25 s)", s.lfo_filt8[0] < -8 * 256 && s.lfo_filt8[0] > -50 * 256);
        mix.filt_slide[0] = 0;           /* let go: there */
        set();
        run(0.2);
        s = st();
        check("let go: there (OFF)", s.lfo_filt8[0] > -256 && s.lfo_filt8[0] < 256);
    }
    DJ_FX[7] |= 0x60u;
    player_dj(1, DJ_FX);

    /* ---- SYNC: a deck armed starts on the main's bar line, and stays in phase ---- */
    fresh();
    player_deck_load(0, 0x08);
    player_deck_load(1, 0x08);
    player_deck_play(0, 1, 1, 0);
    run(1.3);                            /* (into its bar) */
    s = st();
    player_deck_tempo(1, 65536, s.deck[0].bpm10);
    mix.sync = 1;
    set();
    player_deck_arm(1, 1, 0);
    ok = 0;
    for (n = 0; n < 2000; n++) {
        run(256 / 44100.0);
        s = st();
        if (s.deck[1].playing && !s.deck[1].armed) {
            ok = s.deck[0].row % 16 <= 1 && s.deck[1].row == 0;
            printf("  the right started with the main at row %d (pos %d), itself at row %d\n", s.deck[0].row, s.deck[0].pos,
                   s.deck[1].row);
            break;
        }
    }
    check("armed, the right deck starts on the main's bar line", ok);
    {
        int same = 0, total = 0, d0 = (int)s.deck[0].rows_played - (int)s.deck[1].rows_played, d1;
        for (n = 0; n < 1400; n++) {     /* (8 s) */
            run(256 / 44100.0);
            s = st();
            total++;
            same += s.deck[0].row % 4 == s.deck[1].row % 4;
        }
        d1 = (int)s.deck[0].rows_played - (int)s.deck[1].rows_played;
        printf("  over 8 s: on the same beat row %d of %d halves; rows apart %d then %d\n", same, total, d0, d1);
        check("synced: the beats stay together, no drift", same > total * 95 / 100 && (d1 - d0 <= 1 && d0 - d1 <= 1));
    }
    /* ---- SYNC switched on while both play, out of phase: the right's beats land on the main's, to a block (the
     * lock once read the right's place a tick short, a row's ticks counting down, and held it 30 ms off) ---- */
    fresh();
    player_deck_load(0, 0x08);           /* TECHNO LOOP */
    player_deck_load(1, 0x10);           /* ELECTRO LOOP, 126 BPM too */
    player_deck_play(0, 1, 1, 0);
    run(1.37);
    s = st();
    player_deck_tempo(1, 65536, s.deck[0].bpm10);   /* (the same tempo, its own: no lock yet) */
    player_deck_play(1, 1, 1, 0);
    run(2);
    mix.sync = 1;
    set();
    run(8);                              /* (time for the lock: it nudges an eighth of a tick a beat at most) */
    {
        static int16_t blk[128];
        int last[2] = {-1, -1}, nb[2] = {0, 0}, worst = 0;
        long f, at[2][64];
        for (f = 0; f < 4 * 44100L; f += 64) {   /* (4 s, a block at a time: a row starts on a block) */
            player_audio(blk, 64);
            s = st();
            for (int k = 0; k < 2; k++) {
                if (s.deck[k].row != last[k] && s.deck[k].row % 4 == 0 && nb[k] < 64)
                    at[k][nb[k]++] = f;  /* (a beat's first row started) */
                last[k] = s.deck[k].row;
            }
        }
        for (int i = 0; i < nb[1]; i++) {        /* each of the right's beats against the main's nearest */
            long best = 1L << 30;
            for (int j = 0; j < nb[0]; j++) {
                long d = at[1][i] - at[0][j] < 0 ? at[0][j] - at[1][i] : at[1][i] - at[0][j];
                best = d < best ? d : best;
            }
            worst = (int)best > worst ? (int)best : worst;
        }
        printf("  SYNC switched on while both play: %d beats of the right, at most %d frames from the main's\n", nb[1], worst);
        check("SYNC on while both play: the right's beats on the main's, to a block", nb[0] > 4 && nb[1] > 4 && worst <= 64);
    }

    /* ---- loops ---- */
    fresh();
    player_deck_load(0, 0x08);
    mix.xf = 0;
    set();
    player_deck_play(0, 1, 0, 0);        /* (its intro: 8 bars played once, then position 01) */
    run(0.5);
    player_deck_loop(0, PL_DLOOP_PATTERN, 0, 0, 0, 0);
    run(20);
    s = st();
    check("REPEAT PATTERN: still the intro's position after 20 s", s.deck[0].pos == 0 && s.deck[0].playing);
    player_deck_loop(0, PL_DLOOP_NONE, 0, 0, 0, 0);
    run(16);
    s = st();
    check("off: on through its end", s.deck[0].pos >= 1);
    s = st();
    {
        int r = s.deck[0].row / 4 * 4, inside = 1;
        player_deck_loop(0, PL_DLOOP_ROWS, s.deck[0].pos, (uint8_t)r, 4, 0);   /* a beat round and round */
        run(0.6);
        for (n = 0; n < 500; n++) {
            run(256 / 44100.0);
            s = st();
            inside &= s.deck[0].row >= r && s.deck[0].row < r + 4;
        }
        check("a free loop of a beat: its rows round and round", inside);
        player_deck_loop(0, PL_DLOOP_NONE, 0, 0, 0, 0);
    }
    {                                    /* a ROLL let go: on from where its loop is (no landing in time) */
        uint8_t pos_ref, row_ref, rpos;
        int r, r0;
        player_deck_play(0, 1, 1, 0);
        run(1.0 + 2.0 + 0.5);
        s = st();
        pos_ref = s.deck[0].pos;
        row_ref = s.deck[0].row;
        player_deck_play(0, 1, 1, 0);
        run(1.0);
        s = st();
        rpos = s.deck[0].pos;
        r0 = s.deck[0].row;              /* (the rows a second at this tempo, from row 0) */
        r = r0 / 4 * 4;
        player_deck_loop(0, PL_DLOOP_ROWS, rpos, (uint8_t)r, 4, 1);
        run(2.0);
        player_deck_roll_end(0);
        run(0.5);
        s = st();
        printf("  a ROLL let go: at %02X/%02X (its loop %02X/%02X-%02X), without it %02X/%02X\n", s.deck[0].pos,
               s.deck[0].row, rpos, r, r + 3, pos_ref, row_ref);
        check("a ROLL let go: on from where its loop is, out through its end", s.deck[0].pos == rpos &&
              s.deck[0].row >= r && s.deck[0].row <= r + 4 + (r0 + 1) / 2 + 1 && (s.deck[0].pos != pos_ref ||
              s.deck[0].row + 4 < row_ref));
    }

    /* ---- the stops (DJ's DELAY and REVERB off: what is left is the stops' own) ---- */
    DJ_FX[7] &= (uint8_t)~0x60u;
    player_dj(1, DJ_FX);
    {
        uint8_t pos_ref, row_ref;
        player_deck_play(0, 1, 1, 0);
        run(3.5);
        s = st();
        pos_ref = s.deck[0].pos;
        row_ref = s.deck[0].row;
        player_deck_play(0, 1, 1, 0);
        run(0.5);
        player_deck_stop(0, PL_STOP_BRAKE, 1, 0);
        run(0.2);
        check("BRAKE: still sounding as it slows", rms > -50);
        run(0.45);
        run(0.3);
        s = st();
        check("BRAKE held: stopped, silent", s.deck[0].stopped && rms < -70);
        run(1.55);
        player_deck_stop(0, PL_STOP_BRAKE, 0, 0);
        run(0.5);
        s = st();
        printf("  BRAKE let go: at %02X/%02X, without it %02X/%02X; %.1f dB\n", s.deck[0].pos, s.deck[0].row, pos_ref, row_ref, rms);
        check("BRAKE let go: playing on where it would have been", s.deck[0].playing && !s.deck[0].stop && rms > -45 &&
              s.deck[0].pos == pos_ref && s.deck[0].row + 1 >= row_ref && s.deck[0].row <= row_ref + 1);
        player_deck_stop(0, PL_STOP_BACKSPIN, 1, 0);
        run(0.3);
        check("BACKSPIN: sounding as it spins back", rms > -50);
        run(0.8);
        run(0.3);
        s = st();
        check("BACKSPIN held: stopped, silent", s.deck[0].stopped && rms < -60);
        player_deck_stop(0, PL_STOP_BACKSPIN, 0, 0);
        run(0.3);
        check("BACKSPIN let go: playing on", st().deck[0].playing && rms > -45);
        player_deck_stop(0, PL_STOP_ECHO, 1, 0);
        for (n = 0; n < 400 && !st().deck[0].stopped; n++)
            run(256 / 44100.0);
        s = st();
        run(0.4);
        printf("  ECHO OUT: stopped after %.2f s (a beat at most), the echoes after %.1f dB\n", n * 256 / 44100.0, rms);
        check("ECHO OUT: the deck stops on its beat, its echoes ring on", s.deck[0].stopped && n * 256 < 44100 * 6 / 10 &&
              rms > -75);
        player_deck_stop(0, PL_STOP_ECHO, 0, 0);
        run(0.3);
        check("ECHO OUT let go: playing on", st().deck[0].playing && rms > -45);
        player_deck_stop(0, PL_STOP_BRAKE, 1, 0);
        run(0.2);
        player_deck_stop(0, PL_STOP_BRAKE, 0, 1);   /* (its PLAY key turned it: let go, it stays) */
        run(0.8);
        s = st();
        check("a stop let go to stay: paused once it stood", !s.deck[0].playing && !s.deck[0].stop);
    }
    DJ_FX[7] |= 0x60u;
    player_dj(1, DJ_FX);

    /* ---- the LFOs ---- */
    fresh();
    player_deck_load(0, 0x08);
    player_deck_load(1, 0x0A);
    player_deck_play(0, 1, 1, 0);
    mix.xf = 16;
    mix.lfo[PL_LFO_FADER].on = 1;
    mix.lfo[PL_LFO_FADER].gen = 1;
    mix.lfo[PL_LFO_FADER].range = 1;     /* L TO R */
    mix.lfo[PL_LFO_FADER].len = 8;       /* 1/8 */
    set();
    {
        int lo = 1 << 30, hi = -(1 << 30);
        run(0.3);
        for (n = 0; n < 200; n++) {
            run(256 / 44100.0);
            s = st();
            lo = s.lfo_xf8 < lo ? s.lfo_xf8 : lo;
            hi = s.lfo_xf8 > hi ? s.lfo_xf8 : hi;
        }
        printf("  the crossfader's LFO, L TO R at 1/8: %.1f to %.1f\n", lo / 256.0, hi / 256.0);
        check("the crossfader's LFO swings end to end", lo < 4 * 256 && hi > 28 * 256);
        mix.lfo[PL_LFO_FADER].on = 0;
        set();
        run(0.5);
        s = st();
        check("switched off: back where the crossfader is set", s.lfo_xf8 > 15 * 256 && s.lfo_xf8 < 17 * 256 &&
              !(s.lfo_live & 1u << PL_LFO_FADER));
    }
    {
        int lo = 1 << 30, hi = -(1 << 30), plo = 1 << 30, phi = -(1 << 30);
        mix.filt[0] = -30;
        mix.filt_on[0] = 0;              /* the filter off: the LFO swings unheard, a preview */
        mix.lfo[PL_LFO_FILT_L].on = 1;
        mix.lfo[PL_LFO_FILT_L].gen = 1;
        mix.lfo[PL_LFO_FILT_L].range = 0;   /* OFF TO HERE */
        mix.lfo[PL_LFO_FILT_L].len = 7;
        set();
        run(0.3);
        for (n = 0; n < 200; n++) {
            run(256 / 44100.0);
            s = st();
            lo = s.lfo_filt8[0] < lo ? s.lfo_filt8[0] : lo;
            hi = s.lfo_filt8[0] > hi ? s.lfo_filt8[0] : hi;
            plo = s.lfo_prev8[0] < plo ? s.lfo_prev8[0] : plo;
            phi = s.lfo_prev8[0] > phi ? s.lfo_prev8[0] : phi;
        }
        printf("  a filter's LFO with the filter off: heard %.1f to %.1f, its preview %.1f to %.1f\n", lo / 256.0, hi / 256.0,
               plo / 256.0, phi / 256.0);
        check("the filter off: the LFO's preview swings, nothing heard", plo < -25 * 256 && phi > -5 * 256 && lo > -256 && hi < 256);
        mix.filt_on[0] = 1;
        set();
        lo = 1 << 30;
        hi = -(1 << 30);
        run(0.3);
        for (n = 0; n < 200; n++) {
            run(256 / 44100.0);
            s = st();
            lo = s.lfo_filt8[0] < lo ? s.lfo_filt8[0] : lo;
            hi = s.lfo_filt8[0] > hi ? s.lfo_filt8[0] : hi;
        }
        printf("  its key on: heard %.1f to %.1f\n", lo / 256.0, hi / 256.0);
        check("its key on: the filter heard swinging, OFF to here", lo < -25 * 256 && hi > -5 * 256);
        mix.lfo[PL_LFO_FILT_L].on = 0;
        mix.filt_on[0] = 0;
        set();
    }
    {
        int lo = 1000, hi = -1000;
        mix.lfo[PL_LFO_FX].on = 1;
        mix.lfo[PL_LFO_FX].gen = 1;
        mix.lfo[PL_LFO_FX].len = 7;
        mix.lfo_fx = 0;                  /* REVERB MIX: 35 % as set, HERE TO 100 % */
        set();
        run(0.3);
        for (n = 0; n < 300; n++) {
            run(256 / 44100.0);
            s = st();
            lo = s.lfo_mix >= 0 && s.lfo_mix < lo ? s.lfo_mix : lo;
            hi = s.lfo_mix > hi ? s.lfo_mix : hi;
        }
        printf("  the FX LFO on the REVERB's MIX: %d to %d %%\n", lo, hi);
        check("the FX LFO swings the MIX from its setting to 100 %", lo <= 40 && hi >= 95);
        mix.lfo[PL_LFO_FX].on = 0;
        set();
        run(0.5);
        check("switched off: no FX LFO", st().lfo_mix < 0);
    }

    /* ---- the loops on tracks 16-31 ---- */
    player_play_source(PL_LOOPS, &SRC, 0, 0);
    run(1.5);
    s = st();
    check("the loops: what plays, no longer DJ", s.what == PL_LOOPS && s.playing);
    check("DJ ended: the second deck's memory given back up", !s.dj_mem);
    check("their voices on tracks 16-31, none on 0-15", track_voices(16, 31) > 0 && track_voices(0, 15) == 0 && rms > -50);
    player_play_song(0x08, 0, 0);
    run(1.0);
    check("a song: its voices on tracks 0-15 again", st().what == PL_SONG && track_voices(0, 15) > 0);

    /* ---- the stand through the store and back ---- */
    {
        uint8_t keep[DJ_REC_LEN];
        dj_record_default(DJ_REC);
        dj_deck(0)[DK_SONG] = 0x08;
        dj_deck_key(0, 3);
        dj_loop(0, 3, 1, 16, 32, 0);     /* REPEAT 2 BARS */
        dj_lfo_bytes(DJ_LFO_FILT_L)[0] = 1;
        dj_lfo_bytes(DJ_LFO_FADER)[2] = 2;
        dj_set_xf(9);
        memcpy(keep, DJ_REC, DJ_REC_LEN);
        disk_put(REC_DJ, 0, 0, keep, DJ_REC_LEN);   /* (the store keeps the pointer: the copy's) */
        memset(DJ_REC, 0, DJ_REC_LEN);
        dj_load();
        check("the stand: written, read back the same", !memcmp(keep, DJ_REC, DJ_REC_LEN));
        check("its KEY, loop and LFOs as set", (int8_t)dj_deck(0)[DK_KEY] == 3 && dj_deck(0)[DK_LOOP] == 3 &&
              dj_deck(0)[DK_LOOP_LEN] == 16 && (dj_lfo_bytes(DJ_LFO_FILT_L)[0] & 1) && dj_xf() == 9);
    }

    /* ---- the request queue never loses a key-off; bends merged ---- */
    {
        pl_inst_t tb = {0xFA, 0};        /* (the built-in TB-303's first preset: notes end as soon as let go) */
        pl_voice_info_t v[PL_VOICES];
        player_play_song(0x08, 0, 0);
        player_pause();
        run(0.5);
        for (i = 0; i < 40; i++)         /* (a flood within one half: more than the queue holds) */
            player_key_on((uint8_t)(48 + i), tb, 100);
        for (i = 0; i < 300; i++)
            player_keys_bend((int16_t)(i - 150));
        for (i = 0; i < 40; i++)
            player_key_off((uint8_t)(48 + i));
        run(0.1);
        run(3);
        n = player_voices_now(v, PL_VOICES);
        ok = 0;
        for (i = 0; i < n; i++)
            ok += v[i].on && v[i].track == 0xFF;
        printf("  40 keys on and off, 300 bends, in one half: %d key voices left after 3 s\n", ok);
        check("a flood of requests: every key-off taken, no note held", ok == 0);
        player_keys_bend(0);
    }

    /* ---- a pattern whose stream does not end in its part: refused, never read past it ---- */
    {
        static uint8_t bad[5 + 6] = {1, 63, 1, 1, 0,   /* 1 track, 64 rows, step 1, track 0's stream: */
                                     0x88, 60, 0xFF, 0xF0, 0x40, 0x10};   /* (a cell claiming 240 commands) */
        static uint8_t cut[4] = {0x88, 60, 0xFF, 0x30};
        rec_pat_t pt;
        rec_cur_t cu;
        rec_cell_t ce;
        disk_put(REC_PATTERN, 0xEE, 0, bad, sizeof bad);
        check("a stream not ending in its part: no track", !rec_pattern(0xEE, &pt) && !pt.trk[0]);
        rec_cur_set_end(&cu, cut, cut + sizeof cut);
        ok = 1;
        for (i = 0; i < 64; i++)
            ok &= !rec_cur_at(&cu, i, &ce) || i == 0;
        check("a cursor never reads past its part's end (ASan watching)", ok);
    }

    /* ---- SWAP (dj.md#the-main-and-swap): the decks exchange sides as they play, nothing heard changes. The session
     * forked: one process plays on, the other swaps (the mix mirrored as the stand sends it, the tempos sent again by
     * side); what both play after it the same, sample for sample. Two DJ loops of one tempo, SYNC off: the shared
     * effects' clock is the main's, the same either way ---- */
    {
        enum { SW_HALVES = 600 };        /* (3.5 s) */
        static int16_t half[512];
        int16_t *other = mmap(0, SW_HALVES * sizeof half, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        int a = -1, b = -1, same = 1, diff_at = -1, wst = 1;
        uint32_t rows0[2];
        pid_t pid;
        for (i = 6; i < 18 && b < 0; i++)   /* (the first two DJ loops of the same tempo) */
            for (n = i + 1; n < 18 && b < 0; n++)
                if (dj_song_own10((uint8_t)i) && dj_song_own10((uint8_t)i) == dj_song_own10((uint8_t)n))
                    a = i, b = n;
        printf("  SWAP: songs %02X and %02X, both %d.%d BPM\n", a, b, dj_song_own10((uint8_t)a) / 10, dj_song_own10((uint8_t)a) % 10);
        check("SWAP: two DJ loops of one tempo to swap", other != MAP_FAILED && b >= 0);
        if (other == MAP_FAILED || b < 0)
            return 1;
        spare_return();                  /* (DJ's memory lent again: the loops took DJ's end) */
        player_dj_mem(spare_borrow(player_dj_mem_size()));
        DJ_FX[7] |= 0x60u;               /* (DJ's DELAY and REVERB on) */
        player_dj(1, DJ_FX);
        fresh();
        player_deck_load(0, (uint8_t)a);
        player_deck_load(1, (uint8_t)b);
        mix.xf = 10;
        mix.vol[1] = -3;
        mix.filt[0] = -20;
        mix.filt[1] = 15;
        mix.filt_on[0] = mix.filt_on[1] = 1;
        mix.key[0] = 2;
        mix.lfo[PL_LFO_FADER].on = 1;    /* (the crossfader's LFO, MIRROR, and the right filter's, SINE) */
        mix.lfo[PL_LFO_FADER].range = 3;
        mix.lfo[PL_LFO_FADER].len = 4;
        mix.lfo[PL_LFO_FILT_R].on = 1;
        mix.lfo[PL_LFO_FILT_R].shape = 0;
        mix.lfo[PL_LFO_FILT_R].range = 1;
        mix.lfo[PL_LFO_FILT_R].len = 5;
        set();
        player_deck_play(0, 1, 1, 0);
        player_deck_play(1, 1, 1, 0);
        player_deck_loop(1, PL_DLOOP_ROWS, 1, 4, 8, 0);   /* (a free loop on the right) */
        run(2.5);
        s = st();
        rows0[0] = s.deck[0].rows_played;
        rows0[1] = s.deck[1].rows_played;
        fflush(stdout);
        pid = fork();
        if (pid == 0) {                  /* (plays on, unswapped) */
            for (i = 0; i < SW_HALVES; i++)
                player_audio(other + i * 512, 256);
            _exit(0);
        }
        {                                /* (SWAP: the stand's decks exchanged, the mix sent again for them) */
            player_dj_t m = mix;
            pl_lfo_t t = m.lfo[PL_LFO_FILT_L];
            m.xf = (uint8_t)(32 - mix.xf);
            for (n = 0; n < 2; n++) {
                m.vol[n] = mix.vol[1 - n];
                m.filt[n] = mix.filt[1 - n];
                m.filt_on[n] = mix.filt_on[1 - n];
                m.filt_slide[n] = mix.filt_slide[1 - n];
                m.key[n] = mix.key[1 - n];
            }
            m.lfo[PL_LFO_FILT_L] = m.lfo[PL_LFO_FILT_R];
            m.lfo[PL_LFO_FILT_R] = t;
            m.lfo[PL_LFO_FADER].rev ^= 1u;
            m.swaps = 1;
            player_dj_swap(1);
            player_dj_set(&m);
            player_deck_tempo(0, 65536, 0);
            player_deck_tempo(1, 65536, 0);
        }
        {
            static int16_t mine[SW_HALVES * 512];
            for (i = 0; i < SW_HALVES; i++)
                player_audio(mine + i * 512, 256);
            waitpid(pid, &wst, 0);       /* (the other's halves all written) */
            for (i = 0; i < SW_HALVES && same; i++)
                if (memcmp(mine + i * 512, other + i * 512, sizeof half)) {
                    same = 0;
                    diff_at = i;
                }
        }
        s = st();
        printf("  after the SWAP: the left plays %02X (%u rows on), the right %02X (%u rows on)%s\n", s.deck[0].song,
               (unsigned)(s.deck[0].rows_played - rows0[1]), s.deck[1].song, (unsigned)(s.deck[1].rows_played - rows0[0]),
               same ? "" : ", the sound differs");
        if (!same)
            printf("  ... from half %d after the SWAP\n", diff_at);
        check("SWAP: the decks change sides, each going on from where it was", wst == 0 && s.deck[0].song == b &&
              s.deck[1].song == a && s.deck[0].playing && s.deck[1].playing && s.deck[0].rows_played > rows0[1] + 15 &&
              s.deck[1].rows_played > rows0[0] + 15 && s.deck[0].loop == PL_DLOOP_ROWS && !s.deck[1].loop);
        check("SWAP: nothing heard changes (3.5 s the same, sample for sample)", same);
        check("SWAP: both decks' voices sound on (none cut)", track_voices(0, 15) > 0 && track_voices(16, 31) > 0);
        munmap(other, SW_HALVES * sizeof half);
    }

    /* ---- HOLD's hand (dj.md#hold): a paused deck's rows heard a click at a time, each held ringing, as a step; the
     * deck reads as not playing meanwhile ---- */
    {
        int v0, r0, r1, playing;
        player_deck_play(0, 0, 0, 0);
        player_deck_play(1, 0, 0, 0);
        run(1.5);                        /* (paused, rung out) */
        v0 = track_voices(0, 31);
        s = st();
        r0 = (s.deck[0].row / 4 + 1) * 4 % 64;          /* (the next beat: every DJ loop's kick) */
        player_deck_hand(0, s.deck[0].pos, (uint8_t)r0, 200);
        run(0.1);
        s = st();
        r1 = s.deck[0].row;
        playing = s.deck[0].playing;
        printf("  the hand: to row %02X (%02X), %d voices (paused: %d), playing %d\n", r0, r1, track_voices(0, 31), v0, playing);
        check("HOLD's hand: the row heard, the deck held there (not playing)", v0 == 0 && r1 == r0 &&
              track_voices(0, 31) > 0 && !playing);
        run(1.0);
        s = st();
        check("... and held: still on that row a second later", s.deck[0].row == r1 && !s.deck[0].playing);
        player_deck_hand(0, s.deck[0].pos, (uint8_t)(r1 + 1), 20);
        player_deck_hand(0, s.deck[0].pos, (uint8_t)(r1 + 2), 20);
        run(0.1);
        s = st();
        check("a fast turn: each row on, the last one held", s.deck[0].row == r1 + 2 && !s.deck[0].playing);
        player_deck_play(0, 0, 0, 0);
        run(1.5);
        check("paused: the hand's notes let go", track_voices(0, 31) == 0);
        {                                /* let go playing: on from the hand's row at once, a loop counted from there */
            uint8_t pos = s.deck[0].pos;
            uint32_t rp0;
            player_deck_loop(0, PL_DLOOP_ROWS, pos, 4, 8, 0);   /* (rows 4-11) */
            player_deck_hand(0, pos, 7, 100);
            run(0.1);
            player_deck_play(0, 1, pos, 7);
            run(0.02);
            s = st();
            rp0 = s.deck[0].rows_played;
            r0 = s.deck[0].row;
            printf("  let go at row %02X (playing %d, rows played %u):", r0, s.deck[0].playing, (unsigned)rp0);
            for (i = 0, n = (int)rp0; i < 2000 && st().deck[0].rows_played < rp0 + 5; i++) {
                run(0.006);                      /* (a half) */
                if ((int)st().deck[0].rows_played != n) {
                    n = (int)st().deck[0].rows_played;
                    printf(" %02X", st().deck[0].row);
                }
            }
            s = st();
            printf("; 5 rows on, row %02X (the loop 04-0B)\n", s.deck[0].row);
            check("HOLD let go: on from the hand's row at once, its loop counted from there", r0 == 7 && s.deck[0].playing &&
                  s.deck[0].row == 4);
            player_deck_loop(0, PL_DLOOP_NONE, 0, 0, 0, 0);
            player_deck_play(0, 0, 0, 0);
            run(0.5);
        }
    }

    printf(fails ? "dj: %d WRONG\n" : "dj: all as dj.md says\n", fails);
    return fails != 0;
}
