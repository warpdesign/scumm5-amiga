/* scumm5-amiga - Paula emulation writing a WAV file, for the host
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Each voice plays its current block once, then the latched loop block
 * again and again, holding each 8-bit sample for its period like Paula.
 * Voices 0 and 3 go left, 1 and 2 right (and so on in fours for a host
 * build with more voices). No output filter.
 */
#include "platform.h"
#include "synth.h"
#include "paula_host.h"

#define RATE 44100

struct pvoice {
	uint8_t on, volume;
	uint16_t period;
	const int8_t *ptr, *loop;
	uint32_t left, loop_len;	/* bytes */
	uint32_t phase;			/* Paula clock ticks into the current sample, << 8 */
	int8_t sample;
};

static struct pvoice pv[SYNTH_VOICES];
static plat_file wav;
static uint32_t frames, clock_hz;

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void header(void)
{
	uint8_t h[45] = "RIFF....WAVEfmt \x10\0\0\0\x01\0\x02\0....\0\0\0\0\x04\0\x10\0data....";

	put32(h + 4, 36 + frames * 4);
	put32(h + 24, RATE);
	put32(h + 28, RATE * 4);
	put32(h + 40, frames * 4);
	plat_seek(wav, 0);
	plat_write(wav, h, 44);
}

int paula_host_open(const char *path, uint32_t clock)
{
	wav = plat_create(path);
	if (!wav)
		return -1;
	clock_hz = clock;
	frames = 0;
	header();
	return 0;
}

void paula_host_close(void)
{
	if (!wav)
		return;
	header();
	plat_close(wav);
	wav = 0;
}

void paula_play(int v, const int8_t *data, uint16_t words, const int8_t *loop, uint16_t loop_words,
		uint16_t period, uint8_t volume)
{
	struct pvoice *p = &pv[v];

	p->on = 1;
	p->ptr = data;
	p->left = (uint32_t)words * 2;
	p->loop = loop;
	p->loop_len = (uint32_t)loop_words * 2;
	p->period = period;
	p->volume = volume;
	p->phase = 0;
	p->sample = *p->ptr++;
	p->left--;
}

void paula_set(int v, uint16_t period, uint8_t volume)
{
	pv[v].period = period;
	pv[v].volume = volume;
}

void paula_stop(int v)
{
	pv[v].on = 0;
	pv[v].volume = 0;
}

/* Renders 10 ms of audio. */
void paula_host_tick(void)
{
	int16_t buf[RATE / 100 * 2];
	uint32_t step = (uint32_t)(((uint64_t)clock_hz << 8) / RATE);
	int i, v;

	if (!wav)
		return;
	for (i = 0; i < RATE / 100; i++) {
		int32_t mix[2] = { 0, 0 };
		for (v = 0; v < SYNTH_VOICES; v++) {
			struct pvoice *p = &pv[v];
			if (!p->on)
				continue;
			mix[(v & 3) == 1 || (v & 3) == 2] += p->sample * p->volume;
			p->phase += step;
			while (p->phase >= (uint32_t)p->period << 8) {
				p->phase -= (uint32_t)p->period << 8;
				if (!p->left) {
					p->ptr = p->loop;
					p->left = p->loop_len;
				}
				p->sample = *p->ptr++;
				p->left--;
			}
		}
		buf[i * 2] = (int16_t)(mix[0] * 2);
		buf[i * 2 + 1] = (int16_t)(mix[1] * 2);
	}
	plat_write(wav, buf, sizeof(buf));
	frames += RATE / 100;
}
