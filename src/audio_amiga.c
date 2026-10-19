/* scumm5-amiga - music on Paula: a 100 Hz CIA-B timer runs iMUSE and the
 * synthesizer, which drives the four audio channels directly
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "amiga.h"
#include "scumm/imuse.h"
#include "scumm/synth.h"

#define CIAB_TALO (*(volatile UBYTE *)0xbfd400)
#define CIAB_TAHI (*(volatile UBYTE *)0xbfd500)
#define CIAB_TBLO (*(volatile UBYTE *)0xbfd600)
#define CIAB_TBHI (*(volatile UBYTE *)0xbfd700)
#define CIAB_CRA (*(volatile UBYTE *)0xbfde00)
#define CIAB_CRB (*(volatile UBYTE *)0xbfdf00)
#define AUD(v, off) REG(0x0a0 + (v) * 0x10 + (off))

extern APTR GfxBase;

static APTR ciab;
static struct Interrupt timer_int;
static int timer_bit = -1;
static UWORD old_intena;
static volatile UBYTE busy, depth;
static volatile UWORD missed;

uint32_t display_lines(void);
uint32_t audio_ticks, audio_lines_imuse, audio_lines_synth;

static void tick(void)
{
	uint32_t t0 = display_lines(), t1, t2;

	synth_tick_start();
	t1 = display_lines();
	imuse_timer();
	t2 = display_lines();
	synth_tick_end();
	audio_lines_imuse += t2 - t1;
	audio_lines_synth += display_lines() - t2 + t1 - t0;
	audio_ticks++;
}

static void timer_server(void)
{
	if (busy) {
		missed++;
		return;
	}
	tick();
}

void audio_lock(void)
{
	busy = 1;
	depth++;
}

/* Runs the ticks the interrupt skipped while the program was inside. */
void audio_unlock(void)
{
	UWORD m;

	if (--depth)
		return;
	for (;;) {
		Disable();
		m = missed;
		missed = 0;
		if (!m)
			busy = 0;
		Enable();
		if (!m)
			break;
		while (m--)
			tick();
	}
}

void paula_stop(int v)
{
	REG(0x096) = (UWORD)(1 << v);
	AUD(v, 8) = 0;
}

void paula_set(int v, uint16_t period, uint8_t volume)
{
	AUD(v, 6) = period;
	AUD(v, 8) = volume;
}

/* Paula latches the location and length when the channel's DMA starts and
 * raises its interrupt request then; after that, the loop goes into the
 * registers and takes over once the first block has played. */
void paula_play(int v, const int8_t *data, uint16_t words, const int8_t *loop, uint16_t loop_words,
		uint16_t period, uint8_t volume)
{
	UWORD bit = (UWORD)(0x80 << v), line, lines = 0;

	*(volatile ULONG *)&AUD(v, 0) = (ULONG)data;
	AUD(v, 4) = words;
	AUD(v, 6) = period;
	AUD(v, 8) = volume;
	REG(0x09c) = bit;
	REG(0x096) = (UWORD)(0x8000 | 1 << v);
	line = REG(0x006) >> 8;
	while (!(REG(0x01e) & bit) && lines < 4) {
		UWORD now = REG(0x006) >> 8;
		if (now != line) {
			line = now;
			lines++;
		}
	}
	*(volatile ULONG *)&AUD(v, 0) = (ULONG)loop;
	AUD(v, 4) = loop_words;
	REG(0x09c) = bit;
}

uint32_t audio_paula_clock(void)
{
	return *((UBYTE *)GfxBase + 160) == 60 ? 3579545 : 3546895;
}

int audio_open(void)
{
	UWORD count = (UWORD)(audio_paula_clock() == 3579545 ? 715909 / 100 : 709379 / 100);
	int v;

	/* the program owns the audio channels; keep the OS audio interrupts out */
	old_intena = REG(0x01c) & 0x0780;
	REG(0x09a) = 0x0780;
	REG(0x09c) = 0x0780;
	for (v = 0; v < 4; v++)
		paula_stop(v);

	ciab = OpenResource("ciab.resource");
	if (!ciab)
		return -1;
	timer_int.is_Node.ln_Type = NT_INTERRUPT;
	timer_int.is_Node.ln_Name = "scumm5 music";
	timer_int.is_Code = timer_server;
	if (!AddICRVector(ciab, 0, &timer_int)) {
		timer_bit = 0;
		CIAB_CRA = (UBYTE)(CIAB_CRA & 0xc0);
		CIAB_TALO = (UBYTE)count;
		CIAB_TAHI = (UBYTE)(count >> 8);
		CIAB_CRA = (UBYTE)((CIAB_CRA & 0xc0) | 0x11);
	} else if (!AddICRVector(ciab, 1, &timer_int)) {
		timer_bit = 1;
		CIAB_CRB = (UBYTE)(CIAB_CRB & 0x80);
		CIAB_TBLO = (UBYTE)count;
		CIAB_TBHI = (UBYTE)(count >> 8);
		CIAB_CRB = (UBYTE)((CIAB_CRB & 0x80) | 0x11);
	} else {
		return -1;
	}
	return 0;
}

void audio_close(void)
{
	int v;

	if (timer_bit == 0)
		CIAB_CRA = (UBYTE)(CIAB_CRA & 0xc0);
	else if (timer_bit == 1)
		CIAB_CRB = (UBYTE)(CIAB_CRB & 0x80);
	if (timer_bit >= 0)
		RemICRVector(ciab, timer_bit, &timer_int);
	timer_bit = -1;
	for (v = 0; v < 4; v++)
		paula_stop(v);
	REG(0x09c) = 0x0780;
	REG(0x09a) = (UWORD)(0x8000 | old_intena);
}
