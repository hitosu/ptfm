/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The voice pool when notes want more voices than VOICES gives (specs/ui/screens/settings.md#voices), through
 * player.h alone, on a disk: a held note taken from its voice is parked, unheard, and comes back once a voice is
 * free, where its envelope would be by then (its sound within 1 dB of the same note never taken), for each engine
 * that parks (DX7, Juno-106, sampler); let go while parked, it is gone; the keys' held notes are taken last; a
 * smaller VOICES parks the held notes above it and a larger one brings them back.
 *   voices_test DISK */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../core/play/player.h"
#include "../../core/play/rec.h"
#include "../../core/store/store.h"

int disk_load(const char *path);
static int fails;
static double at_s;                      /* seconds played since the take began */

static double run(double seconds)        /* that long on: the output's RMS (dB) over it */
{
    static int16_t half[512];
    int n = (int)(seconds * 44100 / 256 + 0.5), i, k;
    double s = 0;
    for (k = 0; k < n; k++) {
        player_audio(half, 256);
        for (i = 0; i < 512; i++)
            s += (double)half[i] * half[i];
    }
    at_s += n * 256 / 44100.0;
    return 10 * log10(s / (512.0 * (n ? n : 1)) / (32768.0 * 32768.0) + 1e-20);
}

static player_state_t st(void)
{
    player_state_t s;
    player_state(&s, 0);
    return s;
}

static int key_on_now(int note)          /* the keys' note `note` is heard */
{
    pl_voice_info_t v[PL_VOICES];
    int n = player_voices_now(v, PL_VOICES), i;
    for (i = 0; i < n; i++)
        if (v[i].on && v[i].track == 0xFF && v[i].note == note)
            return 1;
    return 0;
}

static int keys_heard(int *song)         /* the keys' notes heard; *song: the song's */
{
    pl_voice_info_t v[PL_VOICES];
    int n = player_voices_now(v, PL_VOICES), i, k = 0;
    *song = 0;
    for (i = 0; i < n; i++)
        if (v[i].on) {
            if (v[i].track == 0xFF)
                k++;
            else
                (*song)++;
        }
    return k;
}

static void check(const char *what, int ok)
{
    printf("%-66s %s\n", what, ok ? "ok" : "WRONG");
    fails += !ok;
}

static void quiet(void)                  /* nothing sounding, the count back at 12 */
{
    player_keys_off();
    player_stop();
    player_all_sound_off();
    player_voice_count(12);
    run(1.5);
    at_s = 0;
}

/* note 60 of `in` alone, or taken by 8 more keys at 0.5 s (VOICES 8) which are let go at 1 s (let_go: 60 is let go
 * too, at 0.8 s, while parked); the output's RMS over 2.5-3 s; *was_parked: 60 was parked, unheard, at 0.9 s */
static double take(pl_inst_t in, int others, int let_go, int *was_parked, int *back_at_25)
{
    int k;
    double r;
    quiet();
    player_voice_count(8);
    run(0.02);
    player_key_on(60, in, 100);
    run(0.5);
    for (k = 0; k < others; k++)
        player_key_on((uint8_t)(62 + k), in, 100);
    run(0.3);
    if (let_go)
        player_key_off(60);
    run(0.1);
    if (was_parked)
        *was_parked = !key_on_now(60) && st().parked == (let_go ? 0 : 1);
    run(0.1);
    for (k = 0; k < others; k++)
        player_key_off((uint8_t)(62 + k));
    run(1.5);
    if (back_at_25)
        *back_at_25 = key_on_now(60) && st().voices == 1 && st().parked == 0;
    r = run(0.5);
    player_key_off(60);
    return r;
}

/* the first instrument of that kind still sounding 2.5 s into a held note, its RMS then in *alone; 0 none */
static int sustained(uint8_t kind, pl_inst_t *in, double *alone)
{
    int c, s;
    for (c = 0; c < 256; c++) {
        rec_cart_t rc;
        if (rec_cart((uint8_t)c, &rc) || rc.kind != kind)
            continue;
        for (s = 0; s < rc.nslots && s < 128; s++) {
            if (!rec_slot(&rc, (uint8_t)s))
                continue;
            in->cart = (uint8_t)c;
            in->slot = (uint8_t)s;
            if ((*alone = take(*in, 0, 0, 0, 0)) > -45)
                return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const struct {
        uint8_t kind;
        const char *what;
    } P[] = {{REC_DX7, "a DX7"}, {REC_JUNO, "a Juno-106"}, {REC_SAMPLER, "a sampler"}};
    unsigned i;
    if (argc != 2 || disk_load(argv[1])) {
        fprintf(stderr, "voices_test DISK\n");
        return 2;
    }
    player_init();
    for (i = 0; i < sizeof P / sizeof P[0]; i++) {
        int parked = 0, back = 0;
        pl_inst_t in;
        double alone = -200, taken, gone;
        char what[96];
        snprintf(what, sizeof what, "%s: an instrument that sustains", P[i].what);
        check(what, sustained(P[i].kind, &in, &alone));
        taken = take(in, 8, 0, &parked, &back);
        gone = take(in, 8, 1, 0, 0);
        printf("%s %02X:%02X: alone %.2f dB, parked and back %.2f dB, let go while parked %.2f dB\n", P[i].what,
               in.cart, in.slot, alone, taken, gone);
        snprintf(what, sizeof what, "%s: a 9th key parks the oldest, unheard", P[i].what);
        check(what, parked);
        snprintf(what, sizeof what, "%s: once a voice is free it comes back", P[i].what);
        check(what, back);
        snprintf(what, sizeof what, "%s: where its envelope would be (within 1 dB)", P[i].what);
        check(what, fabs(taken - alone) <= 1.0 && alone > -60);
        snprintf(what, sizeof what, "%s: let go while parked, it is gone", P[i].what);
        check(what, gone < -80);
    }
    {   /* two keys held over MIDNIGHT RUN at VOICES 8: the song's notes are taken, never the keys' */
        pl_inst_t pad;
        double r;
        int k, held = 1, most = 0;
        uint32_t s0;
        check("a Juno-106 pad for the keys", sustained(REC_JUNO, &pad, &r));
        quiet();
        player_voice_count(8);
        player_play_song(2, 0, 0);
        run(0.5);
        player_key_on(48, pad, 100);
        player_key_on(55, pad, 100);
        s0 = st().steals;
        for (k = 0; k < 200; k++) {
            run(0.1);
            held &= key_on_now(48) && key_on_now(55);
            most = st().voices > most ? st().voices : most;
        }
        printf("MIDNIGHT RUN at 8 voices, two keys held 20 s: %u notes taken, the most voices %d\n",
               st().steals - s0, most);
        check("the keys' held notes taken last: never, with song notes there", held && st().steals > s0);
        check("never more than 8 voices heard", most <= 8);
        player_keys_off();
        player_stop();
    }
    {   /* 12 keys held: VOICES 8 parks 4, VOICES 12 brings them back */
        pl_inst_t pad;
        double r;
        int k;
        player_state_t a, b;
        sustained(REC_JUNO, &pad, &r);
        quiet();
        for (k = 0; k < 12; k++)
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
        run(0.5);
        player_voice_count(8);
        run(0.1);
        a = st();
        player_voice_count(12);
        run(0.1);
        b = st();
        printf("12 keys held: VOICES 8: %d heard, %d parked; VOICES 12: %d heard, %d parked\n", a.voices, a.parked,
               b.voices, b.parked);
        check("VOICES 8 with 12 keys held: 8 heard, 4 parked", a.voices == 8 && a.parked == 4);
        check("VOICES 12 again: all 12 back", b.voices == 12 && b.parked == 0);
        player_keys_off();
    }
    {   /* 20 keys held: below VOICES 16 the pool holds 16 notes, parked ones included (4 wait at 12, 8 at 8) */
        pl_inst_t pad;
        double r;
        int k;
        player_state_t a, b;
        sustained(REC_JUNO, &pad, &r);
        quiet();
        for (k = 0; k < 20; k++) {
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
            run(0.05);
        }
        run(0.5);
        a = st();
        player_keys_off();
        quiet();
        player_voice_count(8);
        for (k = 0; k < 20; k++) {
            player_key_on((uint8_t)(48 + 2 * k), pad, 100);
            run(0.05);
        }
        run(0.5);
        b = st();
        printf("20 keys held: VOICES 12: %d heard, %d parked; VOICES 8: %d heard, %d parked\n", a.voices, a.parked,
               b.voices, b.parked);
        check("20 keys at VOICES 12: 12 heard, 4 parked", a.voices == 12 && a.parked == 4);
        check("20 keys at VOICES 8: 8 heard, 8 parked", b.voices == 8 && b.parked == 8);
        player_keys_off();
    }
    {   /* the keys first, whatever their level: 8 keys held at VOICES 8 with a song playing, every key heard and the
         * song's notes parked; 2 more keys park the 2 oldest; one let go, a parked key's note comes back, not the
         * song's; all let go, the song is heard again */
        pl_inst_t pad;
        double r;
        int k, song, all = 1, none = 1, most = 0, parked = 0;
        sustained(REC_JUNO, &pad, &r);
        quiet();
        player_voice_count(8);
        player_play_song(2, 0, 0);
        run(0.5);
        for (k = 0; k < 8; k++)
            player_key_on((uint8_t)(48 + 2 * k), pad, 20);   /* (quiet ones: the keys win however quiet) */
        run(0.1);
        for (k = 0; k < 50; k++) {
            run(0.1);
            all &= keys_heard(&song) == 8;
            none &= song == 0;
            most = st().voices > most ? st().voices : most;
            parked = st().parked > parked ? st().parked : parked;
        }
        printf("MIDNIGHT RUN at 8 voices, 8 quiet keys held 5 s: keys always heard %d, song never heard %d, the most "
               "voices %d, the most parked %d\n", all, none, most, parked);
        check("8 keys at VOICES 8: every key heard, the song's notes parked", all && none && most <= 8 && parked > 0);
        player_key_on(80, pad, 20);
        player_key_on(82, pad, 20);
        run(0.2);
        int k1 = keys_heard(&song), oldest = key_on_now(48) || key_on_now(50);
        check("10 keys: 8 heard, the 2 oldest parked", k1 == 8 && !oldest);
        player_key_off(60);
        run(2.5);                        /* (its tail rung out: a voice free) */
        int k2 = keys_heard(&song), s2 = song;
        printf("10 keys, one let go: %d keys heard, %d of the song's notes\n", k2, s2);
        check("one let go: a parked key's note comes back, not the song's", k2 == 8 && s2 == 0 &&
              (key_on_now(48) || key_on_now(50)));
        player_keys_off();
        run(3);
        keys_heard(&song);
        check("all let go: the song heard again", song > 0);
        player_stop();
    }
    printf("voices: %s\n", fails ? "FAILED" : "all as settings.md#voices says");
    return fails != 0;
}
