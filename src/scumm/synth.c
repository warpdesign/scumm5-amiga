/* scumm5-amiga - General MIDI synthesizer on Paula voices
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Plays the bank of tools/mkbank.py: each note takes one Paula voice, which
 * plays the zone's sample at a pitch set by the period register and a
 * volume set by the volume register, so no sample data passes through the
 * CPU. Volume follows SoundFont rules: attenuations in centibels add up
 * (zone, velocity and channel volume on the concave curve, the volume
 * envelope), and the sum maps to Paula's linear 0-64. Envelopes run at the
 * 100 Hz sequencer tick.
 *
 * A note starts one tick after it is played: a voice that was sounding is
 * stopped at once, and Paula needs time to see its DMA off before a new
 * sample can start on it.
 */
#include "platform.h"
#include "synth.h"
#include "synth_tables.h"
#include "imuse.h"

#define NO_ZONE 0xFF
#define SILENT_CB 960
#define BOOST_CB 60	/* the bank is normalised; this restores some headroom loss */

enum { V_FREE, V_PENDING, V_ATTACK, V_HOLD, V_DECAY, V_SUSTAIN, V_RELEASE };

struct zone {
	uint8_t key_hi, root, excl, flags;
	int16_t tune, atten;
	uint32_t rate, offset, length, loop_start, loop_length;
	uint16_t attack, hold, decay, release, sustain, unit;
};

struct unit {
	uint32_t file_offset, size;
	int8_t *data;			/* chip RAM, 0 while not loaded */
	uint32_t used;			/* load_clock when last asked for */
};

struct channel {
	uint8_t program, volume, pan, sustain;
	uint8_t rpn_lsb, rpn_msb, bend_range;
	int16_t bend;			/* -8192..8191 */
};

struct voice {
	uint8_t state, chan, note, velocity;
	uint8_t sustained, volume;
	uint16_t period;
	const struct zone *zone;
	uint32_t period_root;		/* period at the root key, << 4 */
	int32_t env;			/* attenuation, cB << 4 */
	uint16_t count;
	uint32_t age;
	int32_t remain;		/* one-shots: sample bytes left << 8 */
};

static plat_file bank;
static uint8_t *head;			/* program and drum tables, sound lists */
static uint32_t head_size;
static const uint8_t *first_zone, *num_zones, *drum_zone, *sound_tab, *unit_lists;
static struct zone *zones;
static struct unit *units;
static uint16_t nzones, nunits, nsounds;
static uint32_t pool_size, pool_used, load_clock;
static int8_t *silence;
static uint32_t paula_clock;
static struct channel channels[16];
static struct voice voices[SYNTH_VOICES];
static uint32_t age;

int (*synth_sound_active)(int sound);
uint32_t synth_missing, synth_pool_peak;

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] << 8 | p[1]);
}

static void reset_channels(void)
{
	int i;

	for (i = 0; i < 16; i++) {
		channels[i].program = 0;
		channels[i].volume = 100;
		channels[i].pan = 64;
		channels[i].sustain = 0;
		channels[i].rpn_lsb = channels[i].rpn_msb = 127;
		channels[i].bend_range = 2;
		channels[i].bend = 0;
	}
}

int synth_open(const char *path, uint32_t clock, uint32_t pool)
{
	uint8_t hdr[12];
	uint32_t i, data_start;
	const uint8_t *p;

	bank = plat_open(path);
	if (!bank)
		return -1;
	if (plat_read(bank, hdr, 12) != 12 || memcmp(hdr, "S5BK", 4) || rd16(hdr + 4) != 2) {
		synth_close();
		return -1;
	}
	nzones = rd16(hdr + 6);
	nunits = rd16(hdr + 8);
	nsounds = rd16(hdr + 10);
	/* everything before the sample data */
	head_size = 384 + 8 * nunits + 4 * nsounds + 40 * nzones;
	head = plat_alloc(head_size, 0);
	zones = plat_alloc(nzones * sizeof(struct zone), 0);
	units = plat_alloc(nunits * sizeof(struct unit), PLAT_CLEAR);
	silence = plat_alloc(4, PLAT_CHIP | PLAT_CLEAR);
	if (!head || !zones || !units || !silence || plat_read(bank, head, head_size) != (int32_t)head_size) {
		synth_close();
		return -1;
	}
	first_zone = head;
	num_zones = head + 128;
	drum_zone = head + 256;
	p = head + 384;
	for (i = 0; i < nunits; i++, p += 8) {
		units[i].file_offset = rd32(p);
		units[i].size = rd32(p + 4);
	}
	sound_tab = p;
	p += 4 * nsounds;
	for (i = 0; i < nzones; i++, p += 40) {
		struct zone *z = &zones[i];
		z->key_hi = p[0];
		z->root = p[1];
		z->excl = p[2];
		z->flags = p[3];
		z->tune = (int16_t)rd16(p + 4);
		z->atten = (int16_t)rd16(p + 6);
		z->rate = rd32(p + 8);
		z->offset = rd32(p + 12);
		z->length = rd32(p + 16);
		z->loop_start = rd32(p + 20);
		z->loop_length = rd32(p + 24);
		z->attack = rd16(p + 28);
		z->hold = rd16(p + 30);
		z->decay = rd16(p + 32);
		z->release = rd16(p + 34);
		z->sustain = rd16(p + 36);
		z->unit = rd16(p + 38);
	}
	/* the unit lists follow the zones, up to the first sample data */
	data_start = nunits ? units[0].file_offset : 0;
	unit_lists = 0;
	if (data_start > 12 + head_size) {
		uint32_t n = data_start - 12 - head_size;
		uint8_t *h = plat_alloc(head_size + n, 0);
		if (!h || plat_read(bank, h + head_size, n) != (int32_t)n) {
			plat_free(h, head_size + n);
			synth_close();
			return -1;
		}
		memcpy(h, head, head_size);
		plat_free(head, head_size);
		head = h;
		head_size += n;
		first_zone = head;
		num_zones = head + 128;
		drum_zone = head + 256;
		sound_tab = head + 384 + 8 * nunits;
		unit_lists = head + 384 + 8 * nunits + 4 * nsounds + 40 * nzones;
	}
	pool_size = pool;
	pool_used = 0;
	paula_clock = clock;
	reset_channels();
	for (i = 0; i < SYNTH_VOICES; i++)
		voices[i].state = V_FREE;
	return 0;
}

static void unload_unit(int u)
{
	int i;

	for (i = 0; i < SYNTH_VOICES; i++)
		if (voices[i].state != V_FREE && voices[i].zone->unit == u) {
			paula_stop(i);
			voices[i].state = V_FREE;
		}
	plat_free(units[u].data, units[u].size);
	units[u].data = 0;
	pool_used -= units[u].size;
}

void synth_close(void)
{
	int i;

	for (i = 0; i < SYNTH_VOICES; i++)
		if (voices[i].state != V_FREE) {
			paula_stop(i);
			voices[i].state = V_FREE;
		}
	for (i = 0; units && i < nunits; i++)
		if (units[i].data)
			unload_unit(i);
	if (bank)
		plat_close(bank);
	plat_free(head, head_size);
	plat_free(zones, nzones * sizeof(struct zone));
	plat_free(units, nunits * sizeof(struct unit));
	plat_free(silence, 4);
	bank = 0;
	head = 0;
	zones = 0;
	units = 0;
	silence = 0;
}

static int unit_needed(int u)
{
	int s, k;

	if (units[u].used == load_clock)
		return 1;
	for (s = 1; s < nsounds && synth_sound_active; s++) {
		const uint8_t *t = sound_tab + 4 * s;
		int f = rd16(t), n = rd16(t + 2);
		if (!n || !synth_sound_active(s))
			continue;
		for (k = 0; k < n; k++)
			if (rd16(unit_lists + 2 * (f + k)) == u)
				return 1;
	}
	return 0;
}

/* Frees the least recently asked for unit that no playing sound uses. */
static int evict_one(void)
{
	int u, best = -1;

	for (u = 0; u < nunits; u++)
		if (units[u].data && (best < 0 || units[u].used < units[best].used) && !unit_needed(u))
			best = u;
	if (best < 0)
		return 0;
	audio_lock();
	unload_unit(best);
	audio_unlock();
	return 1;
}

void synth_prepare(int sound)
{
	const uint8_t *t;
	int f, n, k;

	if (!bank || sound <= 0 || sound >= nsounds || !unit_lists)
		return;
	t = sound_tab + 4 * sound;
	f = rd16(t);
	n = rd16(t + 2);
	load_clock++;
	for (k = 0; k < n; k++)
		units[rd16(unit_lists + 2 * (f + k))].used = load_clock;
	for (k = 0; k < n; k++) {
		struct unit *u = &units[rd16(unit_lists + 2 * (f + k))];
		int8_t *d;
		if (u->data || !u->size)
			continue;
		while (pool_used + u->size > pool_size && evict_one())
			;
		if (pool_used + u->size > pool_size)
			return;
		d = plat_alloc(u->size, PLAT_CHIP);
		if (!d || plat_seek(bank, u->file_offset) || plat_read(bank, d, u->size) != (int32_t)u->size) {
			plat_free(d, u->size);
			return;
		}
		pool_used += u->size;
		if (pool_used > synth_pool_peak)
			synth_pool_peak = pool_used;
		u->data = d;
	}
}

static const struct zone *find_zone(int chan, int note)
{
	int i, first, n;

	if (chan == 9)
		return drum_zone[note] == NO_ZONE ? 0 : &zones[drum_zone[note]];
	first = first_zone[channels[chan].program];
	n = num_zones[channels[chan].program];
	if (first == NO_ZONE)
		return 0;
	for (i = 0; i < n - 1; i++)
		if (note <= zones[first + i].key_hi)
			break;
	return &zones[first + i];
}

static uint16_t voice_period(const struct voice *v)
{
	const struct channel *c = &channels[v->chan];
	int32_t semi = (v->chan == 9 ? 0 : (v->note - v->zone->root) * 64) + v->zone->tune;
	uint32_t p;
	int oct;

	if (v->chan != 9)
		semi += c->bend * c->bend_range / 128;
	oct = semi >= 0 ? semi / 768 : -((767 - semi) / 768);
	semi -= oct * 768;
	p = (v->period_root * exp2_neg[semi]) >> 16;
	if (oct > 0)
		p >>= oct;
	else if (oct < 0)
		p <<= -oct;
	p >>= 4;
	return (uint16_t)(p < 124 ? 124 : p > 65535 ? 65535 : p);
}

/* Total attenuation in cB: 0 is full volume. */
static int32_t voice_cb(const struct voice *v)
{
	int32_t cb = (v->env >> 4) + v->zone->atten + curve_cb[v->velocity] + curve_cb[channels[v->chan].volume] - BOOST_CB;

	return cb < 0 ? 0 : cb;
}

static uint8_t voice_volume(const struct voice *v)
{
	int32_t cb = voice_cb(v);

	return cb >= 1024 ? 0 : cb_volume[cb];
}

static void free_voice(int i)
{
	paula_stop(i);
	voices[i].state = V_FREE;
}

static void release_voice(struct voice *v)
{
	if (v->state == V_PENDING) {
		v->state = V_FREE;
		return;
	}
	v->state = V_RELEASE;
}

/* Paula 0 and 3 are on the left, 1 and 2 on the right. */
static int on_side(int i, int pan)
{
	int left = (i & 3) == 0 || (i & 3) == 3;
	return pan < 48 ? left : pan > 80 ? !left : 1;
}

/* With no voice free, the one whose loss is heard least: a releasing note,
 * then a drum hit or one note of a chord, then the quietest; a note still to
 * start only if all are. */
static int pick_voice(int pan)
{
	int i, k, best = -1;
	int32_t score, best_score = 0;

	for (i = 0; i < SYNTH_VOICES; i++)
		if (voices[i].state == V_FREE && on_side(i, pan))
			return i;
	for (i = 0; i < SYNTH_VOICES; i++)
		if (voices[i].state == V_FREE)
			return i;
	for (i = 0; i < SYNTH_VOICES; i++) {
		const struct voice *v = &voices[i];
		if (v->state == V_PENDING)
			score = -(int32_t)(age - v->age);
		else
			score = 10000 + voice_cb(v) + (v->state == V_RELEASE ? 4000 : 0) + (v->chan == 9 ? 300 : 0);
		if (v->state != V_PENDING && v->state != V_RELEASE && v->chan != 9)
			for (k = 0; k < SYNTH_VOICES; k++)
				if (k != i && voices[k].chan == v->chan && voices[k].state > V_PENDING && voices[k].state != V_RELEASE) {
					score += 300;
					break;
				}
		if (best < 0 || score > best_score || (score == best_score && v->age < voices[best].age)) {
			best = i;
			best_score = score;
		}
	}
	return best;
}

static void note_on(int chan, int note, int velocity)
{
	const struct zone *z = find_zone(chan, note);
	struct voice *v;
	int i;

	if (!z || !z->length)
		return;
	if (!units[z->unit].data) {
		synth_missing++;
		return;
	}
	for (i = 0; i < SYNTH_VOICES; i++) {
		v = &voices[i];
		if (v->state == V_FREE || v->chan != chan)
			continue;
		if (v->note == note || (z->excl && v->zone->excl == z->excl))
			break;
	}
	if (i == SYNTH_VOICES)
		i = pick_voice(channels[chan].pan);
	v = &voices[i];
	if (v->state != V_FREE && v->state != V_PENDING)
		paula_stop(i);
	v->state = V_PENDING;
	v->chan = (uint8_t)chan;
	v->note = (uint8_t)note;
	v->velocity = (uint8_t)velocity;
	v->sustained = 0;
	v->zone = z;
	v->period_root = (paula_clock << 4) / ((z->rate + 128) >> 8);
	v->age = ++age;
}

static void note_off(int chan, int note)
{
	int i;

	for (i = 0; i < SYNTH_VOICES; i++) {
		struct voice *v = &voices[i];
		if (v->state == V_FREE || v->state == V_RELEASE || v->chan != chan || v->note != note)
			continue;
		if (channels[chan].sustain)
			v->sustained = 1;
		else
			release_voice(v);
	}
}

static void all_notes_off(int chan)
{
	int i;

	for (i = 0; i < SYNTH_VOICES; i++)
		if (voices[i].state != V_FREE && voices[i].state != V_RELEASE && voices[i].chan == chan)
			release_voice(&voices[i]);
}

static void update_pitch(int chan)
{
	int i;

	for (i = 0; i < SYNTH_VOICES; i++) {
		struct voice *v = &voices[i];
		if (v->state > V_PENDING && v->chan == chan) {
			v->period = voice_period(v);
			paula_set(i, v->period, v->volume);
		}
	}
}

static void control(int chan, int ctrl, int value)
{
	struct channel *c = &channels[chan];
	int i;

	switch (ctrl) {
	case 6:
		if (c->rpn_lsb == 0 && c->rpn_msb == 0)
			c->bend_range = (uint8_t)value;
		break;
	case 7:
		c->volume = (uint8_t)value;
		break;
	case 10:
		c->pan = (uint8_t)value;
		break;
	case 64:
		c->sustain = value >= 64 || value == 1;
		if (!c->sustain)
			for (i = 0; i < SYNTH_VOICES; i++)
				if (voices[i].state != V_FREE && voices[i].chan == chan && voices[i].sustained) {
					voices[i].sustained = 0;
					release_voice(&voices[i]);
				}
		break;
	case 100:
		c->rpn_lsb = (uint8_t)value;
		break;
	case 101:
		c->rpn_msb = (uint8_t)value;
		break;
	case 120:
	case 123:
		all_notes_off(chan);
		break;
	default:
		break;
	}
}

void synth_midi(uint32_t msg)
{
	int chan = msg & 0x0F, a = (msg >> 8) & 0x7F, b = (msg >> 16) & 0x7F;

	if (!zones)
		return;
	switch ((msg >> 4) & 0x0F) {
	case 0x8:
		note_off(chan, a);
		break;
	case 0x9:
		if (b)
			note_on(chan, a, b);
		else
			note_off(chan, a);
		break;
	case 0xB:
		control(chan, a, b);
		break;
	case 0xC:
		channels[chan].program = (uint8_t)a;
		break;
	case 0xE:
		channels[chan].bend = (int16_t)((b << 7 | a) - 0x2000);
		update_pitch(chan);
		break;
	default:
		break;
	}
}

void synth_tick_start(void)
{
	int i;

	for (i = 0; i < SYNTH_VOICES; i++) {
		struct voice *v = &voices[i];
		const struct zone *z = v->zone;
		const int8_t *data, *loop;
		uint32_t loop_len;

		if (v->state != V_PENDING)
			continue;
		if (!units[z->unit].data) {
			v->state = V_FREE;
			continue;
		}
		data = units[z->unit].data + z->offset;
		if (z->flags & 1) {
			loop = data + z->loop_start;
			loop_len = z->loop_length;
		} else {
			loop = silence;
			loop_len = 2;
		}
		v->state = V_ATTACK;
		v->count = 0;
		v->env = z->attack ? (int32_t)attack_cb[0] << 4 : 0;
		if (!z->attack)
			v->state = z->hold ? V_HOLD : V_DECAY;
		v->remain = (int32_t)(z->length << 8);
		v->period = voice_period(v);
		v->volume = voice_volume(v);
		paula_play(i, data, (uint16_t)(z->length >> 1), loop, (uint16_t)(loop_len >> 1), v->period, v->volume);
	}
}

/* Ticks for 100 dB become a step in cB << 4 per tick. */
static int32_t env_step(uint16_t ticks)
{
	return ticks ? (int32_t)(1000 * 16 / ticks) + 1 : 1000 * 16;
}

void synth_tick_end(void)
{
	int i;

	for (i = 0; i < SYNTH_VOICES; i++) {
		struct voice *v = &voices[i];
		const struct zone *z = v->zone;
		uint8_t vol;

		if (v->state <= V_PENDING)
			continue;
		switch (v->state) {
		case V_ATTACK:
			if (++v->count >= z->attack) {
				v->env = 0;
				v->count = 0;
				v->state = z->hold ? V_HOLD : V_DECAY;
			} else {
				v->env = (int32_t)attack_cb[v->count * 64 / z->attack] << 4;
			}
			break;
		case V_HOLD:
			if (++v->count >= z->hold)
				v->state = V_DECAY;
			break;
		case V_DECAY:
			v->env += env_step(z->decay);
			if (v->env >= (int32_t)z->sustain << 4) {
				v->env = (int32_t)z->sustain << 4;
				v->state = V_SUSTAIN;
			}
			break;
		case V_RELEASE:
			v->env += env_step(z->release);
			break;
		default:
			break;
		}
		if (!(z->flags & 1)) {
			v->remain -= (int32_t)((paula_clock << 8) / v->period / 100);
			if (v->remain <= 0) {
				free_voice(i);
				continue;
			}
		}
		if (v->env >= SILENT_CB << 4) {
			free_voice(i);
			continue;
		}
		vol = voice_volume(v);
		if (vol != v->volume) {
			v->volume = vol;
			paula_set(i, v->period, vol);
		}
	}
}
