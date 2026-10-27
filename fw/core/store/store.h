/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The store (specs/files/store.md): records in the flash, the built-in instruments beside them, and the
 * edit memory (specs/files/ram.md#editing-and-saving): what edits change waits in RAM until SAVE, as pieces.
 *   Reading: store_get / store_info / store_read / store_list answer the edits first (an object deleted by an
 *   edit is absent), then the flash (the user area: FWS is the firmware's, never read for records), then the
 *   built-in things (F0-FF: changed ones are found first, deleted changes leave the firmware's). A pattern, a
 *   cartridge and LOOPS are edited as pieces laid over the flash's record: store_get answers that record, and
 *   rec.c (play/rec.h) reads them with their pieces (store_piece).
 *   Editing: store_edit_put / store_edit_piece / store_edit_del change things in RAM, heard at once
 *   (store_set_watch); store_edit_count (unsaved changes, SAVE's LED), store_edit_used / store_edit_room;
 *   store_edit_save (SAVE: every edit as one save, then the edits dropped; progress by store_save_status);
 *   store_edit_forget (REVERT of one thing), store_edit_revert (REVERT ALL: all dropped).
 *   Saving itself: store_save_begin / put / del / commit (below). Seeding: seed.h. */
#ifndef STORE_H
#define STORE_H
#include <stdint.h>
#include "builtin.h"

/* Record kinds (specs/files/store.md#a-record) */
enum {
    REC_DX7 = 0x01, REC_JUNO = 0x02, REC_TB303 = 0x03, REC_SAMPLER = 0x04, REC_FUSION = 0x05, REC_CHIP = 0x06,
    REC_CART_LAST = REC_CHIP,           /* (the cartridges: REC_DX7 .. REC_CART_LAST) */
    REC_AUDIO = 0x10, REC_PATTERN = 0x20, REC_SONG = 0x21, REC_LOOPS = 0x22, REC_DJ = 0x23,
    REC_SETTINGS = 0x30, REC_MIDIMAP = 0x31
};

/* The flash's areas (specs/files/store.md#where-things-go) */
#define STORE_BLOCK      4096u
#define STORE_HDR        16u
#define STORE_MAX_REC    4096u          /* a record a SAVE writes, header included, within one block */
#define STORE_U1_START   0x093000u      /* the user area, fast region 1 */
#define STORE_U1_END     0x0E0000u
#define STORE_U2_START   0x0E5000u      /* the user area, fast region 2 */
#define STORE_U2_END     0x0FF000u
#ifdef PTFM_EDITOR
/* The editor page's preview only (its WebAssembly; never the device, never the page's virtual FM-1): with
 * store_preview_room(1) before a mount, the user area is the flash from the firmware slot's start to the user area's
 * end, one region (the page's firmware is WebAssembly: nothing of it is in that flash), so an item bigger than an FM-1
 * holds still plays there */
#define STORE_P_START    0x004000u
#define STORE_P_END      0x0FF000u
void store_preview_room(int on);
#endif
#define STORE_RESERVE    16             /* blocks the uploader leaves free */
#define STORE_MIN_FREE   2              /* SAVE refuses to leave fewer free blocks than this */
#define STORE_EDITOR_ROOM 8u            /* blocks kept free on top of those for the EDITOR's own pattern: the
                                         * largest pattern's (specs/files/store.md#the-room-for-the-editors-pattern) */
#define STORE_EDITOR_PAT 0xFEu          /* the EDITOR's own pattern (specs/files/patterns.md#numbers) */
#define PAT_ROWS_MAX 128u               /* a pattern's rows at most (specs/files/patterns.md#size): a record of more
                                         * reads as its first 128, and is saved so */
#define STORE_LOOP_ROWS (4u * PAT_ROWS_MAX)    /* a loop's stream's rows at most: 4 lanes a row (specs/files/loops.md) */
/* a stream of n bytes with its 0x00 (specs/files/patterns.md): the bytes of its cells on the first rows (a pattern
 * track's PAT_ROWS_MAX, a loop's STORE_LOOP_ROWS) and one more for the 0x00 after them, what a save or a copy keeps
 * of it */
uint32_t store_track_kept(const uint8_t *s, uint32_t n, uint32_t rows);

void store_init(void);                  /* power-on: the scan, the checks, the map (= store_mount) */
void store_mount(void);                 /* the scan again: after seeding, and REVERT */
void store_poll(void);                  /* the main loop: a SAVE under way, a page or so a pass */

/* The live record (kind, num, sub): a pointer to its payload, readable in place (the user area, or the
 * firmware's built-in things for F0-FF), its length and version; NULL when there is none. For a pattern, a
 * cartridge and LOOPS: the flash's record the pieces lie over (store_piece), NULL while an edit leaves it out (the
 * thing deleted, or made anew; a built-in one's own then). LOOPS' in any version: its pieces lie over version 4
 * only, and its readers take that alone (another is kept in the flash as it is, till LOOPS is saved).
 * The map changes only at a mount and when a save ends: don't hold a pointer across a SAVE, and read
 * nothing from the audio context while a save is being written (SAVE waits for a pause). */
const uint8_t *store_get(uint8_t kind, uint8_t num, uint8_t sub, uint32_t *len, uint8_t *ver);
/* the record as saved: the flash's (or the firmware's built-in one), the edits left aside (REVERT's "as saved") */
const uint8_t *store_get_saved(uint8_t kind, uint8_t num, uint8_t sub, uint32_t *len, uint8_t *ver);

/* The numbers that have a live record of a kind (sub 0, or any sub for REC_PATTERN), ascending, built-in
 * ones included, the edits counted; returns how many (at most max). */
int store_list(uint8_t kind, uint8_t *nums, int max);

/* one built-in more, made by the firmware at start and read, listed and edited as builtin.c's are (the keys' song,
 * the EDITOR's own pattern: ui_data.c); up to 4, one of the same kind, number and sub replaced; its payload the
 * caller's, for good */
void store_builtin_extra(const builtin_rec_t *r);
/* every built-in: builtin.c's, then the extras (seeding's layout lists them all) */
int store_builtin_count(void);
const builtin_rec_t *store_builtin_at(int i);

/* ---- the edit memory (specs/files/ram.md#editing-and-saving) ----
 * What differs from the flash, as pieces, each piece once: a pattern's tracks (id 0-15, each a stream ending in
 * its 0x00; an empty one a lone 0x00) and its head (STORE_PC_HEAD: tracks, rows - 1, EDIT STEP); a cartridge's
 * slots (id 0-255), its head (STORE_PC_HEAD: the record's bytes before its slots) and a sampler's samples' table
 * (STORE_PC_TABLE); LOOPS' head (STORE_PC_HEAD: its record before the streams, the loop table's end), its streams'
 * head (STORE_PC_PHEAD: the loops, rows - 1, 0) and each loop's stream (id 0-15, STORE_LOOP_STREAM_MAX at most;
 * the flash's LOOPS lies under them only in version 4); every other record whole. A piece changed again is
 * rewritten where it lies; one that becomes what the flash holds goes (nothing unsaved by it). Payloads are
 * copied in (never from the edit memory itself). Every change is made with the audio context held off and told
 * to the watch (store_set_watch) meanwhile, so what plays hears it from its next row or note.
 * Results: STORE_OK, or STORE_ERR_FULL (MEMORY FOR EDITS IS FULL: nothing changed), STORE_ERR_BIG (over 4 KB; a loop's
 * stream over STORE_LOOP_STREAM_MAX), STORE_ERR_STATE (while seeding or a save is being written). A built-in thing
 * (F0-FF) is edited as any: its changed copy is saved in the user area and found before the firmware's own; deleted,
 * the firmware's own is back. A pattern and LOOPS are saved in as many parts as their streams need. */
#ifndef STORE_EDIT_MAX                  /* (the tests build it larger too) */
#define STORE_EDIT_MAX   68608u         /* the edit memory, 67 KB: all the edits' (the spare zone is a zone of its own) */
#endif
enum { STORE_PC_HEAD = 0x100, STORE_PC_TABLE = 0x101, STORE_PC_PHEAD = 0x102 };
#define STORE_LOOP_STREAM_MAX 3800u     /* a loop's stream at most (specs/files/loops.md#a-loops-stream): it fits any
                                         * part with LOOPS' head before it */
/* a whole record: a pattern or LOOPS (sub 0, all its streams in this one part), a cartridge laid out as pieces,
 * only those that differ from the flash's kept, all or nothing; any other record whole */
int store_edit_put(uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, const void *payload, uint32_t len);
int store_edit_piece(uint8_t kind, uint8_t num, uint16_t id, uint8_t ver, const void *p, uint32_t len);
int store_edit_del(uint8_t kind, uint8_t num, uint8_t sub);     /* a pattern, a cartridge: the thing whole (any sub) */
/* REVERT (specs/ui/saving.md#revert): the thing's changes dropped, back as the flash has it: a record; a pattern or
 * LOOPS whole; a cartridge whole, with the other cartridge kinds at its number and its samples' deletes */
int store_edit_forget(uint8_t kind, uint8_t num, uint8_t sub);
int store_edit_forget_piece(uint8_t kind, uint8_t num, uint16_t id);   /* one piece (a slot) back as saved */
const uint8_t *store_piece(uint8_t kind, uint8_t num, uint16_t id, uint32_t *len);   /* the edited piece, or NULL
                                         * (the audio context too) */
enum { STORE_OBJ_EDITED = 1, STORE_OBJ_HIDDEN = 2 };
int store_edit_obj(uint8_t kind, uint8_t num);  /* a pattern's, a cartridge's, LOOPS': 0 not edited, or EDITED with
                                         * HIDDEN when the flash's record is left out (deleted, or made anew) */
/* the record as the edits make it, built into dst (at most max): a pattern's or LOOPS' part 0 with all its streams
 * (-2 when they do not fit one), a cartridge's, a whole one's. 0 done (len, ver), -1 none, -2 too big */
int store_edit_build(uint8_t kind, uint8_t num, uint8_t sub, uint8_t *dst, uint32_t max, uint32_t *len, uint8_t *ver);
typedef void (*store_watch_fn)(uint8_t kind, uint8_t num, uint16_t id);
void store_set_watch(store_watch_fn fn); /* told of every piece written, moved or gone (the main loop, the audio
                                         * context held off): kind, num and the piece's id (a whole record's sub;
                                         * 0x1FF the thing's own entry, and after a SAVE or REVERT ALL) */
/* While held, a piece rewritten goes to the pieces' top, its old bytes left where they lay (pointers into them taken
 * before still read them), and no piece moves: an edit of several pieces that reads the old ones as it goes (a
 * block moved between tracks). store_edit_reserve first: n bytes clear above the pieces (moved down now if need be);
 * 1 there, 0 not (MEMORY FOR EDITS IS FULL). */
void store_edit_hold(int on);
int store_edit_reserve(uint32_t n);
int store_edit_count(void);             /* unsaved changes: the things changed */
int store_edit_has(uint8_t kind, uint8_t num, uint8_t sub);     /* 1 changed, 2 deleted, 0 not edited (a pattern,
                                         * a cartridge: the thing, any sub) */
uint32_t store_edit_used(void);         /* the bytes the edits take (their pieces and their list's entries) */
uint32_t store_edit_room(void);         /* what is left for edits */
uint32_t store_edit_top(void);          /* the edit memory filled: from its bottom to the last piece's end, plus the
                                         * list at its top (room freed between pieces counts until pieces move down) */
uint32_t store_edit_limit(void);        /* its size (STORE_EDIT_MAX: nothing borrows from it) */
int store_edit_save(void);              /* SAVE: starts the save (store_poll writes it); STORE_OK or why not */
void store_edit_revert(void);           /* REVERT ALL: every edit dropped */
uint32_t store_edit_moves(void);        /* goes up whenever a piece moves or goes: a pointer into one taken before
                                           is stale */
/* An edit mode's copy (specs/ui/controls.md: EDIT keeps, HOME drops): from store_edit_session(1), each thing's piece
 * as it was before the mode first changed it is kept in the edit memory; store_edit_session(0) drops the copies
 * (EDIT: the changes kept), store_edit_session_drop puts every piece back as its copy has it (HOME): 1 when
 * something changed back. A SAVE or REVERT ALL ends the copies. */
void store_edit_session(int on);
void store_edit_session_of(uint8_t kind, uint8_t num);  /* the copies of one thing's pieces only (SONGS' order, which
                                         * the EDITOR may be opened from meanwhile) */
int store_edit_session_drop(void);

/* Where a live record is. */
enum { STORE_AREA_USER = 0, STORE_AREA_BUILTIN = 2, STORE_AREA_EDIT = 3 };
typedef struct {
    uint8_t kind, num, sub, ver;
    uint8_t area;                       /* STORE_AREA_* */
    uint8_t deleted;                    /* a deleted record (the map keeps it while it hides an older one) */
    uint32_t addr;                      /* the header's flash address (0 for a built-in one) */
    uint32_t len;                       /* the payload's length */
} store_rec_t;
int store_info(uint8_t kind, uint8_t num, uint8_t sub, store_rec_t *r);     /* 1 live, 0 none */
/* Reads part of a live record's payload, wherever it is; 0 done. */
int store_read(uint8_t kind, uint8_t num, uint8_t sub, uint32_t off, void *dst, uint32_t n);

/* ---- saving (specs/files/store.md#saving) ----
 * begin; put / del each record; commit. Payloads must stay where they are until the save ends. commit
 * checks the room first (compacting when free blocks run short, as saves of their own) and lays the
 * records out; store_poll() then writes them a record at a pass (store_save_run() writes it all at once).
 * Putting a cartridge under a number another cartridge kind holds deletes that one in the same save (a
 * sampler's audio with it). */
enum {
    STORE_OK = 0, STORE_BUSY = 1,
    STORE_ERR_FULL = -1,                /* fewer than 2 free blocks would be left, even after compaction, or (what
                                         * it writes, the EDITOR's pattern and deletes apart) fewer than 10 */
    STORE_ERR_BIG = -2,                 /* a record over 4 KB */
    STORE_ERR_MANY = -3,                /* more records than a save takes (STORE_SAVE_MAX) */
    STORE_ERR_NUM = -4,                 /* (no longer returned: F0-FF are edited as any number) */
    STORE_ERR_FLASH = -5,               /* a program or erase failed, or read back wrong */
    STORE_ERR_STATE = -6                /* not now: no save begun, one being written, or seeding */
};
#define STORE_SAVE_MAX 208              /* records a save through this API takes (the edits' save: all of them) */
int store_save_begin(void);
int store_save_put(uint8_t kind, uint8_t num, uint8_t sub, uint8_t ver, const void *payload, uint32_t len);
int store_save_del(uint8_t kind, uint8_t num, uint8_t sub);
int store_save_commit(void);
void store_save_cancel(void);           /* drops a save not yet committed */
int store_save_status(uint8_t *percent);/* STORE_BUSY while writing, then its result once (STORE_OK when idle) */
int store_save_run(void);               /* commit and write it all now: the result */
uint32_t store_save_short(void);        /* the last save refused for room (STORE_ERR_FULL): the bytes it lacked */

/* ---- the figures (STATUS' STORAGE) ---- */
#define STORE_NKINDS 13
extern const uint8_t store_kinds[STORE_NKINDS];        /* the kinds the counts below are by */
typedef struct {
    uint32_t user_bytes, user_free;     /* the user area: its size; free = free blocks and the open block's tail,
                                         * less compaction's blocks and the EDITOR's room (counted as used) */
    uint16_t user_blocks, user_free_blocks;   /* (the free blocks as they are) */
    uint32_t user_records, user_audio;  /* the live records' bytes there (headers included), audio among them */
    uint32_t fw_end;                    /* where the firmware's image ends */
    uint16_t count[STORE_NKINDS];       /* live records by kind */
    uint32_t bytes[STORE_NKINDS];       /* their payloads' bytes */
    uint16_t bad;                       /* records that failed their check at the last mount */
    uint16_t lost;                      /* records the map had no room for at the last mount */
    uint32_t last_save;                 /* the newest save's number */
    uint8_t mount_walks;                /* walks over the flash the last mount took (1 unless a save was cut short) */
} store_usage_t;
void store_usage(store_usage_t *u);

/* ---- things ----
 * A thing is the records that make it: a song (REC_SONG), a pattern or LOOPS (all its parts), a
 * cartridge (any cartridge kind: the cartridge, a sampler's with its samples' audio), a sample (REC_AUDIO:
 * num the cartridge, sub the sample). store_thing lists a thing's live records (what the flash holds, not the
 * edits; none for a built-in one); store_thing_bytes what they take (headers included). */
typedef struct { uint8_t kind, num, sub; } store_key_t;
#define STORE_MOVE_MAX 128              /* records a thing has at most (a sampler cartridge: 1 + up to 64 samples) */
int store_thing(uint8_t kind, uint8_t num, uint8_t sub, store_key_t *keys, int max);
uint32_t store_thing_bytes(const store_key_t *keys, int n);
/* CLONE of a sampler cartridge with samples (specs/files/store.md): every sample's audio of `from` (wherever it
 * lies; one deleted by an edit left out) copied under `to` in the user area, then its record (rec, len, ver: the
 * cartridge as it stands, renamed by the caller) under `to`, as one save, at once (the audio paused, as for any
 * write; an UNDO does not take it back). move: MOVE instead, the old number's records deleted in the same save
 * (refused while edits of them wait). STORE_ERR_STATE (an edit waits at `to`, or a move's at `from`; seeding, a
 * save under way), STORE_ERR_FULL (no room: audio in one run of free blocks, the reserve kept), STORE_ERR_BIG,
 * STORE_ERR_FLASH */
int store_clone_sampler(uint8_t from, uint8_t to, uint8_t ver, const uint8_t *rec, uint32_t len, int move);

/* store_gen: the store's generation, up whenever a pointer a reader took into a record may no longer hold (a record
 * put, dropped, mounted; an edit's piece written, moved, gone); whoever keeps such pointers across calls (rec.c's
 * patterns found ahead) trusts them only while it is unchanged. Read anywhere. */
uint32_t store_gen(void);

/* CHECK STORAGE (specs/ui/screens/settings.md): every record in the user area read and its CRC checked; what fails
 * and mattered: a thing that fell back to an older version, one now missing, or one damaged where it is read.
 * Writes nothing. Returns how many (out: the first max of them). */
enum { STORE_FELL_BACK = 1, STORE_MISSING = 2, STORE_DAMAGED = 3 };
typedef struct { uint8_t kind, num, sub, area, what; } store_problem_t;
int store_check(store_problem_t *out, int max);

/* ---- for seeding (seed.c) ---- */
#define STORE_SEED_SCRATCH 1152         /* seeding's messages in the scratch: one in (unpacked), one out (packed) */
void store_lock(int on);                /* while seeding: store_get answers NULL, no save starts */
int store_saving(void);                 /* a save is being made (its list) or written */
/* STATUS' USER FLASH MAP (specs/ui/screens/status.md): the user area KB by KB in the flash's order (its two regions one
 * after the other), what each holds: the kind with most live bytes in it; a KB of a block in use without a live
 * byte (an older copy's), its block's kind; FREE a free block's (as store_usage counts them), OPEN the open block's
 * tail. Returns the KB written (the user area's, at most max). Main loop only. */
enum { STORE_KB_FREE, STORE_KB_OPEN, STORE_KB_SAMPLES, STORE_KB_INSTRUMENTS, STORE_KB_PATTERNS, STORE_KB_SONGS,
       STORE_KB_LOOPS, STORE_KB_OTHER };
int store_kb_map(uint8_t *kb, int max);
uint32_t store_ram_index(void);         /* the bytes of its index (the map) and scratch in RAM (STATUS' RAM MAP: SYSTEM) */
int store_count(void);                  /* entries in the map */
int store_entry(int i, store_rec_t *r, uint32_t *save, uint32_t *crc);     /* the i-th, its save, its payload's CRC */
uint32_t store_last_save(void);
/* The scratch, STORE_SCRATCH_BYTES, shared by the store's mount, a save's list (store_save_*: from begin
 * until the save is written), the edits' save (each record built there as it is written), an edit mode's copy
 * put back, seeding's messages, and anyone else who claims it: store_scratch_claim(1) gives it, or NULL while a
 * save is being made or another holds it; while claimed no save begins; claim(0) gives it back. A mount
 * (power-on, seeding's end) uses it regardless.
 * store_scratch() gives it without claiming: only where nothing else can be using it. */
#define STORE_SCRATCH_BYTES 4096
uint8_t *store_scratch_claim(int on);
uint8_t *store_scratch(void);
uint32_t store_crc_flash(uint32_t crc, uint32_t addr, uint32_t n);      /* CRC-32 of flash bytes */
int store_all_ff(uint32_t addr, uint32_t n);                            /* flash bytes all erased */

#endif
