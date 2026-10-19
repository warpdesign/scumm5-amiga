/* scumm5-amiga - iMUSE sequencer driving a General MIDI device
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef IMUSE_H
#define IMUSE_H

#include <stdint.h>

/* imuse_timer() advances the sequencer by this many microseconds: the
 * base tempo of ScummVM's MPU-401 style MIDI drivers (100 Hz). */
#define IMUSE_TIMER_US 10000

/* Sends the General MIDI initialisation; monkey2 selects MI2's quirks. */
void imuse_init(int monkey2);
int imuse_start_sound(int sound);
void imuse_stop_sound(int sound);
void imuse_stop_all(void);
/* 1 if playing, 2 if queued to start at a marker, 0 otherwise. */
int imuse_sound_status(int sound, int ignore_fadeouts);
int32_t imuse_do_command(int num, const int32_t *args);
int imuse_music_timer(void);
void imuse_timer(void);

/* Provided by the audio driver: while locked, the music interrupt does not
 * run the sequencer (it catches up on unlock). */
void audio_lock(void);
void audio_unlock(void);

/* The MIDI device: status | param1 << 8 | param2 << 16. */
extern void (*imuse_midi_send)(uint32_t msg);
/* Conditions ScummVM treats as errors; the sequencer may be running in an
 * interrupt, so this must not end the program there. */
extern void (*imuse_warn)(const char *msg, int a, int b);

#endif
