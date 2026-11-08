/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM's records as the player and the screens read them (specs/files/): songs, patterns (a cursor for
 * each track's stream), cartridges and their slots, names. Every reader takes the record's payload through
 * store_get (store.h), in place, and a pattern's or a cartridge's edited pieces over it (store_piece): a changed
 * track, slot or head from the edit memory, the rest from the flash; nothing is copied but what a
 * caller asks for. Portable; its one state is the patterns found ahead (rec_ahead). */
#ifndef REC_H
#define REC_H
#include <stdint.h>

/* ---- bits and names (specs/files/README.md#names-numbers-and-bits) ---- */
uint32_t rec_bits(const uint8_t *p, uint32_t pos, int n);       /* n (<= 32) bits from bit pos, LSB first */
void rec_name(const uint8_t *p, uint32_t pos, int chars, char *out);   /* 6-bit characters; out: chars + 1 */
#define REC_NAME_EMPTY 63                                       /* a slot's first character: empty */

/* ---- songs (specs/files/songs.md) ---- */
enum { RS_STOP = 1, RS_LINEAR = 2, RS_FASTVOL = 4 };            /* the flags */
#define REC_TEMPO_MIN 3u                                        /* TEMPO, a song's and the loops' (a tick 2.5 s / TEMPO) */
#define REC_TEMPO_MAX 600u
enum { RM_LEVEL, RM_PAN, RM_DUCK, RM_CHORUS, RM_DELAY, RM_REVERB, RM_N };   /* a track's mixer bytes */
typedef struct {
    char name[17];
    uint16_t tempo;             /* TEMPO, 3-600 */
    uint8_t ticks, rpb, swing;  /* TICKS A ROW, ROWS A BEAT, SWING 50-75 */
    uint8_t flags;              /* RS_* */
    uint8_t loop_from, gvol;    /* LOOP FROM, the global volume at the start (0-64) */
    uint16_t len;               /* positions, 1-256 */
    const uint8_t *order;       /* len pattern numbers, in the record */
    uint8_t mix[16][RM_N];      /* every track's mixer (the defaults past those stored) */
    const uint8_t *fx;          /* the FX setup, 64 B, in the record */
} rec_song_t;
int rec_song(uint8_t num, rec_song_t *s);                       /* 0 read, -1 none or bad */

/* ---- patterns (specs/files/patterns.md) ---- */
#define REC_NOTE_OFF 0x01
typedef struct {
    uint8_t ntrk;               /* tracks, 1-16 */
    uint16_t rows;              /* rows, 1-128 (PAT_ROWS_MAX, store.h: a record of more reads as its first 128) */
    uint8_t step;               /* EDIT STEP */
    const uint8_t *trk[16];     /* each track's stream (NULL: an empty track, or one not ending in its part) */
    const uint8_t *trk_end[16]; /* where each stream's part ends (no cell is read past it) */
} rec_pat_t;
int rec_pattern(uint8_t num, rec_pat_t *pt);                    /* 0 read (all its parts), -1 none or bad */
int rec_pattern_size(uint8_t num, uint8_t *ntrk, uint16_t *rows);   /* its header alone: 0 read, -1 none */
/* Patterns found ahead: the main loop has rec_pattern's answer ready for the patterns about to play (rec_ahead:
 * these, at most REC_AHEAD; the others let go), so the audio context copies it instead of walking every track's
 * cells to find where the next starts. An answer is trusted only while the store's generation (store_gen) is what
 * it was when found; else rec_pattern finds it again, as without it. rec_ahead: main loop only */
#define REC_AHEAD 8
void rec_ahead(const uint8_t *nums, int n);
extern volatile uint8_t rec_audio_ctx;  /* set by the player while the audio context runs (the host tests' checks of
                                         * the patterns found ahead count by it) */

enum { RC_NOTE = 1, RC_VOL = 2, RC_INS = 4, RC_CMD = 8 };
typedef struct {
    uint8_t has;                /* RC_* */
    uint8_t note;               /* MIDI 12-127, or REC_NOTE_OFF */
    uint8_t cart, slot;         /* the instrument (RC_INS) */
    uint8_t vol;                /* 00-7F (RC_VOL) */
    uint8_t n;                  /* commands, 0-4 */
    uint8_t cmd[4][2];          /* code, value */
} rec_cell_t;

/* A track's cursor: rec_cur_at() gives the cell on a row (1) or says there is none (0); rows asked for in
 * order cost one step each, a row before the last asked for starts again from the stream's start. */
typedef struct {
    const uint8_t *s, *p;       /* the stream, the next cell's head */
    int16_t row;                /* the next cell's row (0x7FFF: no more) */
    int16_t asked;              /* the last row asked for */
    uint8_t cart, slot, ins;    /* the last instrument in this record (ins: one has come) */
    const uint8_t *e;           /* nothing is read from here on: its part's end (else 4 KB on, a part's most) */
} rec_cur_t;
void rec_cur_set(rec_cur_t *c, const uint8_t *stream);          /* NULL: an empty track */
void rec_cur_set_end(rec_cur_t *c, const uint8_t *stream, const uint8_t *end);   /* its part's end known */
int rec_cur_at(rec_cur_t *c, int row, rec_cell_t *out);
/* the cursor onto another stream of its track (an edit: the track rewritten, moved, back as saved), at the same
 * place: the rows up to the last asked for passed again, their instruments taken; the next row asked for goes on */
void rec_cur_reseat(rec_cur_t *c, const uint8_t *stream, const uint8_t *end);

/* ---- the loops (specs/files/loops.md, version 4) ----
 * Their head (the record's bytes before the streams: 80 B of setup, then 12 a loop) as the edits leave it (its piece,
 * else the flash's part 0), and each loop's stream (its piece, else whichever of the flash's parts holds it). Reads
 * only, through store_piece and store_get: the audio context may call it too. The pointers hold until the store
 * changes (store_gen, the store's watch). */
#define REC_LOOPS_VER 4
#define REC_LOOPS_TABLE 80                                      /* the loop table's start in the head */
#define REC_LOOPS_ENTRY 12                                      /* a loop's bytes in it */
#define REC_LOOPS_MAX 16
typedef struct {
    const uint8_t *head;        /* the head (bytes 0-79 the setup, then the table) */
    uint32_t head_len;
    uint8_t n;                  /* the loops, 0-16 (as many as the head holds) */
    uint8_t rows;               /* the length in rows, 1-128, with loops or none (byte 7; a byte 7 of 0, a record from
                                 * before with no loops: rec_loops_rows_default); 0 when there is no record (-1) */
    const uint8_t *trk[16];     /* each loop's stream (NULL: empty; filled only when asked for) */
    const uint8_t *trk_end[16]; /* where each stream's part ends */
} rec_loops_t;
int rec_loops(rec_loops_t *l, int streams);                     /* 0 read (streams: theirs too), -1 none */
/* the length a head with none (byte 7 0) has: 4 bars, or the most whole bars that fit 128 rows where 4 do not (a bar
 * BEATS A BAR beats, the FX setup's byte 11 (1-16; 0 read as 4), each the grid's rows: 4, 6 or 8) */
int rec_loops_rows_default(const uint8_t *head);
typedef struct {
    uint8_t cart, slot;         /* its instrument */
    uint8_t colour;             /* 0-11 */
    uint8_t mono, muted;
    uint8_t lanes;              /* 1-4 */
    uint8_t base;               /* the rows its notes cover, 1-128 */
    uint8_t repeats;            /* 1-255 */
    uint8_t shift;              /* its SHIFT: the rows it plays later, round the length (stored 0-127: entry byte 2's bit 7
                                 * its bit 6, byte 3's bits 7-2 its bits 5-0; read modulo the length) */
    const uint8_t *mix;         /* its mixer bytes (RM_*), in the head */
} rec_loop_t;
void rec_loop(const rec_loops_t *l, int i, rec_loop_t *o);      /* loop i's table entry (i < l->n) */
#define REC_LOOP_SHIFT(e) (((e)[2] >> 7) << 6 | (e)[3] >> 2)    /* an entry's SHIFT as stored, 0-127 */
/* the loops' cells as the player and LOOPS' screen read them, one copy for both. rec_bridge: c is a gap's bridge, nothing
 * in it (an ED0 alone: no cell to a reader). rec_stage_cells: the stage's row r through cu (set on its stream, rows asked
 * for in order), *c4 its lanes' cells; the lanes with one returned (a bit a lane; a bridge none). rec_stage_merge: a MONO
 * stage's row (has: the lanes with a cell; *from: the lane whose note sounds, -1 none, carried from row to row), as REC
 * writes it: the later lane's note takes over; else an end on from's lane (from none then); else nothing. The lane of
 * the cell it plays, -1 none */
int rec_bridge(const rec_cell_t *c);
int rec_stage_cells(rec_cur_t *cu, int r, rec_cell_t *c4);
int rec_stage_merge(int has, const rec_cell_t *c4, int8_t *from);
/* a lane's note offs over its n steps (bit k: at step k), the pattern recorder's (REC's stream and the preview): each of
 * its notes (on) held nr rows past its ties' steps (tie), ended where that comes before the lane's next note (round its
 * steps: the next repeat's first) and the step is not one of its notes; nr 0 (TO THE NEXT): none */
uint32_t rec_note_offs(uint32_t on, uint32_t tie, uint32_t n, uint32_t nr);
int rec_free_pattern(void);             /* the first pattern number of the user's (00-EF) without a pattern, -1 none */

/* ---- cartridges (specs/files/instruments.md) ----
 * A cartridge's record: its 10 bytes (the name, the slots' count), then the slots, every kind alike; read in the
 * version this firmware reads (a sampler's REC_SAMPLER_VER: instruments only, each naming a sample by its number; every
 * other kind's 1). A record of another version (an old sampler) is not read: its number is only taken */
#define REC_CART_HEAD 10
typedef struct {
    uint8_t kind;               /* REC_DX7 ... REC_CART_LAST (store.h); 0 none */
    uint8_t num;
    uint8_t nslots;             /* slots stored (a sampler: instruments) */
    uint8_t edited;             /* it has edited pieces (store_piece) */
    const uint8_t *head;        /* its first bytes: the name (12 characters), the slots' count */
    const uint8_t *base;        /* the flash's record under the pieces (NULL: none, a new one) */
    uint32_t base_len;
    uint8_t base_n;             /* the slots the flash's holds */
} rec_cart_t;
int rec_cart(uint8_t num, rec_cart_t *c);                       /* 0 read, -1 none or bad */
uint8_t rec_cart_ver(uint8_t kind);                             /* the version read and written: a sampler's
                                                                 * REC_SAMPLER_VER, any other cartridge's 1 */
/* the number in use by a cartridge: one rec_cart reads, or one it does not (an old sampler: never touched, its number
 * taken: no NEW, CLONE or MOVE lands there, specs/files/instruments.md#the-old-form); 1 so */
int rec_cart_taken(uint8_t num);
const uint8_t *rec_slot(const rec_cart_t *c, uint8_t slot);     /* its bytes; NULL past those stored or empty (and
                                                                 * always in a FAVORITES cartridge: no instrument) */
/* a FAVORITES cartridge's slot (specs/files/instruments.md#favorites): its 3 bytes, the source's cartridge and slot;
 * NULL empty, past those stored, or not a FAVORITES cartridge */
const uint8_t *rec_fav(const rec_cart_t *c, uint8_t slot);
void rec_cart_name(const rec_cart_t *c, char *out);             /* 12 characters + 0 */
uint32_t rec_slot_at(const rec_cart_t *c, uint8_t slot);        /* where a slot lies in the record (store_edit_build) */
int rec_slot_size(uint8_t kind);
int8_t rec_level(uint8_t kind, const uint8_t *slot);            /* LEVEL, signed half-dB */
void rec_slot_name(uint8_t kind, const uint8_t *slot, char *out);   /* 10 characters + 0 */

void rec_dx7_vced(const uint8_t *slot, uint8_t *vced);          /* into MSFA's 156-byte layout */
void rec_juno_patch(const uint8_t *slot, uint8_t *patch);       /* into the Juno-106's 18 bytes (juno.h) */
void rec_tb_knobs(const uint8_t *slot, uint8_t *knobs);         /* WAVE, CUTOFF, RESONANCE, ENV MOD, DECAY, ACCENT */
uint8_t rec_fusion_pattern(const uint8_t *slot);

/* a sample instrument (specs/files/instruments.md#sample-instruments, version 2): SAMPLE (8 bits, a sample's number),
 * START, END, LOOP START, LOOP END (20 each), LOOP (2), BASE NOTE (7), TUNE (8), LEVEL (8), VOLUME (7), the envelope
 * (8 x 7), the name (60) from bit 176, spare 84 */
enum { RL_OFF, RL_FORWARD, RL_PINGPONG };
typedef struct {
    uint8_t sample;             /* a sample's number, 00-FF (rec_sample) */
    uint32_t start, end, loop_start, loop_end;
    uint8_t loop;               /* RL_* */
    uint8_t base;               /* BASE NOTE */
    uint8_t tune;               /* 1/128 semitones, 0x80 none */
    int8_t level;
    uint8_t volume;             /* 0-64 */
    uint8_t env[8];             /* R1-R4, L1-L4 */
} rec_sinst_t;
void rec_sampler_inst(const uint8_t *slot, rec_sinst_t *si);
uint8_t rec_sampler_volume(const uint8_t *slot);                /* its VOLUME alone (rec_sinst_t's volume) */
/* a sample (specs/files/instruments.md#samples): the record (REC_SAMPLE, num, 0), version 1, read in place: its entry
 * (16 B: length 20 bits, rate 18, coding 4, name 60 from bit 42), then its PTR audio, the index's offsets from there */
typedef struct {
    uint32_t length;            /* samples */
    uint32_t rate;              /* Hz at the BASE NOTE */
    uint8_t coding;             /* 1 PTR 8-bit, 2 PTR 16-bit */
    const uint8_t *entry;       /* its entry, in place (its name: rec_name(entry, 42, 10, ...)) */
    const uint8_t *audio;       /* its audio, in place (eng/samp's PTR record) */
    uint32_t audio_len;
} rec_sample_t;
/* 0 there; -1 none: no such record, another version, or one read as missing (its entry and its audio's head differ,
 * store_sample_ok; its name an empty slot's). Reads only (the audio context too) */
int rec_sample(uint8_t num, rec_sample_t *s);

#endif
