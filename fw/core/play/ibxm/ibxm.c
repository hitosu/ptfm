/* The FM-1 changes: Copyright (C) 2026 Vasily Kazantsev, BSD-3-Clause as IBXM (LICENSE-ibxm.txt). */
/* PT/FM's IBXM (BSD-3-Clause, LICENSE-ibxm.txt), upstream 83b5d9a. Changes, each marked "FM-1:":
   no loader (compiled out: IBXM_LOADERS) and no sample playback (compiled out: IBXM_AUDIO); no heap
   (ibxm_port.h: calloc from an arena, free does nothing); the play counter allocated once; instruments
   share a key map and envelopes (ibxm_no_keys, ibxm_no_env); additions at the end (row seek, stepping,
   replay snapshot).
   The replay runs PT/FM's songs, without audio (the player's engines sound their notes: replay_fm1_*):
   a cell from the song's own hook (module->get_note) has up to four effects (struct note's fx2 / param2:
   each runs through the row's and the tick's switches, now functions of the effect and its param) and
   a volume-column effect of its own (vcol); the replay says what the player needs (a trigger, a key
   off, a note cut; the pitch as a bend from the key, the volume, the pan). With module->fm1_song: the
   sequence's end goes on at restart_pos, F00 is the song's own ticks and tempo, Kxx lets go at tick xx,
   Yxy is IT's PANBRELLO, and E3x GLISSANDO and E5x FINETUNE play as FastTracker 2 plays them. PT/FM's LEG
   (0xD3, put among a cell's effects by the player's hook) is a slide to the note that arrives at once. */
#include "ibxm_port.h"

#include "ibxm.h"

const char *IBXM_VERSION = "ibxm/ac mod/xm/s3m replay 20260303 (c)mumart@gmail.com";

static const int FP_SHIFT = 15, FP_ONE = 32768, FP_MASK = 32767;

static const int exp2_table[] = {
	32768, 32946, 33125, 33305, 33486, 33667, 33850, 34034,
	34219, 34405, 34591, 34779, 34968, 35158, 35349, 35541,
	35734, 35928, 36123, 36319, 36516, 36715, 36914, 37114,
	37316, 37518, 37722, 37927, 38133, 38340, 38548, 38757,
	38968, 39180, 39392, 39606, 39821, 40037, 40255, 40473,
	40693, 40914, 41136, 41360, 41584, 41810, 42037, 42265,
	42495, 42726, 42958, 43191, 43425, 43661, 43898, 44137,
	44376, 44617, 44859, 45103, 45348, 45594, 45842, 46091,
	46341, 46593, 46846, 47100, 47356, 47613, 47871, 48131,
	48393, 48655, 48920, 49185, 49452, 49721, 49991, 50262,
	50535, 50810, 51085, 51363, 51642, 51922, 52204, 52488,
	52773, 53059, 53347, 53637, 53928, 54221, 54515, 54811,
	55109, 55408, 55709, 56012, 56316, 56622, 56929, 57238,
	57549, 57861, 58176, 58491, 58809, 59128, 59449, 59772,
	60097, 60423, 60751, 61081, 61413, 61746, 62081, 62419,
	62757, 63098, 63441, 63785, 64132, 64480, 64830, 65182,
	65536
};

static const short sine_table[] = {
	  0,  24,  49,  74,  97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
	255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120,  97,  74,  49,  24
};

/* FM-1: struct note is in ibxm.h (with PT/FM's extra effects) */

struct channel {
	struct replay *replay;
	struct instrument *instrument;
	struct sample *sample;
	struct note note;
	int id, key_on, random_seed, pl_row;
	int sample_off, sample_idx, sample_fra, freq, ampl, pann;
	int volume, panning, fadeout_vol, vol_env_tick, pan_env_tick;
	int period, porta_period, retrig_count, fx_count, av_count;
	int porta_up_param, porta_down_param, tone_porta_param, offset_param;
	int fine_porta_up_param, fine_porta_down_param, xfine_porta_param;
	int arpeggio_param, vol_slide_param, gvol_slide_param, pan_slide_param;
	int fine_vslide_up_param, fine_vslide_down_param;
	int retrig_volume, retrig_ticks, tremor_on_ticks, tremor_off_ticks;
	int vibrato_type, vibrato_phase, vibrato_speed, vibrato_depth;
	int tremolo_type, tremolo_phase, tremolo_speed, tremolo_depth;
	int tremolo_add, vibrato_add, arpeggio_add;
	int last_key;  /* FM-1: the last note played, for displays */
	/* FM-1: what the player reads (replay_fm1_chan): a trigger, a key off, a note cut since it last
	   read; the trigger's key, its reference period (the key's, without finetune) and offset; PANBRELLO */
	int fm1_trig, fm1_off, fm1_cut, fm1_key, fm1_period0, fm1_offset;
	int fm1_gliss, fm1_gofs, fm1_tp;  /* FM-1: E3x on; the note's own bend (its finetune), glissando's steps
	                                     from it; a slide to a note runs since the note */
	int pb_phase, pb_speed, pb_depth, pan_add;
};

struct replay {
	int sample_rate, interpolation, global_vol;
	int seq_pos, break_pos, row, next_row, tick;
	int speed, tempo, pl_count, pl_chan;
	char **play_count;
	struct channel *channels;
	struct module *module;
	int row_count;  /* FM-1: rows processed (replay_row), for row-exact stepping */
	int fm1_ended;  /* FM-1: times the sequence ended */
	int fm1_fixed;  /* FM-1: speed and tempo held (replay_fm1_timing): Fxx skipped */
};

static int exp_2( int x ) {
	int c, m, y;
	int x0 = ( x & FP_MASK ) >> ( FP_SHIFT - 7 );
	c = exp2_table[ x0 ];
	m = exp2_table[ x0 + 1 ] - c;
	y = ( m * ( x & ( FP_MASK >> 7 ) ) >> 8 ) + c;
	return ( y << FP_SHIFT ) >> ( FP_SHIFT - ( x >> FP_SHIFT ) );
}

static int log_2( int x ) {
	int step;
	int y = 16 << FP_SHIFT;
	for( step = y; step > 0; step >>= 1 ) {
		if( exp_2( y - step ) >= x ) {
			y -= step;
		}
	}
	return y;
}

#if IBXM_LOADERS  /* FM-1: no loaders (songs are built by the player) */
static char* data_ascii( struct data *data, int offset, int length, char *dest ) {
	int idx, chr;
	memset( dest, 32, length );
	if( offset > data->length ) {
		offset = data->length;
	}
	if( ( unsigned int ) offset + length > ( unsigned int ) data->length ) {
		length = data->length - offset;
	}
	for( idx = 0; idx < length; idx++ ) {
		chr = data->buffer[ offset + idx ] & 0xFF;
		if( chr > 32 ) {
			dest[ idx ] = chr;
		}
	}
	return dest;
}

static int data_s8( struct data *data, int offset ) {
	int value = 0;
	if( offset < data->length ) {
		value = data->buffer[ offset ];
		value = ( value & 0x7F ) - ( value & 0x80 );
	}
	return value;
}

static int data_u8( struct data *data, int offset ) {
	int value = 0;
	if( offset < data->length ) {
		value = data->buffer[ offset ] & 0xFF;
	}
	return value;
}

static int data_u16be( struct data *data, int offset ) {
	int value = 0;
	if( offset + 1 < data->length ) {
		value = ( ( data->buffer[ offset ] & 0xFF ) << 8 )
			| ( data->buffer[ offset + 1 ] & 0xFF );
	}
	return value;
}

static int data_u16le( struct data *data, int offset ) {
	int value = 0;
	if( offset + 1 < data->length ) {
		value = ( data->buffer[ offset ] & 0xFF )
			| ( ( data->buffer[ offset + 1 ] & 0xFF ) << 8 );
	}
	return value;
}

static unsigned int data_u32le( struct data *data, int offset ) {
	unsigned int value = 0;
	if( offset + 3 < data->length ) {
		value = ( data->buffer[ offset ] & 0xFF )
			| ( ( data->buffer[ offset + 1 ] & 0xFF ) << 8 )
			| ( ( data->buffer[ offset + 2 ] & 0xFF ) << 16 )
			| ( ( data->buffer[ offset + 3 ] & 0xFF ) << 24 );
	}
	return value;
}

#if IBXM_XM_MOD  /* FM-1: the XM and MOD loaders (16-bit sample copies) are compiled out */
static void data_sam_s8( struct data *data, int offset, int count, short *dest ) {
	int idx, amp, length = data->length;
	char *buffer = data->buffer;
	if( offset > length ) {
		offset = length;
	}
	if( offset + count > length ) {
		count = length - offset;
	}
	for( idx = 0; idx < count; idx++ ) {
		amp = ( buffer[ offset + idx ] & 0xFF ) << 8;
		dest[ idx ] = ( amp & 0x7FFF ) - ( amp & 0x8000 );
	}
}

static void data_sam_s16le( struct data *data, int offset, int count, short *dest ) {
	int idx, amp, length = data->length;
	char *buffer = data->buffer;
	if( offset > length ) {
		offset = length;
	}
	if( offset + count * 2 > length ) {
		count = ( length - offset ) / 2;
	}
	for( idx = 0; idx < count; idx++ ) {
		amp = ( buffer[ offset + idx * 2 ] & 0xFF ) | ( buffer[ offset + idx * 2 + 1 ] << 8 );
		dest[ idx ] = ( amp & 0x7FFF ) - ( amp & 0x8000 );
	}
}
#endif
#endif  /* IBXM_LOADERS */

static int envelope_next_tick( const struct envelope *envelope, int tick, int key_on ) {
	tick++;
	if( envelope->looped && tick >= envelope->loop_end_tick ) {
		tick = envelope->loop_start_tick;
	}
	if( envelope->sustain && key_on && tick >= envelope->sustain_tick ) {
		tick = envelope->sustain_tick;
	}
	return tick;
}
	
static int envelope_calculate_ampl( const struct envelope *envelope, int tick ) {
	int idx, point, dt, da;
	int ampl = envelope->points_ampl[ envelope->num_points - 1 ];
	if( tick < envelope->points_tick[ envelope->num_points - 1 ] ) {
		point = 0;
		for( idx = 1; idx < envelope->num_points; idx++ ) {
			if( envelope->points_tick[ idx ] <= tick ) {
				point = idx;
			}
		}
		ampl = envelope->points_ampl[ point ];
		dt = envelope->points_tick[ point + 1 ] - envelope->points_tick[ point ];
		if( dt > 0 ) {
			da = envelope->points_ampl[ point + 1 ] - envelope->points_ampl[ point ];
			ampl += ( ( da << 24 ) / dt ) * ( tick - envelope->points_tick[ point ] ) >> 24;
		}
	}
	return ampl;
}

#if IBXM_LOADERS  /* FM-1: no loaders */
#if IBXM_XM_MOD  /* FM-1: XM only */
static void sample_ping_pong( struct sample *sample ) {
	int idx;
	int loop_start = sample->loop_start;
	int loop_length = sample->loop_length;
	int loop_end = loop_start + loop_length;
	short *sample_data = sample->data;
	short *new_data = calloc( loop_end + loop_length + 1, sizeof( short ) );
	if( new_data ) {
		memcpy( new_data, sample_data, loop_end * sizeof( short ) );
		for( idx = 0; idx < loop_length; idx++ ) {
			new_data[ loop_end + idx ] = sample_data[ loop_end - idx - 1 ];
		}
		free( sample->data );
		sample->data = new_data;
		sample->loop_length *= 2;
		sample->data[ loop_start + sample->loop_length ] = sample->data[ loop_start ];
	}
}
#endif

/* Deallocate the specified module. */
void dispose_module( struct module *module ) {
	int idx, sam;
	struct instrument *instrument;
	free( module->default_panning );
	free( module->sequence );
	if( module->patterns ) {
		for( idx = 0; idx < module->num_patterns; idx++ ) {
			free( module->patterns[ idx ].data );
		}
		free( module->patterns );
	}
	if( module->instruments ) {
		for( idx = 0; idx <= module->num_instruments; idx++ ) {
			instrument = &module->instruments[ idx ];
			if( instrument->samples ) {
				for( sam = 0; sam < instrument->num_samples; sam++ ) {
					( void ) instrument->samples[ sam ];  /* FM-1: in place, nothing to free */
				}
				free( instrument->samples );
			}
		}
		free( module->instruments );
	}
	free( module );
}

#if IBXM_XM_MOD  /* FM-1: S3M only */
static struct module* module_load_xm( struct data *data, char *message ) {
	int delta_env, offset, next_offset, idx, entry;
	int num_rows, num_notes, pat_data_len, pat_data_offset;
	int sam, sam_head_offset, sam_data_bytes, sam_data_samples;
	int num_samples, sam_loop_start, sam_loop_length, amp;
	int note, flags, key, ins, vol, fxc, fxp;
	int point, point_tick, point_offset;
	int looped, ping_pong, sixteen_bit;
	char ascii[ 16 ], *pattern_data;
	struct instrument *instrument;
	struct sample *sample;
	struct module *module = calloc( 1, sizeof( struct module ) );
	if( module ) {
		if( data_u16le( data, 58 ) != 0x0104 ) {
			strcpy( message, "XM format version must be 0x0104!" );
			dispose_module( module );
			return NULL;
		}
		data_ascii( data, 17, 20, module->name );
		delta_env = !memcmp( data_ascii( data, 38, 15, ascii ), "DigiBooster Pro", 15 );
		offset = 60 + data_u32le( data, 60 );
		module->sequence_len = data_u16le( data, 64 );
		module->restart_pos = data_u16le( data, 66 );
		module->num_channels = data_u16le( data, 68 );
		module->num_patterns = data_u16le( data, 70 );
		module->num_instruments = data_u16le( data, 72 );
		module->linear_periods = data_u16le( data, 74 ) & 0x1;
		module->default_gvol = 64;
		module->default_speed = data_u16le( data, 76 );
		module->default_tempo = data_u16le( data, 78 );
		module->c2_rate = 8363;
		module->gain = 64;
		module->default_panning = calloc( module->num_channels, sizeof( unsigned char ) );
		if( !module->default_panning ) {
			dispose_module( module );
			return NULL;
		}
		for( idx = 0; idx < module->num_channels; idx++ ) {
			module->default_panning[ idx ] = 128;
		}
		module->sequence = calloc( module->sequence_len, sizeof( unsigned char ) );
		if( !module->sequence ) {
			dispose_module( module );
			return NULL;
		}
		for( idx = 0; idx < module->sequence_len; idx++ ) {
			entry = data_u8( data, 80 + idx );
			module->sequence[ idx ] = entry < module->num_patterns ? entry : 0;
		}
		module->patterns = calloc( module->num_patterns, sizeof( struct pattern ) );
		if( !module->patterns ) {
			dispose_module( module );
			return NULL;
		}
		for( idx = 0; idx < module->num_patterns; idx++ ) {
			if( data_u8( data, offset + 4 ) ) {
				strcpy( message, "Unknown pattern packing type!" );
				dispose_module( module );
				return NULL;
			}
			num_rows = data_u16le( data, offset + 5 );
			if( num_rows < 1 ) {
				num_rows = 1;
			}
			pat_data_len = data_u16le( data, offset + 7 );
			offset += data_u32le( data, offset );
			next_offset = offset + pat_data_len;
			num_notes = num_rows * module->num_channels;
			pattern_data = calloc( num_notes, 5 );
			if( !pattern_data ) {
				dispose_module( module );
				return NULL;
			}
			module->patterns[ idx ].num_channels = module->num_channels;
			module->patterns[ idx ].num_rows = num_rows;
			module->patterns[ idx ].data = pattern_data;
			if( pat_data_len > 0 ) {
				pat_data_offset = 0;
				for( note = 0; note < num_notes; note++ ) {
					flags = data_u8( data, offset );
					if( ( flags & 0x80 ) == 0 ) {
						flags = 0x1F;
					} else {
						offset++;
					}
					key = ( flags & 0x01 ) > 0 ? data_u8( data, offset++ ) : 0;
					pattern_data[ pat_data_offset++ ] = key;
					ins = ( flags & 0x02 ) > 0 ? data_u8( data, offset++ ) : 0;
					pattern_data[ pat_data_offset++ ] = ins;
					vol = ( flags & 0x04 ) > 0 ? data_u8( data, offset++ ) : 0;
					pattern_data[ pat_data_offset++ ] = vol;
					fxc = ( flags & 0x08 ) > 0 ? data_u8( data, offset++ ) : 0;
					fxp = ( flags & 0x10 ) > 0 ? data_u8( data, offset++ ) : 0;
					if( fxc >= 0x40 ) {
						fxc = fxp = 0;
					}
					pattern_data[ pat_data_offset++ ] = fxc;
					pattern_data[ pat_data_offset++ ] = fxp;
				}
			}
			offset = next_offset;
		}
		module->instruments = calloc( module->num_instruments + 1, sizeof( struct instrument ) );
		if( !module->instruments ) {
			dispose_module( module );
			return NULL;
		}
		instrument = &module->instruments[ 0 ];
		instrument->samples = calloc( 1, sizeof( struct sample ) );
		if( !instrument->samples ) {
			dispose_module( module );
			return NULL;
		}
		for( ins = 1; ins <= module->num_instruments; ins++ ) {
			instrument = &module->instruments[ ins ];
			data_ascii( data, offset + 4, 22, instrument->name );
			num_samples = data_u16le( data, offset + 27 );
			instrument->num_samples = ( num_samples > 0 ) ? num_samples : 1;
			instrument->samples = calloc( instrument->num_samples, sizeof( struct sample ) );
			if( !instrument->samples ) {
				dispose_module( module );
				return NULL;
			}
			if( num_samples > 0 ) {
				for( key = 0; key < 96; key++ ) {
					int sample_idx = data_u8( data, offset + 33 + key );
					instrument->key_to_sample[ key + 1 ] = sample_idx < num_samples ? sample_idx : 0;
				}
				point_tick = 0;
				for( point = 0; point < 12; point++ ) {
					point_offset = offset + 129 + ( point * 4 );
					point_tick = ( delta_env ? point_tick : 0 ) + data_u16le( data, point_offset );
					instrument->vol_env.points_tick[ point ] = point_tick;
					instrument->vol_env.points_ampl[ point ] = data_u16le( data, point_offset + 2 );
				}
				point_tick = 0;
				for( point = 0; point < 12; point++ ) {
					point_offset = offset + 177 + ( point * 4 );
					point_tick = ( delta_env ? point_tick : 0 ) + data_u16le( data, point_offset );
					instrument->pan_env.points_tick[ point ] = point_tick;
					instrument->pan_env.points_ampl[ point ] = data_u16le( data, point_offset + 2 );
				}
				instrument->vol_env.num_points = data_u8( data, offset + 225 );
				if( instrument->vol_env.num_points > 12 ) {
					instrument->vol_env.num_points = 0;
				}
				instrument->pan_env.num_points = data_u8( data, offset + 226 );
				if( instrument->pan_env.num_points > 12 ) {
					instrument->pan_env.num_points = 0;
				}
				instrument->vol_env.sustain_tick = instrument->vol_env.points_tick[ data_u8( data, offset + 227 ) & 0xF ];
				instrument->vol_env.loop_start_tick = instrument->vol_env.points_tick[ data_u8( data, offset + 228 ) & 0xF ];
				instrument->vol_env.loop_end_tick = instrument->vol_env.points_tick[ data_u8( data, offset + 229 ) & 0xF ];
				instrument->pan_env.sustain_tick = instrument->pan_env.points_tick[ data_u8( data, offset + 230 ) & 0xF ];
				instrument->pan_env.loop_start_tick = instrument->pan_env.points_tick[ data_u8( data, offset + 231 ) & 0xF ];
				instrument->pan_env.loop_end_tick = instrument->pan_env.points_tick[ data_u8( data, offset + 232 ) & 0xF ];
				instrument->vol_env.enabled = instrument->vol_env.num_points > 0 && ( data_u8( data, offset + 233 ) & 0x1 );
				instrument->vol_env.sustain = ( data_u8( data, offset + 233 ) & 0x2 ) > 0;
				instrument->vol_env.looped = ( data_u8( data, offset + 233 ) & 0x4 ) > 0;
				instrument->pan_env.enabled = instrument->pan_env.num_points > 0 && ( data_u8( data, offset + 234 ) & 0x1 );
				instrument->pan_env.sustain = ( data_u8( data, offset + 234 ) & 0x2 ) > 0;
				instrument->pan_env.looped = ( data_u8( data, offset + 234 ) & 0x4 ) > 0;
				instrument->vib_type = data_u8( data, offset + 235 );
				instrument->vib_sweep = data_u8( data, offset + 236 );
				instrument->vib_depth = data_u8( data, offset + 237 );
				instrument->vib_rate = data_u8( data, offset + 238 );
				instrument->vol_fadeout = data_u16le( data, offset + 239 );
			}
			offset += data_u32le( data, offset );
			sam_head_offset = offset;
			offset += num_samples * 40;
			for( sam = 0; sam < num_samples; sam++ ) {
				sample = &instrument->samples[ sam ];
				sam_data_bytes = data_u32le( data, sam_head_offset );
				sam_loop_start = data_u32le( data, sam_head_offset + 4 );
				sam_loop_length = data_u32le( data, sam_head_offset + 8 );
				sample->volume = data_u8( data, sam_head_offset + 12 );
				sample->fine_tune = data_s8( data, sam_head_offset + 13 );
				looped = ( data_u8( data, sam_head_offset + 14 ) & 0x3 ) > 0;
				ping_pong = ( data_u8( data, sam_head_offset + 14 ) & 0x2 ) > 0;
				sixteen_bit = ( data_u8( data, sam_head_offset + 14 ) & 0x10 ) > 0;
				sample->panning = data_u8( data, sam_head_offset + 15 ) + 1;
				sample->rel_note = data_s8( data, sam_head_offset + 16 );
				data_ascii( data, sam_head_offset + 18, 22, sample->name );
				sam_head_offset += 40;
				sam_data_samples = sam_data_bytes;
				if( sixteen_bit ) {
					sam_data_samples = sam_data_samples >> 1;
					sam_loop_start = sam_loop_start >> 1;
					sam_loop_length = sam_loop_length >> 1;
				}
				if( !looped || ( sam_loop_start + sam_loop_length ) > sam_data_samples ) {
					sam_loop_start = sam_data_samples;
					sam_loop_length = 0;
				}
				sample->loop_start = sam_loop_start;
				sample->loop_length = sam_loop_length;
				sample->data = calloc( sam_data_samples + 1, sizeof( short ) );
				if( sample->data ) {
					if( sixteen_bit ) {
						data_sam_s16le( data, offset, sam_data_samples, sample->data );
					} else {
						data_sam_s8( data, offset, sam_data_samples, sample->data );
					}
					amp = 0;
					for( idx = 0; idx < sam_data_samples; idx++ ) {
						amp = amp + sample->data[ idx ];
						amp = ( amp & 0x7FFF ) - ( amp & 0x8000 );
						sample->data[ idx ] = amp;
					}
					sample->data[ sam_loop_start + sam_loop_length ] = sample->data[ sam_loop_start ];
					if( ping_pong ) {
						sample_ping_pong( sample );
					}
				} else {
					dispose_module( module );
					return NULL;
				}
				offset += sam_data_bytes;
			}
		}
	}
	return module;
}
#endif

static struct module* module_load_s3m( struct data *data, char *message ) {
	int idx, module_data_idx, inst_offset, flags;
	int version, sixteen_bit, tune, signed_samples;
	int stereo_mode, default_pan, channel_map[ 32 ];
	int sample_offset, sample_length, loop_start, loop_length;
	int pat_offset, chan, ins, panning;  /* FM-1: the unpacking moved to s3m_unpack() */
	struct instrument *instrument;
	struct sample *sample;
	struct module *module = calloc( 1, sizeof( struct module ) );
	if( module ) {
		data_ascii( data, 0, 28, module->name );
		module->sequence_len = data_u16le( data, 32 );
		module->num_instruments = data_u16le( data, 34 );
		module->num_patterns = data_u16le( data, 36 );
		flags = data_u16le( data, 38 );
		version = data_u16le( data, 40 );
		module->fast_vol_slides = ( ( flags & 0x40 ) == 0x40 ) || version == 0x1300;
		signed_samples = data_u16le( data, 42 ) == 1;
		if( data_u32le( data, 44 ) != 0x4d524353 ) {
			strcpy( message, "Not an S3M file!" );
			dispose_module( module );
			return NULL;
		}
		module->default_gvol = data_u8( data, 48 );
		module->default_speed = data_u8( data, 49 );
		module->default_tempo = data_u8( data, 50 );
		module->c2_rate = 8363;
		module->gain = data_u8( data, 51 ) & 0x7F;
		stereo_mode = ( data_u8( data, 51 ) & 0x80 ) == 0x80;
		default_pan = data_u8( data, 53 ) == 0xFC;
		for( idx = 0; idx < 32; idx++ ) {
			channel_map[ idx ] = -1;
			if( data_u8( data, 64 + idx ) < 16 ) {
				channel_map[ idx ] = module->num_channels++;
			}
		}
		module_data_idx = 96 + module->sequence_len;
		module->sequence = calloc( module->sequence_len, sizeof( unsigned char ) );
		if( !module->sequence ){
			dispose_module( module );
			return NULL;
		}
		for( idx = 0; idx < module->sequence_len; idx++ ) {
			module->sequence[ idx ] = data_u8( data, 96 + idx );
		}
		while( module->sequence_len > 0 && module->sequence[ module->sequence_len - 1 ] >= module->num_patterns ) {
			module->sequence_len--;
		}
		module->instruments = calloc( module->num_instruments + 1, sizeof( struct instrument ) );
		if( !module->instruments ) {
			dispose_module( module );
			return NULL;
		}
		instrument = &module->instruments[ 0 ];
		instrument->num_samples = 1;
		instrument->samples = calloc( 1, sizeof( struct sample ) );
		if( !instrument->samples ) {
			dispose_module( module );
			return NULL;
		}
		for( ins = 1; ins <= module->num_instruments; ins++ ) {
			instrument = &module->instruments[ ins ];
			instrument->num_samples = 1;
			instrument->samples = calloc( 1, sizeof( struct sample ) );
			if( !instrument->samples ) {
				dispose_module( module );
				return NULL;
			}
			sample = &instrument->samples[ 0 ];
			inst_offset = data_u16le( data, module_data_idx ) << 4;
			module_data_idx += 2;
			data_ascii( data, inst_offset + 48, 28, instrument->name );
			if( data_u8( data, inst_offset ) == 1 && data_u16le( data, inst_offset + 76 ) == 0x4353 ) {
				sample_offset = ( data_u8( data, inst_offset + 13 ) << 20 )
					+ ( data_u16le( data, inst_offset + 14 ) << 4 );
				sample_length = data_u32le( data, inst_offset + 16 );
				loop_start = data_u32le( data, inst_offset + 20 );
				loop_length = data_u32le( data, inst_offset + 24 ) - loop_start;
				sample->volume = data_u8( data, inst_offset + 28 );
				if( data_u8( data, inst_offset + 30 ) != 2 ) {  /* FM-1: pack 2, a compressed sample */
					strcpy( message, "Not a compressed sample!" );
					dispose_module( module );
					return NULL;
				}
				if( loop_start + loop_length > sample_length ) {
					loop_length = sample_length - loop_start;
				}
				if( loop_length < 1 || !( data_u8( data, inst_offset + 31 ) & 0x1 ) ) {
					loop_start = sample_length;
					loop_length = 0;
				}
				sample->loop_start = loop_start;
				sample->loop_length = loop_length;
				/* stereo = data_u8( data, inst_offset + 31 ) & 0x2; */
				sixteen_bit = data_u8( data, inst_offset + 31 ) & 0x4;
				tune = ( log_2( data_u32le( data, inst_offset + 32 ) ) - log_2( module->c2_rate ) ) * 12;
				sample->rel_note = tune >> FP_SHIFT;
				sample->fine_tune = ( tune & FP_MASK ) >> ( FP_SHIFT - 7 );
				/* FM-1: no 16-bit copy: the sample stays in place in the module data, 8-bit
				   signed, compressed (pack 2), its block table first: one u32 per block of 256
				   (from 0 to the loop start, then on to the loop end). */
				sample->length = loop_length > 0 ? loop_start + loop_length : sample_length;
				if( sixteen_bit || !signed_samples || sample_offset < 0 || sample_length < 0
					|| sample_offset + 4 * ( ( loop_start + 255 ) / 256
						+ ( sample->length - loop_start + 255 ) / 256 ) > data->length ) {
					strcpy( message, "Not an 8-bit signed compressed sample!" );
					dispose_module( module );
					return NULL;
				}
				sample->packed = ( const unsigned char * ) data->buffer + sample_offset;
			}
		}
		module->patterns = calloc( module->num_patterns, sizeof( struct pattern ) );
		if( !module->patterns ) {
			dispose_module( module );
			return NULL;
		}
		for( idx = 0; idx < 32; idx++ ) {
			module->channel_map[ idx ] = channel_map[ idx ];
		}
		for( idx = 0; idx < module->num_patterns; idx++ ) {
			module->patterns[ idx ].num_channels = module->num_channels;
			module->patterns[ idx ].num_rows = 64;
			/* FM-1: kept packed in the module data; s3m_unpack() when played or shown */
			pat_offset = ( data_u16le( data, module_data_idx ) << 4 ) + 2;
			if( pat_offset > data->length ) {
				pat_offset = data->length;
			}
			module->patterns[ idx ].packed = ( const unsigned char * ) data->buffer + pat_offset;
			module->patterns[ idx ].packed_len = data->length - pat_offset;
			module_data_idx += 2;
		}
		module->default_panning = calloc( module->num_channels, sizeof( unsigned char ) );
		if( module->default_panning ) {
			for( chan = 0; chan < 32; chan++ ) {
				if( channel_map[ chan ] >= 0 ) {
					panning = 7;
					if( stereo_mode ) {
						panning = 12;
						if( data_u8( data, 64 + chan ) < 8 ) {
							panning = 3;
						}
					}
					if( default_pan ) {
						flags = data_u8( data, module_data_idx + chan );
						if( ( flags & 0x20 ) == 0x20 ) {
							panning = flags & 0xF;
						}
					}
					module->default_panning[ channel_map[ chan ] ] = panning * 17;
				}
			}
		} else {
			dispose_module( module );
			return NULL;
		}
	}
	return module;
}

#if IBXM_XM_MOD  /* FM-1: S3M only */
static struct module* module_load_mod( struct data *data, char *message ) {
	int idx, pat, module_data_idx, pat_data_len, pat_data_idx;
	int period, key, ins, effect, param, fine_tune;
	int sample_length, loop_start, loop_length;
	char *pattern_data;
	struct instrument *instrument;
	struct sample *sample;
	struct module *module = calloc( 1, sizeof( struct module ) );
	if( module ) {
		data_ascii( data, 0, 20, module->name );
		module->sequence_len = data_u8( data, 950 ) & 0x7F;
		module->restart_pos = data_u8( data, 951 ) & 0x7F;
		if( module->restart_pos >= module->sequence_len ) {
			module->restart_pos = 0;
		}
		module->sequence = calloc( 128, sizeof( unsigned char ) );
		if( !module->sequence ){
			dispose_module( module );
			return NULL;
		}
		for( idx = 0; idx < 128; idx++ ) {
			pat = data_u8( data, 952 + idx ) & 0x7F;
			module->sequence[ idx ] = pat;
			if( pat >= module->num_patterns ) {
				module->num_patterns = pat + 1;
			}
		}
		switch( data_u16be( data, 1082 ) ) {
			case 0x4b2e: /* M.K. */
			case 0x4b21: /* M!K! */
			case 0x5434: /* FLT4 */
				module->num_channels = 4;
				module->c2_rate = 8287;
				module->gain = 64;
				break;
			case 0x484e: /* xCHN */
				module->num_channels = data_u8( data, 1080 ) - 48;
				module->c2_rate = 8363;
				module->gain = 32;
				break;
			case 0x4348: /* xxCH */
				module->num_channels = ( data_u8( data, 1080 ) - 48 ) * 10;
				module->num_channels += data_u8( data, 1081 ) - 48;
				module->c2_rate = 8363;
				module->gain = 32;
				break;
			default:
				strcpy( message, "MOD Format not recognised!" );
				dispose_module( module );
				return NULL;
		}
		module->default_gvol = 64;
		module->default_speed = 6;
		module->default_tempo = 125;
		module->default_panning = calloc( module->num_channels, sizeof( unsigned char ) );
		if( !module->default_panning ) {
			dispose_module( module );
			return NULL;
		}
		for( idx = 0; idx < module->num_channels; idx++ ) {
			module->default_panning[ idx ] = 51;
			if( ( idx & 3 ) == 1 || ( idx & 3 ) == 2 ) {
				module->default_panning[ idx ] = 204;
			}
		}
		module_data_idx = 1084;
		module->patterns = calloc( module->num_patterns, sizeof( struct pattern ) );
		if( !module->patterns ) {
			dispose_module( module );
			return NULL;
		}
		pat_data_len = module->num_channels * 64 * 5;
		for( pat = 0; pat < module->num_patterns; pat++ ) {
			module->patterns[ pat ].num_channels = module->num_channels;
			module->patterns[ pat ].num_rows = 64;
			pattern_data = calloc( 1, pat_data_len );
			if( !pattern_data ) {
				dispose_module( module );
				return NULL;
			}
			module->patterns[ pat ].data = pattern_data;
			for( pat_data_idx = 0; pat_data_idx < pat_data_len; pat_data_idx += 5 ) {
				period = ( data_u8( data, module_data_idx ) & 0xF ) << 8;
				period = ( period | data_u8( data, module_data_idx + 1 ) ) * 4;
				if( period >= 112 && period <= 6848 ) {
					key = -12 * log_2( ( period << FP_SHIFT ) / 29021 );
					key = ( key + ( key & ( FP_ONE >> 1 ) ) ) >> FP_SHIFT;
					pattern_data[ pat_data_idx ] = key;
				}
				ins = ( data_u8( data, module_data_idx + 2 ) & 0xF0 ) >> 4;
				ins = ins | ( data_u8( data, module_data_idx ) & 0x10 );
				pattern_data[ pat_data_idx + 1 ] = ins;
				effect = data_u8( data, module_data_idx + 2 ) & 0x0F;
				param  = data_u8( data, module_data_idx + 3 );
				if( param == 0 && ( effect < 3 || effect == 0xA ) ) {
					effect = 0;
				}
				if( param == 0 && ( effect == 5 || effect == 6 ) ) {
					effect -= 2;
				}
				if( effect == 8 ) {
					if( module->num_channels == 4 ) {
						effect = param = 0;
					} else if( param > 128 ) {
						param = 128;
					} else {
						param = ( param * 255 ) >> 7;
					}
				}
				pattern_data[ pat_data_idx + 3 ] = effect;
				pattern_data[ pat_data_idx + 4 ] = param;
				module_data_idx += 4;
			}
		}
		module->num_instruments = 31;
		module->instruments = calloc( module->num_instruments + 1, sizeof( struct instrument ) );
		if( !module->instruments ) {
			dispose_module( module );
			return NULL;
		}
		instrument = &module->instruments[ 0 ];
		instrument->num_samples = 1;
		instrument->samples = calloc( 1, sizeof( struct sample ) );
		if( !instrument->samples ) {
			dispose_module( module );
			return NULL;
		}
		for( ins = 1; ins <= module->num_instruments; ins++ ) {
			instrument = &module->instruments[ ins ];
			instrument->num_samples = 1;
			instrument->samples = calloc( 1, sizeof( struct sample ) );
			if( !instrument->samples ) {
				dispose_module( module );
				return NULL;
			}
			sample = &instrument->samples[ 0 ];
			data_ascii( data, ins * 30 - 10, 22, instrument->name );
			sample_length = data_u16be( data, ins * 30 + 12 ) * 2;
			fine_tune = ( data_u8( data, ins * 30 + 14 ) & 0xF ) << 4;
			sample->fine_tune = ( fine_tune & 0x7F ) - ( fine_tune & 0x80 );
			sample->volume = data_u8( data, ins * 30 + 15 ) & 0x7F;
			if( sample->volume > 64 ) {
				sample->volume = 64;
			}
			loop_start = data_u16be( data, ins * 30 + 16 ) * 2;
			loop_length = data_u16be( data, ins * 30 + 18 ) * 2;
			if( loop_start + loop_length > sample_length ) {
				if( loop_start / 2 + loop_length <= sample_length ) {
					/* Some old modules have loop start in bytes. */
					loop_start = loop_start / 2;
				} else {
					loop_length = sample_length - loop_start;
				}
			}
			if( loop_length < 4 ) {
				loop_start = sample_length;
				loop_length = 0;
			}
			sample->loop_start = loop_start;
			sample->loop_length = loop_length;
			sample->data = calloc( sample_length + 1, sizeof( short ) );
			if( sample->data ) {
				data_sam_s8( data, module_data_idx, sample_length, sample->data );
				sample->data[ loop_start + loop_length ] = sample->data[ loop_start ];
			} else {
				dispose_module( module );
				return NULL;
			}
			module_data_idx += sample_length;
		}
	}
	return module;
}
#endif

/* Allocate and initialize a module from the specified data, returns NULL on error.
   Message must point to a 64-character buffer to receive error messages. */
struct module* module_load( struct data *data, char *message ) {
	/* FM-1: S3M only (module_load_s3m checks the "SCRM" signature) */
	struct module* module = module_load_s3m( data, message );
	if( module && ( module->num_channels < 1 || module->num_patterns < 1 || module->sequence_len < 1 ) ) {
		strcpy( message, "Invalid module file!" );
		dispose_module( module );
		return NULL;
	}
	return module;
}

/* FM-1: one S3M pattern from its packed rows into dest (num_channels * 64 notes of 5 bytes):
   upstream's unpacking loop from module_load_s3m, reading the packed bytes in place. */
static void s3m_unpack( struct module *module, struct pattern *pattern, char *pattern_data ) {
	const unsigned char *packed = pattern->packed;
	int pat_offset = 0, len = pattern->packed_len, row = 0;
	int token, key, ins, volume, effect, param, chan, note_offset;
	memset( pattern_data, 0, module->num_channels * 64 * 5 );
#define S3M_U8() ( pat_offset < len ? packed[ pat_offset++ ] : ( pat_offset++, 0 ) )
	while( row < 64 ) {
		token = S3M_U8();
		if( token ) {
			key = ins = 0;
			if( ( token & 0x20 ) == 0x20 ) {
				/* Key + Instrument.*/
				key = S3M_U8();
				ins = S3M_U8();
				if( key < 0xFE ) {
					key = ( key >> 4 ) * 12 + ( key & 0xF ) + 1;
				} else if( key == 0xFF ) {
					key = 0;
				}
			}
			volume = 0;
			if( ( token & 0x40 ) == 0x40 ) {
				/* Volume Column.*/
				volume = ( S3M_U8() & 0x7F ) + 0x10;
				if( volume > 0x50 ) {
					volume = 0;
				}
			}
			effect = param = 0;
			if( ( token & 0x80 ) == 0x80 ) {
				/* Effect + Param.*/
				effect = S3M_U8();
				param = S3M_U8();
				if( effect < 1 || effect >= 0x40 ) {
					effect = param = 0;
				} else if( effect > 0 ) {
					effect += 0x80;
				}
			}
			chan = module->channel_map[ token & 0x1F ];
			if( chan >= 0 ) {
				note_offset = ( row * module->num_channels + chan ) * 5;
				pattern_data[ note_offset     ] = key;
				pattern_data[ note_offset + 1 ] = ins;
				pattern_data[ note_offset + 2 ] = volume;
				pattern_data[ note_offset + 3 ] = effect;
				pattern_data[ note_offset + 4 ] = param;
			}
		} else {
			row++;
		}
	}
#undef S3M_U8
}
#endif  /* IBXM_LOADERS */

/* FM-1: shared by every instrument (a song's instruments have no key map nor envelopes) */
const char ibxm_no_keys[ 97 ];
const struct envelope ibxm_no_env;

/* FM-1: the song's tables and the replay in the arena (ibxm_port.h); a new song starts it over */
void ibxm_arena_reset( void ) {
	ib_arena_used = 0;
}

void *ibxm_alloc( unsigned int count, unsigned int size ) {
	return ib_calloc( count, size );
}

unsigned int ibxm_arena_used( void ) {
	return ib_arena_used;
}

/* FM-1: which arena the next replay is built in (0, or 1 when it has memory) */
void ibxm_arena_use( int k ) {
	ib_k = k && ib_arena1 ? 1u : 0u;
	ib_arena = ib_k ? ib_arena1 : ib_arena0;
}

/* FM-1: arena 1's memory (IBXM_ARENA bytes, aligned for a long), or none */
void ibxm_arena_mem1( void *mem ) {
	ib_arena1 = ( long * ) mem;
	ib_used[ 1 ] = 0;
	if( !mem ) ibxm_arena_use( 0 );
}

unsigned int ibxm_arena_size( void ) {
	return IBXM_ARENA;
}

/* FM-1: (a module's pattern cache: none, a song's cells come from its hook) */
void ibxm_forget_pattern( void ) {
}

static void pattern_get_note( struct module *module, struct pattern *pattern, int row, int chan, struct note *dest ) {
	memset( dest, 0, sizeof( struct note ) );  /* FM-1: (the extra effects none) */
	if( module->get_note && row < pattern->num_rows && chan < pattern->num_channels ) {  /* FM-1: a song's cell */
		module->get_note( module, ( int ) ( pattern - module->patterns ), row, chan, dest );
	}
}

static void channel_init( struct channel *channel, struct replay *replay, int idx ) {
	memset( channel, 0, sizeof( struct channel ) );
	channel->replay = replay;
	channel->id = idx;
	channel->panning = replay->module->default_panning[ idx ];
	channel->instrument = &replay->module->instruments[ 0 ];
	channel->sample = &channel->instrument->samples[ 0 ];
	channel->random_seed = ( idx + 1 ) * 0xABCDEF;
}

static void channel_volume_slide( struct channel *channel ) {
	int up = channel->vol_slide_param >> 4;
	int down = channel->vol_slide_param & 0xF;
	if( down == 0xF && up > 0 ) {
		/* Fine slide up.*/
		if( channel->fx_count == 0 ) {
			channel->volume += up;
		}
	} else if( up == 0xF && down > 0 ) {
		/* Fine slide down.*/
		if( channel->fx_count == 0 ) {
			channel->volume -= down;
		}
	} else if( channel->fx_count > 0 || channel->replay->module->fast_vol_slides ) {
		/* Normal.*/
		channel->volume += up - down;
	}
	if( channel->volume > 64 ) {
		channel->volume = 64;
	}
	if( channel->volume < 0 ) {
		channel->volume = 0;
	}
}

static void channel_porta_up( struct channel *channel, int param ) {
	switch( param & 0xF0 ) {
		case 0xE0: /* Extra-fine porta.*/
			if( channel->fx_count == 0 ) {
				channel->period -= param & 0xF;
			}
			break;
		case 0xF0: /* Fine porta.*/
			if( channel->fx_count == 0 ) {
				channel->period -= ( param & 0xF ) << 2;
			}
			break;
		default:/* Normal porta.*/
			if( channel->fx_count > 0 ) {
				channel->period -= param << 2;
			}
			break;
	}
	if( channel->period < 0 ) {
		channel->period = 0;
	}
}

static void channel_porta_down( struct channel *channel, int param ) {
	if( channel->period > 0 ) {
		switch( param & 0xF0 ) {
			case 0xE0: /* Extra-fine porta.*/
				if( channel->fx_count == 0 ) {
					channel->period += param & 0xF;
				}
				break;
			case 0xF0: /* Fine porta.*/
				if( channel->fx_count == 0 ) {
					channel->period += ( param & 0xF ) << 2;
				}
				break;
			default:/* Normal porta.*/
				if( channel->fx_count > 0 ) {
					channel->period += param << 2;
				}
				break;
		}
		if( channel->period > 65535 ) {
			channel->period = 65535;
		}
	}
}

static void channel_tone_porta( struct channel *channel ) {
	channel->fm1_tp = 1;  /* FM-1: a slide to a note runs (glissando's) */
	if( channel->period > 0 ) {
		if( channel->period < channel->porta_period ) {
			channel->period += channel->tone_porta_param << 2;
			if( channel->period > channel->porta_period ) {
				channel->period = channel->porta_period;
			}
		} else {
			channel->period -= channel->tone_porta_param << 2;
			if( channel->period < channel->porta_period ) {
				channel->period = channel->porta_period;
			}
		}
	}
}

static int channel_waveform( struct channel *channel, int phase, int type ) {
	int amplitude = 0;
	switch( type ) {
		default: /* Sine. */
			amplitude = sine_table[ phase & 0x1F ];
			if( ( phase & 0x20 ) > 0 ) {
				amplitude = -amplitude;
			}
			break;
		case 6: /* Saw Up.*/
			amplitude = ( ( ( phase + 0x20 ) & 0x3F ) << 3 ) - 255;
			break;
		case 1: case 7: /* Saw Down. */
			amplitude = 255 - ( ( ( phase + 0x20 ) & 0x3F ) << 3 );
			break;
		case 2: case 5: /* Square. */
			amplitude = ( phase & 0x20 ) > 0 ? 255 : -255;
			break;
		case 3: case 8: /* Random. */
			amplitude = ( channel->random_seed >> 20 ) - 255;
			channel->random_seed = ( channel->random_seed * 65 + 17 ) & 0x1FFFFFFF;
			break;
	}
	return amplitude;
}

static void channel_vibrato( struct channel *channel, int fine ) {
	int wave = channel_waveform( channel, channel->vibrato_phase, channel->vibrato_type & 0x3 );
	channel->vibrato_add = wave * channel->vibrato_depth >> ( fine ? 7 : 5 );
}

static void channel_tremolo( struct channel *channel ) {
	int wave = channel_waveform( channel, channel->tremolo_phase, channel->tremolo_type & 0x3 );
	channel->tremolo_add = wave * channel->tremolo_depth >> 6;
}

static void channel_tremor( struct channel *channel ) {
	if( channel->retrig_count >= channel->tremor_on_ticks ) {
		channel->tremolo_add = -64;
	}
	if( channel->retrig_count >= ( channel->tremor_on_ticks + channel->tremor_off_ticks ) ) {
		channel->tremolo_add = channel->retrig_count = 0;
	}
}

static void channel_retrig_vol_slide( struct channel *channel ) {
	if( channel->retrig_count >= channel->retrig_ticks ) {
		channel->retrig_count = channel->sample_idx = channel->sample_fra = 0;
		channel->fm1_trig = 1;  /* FM-1:the player starts the note again */
		channel->fm1_offset = 0;
		switch( channel->retrig_volume ) {
			case 0x1: channel->volume = channel->volume -  1; break;
			case 0x2: channel->volume = channel->volume -  2; break;
			case 0x3: channel->volume = channel->volume -  4; break;
			case 0x4: channel->volume = channel->volume -  8; break;
			case 0x5: channel->volume = channel->volume - 16; break;
			case 0x6: channel->volume = channel->volume * 2 / 3; break;
			case 0x7: channel->volume = channel->volume >> 1; break;
			case 0x8: /* ? */ break;
			case 0x9: channel->volume = channel->volume +  1; break;
			case 0xA: channel->volume = channel->volume +  2; break;
			case 0xB: channel->volume = channel->volume +  4; break;
			case 0xC: channel->volume = channel->volume +  8; break;
			case 0xD: channel->volume = channel->volume + 16; break;
			case 0xE: channel->volume = channel->volume * 3 / 2; break;
			case 0xF: channel->volume = channel->volume << 1; break;
		}
		if( channel->volume <  0 ) {
			channel->volume = 0;
		}
		if( channel->volume > 64 ) {
			channel->volume = 64;
		}
	}
}

/* FM-1:a cell's effects: the first (slot 0) and up to three more. The slot holding effect a or
   b (-1: none), its param; the volume column's effect (its own, else the volume byte's) */
static int note_find( const struct note *note, int a, int b ) {
	int k;
	if( note->effect == a || note->effect == b ) {
		return 0;
	}
	for( k = 0; k < 3; k++ ) {
		if( ( note->fx2[ k ] || note->param2[ k ] ) && ( note->fx2[ k ] == a || note->fx2[ k ] == b ) ) {
			return k + 1;
		}
	}
	return -1;
}

static int note_param( const struct note *note, int slot ) {
	return slot <= 0 ? note->param : note->param2[ slot - 1 ];
}

static int note_vcol( const struct note *note ) {
	return note->vcol ? note->vcol : note->volume;
}

/* FM-1:the reference period of a key (no finetune), as channel_trigger computes periods */
static int fm1_key_period( struct channel *channel, int key ) {
	int period = key << 6;
	if( channel->replay->module->linear_periods ) {
		return 7744 - period;
	}
	return 29021 * exp_2( ( period << FP_SHIFT ) / -768 ) >> FP_SHIFT;
}

static PLAT_COLD void channel_trigger( struct channel *channel ) {   /* FM-1: PLAT_COLD (a note's start) */
	int key, sam, porta, period, fine_tune, ins = channel->note.instrument;
	int slot, legato;  /* FM-1 */
	struct sample *sample;
	if( ins > 0 && ins <= channel->replay->module->num_instruments ) {
		channel->instrument = &channel->replay->module->instruments[ ins ];
		key = channel->note.key < 97 ? channel->note.key : 0;
		sam = channel->instrument->key_to_sample[ key ];
		sample = &channel->instrument->samples[ sam ];
		channel->volume = sample->volume >= 64 ? 64 : sample->volume & 0x3F;
		if( sample->panning > 0 ) {
			channel->panning = ( sample->panning - 1 ) & 0xFF;
		}
		if( channel->period > 0 && sample->loop_length > 1 ) {
			/* Amiga trigger.*/
			channel->sample = sample;
		}
		channel->sample_off = 0;
		channel->vol_env_tick = channel->pan_env_tick = 0;
		channel->fadeout_vol = 32768;
		channel->key_on = 1;
	}
	slot = note_find( &channel->note, 0x09, 0x8F );  /* FM-1:in any of the cell's effects */
	if( slot >= 0 ) {
		/* Set Sample Offset. */
		if( note_param( &channel->note, slot ) > 0 ) {
			channel->offset_param = note_param( &channel->note, slot );
		}
		channel->sample_off = channel->offset_param << 8;
	}
	if( channel->note.volume >= 0x10 && channel->note.volume < 0x60 ) {
		channel->volume = channel->note.volume < 0x50 ? channel->note.volume - 0x10 : 64;
	}
	switch( note_vcol( &channel->note ) & 0xF0 ) {  /* FM-1:note_vcol (a volume-column effect of its own) */
		case 0x80: /* Fine Vol Down.*/
			channel->volume -= note_vcol( &channel->note ) & 0xF;
			if( channel->volume < 0 ) {
				channel->volume = 0;
			}
			break;
		case 0x90: /* Fine Vol Up.*/
			channel->volume += note_vcol( &channel->note ) & 0xF;
			if( channel->volume > 64 ) {
				channel->volume = 64;
			}
			break;
		case 0xA0: /* Set Vibrato Speed.*/
			if( ( note_vcol( &channel->note ) & 0xF ) > 0 ) {
				channel->vibrato_speed = note_vcol( &channel->note ) & 0xF;
			}
			break;
		case 0xB0: /* Vibrato.*/
			if( ( note_vcol( &channel->note ) & 0xF ) > 0 ) {
				channel->vibrato_depth = note_vcol( &channel->note ) & 0xF;
			}
			channel_vibrato( channel, 0 );
			break;
		case 0xC0: /* Set Panning.*/
			channel->panning = ( note_vcol( &channel->note ) & 0xF ) * 17;
			break;
		case 0xF0: /* Tone Porta.*/
			if( ( note_vcol( &channel->note ) & 0xF ) > 0 ) {
				channel->tone_porta_param = note_vcol( &channel->note ) & 0xF;
			}
			break;
	}
	if( channel->note.key > 0 ) {
		if( channel->note.key > 96 ) {
			channel->key_on = 0;
			channel->fm1_off = 1;  /* FM-1 */
		} else {
			channel->last_key = channel->note.key;  /* FM-1: for displays */
			legato = note_find( &channel->note, 0xD3, 0xD3 ) >= 0;  /* FM-1: PT/FM's LEG, the player's hook put it */
			porta = ( note_vcol( &channel->note ) & 0xF0 ) == 0xF0 ||  /* FM-1:any of its effects */
				note_find( &channel->note, 0x03, 0x05 ) >= 0 || note_find( &channel->note, 0x87, 0x8C ) >= 0 || legato;
			if( !porta ) {
				ins = channel->instrument->key_to_sample[ channel->note.key ];
				channel->sample = &channel->instrument->samples[ ins ];
			}
			fine_tune = channel->sample->fine_tune;
			slot = note_find( &channel->note, 0x75, 0xF2 );
			if( slot >= 0 ) {
				/* Set Fine Tune. */
				fine_tune = ( ( note_param( &channel->note, slot ) & 0xF ) << 4 ) - 128;
			}
			key = channel->note.key + channel->sample->rel_note;
			if( key < 1 ) {
				key = 1;
			}
			if( key > 120 ) {
				key = 120;
			}
			period = ( key << 6 ) + ( fine_tune >> 1 );
			if( channel->replay->module->linear_periods ) {
				channel->porta_period = 7744 - period;
			} else {
				channel->porta_period = 29021 * exp_2( ( period << FP_SHIFT ) / -768 ) >> FP_SHIFT;
			}
			if( !porta ) {
				channel->period = channel->porta_period;
				channel->sample_idx = channel->sample_off;
				channel->sample_fra = 0;
				if( channel->vibrato_type < 4 ) {
					channel->vibrato_phase = 0;
				}
				if( channel->tremolo_type < 4 ) {
					channel->tremolo_phase = 0;
				}
				channel->retrig_count = channel->av_count = 0;
				/* FM-1:a note for the player: its key, the key's own period, its offset */
				channel->fm1_trig = 1;
				channel->fm1_key = channel->note.key;
				channel->fm1_period0 = fm1_key_period( channel, channel->note.key );
				channel->fm1_offset = channel->sample_off;
				channel->pb_phase = 0;
			} else if( legato ) {
				/* FM-1:a legato note: a slide to it that arrives at once (the player retunes the sounding
				   note: nothing starts again, fm1_period0 the held note's key, the bend the interval) */
				channel->period = channel->porta_period;
				channel->fm1_trig = 2;
				channel->fm1_key = channel->note.key;
			}
		}
	}
}

static void channel_update_envelopes( struct channel *channel ) {
	if( channel->instrument->vol_env->enabled ) {  /* FM-1: -> (shared envelopes) */
		if( !channel->key_on ) {
			channel->fadeout_vol -= channel->instrument->vol_fadeout;
			if( channel->fadeout_vol < 0 ) {
				channel->fadeout_vol = 0;
			}
		}
		channel->vol_env_tick = envelope_next_tick( channel->instrument->vol_env,
			channel->vol_env_tick, channel->key_on );
	}
	if( channel->instrument->pan_env->enabled ) {
		channel->pan_env_tick = envelope_next_tick( channel->instrument->pan_env,
			channel->pan_env_tick, channel->key_on );
	}
}

static void channel_auto_vibrato( struct channel *channel ) {
	int sweep, rate, type, wave;
	int depth = channel->instrument->vib_depth & 0x7F;
	if( depth > 0 ) {
		sweep = channel->instrument->vib_sweep & 0x7F;
		rate = channel->instrument->vib_rate & 0x7F;
		type = channel->instrument->vib_type;
		if( channel->av_count < sweep ) {
			depth = depth * channel->av_count / sweep;
		}
		wave = channel_waveform( channel, channel->av_count * rate >> 2, type + 4 );
		channel->vibrato_add += wave * depth >> 8;
		channel->av_count++;
	}
}

static void channel_calculate_freq( struct channel *channel ) {
	int per = channel->period + channel->vibrato_add;
	if( channel->replay->module->linear_periods ) {
		per = per - ( channel->arpeggio_add << 6 );
		if( per < 28 || per > 7680 ) {
			per = 7680;
		}
		channel->freq = ( ( channel->replay->module->c2_rate >> 4 )
			* exp_2( ( ( 4608 - per ) * FP_ONE ) / 768 ) ) >> ( FP_SHIFT - 4 );  /* FM-1: * (a negative << is undefined) */
	} else {
		if( per > 29021 ) {
			per = 29021;
		}
		per = ( per << FP_SHIFT ) / exp_2( ( channel->arpeggio_add << FP_SHIFT ) / 12 );
		if( per < 28 ) {
			per = 29021;
		}
		channel->freq = channel->replay->module->c2_rate * 1712 / per;
	}
}

static void channel_calculate_ampl( struct channel *channel ) {
	int vol, range, env_pan = 32, env_vol = channel->key_on ? 64 : 0;
	if( channel->instrument->vol_env->enabled ) {
		env_vol = envelope_calculate_ampl( channel->instrument->vol_env, channel->vol_env_tick );
	}
	vol = channel->volume + channel->tremolo_add;
	if( vol > 64 ) {
		vol = 64;
	}
	if( vol < 0 ) {
		vol = 0;
	}
	vol = ( vol * channel->replay->module->gain * FP_ONE ) >> 13;
	vol = ( vol * channel->fadeout_vol ) >> 15;
	channel->ampl = ( vol * channel->replay->global_vol * env_vol ) >> 12;
	if( channel->instrument->pan_env->enabled ) {
		env_pan = envelope_calculate_ampl( channel->instrument->pan_env, channel->pan_env_tick );
	}
	range = ( channel->panning < 128 ) ? channel->panning : ( 255 - channel->panning );
	channel->pann = channel->panning + ( range * ( env_pan - 32 ) >> 5 );
}

/* FM-1:a tick's work of one of the cell's effects (it was channel_tick's switch; param: the effect's) */
static PLAT_COLD void channel_tick_fx( struct channel *channel, int effect, int param ) {   /* FM-1: PLAT_COLD (a tick's) */
	switch( effect ) {
		case 0x01: case 0x86: /* Porta Up. */
			channel_porta_up( channel, channel->porta_up_param );
			break;
		case 0x02: case 0x85: /* Porta Down. */
			channel_porta_down( channel, channel->porta_down_param );
			break;
		case 0x03: case 0x87: /* Tone Porta. */
			channel_tone_porta( channel );
			break;
		case 0x04: case 0x88: /* Vibrato. */
			channel->vibrato_phase += channel->vibrato_speed;
			channel_vibrato( channel, 0 );
			break;
		case 0x05: case 0x8C: /* Tone Porta + Vol Slide. */
			channel_tone_porta( channel );
			channel_volume_slide( channel );
			break;
		case 0x06: case 0x8B: /* Vibrato + Vol Slide. */
			channel->vibrato_phase += channel->vibrato_speed;
			channel_vibrato( channel, 0 );
			channel_volume_slide( channel );
			break;
		case 0x07: case 0x92: /* Tremolo. */
			channel->tremolo_phase += channel->tremolo_speed;
			channel_tremolo( channel );
			break;
		case 0x0A: case 0x84: /* Vol Slide. */
			channel_volume_slide( channel );
			break;
		case 0x11: /* Global Volume Slide. */
			channel->replay->global_vol = channel->replay->global_vol
				+ ( channel->gvol_slide_param >> 4 )
				- ( channel->gvol_slide_param & 0xF );
			if( channel->replay->global_vol < 0 ) {
				channel->replay->global_vol = 0;
			}
			if( channel->replay->global_vol > 64 ) {
				channel->replay->global_vol = 64;
			}
			break;
		case 0x19: /* Panning Slide. */
			channel->panning = channel->panning
				+ ( channel->pan_slide_param >> 4 )
				- ( channel->pan_slide_param & 0xF );
			if( channel->panning < 0 ) {
				channel->panning = 0;
			}
			if( channel->panning > 255 ) {
				channel->panning = 255;
			}
			break;
		case 0x1B: case 0x91: /* Retrig + Vol Slide. */
			channel_retrig_vol_slide( channel );
			break;
		case 0x1D: case 0x89: /* Tremor. */
			channel_tremor( channel );
			break;
		case 0x79: /* Retrig. */
			if( channel->fx_count >= param ) {
				channel->fx_count = 0;
				channel->sample_idx = channel->sample_fra = 0;
				channel->fm1_trig = 1;  /* FM-1:the player starts the note again */
				channel->fm1_offset = 0;
			}
			break;
		case 0x7C: case 0xFC: /* Note Cut. */
			if( param == channel->fx_count ) {
				channel->volume = 0;
				channel->fm1_cut = 1;  /* FM-1 */
			}
			break;
		case 0x7D: case 0xFD: /* Note Delay. */
			if( param == channel->fx_count ) {
				channel_trigger( channel );
			}
			break;
		case 0x8A: /* Arpeggio. */
			if( channel->fx_count == 1 ) {
				channel->arpeggio_add = channel->arpeggio_param >> 4;
			} else if( channel->fx_count == 2 ) {
				channel->arpeggio_add = channel->arpeggio_param & 0xF;
			} else {
				channel->arpeggio_add = channel->fx_count = 0;
			}
			break;
		case 0x95: /* Fine Vibrato. */
			channel->vibrato_phase += channel->vibrato_speed;
			channel_vibrato( channel, 1 );
			break;
		case 0x14: /* FM-1:Key Off at tick xx (PT/FM's songs; modules: at the row only, as upstream) */
			if( channel->replay->module->fm1_song && param == channel->fx_count ) {
				channel->key_on = 0;
				channel->fm1_off = 1;
			}
			break;
		case 0x22: /* FM-1:Panbrello (PT/FM's songs, IT's Yxy) */
			if( channel->replay->module->fm1_song ) {
				channel->pb_phase += channel->pb_speed;
				channel->pan_add = channel_waveform( channel, channel->pb_phase, 0 ) * channel->pb_depth >> 4;
			}
			break;
	}
}

static void channel_tick( struct channel *channel ) {
	int k, delay = note_find( &channel->note, 0x7D, 0x7D );  /* FM-1:in any of the effects */
	channel->vibrato_add = 0;
	channel->fx_count++;
	channel->retrig_count++;
	if( !( delay >= 0 && channel->fx_count <= note_param( &channel->note, delay ) ) ) {
		switch( note_vcol( &channel->note ) & 0xF0 ) {  /* FM-1:note_vcol */
			case 0x60: /* Vol Slide Down.*/
				channel->volume -= note_vcol( &channel->note ) & 0xF;
				if( channel->volume < 0 ) {
					channel->volume = 0;
				}
				break;
			case 0x70: /* Vol Slide Up.*/
				channel->volume += note_vcol( &channel->note ) & 0xF;
				if( channel->volume > 64 ) {
					channel->volume = 64;
				}
				break;
			case 0xB0: /* Vibrato.*/
				channel->vibrato_phase += channel->vibrato_speed;
				channel_vibrato( channel, 0 );
				break;
			case 0xD0: /* Pan Slide Left.*/
				channel->panning -= note_vcol( &channel->note ) & 0xF;
				if( channel->panning < 0 ) {
					channel->panning = 0;
				}
				break;
			case 0xE0: /* Pan Slide Right.*/
				channel->panning += note_vcol( &channel->note ) & 0xF;
				if( channel->panning > 255 ) {
					channel->panning = 255;
				}
				break;
			case 0xF0: /* Tone Porta.*/
				channel_tone_porta( channel );
				break;
		}
	}
	channel_tick_fx( channel, channel->note.effect, channel->note.param );
	for( k = 0; k < 3; k++ ) {  /* FM-1:the cell's other effects, in their order */
		if( channel->note.fx2[ k ] || channel->note.param2[ k ] ) {
			channel_tick_fx( channel, channel->note.fx2[ k ], channel->note.param2[ k ] );
		}
	}
	channel_auto_vibrato( channel );
	channel_calculate_freq( channel );
	channel_calculate_ampl( channel );
	channel_update_envelopes( channel );
}

/* FM-1:a row's work of one of the cell's effects (it was channel_row's switch; param: the effect's) */
static PLAT_COLD void channel_row_fx( struct channel *channel, int effect, int param ) {   /* FM-1: PLAT_COLD (a row's) */
	switch( effect ) {
		case 0x01: case 0x86: /* Porta Up. */
			if( param > 0 ) {
				channel->porta_up_param = param;
			}
			channel_porta_up( channel, channel->porta_up_param );
			break;
		case 0x02: case 0x85: /* Porta Down. */
			if( param > 0 ) {
				channel->porta_down_param = param;
			}
			channel_porta_down( channel, channel->porta_down_param );
			break;
		case 0x03: case 0x87: /* Tone Porta. */
			if( param > 0 ) {
				channel->tone_porta_param = param;
			}
			break;
		case 0x04: case 0x88: /* Vibrato. */
			if( ( param >> 4 ) > 0 ) {
				channel->vibrato_speed = param >> 4;
			}
			if( ( param & 0xF ) > 0 ) {
				channel->vibrato_depth = param & 0xF;
			}
			channel_vibrato( channel, 0 );
			break;
		case 0x05: case 0x8C: /* Tone Porta + Vol Slide. */
			if( param > 0 ) {
				channel->vol_slide_param = param;
			}
			channel_volume_slide( channel );
			break;
		case 0x06: case 0x8B: /* Vibrato + Vol Slide. */
			if( param > 0 ) {
				channel->vol_slide_param = param;
			}
			channel_vibrato( channel, 0 );
			channel_volume_slide( channel );
			break;
		case 0x07: case 0x92: /* Tremolo. */
			if( ( param >> 4 ) > 0 ) {
				channel->tremolo_speed = param >> 4;
			}
			if( ( param & 0xF ) > 0 ) {
				channel->tremolo_depth = param & 0xF;
			}
			channel_tremolo( channel );
			break;
		case 0x08: /* Set Panning.*/
			channel->panning = param & 0xFF;
			break;
		case 0x0A: case 0x84: /* Vol Slide. */
			if( param > 0 ) {
				channel->vol_slide_param = param;
			}
			channel_volume_slide( channel );
			break;
		case 0x0C: /* Set Volume. */
			channel->volume = param >= 64 ? 64 : param & 0x3F;
			break;
		case 0x10: case 0x96: /* Set Global Volume. */
			channel->replay->global_vol = param >= 64 ? 64 : param & 0x3F;
			break;
		case 0x11: /* Global Volume Slide. */
			if( param > 0 ) {
				channel->gvol_slide_param = param;
			}
			break;
		case 0x14: /* Key Off. */
			if( !channel->replay->module->fm1_song || param == 0 ) {  /* FM-1:Kxx: at tick xx */
				channel->key_on = 0;
				channel->fm1_off = 1;
			}
			break;
		case 0x15: /* Set Envelope Tick. */
			channel->vol_env_tick = channel->pan_env_tick = param & 0xFF;
			break;
		case 0x19: /* Panning Slide. */
			if( param > 0 ) {
				channel->pan_slide_param = param;
			}
			break;
		case 0x1B: case 0x91: /* Retrig + Vol Slide. */
			if( ( param >> 4 ) > 0 ) {
				channel->retrig_volume = param >> 4;
			}
			if( ( param & 0xF ) > 0 ) {
				channel->retrig_ticks = param & 0xF;
			}
			channel_retrig_vol_slide( channel );
			break;
		case 0x1D: case 0x89: /* Tremor. */
			if( ( param >> 4 ) > 0 ) {
				channel->tremor_on_ticks = param >> 4;
			}
			if( ( param & 0xF ) > 0 ) {
				channel->tremor_off_ticks = param & 0xF;
			}
			channel_tremor( channel );
			break;
		case 0x21: /* Extra Fine Porta. */
			if( param > 0 ) {
				channel->xfine_porta_param = param;
			}
			switch( channel->xfine_porta_param & 0xF0 ) {
				case 0x10:
					channel_porta_up( channel, 0xE0 | ( channel->xfine_porta_param & 0xF ) );
					break;
				case 0x20:
					channel_porta_down( channel, 0xE0 | ( channel->xfine_porta_param & 0xF ) );
					break;
			}
			break;
		case 0x71: /* Fine Porta Up. */
			if( param > 0 ) {
				channel->fine_porta_up_param = param;
			}
			channel_porta_up( channel, 0xF0 | ( channel->fine_porta_up_param & 0xF ) );
			break;
		case 0x72: /* Fine Porta Down. */
			if( param > 0 ) {
				channel->fine_porta_down_param = param;
			}
			channel_porta_down( channel, 0xF0 | ( channel->fine_porta_down_param & 0xF ) );
			break;
		case 0x73: /* FM-1: Glissando (E3x), as FastTracker 2 plays it: see replay_fm1_chan */
			channel->fm1_gliss = param & 0xF;
			break;
		case 0x74: case 0xF3: /* Set Vibrato Waveform. */
			if( param < 8 ) {
				channel->vibrato_type = param;
			}
			break;
		case 0x77: case 0xF4: /* Set Tremolo Waveform. */
			if( param < 8 ) {
				channel->tremolo_type = param;
			}
			break;
		case 0x7A: /* Fine Vol Slide Up. */
			if( param > 0 ) {
				channel->fine_vslide_up_param = param;
			}
			channel->volume += channel->fine_vslide_up_param;
			if( channel->volume > 64 ) {
				channel->volume = 64;
			}
			break;
		case 0x7B: /* Fine Vol Slide Down. */
			if( param > 0 ) {
				channel->fine_vslide_down_param = param;
			}
			channel->volume -= channel->fine_vslide_down_param;
			if( channel->volume < 0 ) {
				channel->volume = 0;
			}
			break;
		case 0x7C: case 0xFC: /* Note Cut. */
			if( param <= 0 ) {
				channel->volume = 0;
				channel->fm1_cut = 1;  /* FM-1 */
			}
			break;
		case 0x8A: /* Arpeggio. */
			if( param > 0 ) {
				channel->arpeggio_param = param;
			}
			break;
		case 0x95: /* Fine Vibrato.*/
			if( ( param >> 4 ) > 0 ) {
				channel->vibrato_speed = param >> 4;
			}
			if( ( param & 0xF ) > 0 ) {
				channel->vibrato_depth = param & 0xF;
			}
			channel_vibrato( channel, 1 );
			break;
		case 0xF8: /* Set Panning. */
			channel->panning = param * 17;
			break;
		case 0x22: /* FM-1:Panbrello (PT/FM's songs, IT's Yxy) */
			if( channel->replay->module->fm1_song ) {
				if( ( param >> 4 ) > 0 ) {
					channel->pb_speed = param >> 4;
				}
				if( ( param & 0xF ) > 0 ) {
					channel->pb_depth = param & 0xF;
				}
				channel->pan_add = channel_waveform( channel, channel->pb_phase, 0 ) * channel->pb_depth >> 4;
			}
			break;
	}
}

static void channel_row( struct channel *channel, struct note *note ) {
	int k, delay = note_find( note, 0x7D, 0xFD );  /* FM-1:in any of the effects */
	channel->note = *note;
	channel->retrig_count++;
	channel->vibrato_add = channel->tremolo_add = channel->arpeggio_add = channel->fx_count = 0;
	channel->pan_add = 0;  /* FM-1:Panbrello only while it runs */
	if( !( delay >= 0 && note_param( note, delay ) > 0 ) ) {
		/* Not note delay.*/
		channel_trigger( channel );
	}
	channel_row_fx( channel, channel->note.effect, channel->note.param );
	for( k = 0; k < 3; k++ ) {  /* FM-1:the cell's other effects, in their order */
		if( channel->note.fx2[ k ] || channel->note.param2[ k ] ) {
			channel_row_fx( channel, channel->note.fx2[ k ], channel->note.param2[ k ] );
		}
	}
	channel_auto_vibrato( channel );
	channel_calculate_freq( channel );
	channel_calculate_ampl( channel );
	channel_update_envelopes( channel );
}

/* FM-1: (no sample playback: the player's engines sound the notes) */
static void channel_update_sample_idx( struct channel *channel, int count, int sample_rate ) {
	struct sample *sample = channel->sample;
	int step = ( channel->freq << ( FP_SHIFT - 3 ) ) / ( sample_rate >> 3 );
	channel->sample_fra += step * count;
	channel->sample_idx += channel->sample_fra >> FP_SHIFT;
	if( channel->sample_idx > sample->loop_start ) {
		if( sample->loop_length > 1 ) {
			channel->sample_idx = sample->loop_start
				+ ( channel->sample_idx - sample->loop_start ) % sample->loop_length;
		} else {
			channel->sample_idx = sample->loop_start;
		}
	}
	channel->sample_fra &= FP_MASK;
}

/* FM-1:an effect as replay_row turns it (it was there): Exy into 0x7x, S3M's Sxy into 0xFx, 0xy
   with a param into arpeggio */
static void fm1_convert( unsigned char *effect, unsigned char *param ) {
	if( *effect == 0xE ) {
		*effect = 0x70 | ( *param >> 4 );
		*param &= 0xF;
	}
	if( *effect == 0x93 ) {
		*effect = 0xF0 | ( *param >> 4 );
		*param &= 0xF;
	}
	if( *effect == 0 && *param > 0 ) {
		*effect = 0x8A;
	}
}

/* FM-1:the global part of one of a cell's effects (it was replay_row's switch) */
static void replay_row_fx( struct replay *replay, struct channel *channel, int idx, int effect, int param ) {
	struct module *module = replay->module;
	switch( effect ) {
		case 0x81: /* Set Speed. */
			if( param > 0 ) {
				replay->tick = replay->speed = param;
			}
			break;
		case 0xB: case 0x82: /* Pattern Jump.*/
			if( replay->pl_count < 0 ) {
				replay->break_pos = param;
				replay->next_row = 0;
			}
			break;
		case 0xD: case 0x83: /* Pattern Break.*/
			if( replay->pl_count < 0 ) {
				if( replay->break_pos < 0 ) {
					replay->break_pos = replay->seq_pos + 1;
				}
				replay->next_row = ( param >> 4 ) * 10 + ( param & 0xF );
			}
			break;
		case 0xF: /* Set Speed/Tempo.*/
			if( replay->fm1_fixed ) {  /* FM-1: held by replay_fm1_timing */
			} else if( param > 0 ) {
				if( param < 32 ) {
					replay->tick = replay->speed = param;
				} else {
					replay->tempo = param;
				}
			} else if( module->fm1_song ) {  /* FM-1:F00: the song's own ticks and tempo again */
				replay->tick = replay->speed = module->default_speed;
				replay->tempo = module->default_tempo;
			}
			break;
		case 0x94: /* Set Tempo.*/
			if( param > 32 && !replay->fm1_fixed ) {  /* FM-1: not while held */
				replay->tempo = param;
			}
			break;
		case 0x76: case 0xFB : /* Pattern Loop.*/
			if( param == 0 ) {
				/* Set loop marker on this channel. */
				channel->pl_row = replay->row;
			}
			if( channel->pl_row < replay->row && replay->break_pos < 0 ) {
				/* Marker valid. */
				if( replay->pl_count < 0 ) {
					/* Not already looping, begin. */
					replay->pl_count = param;
					replay->pl_chan = idx;
				}
				if( replay->pl_chan == idx ) {
					/* Next Loop.*/
					if( replay->pl_count == 0 ) {
						/* Loop finished. Invalidate current marker. */
						channel->pl_row = replay->row + 1;
					} else {
						/* Loop. */
						replay->next_row = channel->pl_row;
					}
					replay->pl_count--;
				}
			}
			break;
		case 0x7E: case 0xFE: /* Pattern Delay.*/
			replay->tick = replay->speed + replay->speed * param;
			break;
	}
}

/* FM-1: light: a chase's row (replay_fm1_chase_row): the cells read and the row's global effects taken (the path:
   jumps, breaks, loops, delays; speed, tempo), the channels' own work left out (a chase's channels start over) */
static PLAT_COLD void replay_row_( struct replay *replay, int light ) {   /* FM-1: PLAT_COLD (a row's) */
	int idx, count, k;
	struct note note;
	struct pattern *pattern;
	struct channel *channel;
	struct module *module = replay->module;
	if( replay->next_row < 0 ) {
		replay->break_pos = replay->seq_pos + 1;
		replay->next_row = 0;
	}
	if( replay->break_pos >= 0 ) {
		if( replay->break_pos >= module->sequence_len ) {
			replay->break_pos = replay->next_row = 0;
			if( module->fm1_song ) {  /* FM-1:a PT/FM song goes on at its loop's start */
				replay->break_pos = module->restart_pos < module->sequence_len ? module->restart_pos : 0;
				replay->fm1_ended++;
			}
		}
		while( module->sequence[ replay->break_pos ] >= module->num_patterns ) {
			replay->break_pos++;
			if( replay->break_pos >= module->sequence_len ) {
				replay->break_pos = replay->next_row = 0;
			}
		}
		replay->seq_pos = replay->break_pos;
		for( idx = 0; idx < module->num_channels; idx++ ) {
			replay->channels[ idx ].pl_row = 0;
		}
		replay->break_pos = -1;
	}
	pattern = &module->patterns[ module->sequence[ replay->seq_pos ] ];
	replay->row = replay->next_row;
	if( replay->row >= pattern->num_rows ) {
		replay->row = 0;
	}
	replay->row_count++;  /* FM-1 */
	if( replay->play_count && replay->play_count[ 0 ] ) {
		count = replay->play_count[ replay->seq_pos ][ replay->row ];
		if( replay->pl_count < 0 && count < 127 ) {
			replay->play_count[ replay->seq_pos ][ replay->row ] = count + 1;
		}
	}
	replay->next_row = replay->row + 1;
	if( replay->next_row >= pattern->num_rows ) {
		replay->next_row = -1;
	}
	for( idx = 0; idx < module->num_channels; idx++ ) {
		channel = &replay->channels[ idx ];
		pattern_get_note( module, pattern, replay->row, idx, &note );  /* FM-1: + module */
		fm1_convert( &note.effect, &note.param );  /* FM-1:each of the cell's effects */
		for( k = 0; k < 3; k++ ) {
			if( note.fx2[ k ] || note.param2[ k ] ) {
				fm1_convert( &note.fx2[ k ], &note.param2[ k ] );
			}
		}
		if( !light ) {  /* FM-1 */
			channel_row( channel, &note );
		}
		replay_row_fx( replay, channel, idx, note.effect, note.param );
		for( k = 0; k < 3; k++ ) {
			if( note.fx2[ k ] || note.param2[ k ] ) {
				replay_row_fx( replay, channel, idx, note.fx2[ k ], note.param2[ k ] );
			}
		}
	}
}

static void replay_row( struct replay *replay ) {
	replay_row_( replay, 0 );
}

static int replay_tick( struct replay *replay ) {
	int idx, num_channels, count = 1;
	if( --replay->tick <= 0 ) {
		replay->tick = replay->speed;
		replay_row( replay );
	} else {
		num_channels = replay->module->num_channels;
		for( idx = 0; idx < num_channels; idx++ ) {
			channel_tick( &replay->channels[ idx ] );
		}
	}
	if( replay->play_count && replay->play_count[ 0 ] ) {
		count = replay->play_count[ replay->seq_pos ][ replay->row ] - 1;
	}
	return count;
}

static int module_init_play_count( struct module *module, char **play_count ) {
	int idx, pat, rows, len = 0;
	for( idx = 0; idx < module->sequence_len; idx++ ) {
		pat = module->sequence[ idx ];
		rows = ( pat < module->num_patterns ) ? module->patterns[ pat ].num_rows : 0;
		if( play_count ) {
			play_count[ idx ] = play_count[ 0 ] ? &play_count[ 0 ][ len ] : NULL;
		}
		len += rows;
	}
	return len;
}

/* FM-1:the rows of sequence position pos's pattern (64: a marker, or none) */
static int fm1_rows( struct module *module, int pos ) {
	int pat = pos >= 0 && pos < module->sequence_len ? module->sequence[ pos ] : 255;
	return pat < module->num_patterns ? module->patterns[ pat ].num_rows : 64;
}

/* FM-1: replay_set_sequence_pos with a start row (0..63); the play counter is allocated
   once (the arena never frees) and cleared, as upstream's fresh calloc was. */
static PLAT_COLD void replay_seek_row( struct replay *replay, int pos, int row ) {   /* FM-1: PLAT_COLD (a seek) */
	int idx;
	struct module *module = replay->module;
	if( pos >= module->sequence_len ) {
		pos = 0;
	}
	if( row < 0 || row >= fm1_rows( module, pos ) ) {  /* FM-1:each pattern its own rows (was 63) */
		row = 0;
	}
	replay->break_pos = pos;
	replay->next_row = row;
	replay->tick = 1;
	replay->global_vol = module->default_gvol;
	replay->speed = module->default_speed > 0 ? module->default_speed : 6;
	replay->tempo = module->default_tempo > 0 ? module->default_tempo : 125;
	replay->pl_count = replay->pl_chan = -1;
	if( module->fm1_song ) {
		/* FM-1: a song's end is its order's end (fm1_ended): no play counts, no RAM for them */
	} else if( !replay->play_count ) {
		replay->play_count = calloc( module->sequence_len, sizeof( char * ) );
		if( replay->play_count ) {
			replay->play_count[ 0 ] = calloc( module_init_play_count( module, NULL ), sizeof( char ) );
			module_init_play_count( module, replay->play_count );
		}
	} else if( replay->play_count[ 0 ] ) {
		memset( replay->play_count[ 0 ], 0, module_init_play_count( module, NULL ) );
	}
	for( idx = 0; idx < module->num_channels; idx++ ) {
		channel_init( &replay->channels[ idx ], replay, idx );
	}
	replay_tick( replay );
}

/* Set the pattern in the sequence to play. The tempo is reset to the default. */
void replay_set_sequence_pos( struct replay *replay, int pos ) {
	replay_seek_row( replay, pos, 0 );  /* FM-1 */
}

/* Deallocate the specified replay. */
void dispose_replay( struct replay *replay ) {
	if( replay->play_count ) {
		free( replay->play_count[ 0 ] );
		free( replay->play_count );
	}
	free( replay->channels );
	free( replay );
}

/* Allocate and initialize a replay with the specified sampling rate and interpolation. */
struct replay* new_replay( struct module *module, int sample_rate, int interpolation ) {
	struct replay *replay = calloc( 1, sizeof( struct replay ) );
	if( replay ) {
		replay->module = module;
		replay->sample_rate = sample_rate;
		replay->interpolation = interpolation;
		replay->channels = calloc( module->num_channels, sizeof( struct channel ) );  /* FM-1: (no volume ramp) */
		if( replay->channels ) {
			replay_set_sequence_pos( replay, 0 );
		} else {
			dispose_replay( replay );
			replay = NULL;
		}
	}
	return replay;
}

static int calculate_tick_len( int tempo, int sample_rate ) {
	return ( sample_rate * 5 ) / ( tempo * 2 );
}

/* Returns the length of the output buffer required by replay_get_audio(). */
int calculate_mix_buf_len( int sample_rate ) {
	return ( calculate_tick_len( 32, sample_rate ) + 65 ) * 4;
}

/* Returns the song duration in samples at the current sampling rate. */
int replay_calculate_duration( struct replay *replay ) {
	int count = 0, duration = 0;
	replay_set_sequence_pos( replay, 0 );
	while( count < 1 ) {
		duration += calculate_tick_len( replay->tempo, replay->sample_rate );
		count = replay_tick( replay );
	}
	replay_set_sequence_pos( replay, 0 );
	return duration;
}

/* Seek to approximately the specified sample position.
   The actual sample position reached is returned. */
int replay_seek( struct replay *replay, int sample_pos ) {
	int idx, tick_len, current_pos = 0;
	replay_set_sequence_pos( replay, 0 );
	tick_len = calculate_tick_len( replay->tempo, replay->sample_rate );
	while( ( sample_pos - current_pos ) >= tick_len ) {
		for( idx = 0; idx < replay->module->num_channels; idx++ ) {
			channel_update_sample_idx( &replay->channels[ idx ],
				tick_len * 2, replay->sample_rate * 2 );
		}
		current_pos += tick_len;
		replay_tick( replay );
		tick_len = calculate_tick_len( replay->tempo, replay->sample_rate );
	}
	return current_pos;
}

/* FM-1: (no audio: replay_get_audio and its parts are gone; replay_fm1_tick runs the replay) */

/* Returns the currently playing pattern in the sequence.*/
int replay_get_sequence_pos( struct replay *replay ) {
	return replay->seq_pos;
}

/* Returns the currently playing row in the pattern. */
int replay_get_row( struct replay *replay ) {
	return replay->row;
}

/* ---- FM-1 additions (see ibxm.h) ---- */

void replay_set_position_row( struct replay *replay, int pos, int row ) {
	replay_seek_row( replay, pos, row );
}

int replay_get_row_count( struct replay *replay ) {
	return replay->row_count;
}

void replay_hold_row( struct replay *replay ) {
	if( replay->tick < 2 ) {
		replay->tick = 2;  /* replay_tick() counts down to 1: tick effects, never a new row */
	}
}

void replay_next_row( struct replay *replay ) {
	replay->tick = 1;
	replay_tick( replay );  /* --tick reaches 0: the next row */
}

void replay_fm1_chase_row( struct replay *replay ) {
	replay->tick = replay->speed;  /* (replay_next_row's tick: --1 is 0, the row) */
	replay_row_( replay, 1 );
}

void replay_queue_row( struct replay *replay, int pos, int row ) {
	replay->break_pos = pos >= 0 && pos < replay->module->sequence_len ? pos : 0;
	replay->next_row = row >= 0 && row < fm1_rows( replay->module, replay->break_pos ) ? row : 0;  /* FM-1 */
}

void replay_jump_row( struct replay *replay, int pos, int row ) {
	replay_queue_row( replay, pos, row );
	replay_next_row( replay );  /* replay_row() takes break_pos / next_row as a pattern jump */
}

void replay_cut_notes( struct replay *replay ) {
	int idx;
	for( idx = 0; idx < replay->module->num_channels; idx++ ) {
		struct channel *channel = &replay->channels[ idx ];
		channel->volume = channel->ampl = 0;  /* as Note Cut */
		channel->tremolo_add = 0;
	}
}

int replay_skip_tick( struct replay *replay ) {
	int idx, tick_len = calculate_tick_len( replay->tempo, replay->sample_rate );
	for( idx = 0; idx < replay->module->num_channels; idx++ ) {
		channel_update_sample_idx( &replay->channels[ idx ], tick_len * 2, replay->sample_rate * 2 );
	}
	replay_tick( replay );
	return tick_len;
}

void replay_get_info( struct replay *replay, struct ibxm_info *info ) {
	int idx;
	struct module *module = replay->module;
	info->seq_pos = replay->seq_pos;
	info->row = replay->row;
	info->tick = replay->tick;
	info->speed = replay->speed;
	info->tempo = replay->tempo;
	info->sequence_len = module->sequence_len;
	info->num_channels = module->num_channels;
	info->row_count = replay->row_count;
	for( idx = 0; idx < IBXM_INFO_CHANNELS; idx++ ) {
		struct ibxm_channel_info *ci = &info->channel[ idx ];
		struct channel *channel = &replay->channels[ idx ];
		if( idx >= module->num_channels ) {
			memset( ci, 0, sizeof( *ci ) );
			continue;
		}
		ci->instrument = ( int ) ( channel->instrument - module->instruments );
		ci->key = channel->last_key;
		ci->volume = channel->volume;
		ci->ampl = channel->ampl;
		ci->sample = channel->sample;
		ci->sample_idx = channel->sample_idx;
		ci->freq = channel->freq;
		ci->loop_start = channel->sample->loop_start;
		ci->loop_length = channel->sample->loop_length;
	}
}

/* ---- FM-1 additions: the replay runs a song (see ibxm.h) ---- */

int replay_fm1_tick( struct replay *replay ) {
	int row_count = replay->row_count;
	replay_tick( replay );
	return replay->row_count != row_count;
}

/* FM-1: the pitch at period per against the key's own, in 1/64 semitones (the arpeggio's in it) */
static int fm1_bend( struct replay *replay, struct channel *channel, int per ) {
	if( replay->module->linear_periods ) {  /* a period is 1/64 semitone (lower: higher) */
		return channel->fm1_period0 - ( per - ( channel->arpeggio_add << 6 ) );
	}
	per = per < 28 ? 28 : per > 65535 ? 65535 : per;  /* Amiga periods: 1 / frequency */
	return ( ( log_2( channel->fm1_period0 > 0 ? channel->fm1_period0 : 1 ) - log_2( per ) ) * 768 >> FP_SHIFT )
		+ ( channel->arpeggio_add << 6 );
}

void replay_fm1_chan( struct replay *replay, int idx, struct ibxm_fm1_chan *out ) {
	struct channel *channel = &replay->channels[ idx ];
	int bend, plain, vol, pan;
	out->trig = channel->fm1_trig == 1;
	out->leg = channel->fm1_trig == 2;  /* (a legato note: fm1_trig 2) */
	out->off = channel->fm1_off;
	out->cut = channel->fm1_cut;
	channel->fm1_trig = channel->fm1_off = channel->fm1_cut = 0;
	out->key = channel->fm1_key;
	out->offset = channel->fm1_offset;
	bend = fm1_bend( replay, channel, channel->period + channel->vibrato_add );
	plain = fm1_bend( replay, channel, channel->period );  /* (without the vibrato) */
	if( out->trig || out->leg ) {  /* (a legato note: glissando's steps on its own tuning too) */
		channel->fm1_gofs = plain;
		channel->fm1_tp = 0;
	}
	if( channel->fm1_gliss && channel->fm1_tp ) {  /* FM-1: E3x: a slide to a note heard in semitones on the
	                                                  note's finetune, smooth underneath */
		int d = plain - channel->fm1_gofs;
		d = ( ( d + 32 + 64 * 256 ) / 64 - 256 ) * 64;  /* (to the nearest semitone) */
		bend = channel->fm1_gofs + d + ( bend - plain );
	}
	out->bend64 = bend < -6144 ? -6144 : bend > 6144 ? 6144 : bend;  /* (8 octaves) */
	vol = channel->volume + channel->tremolo_add;
	out->volume = vol < 0 ? 0 : vol > 64 ? 64 : vol;
	pan = channel->panning + channel->pan_add;
	out->panning = pan < 0 ? 0 : pan > 255 ? 255 : pan;
}

/* FM-1: channel idx's panning set from outside: the song's mixer PAN changed by hand while it plays (as an 8xx would) */
void replay_fm1_pan( struct replay *replay, int idx, int panning ) {
	if( idx >= 0 && idx < replay->module->num_channels ) {
		replay->channels[ idx ].panning = panning < 0 ? 0 : panning > 255 ? 255 : panning;
	}
}

/* FM-1: a note started on channel idx within its row (an edit heard at once): triggered as a row would, the row's
   cell and its tick effects going on after it */
void replay_fm1_note( struct replay *replay, int idx, const struct note *note ) {
	struct channel *channel;
	struct note row;
	int k;
	if( idx < 0 || idx >= replay->module->num_channels ) {
		return;
	}
	channel = &replay->channels[ idx ];
	row = channel->note;
	channel->note = *note;
	fm1_convert( &channel->note.effect, &channel->note.param );
	for( k = 0; k < 3; k++ ) {
		if( channel->note.fx2[ k ] || channel->note.param2[ k ] ) {
			fm1_convert( &channel->note.fx2[ k ], &channel->note.param2[ k ] );
		}
	}
	channel_trigger( channel );
	channel->note = row;
}

void replay_fm1_timing( struct replay *replay, int speed, int tempo ) {
	replay->fm1_fixed = speed > 0;
	if( speed > 0 ) {
		replay->speed = speed;
		replay->tempo = tempo;
	}
}

void replay_fm1_state( struct replay *replay, struct ibxm_fm1_state *out ) {
	out->seq_pos = replay->seq_pos;
	out->row = replay->row;
	out->tick = replay->tick;
	out->speed = replay->speed;
	out->tempo = replay->tempo;
	out->global_vol = replay->global_vol;
	out->row_count = replay->row_count;
	out->ended = replay->fm1_ended;
}
