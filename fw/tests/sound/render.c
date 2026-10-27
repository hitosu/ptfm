/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The player on the PC: a song of a disk played as the FM-1 plays it (player_audio, a half at a time), to a
 * WAV, with its figures: loudness (LUFS, ITU-R BS.1770: K-weighted, gated), RMS, peak, the most voices at
 * once, the voices taken from notes; or the notes the replay started (--events), to compare with the
 * playground's replay.
 *
 *   render DISK --list                          the disk's songs
 *   render DISK SONG [--seconds S] [--wav OUT] [--events] [--voices N] [--solo T]
 *          SONG: its number (hex, as SONGS shows it); T: track T (1-16) alone, the others muted (a track's
 *          loudness in its song, for a mix by the tracks' jobs) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/play/player.h"
#include "../../core/play/rec.h"
#include "../../core/store/store.h"

int disk_load(const char *path);

static uint64_t frames;
static int events;
static uint32_t block_at(void);
static void trigger(uint32_t t, uint32_t note, pl_inst_t in)
{
    if (events)
        printf("E %llu %u %u %02X:%02X\n", (unsigned long long)(frames + block_at()), t, note, in.cart, in.slot);
}
#define PL_TEST_TRIGGER(t, note, in) trigger(t, note, in)
#include "../../core/play/player.c"
static uint32_t block_at(void) { return pl_block_at; }

#define M_PI 3.14159265358979323846
/* the K-weighting (two biquads at 44.1 kHz, BS.1770's coefficients re-derived for the rate) */
typedef struct { double b0, b1, b2, a1, a2, x1, x2, y1, y2; } kf_t;
static double kf(kf_t *f, double x)
{
    double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}
static void kw_init(kf_t *s, kf_t *h)
{
    double fs = 44100, f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    double K = tan(M_PI * f0 / fs), Vh = pow(10, G / 20), Vb = pow(Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
    memset(s, 0, sizeof *s);
    memset(h, 0, sizeof *h);
    s->b0 = (Vh + Vb * K / Q + K * K) / a0;
    s->b1 = 2 * (K * K - Vh) / a0;
    s->b2 = (Vh - Vb * K / Q + K * K) / a0;
    s->a1 = 2 * (K * K - 1) / a0;
    s->a2 = (1 - K / Q + K * K) / a0;
    f0 = 38.13547087602444;
    Q = 0.5003270373238773;
    K = tan(M_PI * f0 / fs);
    h->b0 = 1;
    h->b1 = -2;
    h->b2 = 1;
    h->a1 = 2 * (K * K - 1) / (1 + K / Q + K * K);
    h->a2 = (1 - K / Q + K * K) / (1 + K / Q + K * K);
}

static void wav_write(const char *path, const int16_t *s, uint64_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t bytes = (uint32_t)(n * 4u), v;
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f);
    v = 36 + bytes;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    {
        uint16_t fmt[2] = {1, 2};
        uint32_t rate[2] = {44100, 44100 * 4};
        uint16_t al[2] = {4, 16};
        fwrite(fmt, 2, 2, f);
        fwrite(rate, 4, 2, f);
        fwrite(al, 2, 2, f);
    }
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(s, 4, (size_t)n, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    int song = -1, i, voices = PL_VOICES_MOST, ended_for = 0, solo = 0;
    double seconds = 600;
    const char *wav = 0;
    static int16_t buf[2 * 256];
    int16_t *all = 0;
    uint64_t cap = 0;
    double sum = 0, peak = 0, blocks_sum[20000];
    int nblk = 0, most = 0;
    kf_t s1[2], h1[2];
    double bacc = 0;
    uint32_t bn = 0;
    if (argc < 3 || disk_load(argv[1])) {
        fprintf(stderr, "render DISK (--list | SONG [--seconds S] [--wav OUT] [--events] [--voices N] [--solo T])\n");
        return 2;
    }
    player_init();
    if (!strcmp(argv[2], "--list")) {
        uint8_t nums[256];
        int n = store_list(REC_SONG, nums, 256);
        for (i = 0; i < n; i++) {
            rec_song_t s;
            if (!rec_song(nums[i], &s))
                printf("%02X %s\n", nums[i], s.name);
        }
        return 0;
    }
    song = (int)strtol(argv[2], 0, 16);
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
            wav = argv[++i];
        else if (!strcmp(argv[i], "--events"))
            events = 1;
        else if (!strcmp(argv[i], "--voices") && i + 1 < argc)
            voices = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--solo") && i + 1 < argc)
            solo = atoi(argv[++i]);
    }
    kw_init(&s1[0], &h1[0]);
    kw_init(&s1[1], &h1[1]);
    player_voice_count(voices);
    player_play_song((uint8_t)song, 0, 0);
    if (solo > 0)
        player_mutes(~(1u << (solo - 1)) & 0xFFFFu);
    while (frames < (uint64_t)(seconds * 44100)) {
        player_state_t st;
        memset(&st, 0, sizeof st);
        player_audio(buf, 256);
        plat_c2_wait();                  /* (the half whole: what the test platform's second core still has of it) */
        for (i = 0; i < 512; i++) {
            double v = buf[i] / 32768.0;
            sum += v * v;
            if (fabs(v) > peak)
                peak = fabs(v);
        }
        for (i = 0; i < 256; i++) {      /* BS.1770: 400 ms blocks, 75 % overlap: here 100 ms steps */
            double l = kf(&h1[0], kf(&s1[0], buf[2 * i] / 32768.0)), r = kf(&h1[1], kf(&s1[1], buf[2 * i + 1] / 32768.0));
            bacc += l * l + r * r;
            if (++bn == 4410u) {
                if (nblk < 20000)
                    blocks_sum[nblk++] = bacc;
                bacc = 0;
                bn = 0;
            }
        }
        if (wav) {
            if (frames + 256 > cap) {
                cap = cap ? cap * 2 : 1u << 20;
                all = realloc(all, (size_t)cap * 4u);
            }
            memcpy(all + 2 * frames, buf, sizeof buf);
        }
        frames += 256;
        player_state(&st, 0);
        if (st.voices > most)
            most = st.voices;
        if (st.ended && !st.playing && ++ended_for > 172)   /* (its end, then 1 s of its tails) */
            break;
    }
    {
        double gated = 0, rel, lufs = -70;
        int n = 0, k;
        for (k = 3; k < nblk; k++) {     /* 400 ms windows from four 100 ms steps */
            double z = (blocks_sum[k] + blocks_sum[k - 1] + blocks_sum[k - 2] + blocks_sum[k - 3]) / 17640.0;
            double lk = -0.691 + 10 * log10(z + 1e-12);
            if (lk > -70) {
                gated += z;
                n++;
            }
        }
        if (n) {
            rel = -0.691 + 10 * log10(gated / n) - 10;
            gated = 0;
            n = 0;
            for (k = 3; k < nblk; k++) {
                double z = (blocks_sum[k] + blocks_sum[k - 1] + blocks_sum[k - 2] + blocks_sum[k - 3]) / 17640.0;
                double lk = -0.691 + 10 * log10(z + 1e-12);
                if (lk > -70 && lk > rel) {
                    gated += z;
                    n++;
                }
            }
            if (n)
                lufs = -0.691 + 10 * log10(gated / n);
        }
        if (!events)
            printf("song %02X: %.1f s, %.1f LUFS, %.1f dB RMS, peak %.2f, most %d voices, %u steals\n", song,
                   frames / 44100.0, lufs, 10 * log10(sum / (2.0 * (double)frames) + 1e-12), peak, most, pl_steals);
    }
    if (wav)
        wav_write(wav, all, frames);
    free(all);
    return 0;
}
