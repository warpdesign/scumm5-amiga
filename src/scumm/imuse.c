/* scumm5-amiga - iMUSE sequencer driving a General MIDI device
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/imuse/ (imuse.cpp, imuse_player.cpp,
 * imuse_part.cpp, sysex_scumm.cpp, instrument.cpp, drivers/midi.cpp) and
 * audio/ (midiparser.cpp, midiparser_smf.cpp, mididrv.cpp), ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff. Reduced to the old iMUSE system
 * (MI2, INDY4) on a General MIDI device without GS mode and without native
 * MT-32 output: ROL tracks get their programs mapped to GM. SMF tracks have
 * a single subtrack each (format 0 and 2). ScummVM is the legal property of
 * its developers, whose names are too numerous to list here; see the
 * COPYRIGHT file distributed with the ScummVM source.
 */
#include "scumm.h"
#include "imuse.h"

#define TICKS_PER_BEAT 480
#define NUM_PLAYERS 8
#define NUM_PARTS 32
#define NUM_CHANNELS 16
#define NUM_QUEUE 64
#define NUM_DEFERRED 4
#define MAX_TRACKS 32
#define NUM_HANGING 32
#define PERCUSSION_CHANNEL 9
#define TRIGGER_ID 0
#define COMMAND_ID 1
#define IMUSE_SYSEX_ID 0x7D
#define ROLAND_SYSEX_ID 0x41
#define FADER_TICK_US 16667

enum { PF_VOLUME = 1, PF_TRANSPOSE = 3, PF_SPEED = 4 };
enum { IT_NONE, IT_PROGRAM, IT_ADLIB, IT_ROLAND };

void (*imuse_midi_send)(uint32_t msg);
void (*imuse_warn)(const char *msg, int a, int b);

static const uint8_t mt32_to_gm[128] = {
	0, 1, 0, 2, 4, 4, 5, 3, 16, 17, 18, 16, 16, 19, 20, 21,
	6, 6, 6, 7, 7, 7, 8, 112, 62, 62, 63, 63, 38, 38, 39, 39,
	88, 95, 52, 98, 97, 99, 14, 54, 102, 96, 53, 102, 81, 100, 14, 80,
	48, 48, 49, 45, 41, 40, 42, 42, 43, 46, 45, 24, 25, 28, 27, 104,
	32, 32, 34, 33, 36, 37, 35, 35, 79, 73, 72, 72, 74, 75, 64, 65,
	66, 67, 71, 71, 68, 69, 70, 22, 56, 59, 57, 57, 60, 60, 58, 61,
	61, 11, 11, 98, 14, 9, 14, 13, 12, 107, 107, 77, 78, 78, 76, 76,
	47, 117, 127, 118, 118, 116, 115, 119, 115, 112, 55, 124, 123, 0, 14, 117
};

/* LEC's MT-32 percussion bank put notes 24-34 on sounds without reverb. */
static const uint8_t gm_rhythm_map[35] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 36, 37, 38, 39, 40, 41, 66, 47,
	65, 48, 56
};

/* MT-32 custom timbres with a GM equivalent (MI2). */
static const struct {
	char name[11];
	uint8_t program;
} roland_to_gm[] = {
	{ "badspit   ", 62 }, { "Big Drum  ", 116 }, { "burp      ", 58 },
	{ "foghorn   ", 60 }, { "glop      ", 39 }, { "LeshBass  ", 33 },
	{ "ML explosn", 127 }, { "ReggaeBass", 32 }, { "rumble    ", 89 },
	{ "SdTrk Bend", 97 }, { "spitting  ", 62 }, { "Swell 1   ", 95 },
	{ "Swell 2   ", 95 }, { "thnderclap", 127 }
};

/* ---- structures ---- */

struct event {
	const uint8_t *start;
	uint32_t delta;
	uint8_t event, param1, param2, type;
	uint32_t length;
	const uint8_t *data;
};

struct note_timer {
	uint8_t channel, note;
	uint32_t time_left;
};

struct tracker {
	const uint8_t *play_pos;
	uint32_t play_time, play_tick, last_event_time, last_event_tick;
	uint8_t running_status;
};

struct parser {
	const uint8_t *tracks[MAX_TRACKS];
	uint8_t num_tracks, active_track;
	uint8_t abort_parse, jumping, has_driver;
	uint8_t hanging_count;
	uint32_t ppqn, tempo, psec_per_tick, timer_rate, sysex_delay;
	struct tracker pos;
	struct event next;
	uint16_t active_notes[128];
	struct note_timer hanging[NUM_HANGING];
};

struct instrument {
	uint8_t type, program, bank, mt32;	/* IT_ROLAND: program is the GM equivalent */
};

struct channel {
	uint8_t number, allocated;
	int8_t transpose;
	uint8_t pb_sensitivity, prio;
	int16_t detune, pitch_bend_temp;
};

struct player;

struct part {
	struct part *next, *prev;
	struct channel *mc;
	struct player *player;
	int16_t pitchbend;
	uint8_t pitchbend_factor;
	int8_t transpose, transpose_eff;
	uint8_t vol, vol_eff;
	int8_t detune;
	int16_t detune_eff;
	int8_t pan, pan_eff;
	uint8_t polyphony, on, modwheel, pedal;
	int8_t pri;
	uint8_t pri_eff, chan, effect_level, chorus, percussion, bank;
	struct instrument instrument;
	uint8_t unassigned_instrument;
};

struct hook {
	uint8_t jump[2];
	uint8_t transpose;
	uint8_t part_onoff[16], part_volume[16], part_program[16], part_transpose[16];
};

struct fader {
	int param;
	int8_t dir;
	int16_t incr;
	uint16_t ifrac, irem, ttime, cntdwn;
	int16_t state;
};

struct player {
	struct parser parser;
	uint8_t has_parser;
	struct part *parts;
	uint8_t active, scanning;
	int id;
	uint8_t priority, volume;
	int8_t pan, transpose;
	int16_t detune;
	int note_offset;
	uint8_t vol_eff;
	uint32_t track_index, loop_to_beat, loop_from_beat, loop_counter, loop_to_tick, loop_from_tick;
	uint8_t speed;
	uint32_t transition_timer;
	struct hook hook;
	struct fader faders[4];
	uint8_t is_midi, is_mt32, supports_percussion;
	uint32_t vol_chan;
};

struct deferred {
	uint32_t time_left;
	int a, b, c, d, e, f;
};

static struct player players[NUM_PLAYERS];
static struct part parts[NUM_PARTS];
static struct channel channels[NUM_CHANNELS];
static struct part *waiting[NUM_PARTS];
static int num_waiting;
static uint16_t active_notes[128];	/* Player::_active_notes, shared */
static uint8_t midi_reg_state[160];

static uint16_t cmd_queue[NUM_QUEUE][8];
static struct deferred deferred[NUM_DEFERRED];
static unsigned queue_end, queue_pos, queue_sound;
static uint8_t queue_adding, queue_marker, queue_cleared;
static uint16_t trigger_count;
static uint8_t master_volume, music_volume_eff;
static uint16_t channel_volume[8], channel_volume_eff[8], volchan_table[8];
static uint8_t initialized, is_monkey2;
static uint8_t rhy_vol;	/* IMuseInternal::_rhyState.vol */

static void warn(const char *msg, int a, int b)
{
	if (imuse_warn)
		imuse_warn(msg, a, b);
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static int transpose_clamp(int a, int b, int c)
{
	if (b > a)
		a += (b - a + 11) / 12 * 12;
	if (c < a)
		a -= (a - c + 11) / 12 * 12;
	return a;
}

static int32_t do_command(int a, int b, int c, int d, int e, int f, int g, int h);
static void player_clear(struct player *p);
static void player_send(struct player *p, uint32_t b);

/* ---- General MIDI device (IMuseDriver_GMidi, old system) ---- */

/* Drops program, controller and pitch bend messages that repeat the state
 * already sent, as ScummVM's driver does. */
static int track_midi_state(uint32_t b)
{
	static const uint8_t reg_order[] = { 0xFF, 0, 1, 7, 10, 64, 91, 93 };
	int evt = ((int)(b & 0xF0) - 0xB0) >> 4, part = b & 0x0F, i;
	uint8_t *var = &midi_reg_state[part];

	if (evt < 0 || evt > 3)
		return 1;
	b >>= 8;
	switch (evt) {
	case 0:
		for (i = 1; i < (int)sizeof(reg_order); i++)
			if (reg_order[i] == (b & 0xFF))
				break;
		if (i == (int)sizeof(reg_order))
			return 1;
		var += i * 0x10;
		b >>= 8;
		/* fall through */
	case 1:
		if (*var == (b & 0xFF))
			return 0;
		*var = (uint8_t)b;
		break;
	case 3:
		var += 0x80 + part;
		if (var[0] == (b & 0xFF) && var[1] == ((b >> 8) & 0xFF))
			return 0;
		var[0] = (uint8_t)b;
		var[1] = (uint8_t)(b >> 8);
		break;
	default:
		break;
	}
	return 1;
}

static void drv_send(uint32_t b)
{
	if (track_midi_state(b) && imuse_midi_send)
		imuse_midi_send(b);
}

static void ch_send(struct channel *c, int evt, int p1, int p2)
{
	drv_send((uint32_t)(p2 & 0xFF) << 16 | (uint32_t)(p1 & 0xFF) << 8 | (uint32_t)((evt & 0xF0) | c->number));
}

static void ch_pitch_bend(struct channel *c, int bend)
{
	int v;

	c->pitch_bend_temp = (int16_t)bend;
	bend = clampi(((bend * c->pb_sensitivity) >> 6) + c->detune + c->transpose * 128, -2048, 2047) * 4;
	v = bend + 0x2000;
	ch_send(c, 0xE0, v & 0x7F, (v >> 7) & 0x7F);
}

static void ch_pitch_bend_factor(struct channel *c, int v)
{
	ch_pitch_bend(c, 0);
	c->pb_sensitivity = (uint8_t)v;
}

static void ch_transpose(struct channel *c, int v)
{
	c->transpose = (int8_t)v;
	ch_pitch_bend(c, c->pitch_bend_temp);
}

static void ch_detune(struct channel *c, int v)
{
	c->detune = (int16_t)v;
	ch_pitch_bend(c, c->pitch_bend_temp);
}

static void ch_sustain(struct channel *c, int v)
{
	ch_send(c, 0xB0, 0x40, v);
}

static void ch_all_notes_off(struct channel *c)
{
	ch_send(c, 0xB0, 0x7B, 0);
}

static struct channel *drv_allocate_channel(void)
{
	int i;

	for (i = 0; i < NUM_CHANNELS; i++) {
		struct channel *c = &channels[i];
		if (c->number != PERCUSSION_CHANNEL && !c->allocated) {
			c->allocated = 1;
			return c;
		}
	}
	return 0;
}

static void drv_open(void)
{
	int i;

	memset(midi_reg_state, 0xFF, sizeof(midi_reg_state));
	for (i = 0; i < NUM_CHANNELS; i++) {
		struct channel *c = &channels[i];
		memset(c, 0, sizeof(*c));
		c->number = (uint8_t)i;
		c->pb_sensitivity = 2;
		c->prio = 0x80;
	}
	for (i = 0; i < NUM_CHANNELS; i++) {
		drv_send(0x0064B0 | i);
		drv_send(0x0065B0 | i);
		drv_send(0x1006B0 | i);
		drv_send(0x7F07B0 | i);
		drv_send(0x3F0AB0 | i);
		drv_send(0x0000C0 | i);
		drv_send(0x4000E0 | i);
		drv_send(0x0001B0 | i);
		drv_send(0x0040B0 | i);
		drv_send(0x405BB0 | i);
		drv_send(0x005DB0 | i);
		drv_send(0x0000B0 | i);
		drv_send(0x007BB0 | i);
	}
}

/* ---- instruments ---- */

static void instr_program(struct instrument *in, int prog, int bank, int mt32)
{
	in->type = IT_NONE;
	if (prog > 127)
		return;
	in->type = IT_PROGRAM;
	in->program = (uint8_t)prog;
	in->bank = (uint8_t)bank;
	in->mt32 = (uint8_t)mt32;
}

static void instr_roland(struct instrument *in, const uint8_t *sysex)
{
	unsigned i;

	in->type = IT_ROLAND;
	in->program = 255;
	for (i = 0; i < sizeof(roland_to_gm) / sizeof(roland_to_gm[0]); i++)
		if (!memcmp(roland_to_gm[i].name, sysex + 7, 10))
			in->program = roland_to_gm[i].program;
}

static int instr_valid(const struct instrument *in)
{
	switch (in->type) {
	case IT_PROGRAM:
		return in->program < 128 && (!in->mt32 || mt32_to_gm[in->program] < 128);
	case IT_ADLIB:
		return 1;
	case IT_ROLAND:
		return in->program < 128;
	default:
		return 0;
	}
}

static void instr_send(const struct instrument *in, struct channel *mc)
{
	int prog = in->program;

	if (in->type == IT_PROGRAM) {
		if (in->mt32)
			prog = mt32_to_gm[prog];
		ch_send(mc, 0xC0, prog, 0);
	} else if (in->type == IT_ROLAND && prog < 128) {
		ch_send(mc, 0xC0, prog, 0);
	}
}

/* ---- parts ---- */

static void suspend_part(struct part *part)
{
	int i;

	for (i = 0; i < num_waiting; i++)
		if (waiting[i]->pri_eff <= part->pri_eff)
			break;
	memmove(&waiting[i + 1], &waiting[i], (size_t)(num_waiting - i) * sizeof(waiting[0]));
	waiting[i] = part;
	num_waiting++;
}

static void remove_suspended_part(struct part *part)
{
	int i;

	for (i = 0; i < num_waiting; i++)
		if (waiting[i] == part) {
			num_waiting--;
			memmove(&waiting[i], &waiting[i + 1], (size_t)(num_waiting - i) * sizeof(waiting[0]));
			return;
		}
}

static void part_send_all(struct part *p);

static int reassign_channel(struct channel *mc)
{
	while (num_waiting) {
		struct part *part = waiting[0];
		num_waiting--;
		memmove(&waiting[0], &waiting[1], (size_t)num_waiting * sizeof(waiting[0]));
		if (part->player) {
			part->mc = mc;
			part_send_all(part);
			return 1;
		}
	}
	return 0;
}

static void part_off(struct part *p, int release)
{
	if (p->mc) {
		ch_sustain(p->mc, 0);
		ch_all_notes_off(p->mc);
		if (!release)
			return;
		p->mc->allocated = 0;
		p->mc = 0;
	}
}

static struct channel *allocate_channel(int prio)
{
	struct channel *mc = drv_allocate_channel();
	struct part *best = 0;
	int i;

	if (mc)
		return mc;
	for (i = 0; i < NUM_PARTS; i++) {
		struct part *p = &parts[i];
		if (!p->percussion && p->mc && p->pri_eff <= prio) {
			prio = p->pri_eff;
			best = p;
		}
	}
	if (best) {
		part_off(best, 1);
		suspend_part(best);
		mc = drv_allocate_channel();
	}
	return mc;
}

static void player_remove_part(struct player *pl, struct part *part)
{
	if (part->next)
		part->next->prev = part->prev;
	if (part->prev)
		part->prev->next = part->next;
	else
		pl->parts = part->next;
	part->next = part->prev = 0;
}

static void part_uninit(struct part *p)
{
	if (!p->player)
		return;
	part_off(p, 0);
	if (p->mc && !reassign_channel(p->mc))
		p->mc->allocated = 0;
	p->mc = 0;
	player_remove_part(p->player, p);
	remove_suspended_part(p);
	p->player = 0;
}

static void part_send_pitch_bend(struct part *p)
{
	if (p->mc)
		ch_pitch_bend(p->mc, p->pitchbend);
}

static void part_send_volume(struct part *p)
{
	p->vol_eff = (uint8_t)(((p->vol + 1) * p->player->vol_eff) >> 7);
	if (p->mc)
		ch_send(p->mc, 0xB0, 7, p->vol_eff);
}

static void part_send_pan(struct part *p, int value)
{
	if (p->mc)
		ch_send(p->mc, 0xB0, 10, value);
}

static void part_send_effect_level(struct part *p, int value)
{
	if (p->mc)
		ch_send(p->mc, 0xB0, 91, value);
}

static int part_clear_to_transmit(struct part *p)
{
	return p->mc != 0;
}

static void part_send_all(struct part *p)
{
	if (!part_clear_to_transmit(p))
		return;
	ch_pitch_bend_factor(p->mc, p->pitchbend_factor);
	ch_transpose(p->mc, p->transpose_eff);
	ch_detune(p->mc, p->detune_eff);
	part_send_pitch_bend(p);
	ch_send(p->mc, 0xB0, 7, p->vol_eff);
	ch_sustain(p->mc, p->pedal);
	ch_send(p->mc, 0xB0, 1, p->modwheel);
	part_send_pan(p, p->pan_eff + 0x40);
	if (instr_valid(&p->instrument))
		instr_send(&p->instrument, p->mc);
	part_send_effect_level(p, p->effect_level);
	ch_send(p->mc, 0xB0, 93, p->chorus);
	p->mc->prio = p->pri_eff;
}

static void part_volume(struct part *p, int value)
{
	p->vol = (uint8_t)value;
	part_send_volume(p);
}

static void part_set_pri(struct part *p, int pri)
{
	p->pri = (int8_t)pri;
	p->pri_eff = (uint8_t)clampi(p->pri + p->player->priority, 0, 255);
	if (p->mc)
		p->mc->prio = p->pri_eff;
}

static void part_set_pan(struct part *p, int pan)
{
	p->pan = (int8_t)pan;
	p->pan_eff = (int8_t)clampi(p->pan + p->player->pan, -64, 63);
	part_send_pan(p, p->pan_eff + 0x40);
}

static void part_set_transpose(struct part *p, int transpose, int lo, int hi)
{
	p->transpose = (int8_t)transpose;
	p->transpose_eff = (int8_t)(p->transpose == -128 ? 0 : transpose_clamp(p->transpose + p->player->transpose, lo, hi));
	if (p->mc)
		ch_transpose(p->mc, p->transpose_eff);
}

static void part_set_detune(struct part *p, int detune)
{
	p->detune = (int8_t)detune;
	p->detune_eff = (int16_t)clampi(p->detune + p->player->detune, -128, 127);
	if (p->mc)
		ch_detune(p->mc, p->detune_eff);
}

static void part_pitch_bend(struct part *p, int value)
{
	p->pitchbend = (int16_t)value;
	part_send_pitch_bend(p);
}

static void part_pitch_bend_factor(struct part *p, int value)
{
	if (value > 12)
		return;
	part_pitch_bend(p, 0);
	p->pitchbend_factor = (uint8_t)value;
	if (p->mc)
		ch_pitch_bend_factor(p->mc, value);
}

static void part_sustain(struct part *p, int value)
{
	p->pedal = (uint8_t)value;
	if (p->mc)
		ch_sustain(p->mc, value);
}

static void part_effect_level(struct part *p, int value)
{
	p->effect_level = (uint8_t)value;
	part_send_effect_level(p, value);
}

static void part_set_onoff(struct part *p, int on)
{
	if (p->on != on) {
		p->on = (uint8_t)on;
		if (!on)
			part_off(p, 0);
	}
}

static void part_program_change(struct part *p, int value)
{
	p->bank = 0;
	instr_program(&p->instrument, value, 0, p->player->is_mt32);
	if (part_clear_to_transmit(p))
		instr_send(&p->instrument, p->mc);
}

static void part_set_instrument(struct part *p, unsigned b)
{
	p->bank = (uint8_t)(b >> 8);
	instr_program(&p->instrument, b & 0xFF, p->bank, p->player->is_mt32);
	if (part_clear_to_transmit(p))
		instr_send(&p->instrument, p->mc);
}

static void part_note_on(struct part *p, int note, int velocity)
{
	struct channel *mc = p->mc;

	if (!p->on)
		return;
	if (p->unassigned_instrument && !p->percussion) {
		p->unassigned_instrument = 0;
		if (!instr_valid(&p->instrument))
			return;
	}
	if (mc && instr_valid(&p->instrument)) {
		ch_send(mc, 0x90, note, velocity);
	} else if (p->percussion) {
		mc = &channels[PERCUSSION_CHANNEL];
		if (p->vol_eff != rhy_vol)
			ch_send(mc, 0xB0, 7, p->vol_eff);
		if (note < 35)
			note = gm_rhythm_map[note];
		rhy_vol = p->vol_eff;
		ch_send(mc, 0x90, note, velocity);
	}
}

static void part_note_off(struct part *p, int note)
{
	if (!p->on)
		return;
	if (p->mc)
		ch_send(p->mc, 0x80, note, 0x40);
	else if (p->percussion)
		ch_send(&channels[PERCUSSION_CHANNEL], 0x80, note, 0x40);
}

static void part_all_notes_off(struct part *p)
{
	if (p->mc)
		ch_all_notes_off(p->mc);
}

static void part_setup(struct part *p, struct player *pl)
{
	p->player = pl;
	p->percussion = pl->is_midi && p->chan == PERCUSSION_CHANNEL;
	p->on = 1;
	p->pri_eff = pl->priority;
	p->pri = 0;
	p->vol = 127;
	p->vol_eff = pl->vol_eff;
	p->pan = (int8_t)clampi(pl->pan, -64, 63);
	p->transpose_eff = pl->transpose;
	p->transpose = 0;
	p->detune = 0;
	p->detune_eff = pl->detune;
	p->pitchbend_factor = 2;
	p->polyphony = 1;
	p->pitchbend = 0;
	p->effect_level = 64;
	p->instrument.type = IT_NONE;
	p->unassigned_instrument = 1;
	p->chorus = 0;
	p->modwheel = 0;
	p->bank = 0;
	p->pedal = 0;
	p->mc = 0;
}

static struct part *allocate_part(int pri)
{
	struct part *best = 0;
	int i;

	for (i = 0; i < NUM_PARTS; i++) {
		struct part *p = &parts[i];
		if (!p->player)
			return p;
		if (pri >= p->pri_eff) {
			pri = p->pri_eff;
			best = p;
		}
	}
	if (best)
		part_uninit(best);
	return best;
}

/* ---- sound resources ---- */

static const uint8_t *sound_data(int sound)
{
	if (sound <= 0 || sound >= res_count(RT_SOUND))
		return 0;
	return res_peek(RT_SOUND, (uint16_t)sound);
}

enum { CT_MTHD = 1, CT_FORM = 2, CT_MDHD = 4 };

static const uint8_t *find_start_of_sound(int sound, int ct)
{
	static const uint32_t ids[] = { TAG('M', 'T', 'h', 'd'), TAG('F', 'O', 'R', 'M'), TAG('M', 'D', 'h', 'd') };
	const uint8_t *ptr = sound_data(sound);
	int pos, i;

	if (!ptr)
		return 0;
	if (ptr[0] == 'R' && ptr[1] == 'O' && ptr[2] != 'L')
		return ct == (CT_MTHD | CT_FORM) ? ptr : 0;
	if (ptr[4] == 'S' && ptr[5] == 'O')
		return ct == (CT_MTHD | CT_FORM) ? ptr + 4 : 0;
	ptr += 8;
	for (pos = 0; pos < 48; pos++)
		for (i = 0; i < 3; i++)
			if ((ct & (1 << i)) && rd_be32(ptr + pos) == ids[i])
				return ptr + pos;
	return 0;
}

static uint32_t sound_tag(int sound)
{
	const uint8_t *ptr = sound_data(sound);
	return ptr ? rd_be32(ptr) : 0;
}

static int is_mt32(int sound)
{
	uint32_t tag = sound_tag(sound);
	return tag == TAG('R', 'O', 'L', ' ');
}

static int is_midi(int sound)
{
	switch (sound_tag(sound)) {
	case TAG('A', 'M', 'I', ' '):
	case TAG('R', 'O', 'L', ' '):
	case TAG('M', 'A', 'C', ' '):
	case TAG('G', 'M', 'D', ' '):
	case TAG('M', 'I', 'D', 'I'):
		return 1;
	default:
		return 0;
	}
}

static int supports_percussion(int sound)
{
	switch (sound_tag(sound)) {
	case TAG('R', 'O', 'L', ' '):
	case TAG('G', 'M', 'D', ' '):
	case TAG('M', 'I', 'D', 'I'):
		return 1;
	default:
		return 0;
	}
}

/* ---- SMF parser (MidiParser, MidiParser_SMF) ---- */

static uint32_t read_vlq(const uint8_t **data)
{
	uint32_t value = 0;
	int i;

	for (i = 0; i < 4; i++) {
		uint8_t c = *(*data)++;
		value = value << 7 | (c & 0x7F);
		if (!(c & 0x80))
			break;
	}
	return value;
}

static void parse_next_event(struct parser *ps, struct event *info)
{
	const uint8_t *play_pos = ps->pos.play_pos;

	info->start = play_pos;
	info->delta = read_vlq(&play_pos);
	if ((play_pos[0] & 0xF0) >= 0x80)
		info->event = *play_pos++;
	else
		info->event = ps->pos.running_status;
	if (info->event < 0x80) {
		ps->pos.play_pos = play_pos;
		return;
	}
	ps->pos.running_status = info->event;
	switch (info->event >> 4) {
	case 0x9:
		info->param1 = *play_pos++;
		info->param2 = *play_pos++;
		if (info->param2 == 0)
			info->event = (uint8_t)((info->event & 0x0F) | 0x80);
		info->length = 0;
		break;
	case 0xC:
	case 0xD:
		info->param1 = *play_pos++;
		info->param2 = 0;
		break;
	case 0x8:
	case 0xA:
	case 0xB:
	case 0xE:
		info->param1 = *play_pos++;
		info->param2 = *play_pos++;
		info->length = 0;
		break;
	case 0xF:
		switch (info->event & 0x0F) {
		case 0x2:
			info->param1 = *play_pos++;
			info->param2 = *play_pos++;
			break;
		case 0x3:
			info->param1 = *play_pos++;
			info->param2 = 0;
			break;
		case 0x6:
		case 0x8:
		case 0xA:
		case 0xB:
		case 0xC:
		case 0xE:
			info->param1 = info->param2 = 0;
			break;
		case 0x0:
			info->length = read_vlq(&play_pos);
			info->data = play_pos;
			play_pos += info->length;
			break;
		case 0xF:
			info->type = *play_pos++;
			info->length = read_vlq(&play_pos);
			info->data = play_pos;
			play_pos += info->length;
			break;
		default:
			warn("unsupported MIDI event", info->event, 0);
			break;
		}
		break;
	default:
		break;
	}
	ps->pos.play_pos = play_pos;
}

static struct player *parser_player(struct parser *ps)
{
	return (struct player *)((uint8_t *)ps - offsetof(struct player, parser));
}

static void parser_send(struct parser *ps, int status, int p1, int p2)
{
	player_send(parser_player(ps), (uint32_t)status | (uint32_t)p1 << 8 | (uint32_t)p2 << 16);
}

static void set_tempo(struct parser *ps, uint32_t tempo)
{
	ps->tempo = tempo;
	if (ps->ppqn)
		ps->psec_per_tick = (tempo + (ps->ppqn >> 2)) / ps->ppqn;
}

static void active_note(struct parser *ps, int channel, int note, int active)
{
	struct note_timer *ptr = ps->hanging;
	int i;

	if (note >= 128 || channel >= 16)
		return;
	if (active)
		ps->active_notes[note] |= (uint16_t)(1 << channel);
	else
		ps->active_notes[note] &= (uint16_t)~(1 << channel);
	for (i = NUM_HANGING; i; --i, ++ptr) {
		if (ptr->channel == channel && ptr->note == note && ptr->time_left) {
			ptr->time_left = 0;
			--ps->hanging_count;
			break;
		}
	}
}

static void hanging_note(struct parser *ps, int channel, int note, uint32_t time_left, int recycle)
{
	struct note_timer *best = 0, *ptr = ps->hanging;
	int i;

	if (ps->hanging_count >= NUM_HANGING) {
		warn("hangingNote: exceeded polyphony", channel, note);
		return;
	}
	for (i = NUM_HANGING; i; --i, ++ptr) {
		if (ptr->channel == channel && ptr->note == note) {
			if (ptr->time_left && ptr->time_left < time_left && recycle)
				return;
			best = ptr;
			if (ptr->time_left) {
				if (recycle)
					parser_send(ps, 0x80 | channel, note, 0);
				--ps->hanging_count;
			}
			break;
		} else if (!best && ptr->time_left == 0) {
			best = ptr;
		}
	}
	if (!time_left || (time_left & 0x80000000))
		time_left = 1;
	if (best) {
		best->channel = (uint8_t)channel;
		best->note = (uint8_t)note;
		best->time_left = time_left;
		++ps->hanging_count;
	}
}

static void parser_all_notes_off(struct parser *ps)
{
	int i, j;

	if (!ps->has_driver)
		return;
	for (i = 0; i < 128; ++i)
		for (j = 0; j < 16; ++j)
			if (ps->active_notes[i] & (1 << j))
				parser_send(ps, 0x80 | j, i, 0);
	for (i = 0; i < NUM_HANGING; i++) {
		if (ps->hanging[i].time_left) {
			parser_send(ps, 0x80 | ps->hanging[i].channel, ps->hanging[i].note, 0);
			ps->hanging[i].time_left = 0;
		}
	}
	ps->hanging_count = 0;
	/* MidiDriver_BASE::stopAllNotes() on the player */
	for (i = 0; i < 16; ++i)
		parser_send(ps, 0xB0 | i, 0x7B, 0);
	memset(ps->active_notes, 0, sizeof(ps->active_notes));
}

static void reset_tracking(struct parser *ps)
{
	memset(&ps->pos, 0, sizeof(ps->pos));
}

static int is_playing(const struct parser *ps)
{
	return ps->pos.play_pos != 0;
}

static void stop_playing(struct parser *ps)
{
	if (is_playing(ps))
		parser_all_notes_off(ps);
	reset_tracking(ps);
}

static void hang_all_active_notes(struct parser *ps)
{
	uint16_t temp_active[128];
	struct tracker current = ps->pos;
	int i, j;

	memcpy(temp_active, ps->active_notes, sizeof(temp_active));
	for (;;) {
		uint32_t event_tick;
		struct event *ev = &ps->next;

		for (i = 0; i < 128; ++i)
			if (temp_active[i])
				break;
		if (i == 128)
			break;
		if (ps->pos.play_pos)
			parse_next_event(ps, ev);
		event_tick = ps->pos.last_event_tick + ev->delta;
		if ((ev->event >> 4) == 0x8) {
			if (temp_active[ev->param1] & (1 << (ev->event & 0x0F))) {
				hanging_note(ps, ev->event & 0x0F, ev->param1, (event_tick - current.last_event_tick) * ps->psec_per_tick, 0);
				temp_active[ev->param1] &= (uint16_t)~(1 << (ev->event & 0x0F));
			}
		} else if (!ps->pos.play_pos || (ev->event == 0xFF && ev->type == 0x2F)) {
			ps->pos.play_pos = 0;
			for (i = 0; i < 128; ++i)
				for (j = 0; j < 16; ++j)
					if (temp_active[i] & (1 << j)) {
						active_note(ps, j, i, 0);
						parser_send(ps, 0x80 | j, i, 0);
					}
			break;
		}
		ps->pos.last_event_tick = event_tick;
	}
	ps->pos = current;
}

static int parser_set_track(struct parser *ps, int track)
{
	if (track < 0 || track >= ps->num_tracks)
		return 0;
	if (track == ps->active_track && is_playing(ps))
		return 1;
	hang_all_active_notes(ps);
	reset_tracking(ps);
	memset(ps->active_notes, 0, sizeof(ps->active_notes));
	memset(&ps->next, 0, sizeof(ps->next));
	ps->active_track = (uint8_t)track;
	ps->pos.play_pos = ps->tracks[track];
	parse_next_event(ps, &ps->next);
	return 1;
}

static int process_event(struct parser *ps, const struct event *info, int fire);

static int jump_to_tick(struct parser *ps, uint32_t tick, int fire, int stop_notes)
{
	struct tracker current, target;
	struct event current_event, target_event;

	if (ps->active_track >= ps->num_tracks)
		return 0;
	ps->jumping = 1;
	current = ps->pos;
	current_event = ps->next;
	reset_tracking(ps);
	ps->pos.play_pos = ps->tracks[ps->active_track];
	parse_next_event(ps, &ps->next);
	if (tick > 0) {
		for (;;) {
			struct event *info = &ps->next;
			uint32_t event_tick = ps->pos.last_event_tick + info->delta, event_time;
			if (event_tick >= tick) {
				ps->pos.play_time += (tick - ps->pos.last_event_tick) * ps->psec_per_tick;
				ps->pos.play_tick = tick;
				break;
			}
			if (info->event == 0xFF && info->type == 0x2F) {
				ps->pos = current;
				ps->next = current_event;
				ps->jumping = 0;
				return 0;
			}
			process_event(ps, info, fire);
			event_time = ps->pos.last_event_time + (event_tick - ps->pos.last_event_tick) * ps->psec_per_tick;
			ps->pos.play_time = event_time;
			ps->pos.last_event_time = event_time;
			ps->pos.play_tick = event_tick;
			ps->pos.last_event_tick = event_tick;
			if (ps->pos.play_pos)
				parse_next_event(ps, &ps->next);
		}
	}
	if (stop_notes) {
		if (!current.play_pos) {
			parser_all_notes_off(ps);
		} else {
			target = ps->pos;
			target_event = ps->next;
			ps->pos = current;
			ps->next = current_event;
			hang_all_active_notes(ps);
			ps->pos = target;
			ps->next = target_event;
		}
	}
	ps->abort_parse = 1;
	ps->jumping = 0;
	return 1;
}

static void parser_unload(struct parser *ps)
{
	if (ps->num_tracks == 0)
		return;
	stop_playing(ps);
	ps->num_tracks = 0;
	ps->active_track = 255;
	ps->abort_parse = 1;
	memset(ps->tracks, 0, sizeof(ps->tracks));
	memset(&ps->next, 0, sizeof(ps->next));
}

static void parser_init(struct parser *ps)
{
	memset(ps, 0, sizeof(*ps));
	ps->timer_rate = 0x4A0000;
	ps->ppqn = 96;
	ps->tempo = 500000;
	ps->psec_per_tick = 5208;
	ps->active_track = 255;
}

static int parser_load(struct parser *ps, const uint8_t *pos)
{
	unsigned type, chunks, n;

	parser_unload(ps);
	if (memcmp(pos, "MThd", 4) || rd_be32(pos + 4) != 6) {
		warn("bad MThd", 0, 0);
		return 0;
	}
	type = pos[9];
	chunks = (unsigned)(pos[10] << 8 | pos[11]);
	if (type == 1 || chunks > MAX_TRACKS) {
		warn("unsupported SMF", (int)type, (int)chunks);
		return 0;
	}
	ps->num_tracks = (uint8_t)chunks;
	ps->ppqn = (uint32_t)(pos[12] << 8 | pos[13]);
	pos += 14;
	for (n = 0; n < chunks; n++) {
		if (memcmp(pos, "MTrk", 4)) {
			warn("bad MTrk", (int)n, 0);
			return 0;
		}
		ps->tracks[n] = pos + 8;
		pos += 8 + rd_be32(pos + 4);
	}
	reset_tracking(ps);
	set_tempo(ps, 500000);
	parser_set_track(ps, 0);
	return 1;
}

static uint16_t player_sysex(struct player *p, const uint8_t *msg, uint16_t len);
static void player_meta(struct player *p, int type);

static int process_event(struct parser *ps, const struct event *info, int fire)
{
	if (info->event == 0xF0) {
		if (fire) {
			uint16_t delay;
			if (ps->sysex_delay > 0)
				return 0;
			if (info->data[info->length - 1] == 0xF7)
				delay = player_sysex(parser_player(ps), info->data, (uint16_t)(info->length - 1));
			else
				delay = player_sysex(parser_player(ps), info->data, (uint16_t)info->length);
			ps->sysex_delay = (uint32_t)delay * 1000;
		}
	} else if (info->event == 0xFF) {
		if (info->type == 0x2F) {
			ps->pos.play_pos = 0;
			stop_playing(ps);
			if (fire)
				player_meta(parser_player(ps), 0x2F);
			return 0;
		} else if (info->type == 0x51 && info->length >= 3) {
			set_tempo(ps, (uint32_t)(info->data[0] << 16 | info->data[1] << 8 | info->data[2]));
		}
	} else if (fire) {
		parser_send(ps, info->event, info->param1, info->param2);
	}
	return 1;
}

static void parser_on_timer(struct parser *ps)
{
	uint32_t end_time, event_time, event_tick;

	ps->sysex_delay -= ps->sysex_delay > ps->timer_rate ? ps->timer_rate : ps->sysex_delay;
	if (!ps->pos.play_pos || !ps->has_driver)
		return;
	ps->abort_parse = 0;
	end_time = ps->pos.play_time + ps->timer_rate;

	if (ps->hanging_count) {
		struct note_timer *ptr = ps->hanging;
		int i;
		for (i = NUM_HANGING; i; --i, ++ptr) {
			if (ptr->time_left) {
				if (ptr->time_left <= ps->timer_rate) {
					parser_send(ps, 0x80 | ptr->channel, ptr->note, 0);
					ptr->time_left = 0;
					--ps->hanging_count;
				} else {
					ptr->time_left -= ps->timer_rate;
				}
			}
		}
	}

	while (!ps->abort_parse) {
		struct event *info = &ps->next;

		event_tick = ps->pos.last_event_tick + info->delta;
		event_time = ps->pos.last_event_time + (event_tick - ps->pos.last_event_tick) * ps->psec_per_tick;
		if (event_time > end_time)
			break;
		if (info->event < 0x80) {
			warn("bad command or running status", info->event, 0);
			ps->pos.play_pos = 0;
			return;
		}
		if ((info->event >> 4) == 0x8)
			active_note(ps, info->event & 0x0F, info->param1, 0);
		else if ((info->event >> 4) == 0x9)
			active_note(ps, info->event & 0x0F, info->param1, 1);
		if (!process_event(ps, info, 1))
			return;
		if (!ps->abort_parse) {
			ps->pos.play_time = event_time;
			ps->pos.last_event_time = event_time;
			ps->pos.play_tick = event_tick;
			ps->pos.last_event_tick = event_tick;
			if (ps->pos.play_pos)
				parse_next_event(ps, &ps->next);
		}
	}
	if (!ps->abort_parse) {
		ps->pos.play_time = end_time;
		ps->pos.play_tick = (end_time - ps->pos.last_event_time) / ps->psec_per_tick + ps->pos.last_event_tick;
	}
}

/* ---- players ---- */

static struct part *get_active_part(struct player *pl, int chan)
{
	struct part *part;

	for (part = pl->parts; part; part = part->next)
		if (part->chan == chan)
			return part;
	return 0;
}

static struct part *get_part(struct player *pl, int chan)
{
	struct part *part = get_active_part(pl, chan);

	if (part)
		return part;
	part = allocate_part(pl->priority);
	if (!part)
		return 0;
	part->prev = 0;
	part->next = pl->parts;
	if (pl->parts)
		pl->parts->prev = part;
	pl->parts = part;
	part->chan = (uint8_t)chan;
	part_setup(part, pl);
	return part;
}

static int get_channel_volume(unsigned a)
{
	if (a < 8)
		return channel_volume_eff[a];
	return (master_volume * music_volume_eff / 255) / 2;
}

static void player_set_speed(struct player *pl, int speed)
{
	pl->speed = (uint8_t)speed;
	if (pl->has_parser)
		pl->parser.timer_rate = (uint32_t)((IMUSE_TIMER_US * pl->speed) >> 7);
}

static int player_set_volume(struct player *pl, int vol)
{
	struct part *part;

	if (vol > 127 || vol < 0)
		return -1;
	pl->volume = (uint8_t)vol;
	pl->vol_eff = (uint8_t)((get_channel_volume(pl->vol_chan) * (vol + 1)) >> 7);
	for (part = pl->parts; part; part = part->next)
		part_volume(part, part->vol);
	return 0;
}

static void player_set_detune(struct player *pl, int detune)
{
	struct part *part;

	pl->detune = (int16_t)detune;
	for (part = pl->parts; part; part = part->next)
		part_set_detune(part, part->detune);
}

static void player_set_pan(struct player *pl, int pan)
{
	struct part *part;

	pl->pan = (int8_t)pan;
	for (part = pl->parts; part; part = part->next)
		part_set_pan(part, part->pan);
}

static void player_set_priority(struct player *pl, int pri)
{
	struct part *part;

	pl->priority = (uint8_t)pri;
	for (part = pl->parts; part; part = part->next)
		part_set_pri(part, part->pri);
}

static int player_set_transpose(struct player *pl, int relative, int b)
{
	struct part *part;

	if (b > 24 || b < -24 || relative > 1 || relative < 0)
		return -1;
	if (relative)
		b = transpose_clamp(pl->transpose + b, -7, 7);
	pl->transpose = (int8_t)b;
	for (part = pl->parts; part; part = part->next)
		part_set_transpose(part, part->transpose, -24, 24);
	return 0;
}

static void part_set_transpose_rel(struct player *pl, int chan, int relative, int b)
{
	struct part *part;

	if (b > 24 || b < -24)
		return;
	part = get_part(pl, chan);
	if (!part)
		return;
	if (relative)
		b = transpose_clamp(b + part->transpose, -7, 7);
	part_set_transpose(part, b, -24, 24);
}

static void turn_off_pedals(struct player *pl)
{
	struct part *part;

	for (part = pl->parts; part; part = part->next)
		if (part->pedal)
			part_sustain(part, 0);
}

static int player_jump(struct player *pl, unsigned track, unsigned beat, unsigned tick)
{
	if (!pl->has_parser)
		return 0;
	if (parser_set_track(&pl->parser, (int)track))
		pl->track_index = track;
	if (!jump_to_tick(&pl->parser, (beat - 1) * TICKS_PER_BEAT + tick, 0, 1))
		return 0;
	turn_off_pedals(pl);
	return 1;
}

static int player_set_loop(struct player *pl, unsigned count, unsigned tobeat, unsigned totick, unsigned frombeat, unsigned fromtick)
{
	if (tobeat + 1 >= frombeat)
		return 0;
	if (tobeat == 0)
		tobeat = 1;
	pl->loop_counter = 0;
	pl->loop_to_beat = tobeat;
	pl->loop_to_tick = totick;
	pl->loop_from_beat = frombeat;
	pl->loop_from_tick = fromtick;
	pl->loop_counter = count;
	return 1;
}

static void uninit_parts(struct player *pl)
{
	while (pl->parts)
		part_uninit(pl->parts);
}

static void player_clear(struct player *pl)
{
	if (!pl->active)
		return;
	if (pl->has_parser) {
		parser_unload(&pl->parser);
		pl->parser.has_driver = 0;
	}
	uninit_parts(pl);
	pl->active = 0;
	pl->id = 0;
	pl->note_offset = 0;
	pl->speed = 128;
}

static void player_meta(struct player *pl, int type)
{
	if (type == 0x2F)
		player_clear(pl);
}

static unsigned beat_index(struct player *pl)
{
	return pl->has_parser ? pl->parser.pos.play_tick / TICKS_PER_BEAT + 1 : 0;
}

static int query_part_param(struct player *pl, int param, int chan)
{
	struct part *part;

	for (part = pl->parts; part; part = part->next) {
		if (part->chan == chan) {
			switch (param) {
			case 14:
				return part->on;
			case 15:
				return part->vol;
			case 16:
				warn("query of a part instrument", param, chan);
				return -1;
			case 17:
				return part->transpose;
			default:
				return -1;
			}
		}
	}
	return 129;
}

static int hook_query(const struct hook *h, int param, int chan)
{
	switch (param) {
	case 18:
		return h->jump[0];
	case 19:
		return h->transpose;
	case 20:
		return h->part_onoff[chan];
	case 21:
		return h->part_volume[chan];
	case 22:
		return h->part_program[chan];
	case 23:
		return h->part_transpose[chan];
	default:
		return -1;
	}
}

static int hook_set(struct hook *h, int cls, int value, int chan)
{
	uint8_t *arr;

	switch (cls) {
	case 0:
		if (value != h->jump[0]) {
			h->jump[1] = h->jump[0];
			h->jump[0] = (uint8_t)value;
		}
		return 0;
	case 1:
		h->transpose = (uint8_t)value;
		return 0;
	case 2:
		arr = h->part_onoff;
		break;
	case 3:
		arr = h->part_volume;
		break;
	case 4:
		arr = h->part_program;
		break;
	case 5:
		arr = h->part_transpose;
		break;
	default:
		return -1;
	}
	chan &= 0xFF;
	if (chan < 16)
		arr[chan] = (uint8_t)value;
	else if (chan == 16)
		memset(arr, value, 16);
	return 0;
}

static int player_get_param(struct player *pl, int param, int chan)
{
	switch (param) {
	case 0:
		return pl->priority;
	case 1:
		return pl->volume;
	case 2:
		return (uint8_t)pl->pan;
	case 3:
		return (uint8_t)pl->transpose;
	case 4:
		return (uint8_t)pl->detune;
	case 5:
		return pl->speed;
	case 6:
		return (int)pl->track_index;
	case 7:
		return (int)beat_index(pl);
	case 8:
		return pl->has_parser ? (int)(pl->parser.pos.play_tick % TICKS_PER_BEAT) : 0;
	case 9:
		return (int)pl->loop_counter;
	case 10:
		return (int)pl->loop_to_beat;
	case 11:
		return (int)pl->loop_to_tick;
	case 12:
		return (int)pl->loop_from_beat;
	case 13:
		return (int)pl->loop_from_tick;
	case 14:
	case 15:
	case 16:
	case 17:
		return query_part_param(pl, param, chan);
	case 18:
	case 19:
	case 20:
	case 21:
	case 22:
	case 23:
		return hook_query(&pl->hook, param, chan & 0xFF);
	default:
		return -1;
	}
}

static void turn_off_parts(struct player *pl)
{
	struct part *part;

	for (part = pl->parts; part; part = part->next)
		part_off(part, 0);
}

static void play_active_notes(struct player *pl)
{
	int i, j;

	for (i = 0; i < 16; ++i) {
		struct part *part = get_part(pl, i);
		if (part)
			for (j = 0; j < 128; ++j)
				if (active_notes[j] & (1 << i))
					part_note_on(part, j, 80);
	}
}

static int player_scan(struct player *pl, unsigned totrack, unsigned tobeat, unsigned totick)
{
	if (!pl->active || !pl->has_parser)
		return -1;
	if (tobeat == 0)
		tobeat++;
	turn_off_parts(pl);
	memset(active_notes, 0, sizeof(active_notes));
	pl->scanning = 1;
	if (totrack != pl->track_index)
		jump_to_tick(&pl->parser, 0xFFFFFFFF, 1, 1);
	parser_set_track(&pl->parser, (int)totrack);
	if (!jump_to_tick(&pl->parser, (tobeat - 1) * TICKS_PER_BEAT + totick, 1, 1)) {
		pl->scanning = 0;
		return -1;
	}
	pl->scanning = 0;
	play_active_notes(pl);
	if (pl->track_index != totrack) {
		pl->track_index = totrack;
		pl->loop_counter = 0;
	}
	return 0;
}

static int is_fading_out(const struct player *pl)
{
	int i;

	for (i = 0; i < 4; ++i) {
		const struct fader *f = &pl->faders[i];
		if (f->param == PF_VOLUME &&
		    pl->volume + f->cntdwn * f->incr + ((f->irem + f->cntdwn * f->ifrac) / f->ttime) * f->dir == 0)
			return 1;
	}
	return 0;
}

static int add_parameter_fader(struct player *pl, int param, int target, int time)
{
	struct fader *best = 0;
	int start, i, diff;

	switch (param) {
	case PF_VOLUME:
		if (!time) {
			player_set_volume(pl, target);
			return 0;
		}
		start = pl->volume;
		break;
	case PF_TRANSPOSE:
		if (!time) {
			player_set_detune(pl, target);
			return 0;
		}
		start = pl->detune;
		break;
	case PF_SPEED:
		start = pl->speed;
		break;
	case 127:
		for (i = 0; i < 4; ++i)
			pl->faders[i].param = 0;
		return 0;
	default:
		return 0;
	}
	for (i = 0; i < 4; ++i) {
		if (pl->faders[i].param == param) {
			best = &pl->faders[i];
			break;
		} else if (!pl->faders[i].param) {
			best = &pl->faders[i];
		}
	}
	if (!best)
		return -1;
	if (time == 0) {
		warn("parameter fade without time", param, target);
		return -1;
	}
	best->param = param;
	best->state = (int16_t)start;
	best->ttime = best->cntdwn = (uint16_t)time;
	diff = target - start;
	best->dir = diff >= 0 ? 1 : -1;
	best->incr = (int16_t)(diff / time);
	best->ifrac = (uint16_t)((diff < 0 ? -diff : diff) % time);
	best->irem = 0;
	return 0;
}

static void transition_parameters(struct player *pl)
{
	pl->transition_timer += IMUSE_TIMER_US;
	while (pl->transition_timer >= FADER_TICK_US) {
		int i;

		pl->transition_timer -= FADER_TICK_US;
		for (i = 0; i < 4; ++i) {
			struct fader *f = &pl->faders[i];
			int32_t mod;

			if (!f->param)
				continue;
			mod = f->incr;
			f->irem += f->ifrac;
			if (f->irem >= f->ttime) {
				f->irem -= f->ttime;
				mod += f->dir;
			}
			if (!mod) {
				if (!f->cntdwn || !--f->cntdwn)
					f->param = 0;
				continue;
			}
			f->state += mod;
			switch (f->param) {
			case PF_VOLUME:
				if (f->state >= 0 && f->state <= 127) {
					player_set_volume(pl, f->state);
					if (f->state == 0) {
						player_clear(pl);
						return;
					}
				}
				break;
			case PF_TRANSPOSE:
				if (f->state >= -9216 && f->state <= 9216)
					player_set_detune(pl, f->state);
				break;
			case PF_SPEED:
				if (f->state >= 0 && f->state <= 127)
					player_set_speed(pl, f->state);
				break;
			default:
				f->param = 0;
			}
			if (!f->cntdwn || !--f->cntdwn)
				f->param = 0;
		}
	}
}

static void player_on_timer(struct player *pl)
{
	uint32_t target_tick;
	unsigned beat, tick;

	transition_parameters(pl);
	if (!pl->active || !pl->has_parser)
		return;
	target_tick = pl->parser.pos.play_tick;
	beat = target_tick / TICKS_PER_BEAT + 1;
	tick = target_tick % TICKS_PER_BEAT;
	if (pl->loop_counter && (beat > pl->loop_from_beat || (beat == pl->loop_from_beat && tick >= pl->loop_from_tick))) {
		pl->loop_counter--;
		player_jump(pl, pl->track_index, pl->loop_to_beat, pl->loop_to_tick);
	}
	parser_on_timer(&pl->parser);
}

static void player_send(struct player *pl, uint32_t b)
{
	int cmd = b & 0xF0, chan = b & 0x0F;
	int param1 = (b >> 8) & 0xFF, param2 = (b >> 16) & 0xFF;
	struct part *part;

	switch (cmd >> 4) {
	case 0x8:
		if (!pl->scanning) {
			if ((part = get_part(pl, chan)) != 0)
				part_note_off(part, param1);
		} else {
			active_notes[param1] &= (uint16_t)~(1 << chan);
		}
		break;
	case 0x9:
		param1 = (param1 + pl->note_offset) & 0xFF;
		if (!pl->scanning) {
			if (pl->is_mt32)
				param2 = (((param2 * 3) >> 2) + 32) & 0x7F;
			if ((part = get_part(pl, chan)) != 0)
				part_note_on(part, param1, param2);
		} else if (param1 < 128) {
			active_notes[param1] |= (uint16_t)(1 << chan);
		}
		break;
	case 0xB:
		part = param1 == 123 ? get_active_part(pl, chan) : get_part(pl, chan);
		if (!part)
			break;
		switch (param1) {
		case 0:
			break;
		case 1:
			part->modwheel = (uint8_t)param2;
			if (part->mc)
				ch_send(part->mc, 0xB0, 1, param2);
			break;
		case 7:
			part_volume(part, param2);
			break;
		case 10:
			part_set_pan(part, param2 - 0x40);
			break;
		case 16:
			part_pitch_bend_factor(part, param2);
			break;
		case 17:
			part_set_detune(part, param2 - 0x40);
			break;
		case 18:
			part_set_pri(part, param2 - 0x40);
			break;
		case 64:
			part_sustain(part, param2 != 0);
			break;
		case 91:
			part_effect_level(part, param2);
			break;
		case 93:
			part->chorus = (uint8_t)param2;
			if (part->mc)
				ch_send(part->mc, 0xB0, 93, param2);
			break;
		case 116:
		case 117:
			break;
		case 123:
			part_all_notes_off(part);
			break;
		default:
			warn("invalid control change", param1, pl->id);
		}
		break;
	case 0xC:
		part = get_part(pl, chan);
		if (part) {
			if (pl->is_midi) {
				if (param1 < 128)
					part_program_change(part, param1);
			} else if (param1 < 32) {
				part->instrument.type = IT_ADLIB;
				if (part_clear_to_transmit(part))
					instr_send(&part->instrument, part->mc);
			}
		}
		break;
	case 0xE:
		part = get_part(pl, chan);
		if (part)
			part_pitch_bend(part, ((param2 << 7) | param1) - 0x2000);
		break;
	case 0xA:
	case 0xD:
	case 0xF:
		break;
	default:
		if (!pl->scanning) {
			warn("invalid MIDI command", cmd, pl->id);
			player_clear(pl);
		}
	}
}

static void decode_sysex_bytes(const uint8_t *src, uint8_t *dst, int len)
{
	while (len >= 0) {
		*dst++ = (uint8_t)(((src[0] << 4) & 0xFF) | (src[1] & 0xF));
		src += 2;
		len -= 2;
	}
}

static void maybe_jump(struct player *pl, int cmd, unsigned track, unsigned beat, unsigned tick)
{
	if (cmd && pl->hook.jump[0] != cmd)
		return;
	if (cmd != 0 && cmd < 0x80) {
		pl->hook.jump[0] = pl->hook.jump[1];
		pl->hook.jump[1] = 0;
	}
	player_jump(pl, track, beat, tick);
}

/* Hook commands: data[0] channel, data[1] hook id, data[2..] values. A
 * non-zero id must match the hook set by the script; ids below 0x80 are
 * one-shot. */
static int hook_matches(uint8_t *hook, int cmd)
{
	if (cmd && *hook != cmd)
		return 0;
	if (cmd != 0 && cmd < 0x80)
		*hook = 0;
	return 1;
}

static void handle_marker(int id, int data);

static void sysex_scumm(struct player *pl, const uint8_t *msg, int len)
{
	const uint8_t *p = msg;
	uint8_t buf[128];
	struct part *part;
	int a;

	switch (*p++) {
	case 0:
		part = get_part(pl, p[0] & 0x0F);
		decode_sysex_bytes(p + 1, buf + 1, len - 1);
		if (part) {
			part_set_onoff(part, buf[1] & 0x01);
			part_effect_level(part, (buf[1] & 0x02) ? 127 : 0);
			part_set_pri(part, (int8_t)buf[2]);
			part_volume(part, buf[3]);
			part_set_pan(part, (int8_t)buf[4]);
			part->percussion = pl->supports_percussion ? (buf[5] & 0x80) != 0 : 0;
			part_set_transpose(part, (int8_t)buf[5], -12, 12);
			part_set_detune(part, (int8_t)buf[6]);
			part_pitch_bend_factor(part, buf[7]);
			if (part->percussion) {
				if (part->mc)
					part_off(part, 1);
			} else {
				if (pl->is_midi)
					instr_program(&part->instrument, buf[8], 0, pl->is_mt32);
				else if (buf[8] < 32)
					part->instrument.type = IT_ADLIB;
				if (!part->mc) {
					part->mc = allocate_channel(part->pri_eff);
					if (!part->mc)
						suspend_part(part);
				}
				part_send_all(part);
			}
		}
		break;
	case 1:
		part = get_part(pl, p[0]);
		if (part)
			part_uninit(part);
		break;
	case 2:
		uninit_parts(pl);
		break;
	case 16:
		a = *p++ & 0x0F;
		++p;
		part = get_part(pl, a);
		if (part) {
			if (len == 62 || len == 48) {
				part->instrument.type = IT_ADLIB;
				if (part_clear_to_transmit(part))
					instr_send(&part->instrument, part->mc);
			} else {
				part_program_change(part, 254);
			}
		}
		break;
	case 17:
	case 33:
		break;
	case 48:
		if (pl->scanning)
			break;
		decode_sysex_bytes(p + 1, buf, len - 1);
		maybe_jump(pl, buf[0], rd_be16(buf + 1), rd_be16(buf + 3), rd_be16(buf + 5));
		break;
	case 49:
		decode_sysex_bytes(p + 1, buf, len - 1);
		if (hook_matches(&pl->hook.transpose, buf[0]))
			player_set_transpose(pl, buf[1], (int8_t)buf[2]);
		break;
	case 50:
		buf[0] = *p++ & 0x0F;
		decode_sysex_bytes(p, buf + 1, len - 1);
		if (hook_matches(&pl->hook.part_onoff[buf[0]], buf[1]) && (part = get_part(pl, buf[0])) != 0)
			part_set_onoff(part, buf[2] != 0);
		break;
	case 51:
		buf[0] = *p++ & 0x0F;
		decode_sysex_bytes(p, buf + 1, len - 1);
		if (hook_matches(&pl->hook.part_volume[buf[0]], buf[1]) && (part = get_part(pl, buf[0])) != 0)
			part_volume(part, buf[2]);
		break;
	case 52:
		buf[0] = *p++ & 0x0F;
		decode_sysex_bytes(p, buf + 1, len - 1);
		if (hook_matches(&pl->hook.part_program[buf[0]], buf[1]) && (part = get_part(pl, buf[0])) != 0)
			part_program_change(part, buf[2]);
		break;
	case 53:
		buf[0] = *p++ & 0x0F;
		decode_sysex_bytes(p, buf + 1, len - 1);
		if (hook_matches(&pl->hook.part_transpose[buf[0]], buf[1]))
			part_set_transpose_rel(pl, buf[0], buf[2], (int8_t)buf[3]);
		break;
	case 64:
		p++;
		len--;
		while (len--)
			handle_marker(pl->id, *p++);
		break;
	case 80:
		decode_sysex_bytes(p + 1, buf, len - 1);
		player_set_loop(pl, rd_be16(buf), rd_be16(buf + 2), rd_be16(buf + 4), rd_be16(buf + 6), rd_be16(buf + 8));
		break;
	case 81:
		pl->loop_counter = 0;
		break;
	case 96:
		part = get_part(pl, p[0] & 0x0F);
		a = (p[1] & 0x0F) << 12 | (p[2] & 0x0F) << 8 | (p[3] & 0x0F) << 4 | (p[4] & 0x0F);
		if (part)
			part_set_instrument(part, (unsigned)a);
		break;
	default:
		warn("unknown iMUSE sysex", msg[0], pl->id);
	}
}

/* Player::sysEx and sysExNoDelay: returns the delay in ms before the next
 * sysex, which LEC's MT-32 driver needed. */
static uint16_t player_sysex(struct player *pl, const uint8_t *msg, uint16_t length)
{
	const uint8_t *p = msg;
	uint16_t len = length;
	uint8_t a = *p++;

	--len;
	if (a != IMUSE_SYSEX_ID) {
		if (a == ROLAND_SYSEX_ID && (pl->is_midi || pl->is_mt32)) {
			struct part *part = get_part(pl, p[0] & 0x0F);
			if (part) {
				instr_roland(&part->instrument, p - 1);
				if (part_clear_to_transmit(part))
					instr_send(&part->instrument, part->mc);
			}
		}
	} else if (--len < 128) {
		sysex_scumm(pl, p, len);
	}
	if (pl->is_mt32 && !pl->scanning && ((msg[0] == IMUSE_SYSEX_ID && msg[1] == 0) || msg[0] == ROLAND_SYSEX_ID))
		return length >= 25 ? 70 : 20;
	return 0;
}

static void load_start_parameters(struct player *pl, int sound)
{
	const uint8_t *ptr;

	pl->priority = 0x80;
	pl->volume = 0x7F;
	pl->vol_chan = 0xFFFF;
	pl->vol_eff = (uint8_t)get_channel_volume(0xFFFF);
	pl->pan = 0;
	pl->transpose = 0;
	pl->detune = 0;
	ptr = find_start_of_sound(sound, CT_MDHD);
	if (ptr && rd_be32(ptr + 4)) {
		ptr += 8;
		if (ptr[2] | ptr[3] | ptr[7]) {
			pl->priority = ptr[2];
			pl->volume = ptr[3];
			pl->pan = (int8_t)ptr[4];
			pl->transpose = (int8_t)ptr[5];
			pl->detune = (int8_t)ptr[6];
			player_set_speed(pl, ptr[7]);
		}
	}
}

static int start_seq_sound(struct player *pl, int sound)
{
	const uint8_t *ptr;

	pl->loop_to_beat = 1;
	pl->loop_from_beat = 1;
	pl->track_index = 0;
	pl->loop_counter = 0;
	pl->loop_to_tick = 0;
	pl->loop_from_tick = 0;
	ptr = find_start_of_sound(sound, CT_MTHD | CT_FORM);
	if (!ptr)
		return -1;
	if (!memcmp(ptr, "RO", 2) || !memcmp(ptr, "FORM", 4)) {
		warn("unsupported music format", sound, 0);
		return -1;
	}
	if (!pl->has_parser) {
		parser_init(&pl->parser);
		pl->has_parser = 1;
	}
	pl->parser.has_driver = 1;
	parser_load(&pl->parser, ptr);
	parser_set_track(&pl->parser, (int)pl->track_index);
	ptr = find_start_of_sound(sound, CT_MDHD);
	player_set_speed(pl, ptr && rd_be32(ptr + 4) && ptr[15] ? ptr[15] : 128);
	return 0;
}

static int player_start_sound(struct player *pl, int sound)
{
	int i;

	if (!find_start_of_sound(sound, CT_MTHD | CT_FORM))
		return 0;
	pl->is_mt32 = (uint8_t)is_mt32(sound);
	pl->is_midi = (uint8_t)is_midi(sound);
	pl->supports_percussion = (uint8_t)supports_percussion(sound);
	pl->parts = 0;
	pl->active = 1;
	pl->id = sound;
	load_start_parameters(pl, sound);
	for (i = 0; i < 4; ++i)
		pl->faders[i].param = 0;
	memset(&pl->hook, 0, sizeof(pl->hook));
	if (start_seq_sound(pl, sound) != 0) {
		pl->active = 0;
		return 0;
	}
	return 1;
}

static int music_timer(const struct player *pl)
{
	return pl->has_parser ? (int)(pl->parser.pos.play_tick * 2 / pl->parser.ppqn) : 0;
}

/* ---- the sequencer ---- */

static struct player *find_active_player(int id)
{
	int i;

	for (i = 0; i < NUM_PLAYERS; i++)
		if (players[i].active && players[i].id == (uint16_t)id)
			return &players[i];
	return 0;
}

static struct player *allocate_player(int priority)
{
	struct player *best = 0;
	int i, bestpri = 255;

	for (i = 0; i < NUM_PLAYERS; i++) {
		struct player *pl = &players[i];
		if (!pl->active)
			return pl;
		if (pl->priority < bestpri) {
			best = pl;
			bestpri = pl->priority;
		}
	}
	return bestpri < priority ? best : 0;
}

static int queue_sound_status(int sound)
{
	unsigned i;

	for (i = queue_end; i != queue_pos; i = (i + 1) % NUM_QUEUE)
		if (cmd_queue[i][0] == COMMAND_ID && cmd_queue[i][1] == 8 && cmd_queue[i][2] == (uint16_t)sound)
			return 2;
	for (i = 0; i < NUM_DEFERRED; ++i)
		if (deferred[i].time_left && deferred[i].a == 8 && deferred[i].b == sound)
			return 2;
	return 0;
}

static int sound_status(int sound, int ignore_fadeouts)
{
	int i;

	if (!is_monkey2)
		ignore_fadeouts = 0;
	for (i = 0; i < NUM_PLAYERS; i++) {
		struct player *pl = &players[i];
		if (pl->active && (!ignore_fadeouts || !is_fading_out(pl))) {
			if (sound == -1)
				return pl->id;
			if (pl->id == (uint16_t)sound)
				return 1;
		}
	}
	return sound == -1 ? 0 : queue_sound_status(sound);
}

static int stop_sound(int sound)
{
	struct player *pl = find_active_player(sound);

	if (!pl)
		return -1;
	player_clear(pl);
	return 0;
}

static int clear_queue(void)
{
	queue_adding = 0;
	queue_cleared = 1;
	queue_pos = 0;
	queue_end = 0;
	trigger_count = 0;
	return 0;
}

static int stop_all(void)
{
	int i;

	clear_queue();
	for (i = 0; i < NUM_PLAYERS; i++)
		if (players[i].active)
			player_clear(&players[i]);
	return 0;
}

static int start_sound(int sound)
{
	struct player *pl;
	const uint8_t *ptr;

	if (!find_start_of_sound(sound, CT_MTHD | CT_FORM) || !is_midi(sound))
		return 0;
	pl = find_active_player(sound);
	if (!pl) {
		ptr = find_start_of_sound(sound, CT_MDHD);
		pl = allocate_player(ptr && rd_be32(ptr + 4) && ptr[10] ? ptr[10] : 128);
	}
	if (!pl)
		return 0;
	/* MI2 workarounds for Scabb and Booty Island map music (ScummVM bug #1410) */
	if (is_monkey2) {
		if (sound == 107 && sound_status(100, 1) == 1)
			return 0;
		if (sound == 100 && sound_status(107, 1) == 1)
			stop_sound(107);
		if ((sound == 100 || sound == 115 || sound == 118) && sound_status(113, 1) == 1)
			stop_sound(113);
	}
	player_clear(pl);
	pl->note_offset = 0;
	return player_start_sound(pl, sound);
}

static void handle_marker(int id, int data)
{
	uint16_t *p;

	if (queue_end == queue_pos || (queue_adding && queue_sound == (unsigned)id && data == queue_marker))
		return;
	p = cmd_queue[queue_end];
	if (p[0] != TRIGGER_ID || id != p[1] || data != p[2])
		return;
	trigger_count--;
	queue_cleared = 0;
	queue_end = (queue_end + 1) % NUM_QUEUE;
	while (queue_end != queue_pos && cmd_queue[queue_end][0] == COMMAND_ID && !queue_cleared) {
		p = cmd_queue[queue_end];
		do_command(p[1], p[2], p[3], p[4], p[5], p[6], p[7], 0);
		queue_end = (queue_end + 1) % NUM_QUEUE;
	}
}

static int enqueue_command(int a, int b, int c, int d, int e, int f, int g)
{
	uint16_t *p;
	unsigned i = queue_pos;

	if (i == queue_end)
		return -1;
	if (a == -1) {
		queue_adding = 0;
		trigger_count++;
		return 0;
	}
	p = cmd_queue[queue_pos];
	p[0] = COMMAND_ID;
	p[1] = (uint16_t)a;
	p[2] = (uint16_t)b;
	p[3] = (uint16_t)c;
	p[4] = (uint16_t)d;
	p[5] = (uint16_t)e;
	p[6] = (uint16_t)f;
	p[7] = (uint16_t)g;
	i = (i + 1) % NUM_QUEUE;
	if (queue_end != i) {
		queue_pos = i;
		return 0;
	}
	queue_pos = (i - 1) % NUM_QUEUE;
	return -1;
}

static int enqueue_trigger(int sound, int marker)
{
	uint16_t *p = cmd_queue[queue_pos];
	unsigned pos;

	p[0] = TRIGGER_ID;
	p[1] = (uint16_t)sound;
	p[2] = (uint16_t)marker;
	pos = (queue_pos + 1) % NUM_QUEUE;
	if (queue_end == pos) {
		queue_pos = (pos - 1) % NUM_QUEUE;
		return -1;
	}
	queue_pos = pos;
	queue_adding = 1;
	queue_sound = (unsigned)sound;
	queue_marker = (uint8_t)marker;
	return 0;
}

static int query_queue(int param)
{
	switch (param) {
	case 0:
		return trigger_count;
	case 1:
		return queue_end == queue_pos ? -1 : cmd_queue[queue_end][1];
	case 2:
		return queue_end == queue_pos ? 0xFF : cmd_queue[queue_end][2];
	default:
		return -1;
	}
}

static void update_volumes(void)
{
	int i;

	for (i = 0; i < NUM_PLAYERS; i++)
		if (players[i].active)
			player_set_volume(&players[i], players[i].volume);
}

static int set_master_volume(unsigned vol)
{
	unsigned i;

	if (vol > 255)
		vol = 255;
	if (master_volume == vol)
		return 0;
	master_volume = (uint8_t)vol;
	vol = master_volume * music_volume_eff / 255;
	for (i = 0; i < 8; i++)
		channel_volume_eff[i] = (uint16_t)(channel_volume[i] * vol / 255);
	update_volumes();
	return 0;
}

static int set_channel_volume(unsigned chan, unsigned vol)
{
	if (chan >= 8 || vol > 127)
		return -1;
	channel_volume[chan] = (uint16_t)vol;
	channel_volume_eff[chan] = (uint16_t)(master_volume * music_volume_eff * vol / 255 / 255);
	update_volumes();
	return 0;
}

static int set_volchan(int sound, int volchan)
{
	struct player *best = 0, *sameid = 0, *pl;
	int r, i, num = 0;

	if ((unsigned)volchan >= 8)
		return -1;
	r = volchan_table[volchan];
	if (r >= 8) {
		pl = find_active_player(sound);
		if (pl && pl->vol_chan != (uint16_t)volchan) {
			pl->vol_chan = (uint32_t)volchan;
			player_set_volume(pl, pl->volume);
			return 0;
		}
		return -1;
	}
	for (i = 0; i < NUM_PLAYERS; i++) {
		pl = &players[i];
		if (!pl->active)
			continue;
		if (pl->vol_chan == (uint16_t)volchan) {
			num++;
			if (!best || pl->priority <= best->priority)
				best = pl;
		} else if (pl->id == (uint16_t)sound) {
			sameid = pl;
		}
	}
	if (!sameid)
		return -1;
	if (best && num >= r)
		player_clear(best);
	sameid->vol_chan = (uint32_t)volchan;
	player_set_volume(sameid, sameid->volume);
	return 0;
}

static void add_deferred_command(int time, int a, int b, int c, int d, int e, int f)
{
	int i;

	for (i = 0; i < NUM_DEFERRED; i++) {
		if (!deferred[i].time_left) {
			deferred[i].time_left = (uint32_t)time * 10000;
			deferred[i].a = a;
			deferred[i].b = b;
			deferred[i].c = c;
			deferred[i].d = d;
			deferred[i].e = e;
			deferred[i].f = f;
			return;
		}
	}
}

static void handle_deferred_commands(void)
{
	int i;

	for (i = 0; i < NUM_DEFERRED; i++) {
		struct deferred *ptr = &deferred[i];
		if (!ptr->time_left)
			continue;
		if (ptr->time_left <= IMUSE_TIMER_US) {
			do_command(ptr->a, ptr->b, ptr->c, ptr->d, ptr->e, ptr->f, 0, 0);
			ptr->time_left = IMUSE_TIMER_US;
		}
		ptr->time_left -= IMUSE_TIMER_US;
	}
}

static int32_t do_command_args(int numargs, const int *a)
{
	int cmd = a[0] & 0xFF, param = a[0] >> 8;
	struct player *pl = 0;
	struct part *part = 0;

	if (numargs < 1)
		return -1;
	if (!initialized && (cmd || param))
		return -1;
	if (param == 0) {
		switch (cmd) {
		case 6:
			if (a[1] > 127)
				return -1;
			return set_master_volume((unsigned)((a[1] << 1) | (a[1] ? 0 : 1)));
		case 7:
			return master_volume / 2;
		case 8:
			return start_sound(a[1]) ? 0 : -1;
		case 9:
			return stop_sound(a[1]);
		case 10:
		case 11:
			return stop_all();
		case 13:
			return sound_status(a[1], 1);
		case 16:
			return set_volchan(a[1], a[2]);
		case 17:
			return set_channel_volume((unsigned)a[1], (unsigned)a[2]);
		case 18:
			if ((unsigned)a[1] >= 8)
				return -1;
			volchan_table[a[1]] = (uint16_t)a[2];
			return 0;
		case 19:
			return -1;
		case 20:
			add_deferred_command(a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
			return 0;
		case 2:
		case 3:
			return 0;
		default:
			warn("unsupported doCommand", a[0], a[1]);
			return -1;
		}
	} else if (param == 1) {
		if ((1 << cmd) & 0x783FFF) {
			pl = find_active_player(a[1]);
			if (!pl)
				return -1;
			if ((1 << cmd) & (1 << 11 | 1 << 22)) {
				if (a[2] < 0 || a[2] > 15)
					return -1;
				part = get_part(pl, a[2]);
				if (!part)
					return -1;
			}
		}
		switch (cmd) {
		case 0:
			return player_get_param(pl, a[2], (uint8_t)a[3]);
		case 1:
			player_set_priority(pl, a[2]);
			return 0;
		case 2:
			return player_set_volume(pl, (uint8_t)a[2]);
		case 3:
			player_set_pan(pl, a[2]);
			return 0;
		case 4:
			return player_set_transpose(pl, (uint8_t)a[2], a[3]);
		case 5:
			player_set_detune(pl, a[2]);
			return 0;
		case 6:
			/* ScummVM bug #2242: MI2 pauses Rapp's flourishes too late */
			if (!is_monkey2 || pl->id != 183 || a[2] != 0)
				player_set_speed(pl, (uint8_t)a[2]);
			return 0;
		case 7:
			return player_jump(pl, (unsigned)a[2], (unsigned)a[3], (unsigned)a[4]) ? 0 : -1;
		case 8:
			return player_scan(pl, (unsigned)a[2], (unsigned)a[3], (unsigned)a[4]);
		case 9:
			return player_set_loop(pl, (unsigned)a[2], (unsigned)a[3], (unsigned)a[4], (unsigned)a[5], (unsigned)a[6]) ? 0 : -1;
		case 10:
			pl->loop_counter = 0;
			return 0;
		case 11:
			part_set_onoff(part, a[3] != 0);
			return 0;
		case 12:
		case 20:
			return hook_set(&pl->hook, (uint8_t)a[2], (uint8_t)a[3], (uint8_t)a[4]);
		case 13:
			return add_parameter_fader(pl, PF_VOLUME, a[2], a[3]);
		case 14:
			return enqueue_trigger(a[1], a[2]);
		case 15:
			return enqueue_command(a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
		case 16:
			return clear_queue();
		case 19:
			return player_get_param(pl, a[2], (uint8_t)a[3]);
		case 21:
			return -1;
		case 22:
			part_volume(part, a[3]);
			return 0;
		case 23:
			return query_queue(a[1]);
		case 24:
			return 0;
		default:
			warn("unsupported doCommand", a[0], a[1]);
			return -1;
		}
	}
	return -1;
}

static int32_t do_command(int a, int b, int c, int d, int e, int f, int g, int h)
{
	int args[8];

	args[0] = a;
	args[1] = b;
	args[2] = c;
	args[3] = d;
	args[4] = e;
	args[5] = f;
	args[6] = g;
	args[7] = h;
	return do_command_args(8, args);
}

/* ---- interface ---- */

void imuse_init(int monkey2)
{
	int i;

	is_monkey2 = (uint8_t)monkey2;
	rhy_vol = 127;
	drv_open();
	master_volume = 255;
	for (i = 0; i < 8; i++)
		channel_volume[i] = channel_volume_eff[i] = volchan_table[i] = 127;
	for (i = 0; i < NUM_PLAYERS; i++) {
		memset(&players[i], 0, sizeof(players[i]));
		players[i].speed = 128;
	}
	clear_queue();
	queue_cleared = 0;
	for (i = 0; i < NUM_PARTS; i++)
		memset(&parts[i], 0, sizeof(parts[i]));
	num_waiting = 0;
	initialized = 1;
	/* setMusicVolume(255): DOS plays the music at full volume */
	music_volume_eff = 255;
	for (i = 0; i < 8; i++)
		channel_volume_eff[i] = (uint16_t)(channel_volume[i] * (master_volume * music_volume_eff / 255) / 255);
}

int imuse_start_sound(int sound)
{
	return start_sound(sound);
}

void imuse_stop_sound(int sound)
{
	stop_sound(sound);
}

void imuse_stop_all(void)
{
	stop_all();
}

int imuse_sound_status(int sound, int ignore_fadeouts)
{
	return sound_status(sound, ignore_fadeouts);
}

int32_t imuse_do_command(int num, const int32_t *args)
{
	int a[16], i;

	if (num > 16)
		num = 16;
	for (i = 0; i < 16; i++)
		a[i] = i < num ? (int)args[i] : 0;
	return do_command_args(num, a);
}

int imuse_music_timer(void)
{
	int best = 0, i;

	for (i = 0; i < NUM_PLAYERS; i++)
		if (players[i].active) {
			int t = music_timer(&players[i]);
			if (t > best)
				best = t;
		}
	return best;
}

void imuse_timer(void)
{
	int i;

	if (!initialized)
		return;
	handle_deferred_commands();
	for (i = 0; i < NUM_PLAYERS; i++)
		if (players[i].active)
			player_on_timer(&players[i]);
}
