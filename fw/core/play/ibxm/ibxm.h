/* ibxm/ac mod/xm/s3m replay (c)mumart@gmail.com */
/* The FM-1 changes: Copyright (C) 2026 Vasily Kazantsev, BSD-3-Clause as IBXM (LICENSE-ibxm.txt). */

/* PT/FM's IBXM (marked "FM-1:" where it differs from upstream 83b5d9a): the replay runs PT/FM's songs
   (specs/files/commands.md: cells of up to four effects from a hook), without audio: the player's engines
   play its notes (the additions at the end). No loader, no sample playback (compiled out: IBXM_LOADERS,
   IBXM_AUDIO); no heap (ibxm_port.h); instruments point at shared tables (no key map nor envelopes). */

extern const char *IBXM_VERSION;

#define IBXM_MAX_CHANNELS 16             /* FM-1: a pattern's tracks at most */
#define IBXM_TEMPO_MIN 125               /* FM-1: the tick buffers' size */

struct sample {
	int loop_start, loop_length;
	short volume, panning, rel_note, fine_tune;
};

struct envelope {
	char enabled, sustain, looped, num_points;
	short sustain_tick, loop_start_tick, loop_end_tick;
	short points_tick[ 16 ], points_ampl[ 16 ];
};

struct instrument {
	int num_samples, vol_fadeout;
	const char *key_to_sample;  /* FM-1: 97 entries, shared */
	char vib_type, vib_sweep, vib_depth, vib_rate;
	const struct envelope *vol_env, *pan_env;  /* FM-1: shared */
	struct sample *samples;
};

struct pattern {
	unsigned char num_channels;  /* FM-1: a song's tracks (1-16) and rows (1-128) in 4 bytes, 256 of them */
	unsigned short num_rows;
};

/* FM-1: a cell as the replay runs it (it was ibxm.c's own): XM's key (1..96, 97 key off),
   instrument, volume column, effect and param; and, for PT/FM's cells of up to four commands, up to
   three more effects (run after the first, in their order: 0 0 none) and a volume-column effect of
   its own (0x60..0xFF; 0: none, the volume byte's is used), so a cell can set a volume and have one */
struct note {
	unsigned char key, instrument, volume, effect, param;
	unsigned char fx2[ 3 ], param2[ 3 ], vcol;
};

struct module {
	char name[ 32 ];
	int num_channels, num_instruments;
	int num_patterns, sequence_len, restart_pos;
	int default_gvol, default_speed, default_tempo, c2_rate, gain;
	int linear_periods, fast_vol_slides;
	unsigned char *default_panning, *sequence;
	struct pattern *patterns;
	struct instrument *instruments;
	const void *file;  /* FM-1: (a module's file: unused by songs) */
	/* FM-1: a PT/FM song run by the replay (no audio: the player's engines sound it; see the additions
	   below): its cells come from get_note (pattern: its index); fm1_song turns on PT/FM's own rules
	   (sequence end at restart_pos, F00, Kxx's tick, PANBRELLO) */
	void ( *get_note )( struct module *module, int pattern, int row, int chan, struct note *dest );
	int fm1_song;
};

/* FM-1: a key map and envelopes for instruments that have none (all zero) */
extern const char ibxm_no_keys[ 97 ];
extern const struct envelope ibxm_no_env;
/* FM-1: memory for the module's tables and the replay (ibxm_port.h's arena): start over */
void ibxm_arena_reset( void );
void *ibxm_alloc( unsigned int count, unsigned int size );
unsigned int ibxm_arena_used( void );  /* (bytes taken) */
void ibxm_arena_use( int k );           /* FM-1: arena 0 or 1 from now (the player's two replays) */
void ibxm_arena_mem1( void *mem );      /* FM-1: arena 1's memory (ibxm_arena_size bytes), NULL none */
unsigned int ibxm_arena_size( void );
/* FM-1: a new song: forget the pattern unpacked last (call before its first replay) */
void ibxm_forget_pattern( void );

/* Allocate and initialize a replay with the specified module and sampling rate. */
struct replay* new_replay( struct module *module, int sample_rate, int interpolation );
/* Deallocate the specified replay. */
void dispose_replay( struct replay *replay );
/* Returns the song duration in samples at the current sampling rate. */
int replay_calculate_duration( struct replay *replay );
/* Seek to approximately the specified sample position.
   The actual sample position reached is returned. */
int replay_seek( struct replay *replay, int sample_pos );
/* Set the pattern in the sequence to play. The tempo is reset to the default. */
void replay_set_sequence_pos( struct replay *replay, int pos );
/* Returns the currently playing pattern in the sequence.*/
int replay_get_sequence_pos( struct replay *replay );
/* Returns the currently playing row in the pattern. */
int replay_get_row( struct replay *replay );
/* Returns the length of the output buffer required by replay_get_audio(). */
int calculate_mix_buf_len( int sample_rate );

/* ---- FM-1 additions ---- */

/* replay_set_sequence_pos, but starting at a row (0..63) of that position: only that row
   is processed. Channels, speed and tempo are reset as there. */
void replay_set_position_row( struct replay *replay, int pos, int row );
/* Advance one tick without mixing (as replay_seek does); the next mixed tick fades in.
   Returns the tick length in frames. */
int replay_skip_tick( struct replay *replay );

#define IBXM_INFO_CHANNELS 16
struct ibxm_channel_info {
	int instrument, key, volume, ampl;      /* key: the last note played (1..96), 0 none */
	const struct sample *sample;
	int sample_idx, freq, loop_start, loop_length;
};
struct ibxm_info {
	int seq_pos, row, tick, speed, tempo, sequence_len, num_channels;
	int row_count;                          /* rows processed so far (counts on) */
	struct ibxm_channel_info channel[ IBXM_INFO_CHANNELS ];
};
/* A snapshot of the replay for displays. */
void replay_get_info( struct replay *replay, struct ibxm_info *info );
/* Rows processed so far: changes when a tick starts a new row. */
int replay_get_row_count( struct replay *replay );
/* Pause the sequence in the current row: called before each replay_get_audio(), the row never
   ends (an endless pattern delay: notes ring on, tick effects go on, no new row). */
void replay_hold_row( struct replay *replay );
/* Start the next row now (breaks, jumps and loops as usual); nothing is skipped in the audio. */
void replay_next_row( struct replay *replay );
/* FM-1: the next row as replay_next_row takes it, but only its cells read and its global effects (the path,
   speed, tempo): the channels untouched. For a chase whose channels start over at its end. */
void replay_fm1_chase_row( struct replay *replay );
/* Start row (pos, row) now, as if it came next: the channels go on (no reset). */
void replay_jump_row( struct replay *replay, int pos, int row );
/* Go on at row (pos, row) when the current row ends, as a pattern break does: a skip while playing. */
void replay_queue_row( struct replay *replay, int pos, int row );
/* Cut the notes of all channels, as a Note Cut (SCx) on each, and drop the volume ramp's tail;
   the position stays. Notes come again with the next rows. */
void replay_cut_notes( struct replay *replay );

/* ---- FM-1 additions: the replay runs a PT/FM song (module->fm1_song, cells from get_note); the
   player's engines play its channels' notes. Each tick: replay_fm1_tick, then replay_fm1_chan for each
   channel. XM's effects run as IBXM runs them; the pitch, volume and pan they make drive the voices. ---- */
struct ibxm_fm1_chan {
	int trig;    /* a note started this tick (a real trigger: not a tone porta's; a retrigger too) */
	int leg;     /* a legato note this tick (PT/FM's LEG, 0xD3): the pitch moved to key at once, nothing started */
	int off;     /* its note was let go (key off: === or Kxx) */
	int cut;     /* NOTE CUT: its volume went to 0 */
	int key;     /* the note (1..96) the trigger played */
	int offset;  /* its SAMPLE OFFSET (samples) */
	int bend64;  /* the pitch now against the key's, in 1/64 semitones (slides, vibrato, arpeggio, finetune) */
	int volume;  /* 0..64, tremolo and tremor in it */
	int panning; /* 0..255, panbrello in it */
};
struct ibxm_fm1_state {
	int seq_pos, row, tick, speed, tempo, global_vol;
	int row_count, ended;  /* rows processed; times the sequence ended (went on at restart_pos) */
};
/* one tick, no audio: returns 1 if it started a row */
int replay_fm1_tick( struct replay *replay );
/* channel idx's state; trig, off and cut are taken (cleared) */
void replay_fm1_chan( struct replay *replay, int idx, struct ibxm_fm1_chan *out );
/* channel idx's panning (0..255) set from outside, as an 8xx would: the mixer's PAN changed by hand while it plays */
void replay_fm1_pan( struct replay *replay, int idx, int panning );
/* a note started on channel idx now, within its row, as a row would trigger it (its key, instrument, volume, its
   volume-column effect, offset and finetune; a note delay does not hold it back), the row's own cell going on after
   it: an edit heard at once (read its trigger with replay_fm1_chan) */
void replay_fm1_note( struct replay *replay, int idx, const struct note *note );
void replay_fm1_state( struct replay *replay, struct ibxm_fm1_state *out );
/* the speed (ticks a row) and tempo held while speed > 0, Fxx skipped (a song's properties being edited: it plays at
   them); 0: let go, both as they are until the next Fxx */
void replay_fm1_timing( struct replay *replay, int speed, int tempo );
