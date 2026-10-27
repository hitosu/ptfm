/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The transport and the keys (specs/ui/transport.md) through player.h alone, on a disk's songs: pause keeps the
 * place and continue goes on from it; back to the start; stop; a song from a position (the rows before it run
 * through first); the pattern loop going round; a skip at the row's end; the keys (a FUSION instrument's
 * layers, a TB-303's legato); all sound off; mutes; the voice count.
 *   transport_test DISK */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../core/play/player.h"
#include "../../core/store/store.h"
#include "../../core/fx/fx.h"

int disk_load(const char *path);
static int fails;
static int32_t usb_peak;                 /* USB audio's tap: the loudest it was given since the last look */
static void usb_tap(const int32_t *lr, const int16_t *click, int n)
{
    int i;
    for (i = 0; i < 2 * n; i++) {
        int32_t v = lr[i] - (click ? click[i >> 1] : 0), a = v < 0 ? -v : v;
        usb_peak = a > usb_peak ? a : usb_peak;
    }
}

/* the source: TECHNO LOOP's (song 08) settings and its first pattern alone, looping */
static int src_song(rec_song_t *s)
{
    static uint8_t order[1];
    if (rec_song(8, s))
        return -1;
    order[0] = s->order[0];
    s->order = order;
    s->len = 1;
    return 0;
}
static int src_pattern(uint8_t num, rec_pat_t *pt) { return rec_pattern(num, pt); }
static double rms;

static void run(double seconds)          /* that long on, the output's RMS (dB) */
{
    static int16_t half[512];
    int n = (int)(seconds * 44100 / 256), i, k;
    double s = 0;
    for (k = 0; k < n; k++) {
        player_audio(half, 256);
        for (i = 0; i < 512; i++)
            s += (double)half[i] * half[i];
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
    printf("%-58s %s\n", what, ok ? "ok" : "WRONG");
    fails += !ok;
}

int main(int argc, char **argv)
{
    player_state_t s, t;
    pl_voice_info_t v[PL_VOICES];
    int i, n;
    if (argc != 2 || disk_load(argv[1])) {
        fprintf(stderr, "transport_test DISK\n");
        return 2;
    }
    player_init();
    player_play_song(1, 0, 0);
    run(3);
    s = st();
    check("a song plays: playing, sound", s.playing && s.what == PL_SONG && rms > -45);
    player_pause();
    run(1);
    t = st();
    check("PAUSE: not playing, its place kept", !t.playing && t.pos == s.pos && t.row >= s.row && t.row <= s.row + 2);
    player_continue();
    run(0.5);
    s = st();
    /* (the music clock counts the rows from the song's start, so its bar is where the row's is: the METRONOME, GATE
     * and FILTER's envelope on the beat. A start past the first row runs the rows before through first; the row it
     * lands on is counted once, not twice. The demo songs' patterns are whole bars) */
    check("PLAY again: on from that place", s.playing && (s.pos > t.pos || s.row >= t.row));
    check("... its music clock still on the song's beats", s.rows_played && (s.rows_played - 1) % 16 == s.row % 16u);
    player_to_start();
    run(0.05);
    s = st();
    check("HOME + PLAY playing: back at the start, playing", s.playing && s.pos == 0 && s.row <= 2);
    player_stop();
    run(0.05);
    s = st();
    check("PLAY held: stopped, back at the start", !s.playing && s.pos == 0 && s.row == 0);
    player_play_song(5, 2, 0);
    run(0.3);
    s = st();
    check("a song from position 02: there, playing", s.playing && s.pos == 2 && s.row < 8);
    check("... its music clock on the song's beats", s.rows_played && (s.rows_played - 1) % 16 == s.row % 16u);
    player_skip(1);
    run(0.6);
    s = st();
    check("a skip while playing: on at the next position", s.playing && s.pos == 3);
    player_play_pattern(s.pat, 0);
    run(0.2);
    s = st();
    check("the pattern loop: playing it", s.playing && s.what == PL_LOOP);
    {
        int wrapped = 0, last = 0;
        for (i = 0; i < 400 && !wrapped; i++) {
            run(0.05);
            t = st();
            wrapped = t.row < last;
            last = t.row;
        }
        check("the pattern loop goes round", wrapped && t.playing);
    }
    player_mutes(0xFFFF);
    run(0.5);
    run(0.5);
    check("every track muted: silence", rms < -70);
    player_mutes(0);
    run(0.5);
    check("unmuted: sound again", rms > -50);
    player_all_sound_off();
    run(0.02);
    n = player_voices_now(v, PL_VOICES);
    for (i = 0, s.voices = 0; i < n; i++)
        s.voices += v[i].on && v[i].track != 0xFF;
    check("all sound off: every voice gone at once", s.voices == 0);
    player_stop();
    run(0.5);
    {
        pl_inst_t fus = {0x06, 0};       /* the disk's FUSION's first: SPACE PAD, layers */
        pl_inst_t tb = {0xFA, 0};
        player_key_on(60, fus, 127);
        run(0.3);
        n = player_voices_now(v, PL_VOICES);
        for (i = 0, s.voices = 0; i < n; i++)
            s.voices += v[i].on && v[i].track == 0xFF;
        check("a FUSION instrument on the keys: its layers sound", s.voices >= 2 && rms > -60);
        player_key_off(60);
        run(2);
        player_key_on(48, tb, 127);
        player_key_on(55, tb, 127);
        run(0.2);
        n = player_voices_now(v, PL_VOICES);
        for (i = 0, s.voices = 0; i < n; i++)
            s.voices += v[i].on && v[i].track == 0xFF && v[i].engine == PL_ENG_TB303;
        check("a TB-303 on the keys: legato, one voice", s.voices == 1);
        player_keys_off();
        run(1);
    }
    {   /* another source (as LOOPS gives one): a song's first pattern looping, from the source's pointers */
        static const pl_source_t src = {src_song, src_pattern, 0, 0};
        int wrapped = 0, last = 0;
        player_play_source(PL_LOOPS, &src, 0, 0);
        run(0.3);
        s = st();
        check("a source plays: LOOPS, sound", s.playing && s.what == PL_LOOPS && rms > -60);
        for (i = 0; i < 600 && !wrapped; i++) {
            run(0.05);
            t = st();
            wrapped = t.row < last;
            last = t.row;
        }
        check("a source's order goes round", wrapped && t.playing && t.what == PL_LOOPS);
        player_source_changed(0);
        run(0.3);
        check("its cells changed: it plays on", st().playing);
        player_pause();
        run(0.2);
        player_continue();
        run(0.2);
        check("PAUSE and PLAY on a source", st().playing && st().what == PL_LOOPS);
        player_play_source(PL_LOOPS, &src, 0, 5);   /* (LOOPS plays from its cursor: mid-beat, the rows before run through) */
        run(0.3);
        s = st();
        check("a source from row 05: its music clock on the pattern's beats", s.playing && s.row >= 5 && s.rows_played &&
              (s.rows_played - 1) % 16 == s.row % 16u);
        player_play_song(1, 0, 0);
        run(0.2);
        check("a song started: the source goes", st().what == PL_SONG);
    }
    {   /* PLAYBACK's stepping */
        player_state_t a, b;
        player_play_song(1, 0, 0);
        run(2);
        a = st();
        player_step(1);
        run(0.3);
        b = st();
        check("a step while playing: paused, the row it was on, held", !b.playing && b.stepping && b.pos == a.pos &&
              b.row >= a.row && b.row <= a.row + 1);
        a = b;
        player_step(1);
        run(0.3);
        b = st();
        check("a step forward: the next row", b.stepping && b.row == a.row + 1);
        player_step(-1);
        player_step(-1);
        run(0.3);
        b = st();
        check("two steps back: the row before the first", b.stepping && b.row + 1 == a.row);
        player_continue();
        run(0.5);
        b = st();
        check("PLAY after stepping: plays on from there", b.playing && !b.stepping && b.row > a.row);
        player_pause();
        run(0.2);
        a = st();
        player_step(1);
        run(0.3);
        b = st();
        check("a step from paused: stepped at the place kept", b.stepping && b.pos == a.pos && b.row == a.row);
        player_stop();
        run(1);
    }
    {   /* the arpeggiator: UP, #___, LATCH 4 (the keys sending to no effect: its notes alone, no tails after them) */
        pl_inst_t dx = {0xF0, 0};
        static const uint8_t dry_sends[6] = {0x7F, 0x80, 0, 0, 0, 0};
        player_keys_mix(dry_sends);
        player_arp(1, 3, PL_ARP_UP, 4);
        run(0.1);
        player_key_on(60, dx, 127);
        player_key_on(64, dx, 127);
        player_key_on(67, dx, 127);
        run(1);
        s = st();
        check("ARP on: the keys are its notes, and it plays", s.arp_on && s.arp_notes == 3 && rms > -60);
        player_key_off(60);
        player_key_off(64);
        player_key_off(67);
        run(0.5);
        check("ARP's latch: its notes play on after the keys", st().arp_notes == 3 && rms > -60);
        {   /* another current instrument: the arpeggio's next notes play it (F8 the JUNO-106 cartridge) */
            pl_inst_t juno = {0xF8, 0};
            int nj = 0, nd = 0;
            int k, started = 0, was = 0;
            player_arp_inst(juno);
            for (k = 0; k < 300; k++) {          /* (1.7 s, a half at a time: the voices that start) */
                int now = 0;
                run(256.0 / 44100);
                n = player_voices_now(v, PL_VOICES);
                for (i = 0; i < n; i++)
                    if (v[i].on && v[i].track == 0xFF) {
                        nj += v[i].engine == PL_ENG_JUNO && k > 2;
                        nd += v[i].engine == PL_ENG_DX7 && k > 120;
                        now++;
                    }
                started += now > was;
                was = now;
            }
            printf("  after the change, in 1.7 s: %d notes started, Juno-106 seen %d halves, DX7 %d past 0.7 s\n",
                   started, nj, nd);
            check("ARP follows the current instrument: its next notes the new one's", nj > 0 && !nd && st().arp_notes == 3);
            player_arp_inst(dx);
        }
        player_key_on(72, dx, 127);
        run(0.2);
        check("a key with none held: a new set", st().arp_notes == 1);
        player_key_off(72);
        player_arp_clear();
        run(1.5);
        check("LFO: the latched notes let go, it waits", st().arp_notes == 0 && rms < -60);
        player_arp(1, 3, PL_ARP_UP, 0);
        player_key_on(62, dx, 127);
        run(0.3);
        player_key_off(62);
        run(0.2);
        check("LATCH OFF: a note goes with its key", st().arp_notes == 0);
        player_arp(0, 3, PL_ARP_UP, 4);
        player_key_on(60, dx, 127);
        run(0.3);
        check("ARP off: a key sounds by itself again", !st().arp_on && rms > -60);
        player_key_off(60);
        static const uint8_t full_sends[6] = {0x7F, 0x80, 0, 0x7F, 0x7F, 0x7F};
        player_keys_mix(full_sends);                    /* (the keys' strip as at first) */
    }
    player_stop();
    run(3);
    player_click(1);
    run(0.08);
    check("a count-in's click, the METRONOME off: heard", rms > -40);
    run(0.3);
    check("then quiet again", rms < -70);
    player_voice_count(8);
    player_play_song(3, 0, 0);
    {
        int most = 0;
        for (i = 0; i < 60; i++) {
            run(0.1);
            s = st();
            most = s.voices > most ? s.voices : most;
        }
        check("8 voices: never more than 8 sound", most <= 8 && most > 0);
    }
    player_voice_count(20);                             /* (more than the most, 16; one core here: 12 in force) */
    check("VOICES above the most, one core: 12 (PL_VOICES_C1)", (run(0.01), st().voice_count) == PL_VOICES_C1);
    player_play_song(0, 2, 0);                          /* (PUNCH IN's break: a pad held over 16 rows) */
    run(1.0);
    {   /* a skip landing cuts every track's notes, as NOTE CUT: none from before it rings on into the new position */
        pl_voice_info_t was[PL_VOICES];
        int nw = player_voices_now(was, PL_VOICES), pos0 = st().pos, kept = 0, landed = 0;
        player_skip(1);
        for (i = 0; i < 200 && !landed; i++) {
            run(256 / 44100.0);
            landed = st().pos != pos0;
        }
        run(512 / 44100.0);                             /* (a cut fades over a block) */
        n = player_voices_now(v, PL_VOICES);
        for (i = 0; i < nw && i < n; i++)
            if (was[i].on && v[i].on && was[i].track == v[i].track && was[i].note == v[i].note) kept++;
        check("a skip cuts every track's notes as it lands", landed && nw > 2 && kept == 0);
        check("... a clean start there: at the position's first rows", st().row < 4);
    }
    {   /* ... and the effects' tails emptied: every track muted, only the tails are heard, then none */
        double tails;
        int pos0, landed = 0;
        player_play_song(8, 0, 0);                      /* (TECHNO LOOP: DELAY and REVERB) */
        run(2);
        player_mutes(0xFFFFu);
        run(0.1);
        run(0.1);
        tails = rms;
        pos0 = st().pos;
        player_skip(1);
        for (i = 0; i < 400 && !landed; i++) {
            run(256 / 44100.0);
            landed = st().pos != pos0;
        }
        run(0.01);
        run(0.3);
        printf("  the tails before the skip %.1f dB, after it landed %.1f dB\n", tails, rms);
        check("... the effects' tails emptied as it lands", landed && tails > -90 && rms < -100);   /* (without: -83 dB) */
        player_mutes(0);
    }
    {   /* a K1 click while its clean start still runs through the rows before: from there instead, not lost */
        int pos0, landed = 0, k;
        player_play_song(0, 0, 0);                      /* (PUNCH IN: 6 positions of 128 rows) */
        run(1);
        pos0 = st().pos;
        player_skip(1);
        for (i = 0; i < 400 && !landed; i++) {
            run(256 / 44100.0);
            landed = st().pos != pos0;
        }
        player_skip(1);                                 /* (at once: the restart's run-through under way) */
        for (k = 0; k < 400 && st().pos != pos0 + 2; k++)
            run(256 / 44100.0);
        run(0.3);
        s = st();
        check("a K1 click during a clean start's run-through: on to the next position, not lost",
              landed && s.playing && s.pos == pos0 + 2);
        /* K1, then SELECT in the same row: stepping, the skip given up (not played from the skip's position) */
        player_play_song(0, 0, 0);
        run(1);
        pos0 = st().pos;
        player_skip(1);
        player_step(1);
        run(0.6);
        s = st();
        check("K1 then a step in the same row: stepping, the skip given up", !s.playing && s.stepping && s.pos == pos0);
        player_stop();
        run(0.5);
    }
    {   /* CUT (controls.md#cut): silent at once while held, the song playing on unheard, every effect emptied;
         * let go, heard again where the song is */
        player_state_t a, b2;
        player_voice_count(12);
        player_play_song(8, 1, 0);                      /* (TECHNO LOOP's loop: a note every beat, DELAY and REVERB) */
        run(2);
        a = st();
        player_usb = usb_tap;
        player_hush(1);
        run(0.015);                                     /* (its fade, 11.6 ms) */
        usb_peak = 0;
        run(0.03);
        check("CUT: silent after its 12 ms fade, USB audio too", rms < -100 && usb_peak == 0);
        run(1);
        b2 = st();
        check("CUT held: silent, the song's time going on", rms < -100 && usb_peak == 0 && b2.playing && b2.hushed &&
              (b2.pos > a.pos || b2.row > a.row));
        check("CUT held: no note starts", b2.voices == 0);
        check("CUT held: the DELAY and the REVERB emptied, at rest", (fx_asleep() >> FX_DELAY & 1u) &&
              (fx_asleep() >> FX_REVERB & 1u));
        player_hush(0);
        usb_peak = 0;
        run(1);
        printf("(CUT let go: %.1f dB RMS over the next second)\n", rms);
        check("CUT let go: its next notes heard, USB audio too", rms > -60 && usb_peak > 0 && !st().hushed &&
              st().playing);
        player_usb = 0;
        player_stop();
        run(3);
    }
    {   /* DUCK's SOURCE KEYS (specs/ui/screens/fx.md#duck): the keys' notes dip what plays; track 00 as the source,
         * the keys alone dip nothing */
        player_all_sound_off();
        run(1);
        player_fx_cmd(0xAA, FX_DUCK_KEYS);                /* (SOURCE: the keys) */
        player_fx_cmd(0xAE, FX_DUCK);
        player_key_on(60, (pl_inst_t){0xF0, 0x02}, 100);
        run(0.3);
        int32_t held = player_duck_env();
        player_key_off(60);
        run(3);
        int32_t tail = player_duck_env();
        player_all_sound_off();
        run(1);
        int32_t after = player_duck_env();
        player_fx_cmd(0xAA, 0);                           /* (SOURCE: track 00) */
        player_key_on(60, (pl_inst_t){0xF0, 0x02}, 100);
        run(0.3);
        int32_t trk = player_duck_env();
        player_key_off(60);
        player_fx_cmd(0xAF, FX_DUCK);
        run(3);
        printf("(DUCK, SOURCE KEYS: %d while a key is held, %d in its tail, %d once silent; SOURCE 00 with a key: %d)\n",
               (int)held, (int)tail, (int)after, (int)trk);
        check("DUCK SOURCE KEYS: a key held dips (DUCK's level up), less in its tail, none once silent",
              held > 4096 && tail < held && !after);
        check("DUCK SOURCE track 00: the keys alone dip nothing", !trk);
    }
    printf("transport: %s\n", fails ? "FAILED" : "all as transport.md says");
    return fails != 0;
}
