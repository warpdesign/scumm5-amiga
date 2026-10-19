/* scumm5-amiga - General MIDI synthesizer on Paula voices
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#ifndef SYNTH_VOICES
#define SYNTH_VOICES 4
#endif

/* Opens a bank made by tools/mkbank.py; clock is Paula's (3546895 Hz PAL,
 * 3579545 NTSC). Instrument samples are loaded per song into at most pool
 * bytes of chip RAM. 0 on success. */
int synth_open(const char *bank, uint32_t clock, uint32_t pool);
void synth_close(void);
/* Loads the instruments a sound uses, expiring those of sounds no longer
 * active; from the main program, before the sound can start. */
void synth_prepare(int sound);
/* Asked by synth_prepare() which sounds still need their instruments. */
extern int (*synth_sound_active)(int sound);
/* Notes dropped because their instrument was not loaded; most pool bytes used. */
extern uint32_t synth_missing, synth_pool_peak;
void synth_midi(uint32_t msg);
/* Every 10 ms: synth_tick_start() before the sequencer runs (starts the
 * notes of the previous tick), synth_tick_end() after it (envelopes). */
void synth_tick_start(void);
void synth_tick_end(void);

/* Paula, per voice (0 and 3 left, 1 and 2 right). paula_play() starts the
 * voice on data[0..words) and then repeats loop[0..loop_words); it is only
 * called at least one tick after paula_stop() on the same voice. */
void paula_play(int v, const int8_t *data, uint16_t words, const int8_t *loop, uint16_t loop_words,
		uint16_t period, uint8_t volume);
void paula_set(int v, uint16_t period, uint8_t volume);
void paula_stop(int v);

#endif
