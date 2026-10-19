/* scumm5-amiga - sound command queue (no playback yet)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/sound.cpp (startSound,
 * processSoundQueues, isSoundRunning, stopSound, soundKludge), ScummVM
 * commit 96b42d42bc8533953bdd701db059cc33c839a9ff. Until the iMUSE
 * sequencer exists, a sound counts as running only while it is queued.
 * ScummVM is the legal property of its developers, whose names are too
 * numerous to list here; see the COPYRIGHT file distributed with the
 * ScummVM source.
 */
#include "scumm.h"

#define MIDI_QUEUE_SIZE 0x100

static int16_t midi_queue[MIDI_QUEUE_SIZE];
static uint16_t midi_queue_pos;

void sound_start(int sound)
{
	VAR(VAR_LAST_SOUND) = sound;
	if (g.sound_queue_pos >= sizeof(g.sound_queue) / sizeof(g.sound_queue[0]))
		fatal("sound queue overflow", sound, 0);
	g.sound_queue[g.sound_queue_pos++] = (uint16_t)sound;
}

static void trigger_sound(int sound)
{
	res_get(RT_SOUND, (uint16_t)sound);
}

static int imuse_do_command(int num, const int32_t *data)
{
	(void)num;
	(void)data;
	return 0;
}

void sound_process(void)
{
	int i = 0, num, j;
	int32_t data[16];

	while (g.sound_queue_pos) {
		int snd = g.sound_queue[--g.sound_queue_pos];
		if (snd)
			trigger_sound(snd);
	}

	while (i < midi_queue_pos) {
		num = midi_queue[i++];
		if (i + num > midi_queue_pos)
			fatal("processSoundQueues: invalid num value", num, i);
		memset(data, 0, sizeof(data));
		if (num > 0) {
			for (j = 0; j < num; j++)
				data[j] = midi_queue[i + j];
			i += num;
			VAR(VAR_SOUNDRESULT) = (int16_t)imuse_do_command(num, data);
		}
	}
	midi_queue_pos = 0;
}

static int sound_in_queue(int sound)
{
	int i, num;

	for (i = g.sound_queue_pos; i--;)
		if (g.sound_queue[i] == sound)
			return 1;
	i = 0;
	while (i < midi_queue_pos) {
		num = midi_queue[i++];
		if (num > 0) {
			if (midi_queue[i] == 0x10F && midi_queue[i + 1] == 8 && midi_queue[i + 2] == sound)
				return 1;
			i += num;
		}
	}
	return 0;
}

int sound_is_running(int sound)
{
	if (sound_in_queue(sound))
		return 1;
	return 0;
}

void sound_stop(int sound)
{
	unsigned i;
	for (i = 0; i < sizeof(g.sound_queue) / sizeof(g.sound_queue[0]); i++)
		if (g.sound_queue[i] == sound)
			g.sound_queue[i] = 0;
}

void sound_stop_all(void)
{
	g.sound_queue_pos = 0;
	memset(g.sound_queue, 0, sizeof(g.sound_queue));
}

void sound_kludge(const int32_t *items, int num)
{
	int i;

	if (items[0] == -1) {
		sound_process();
		return;
	}
	if (midi_queue_pos + num + 1 > MIDI_QUEUE_SIZE)
		fatal("midi queue overflow", num, 0);
	midi_queue[midi_queue_pos++] = (int16_t)num;
	for (i = 0; i < num; i++)
		midi_queue[midi_queue_pos++] = (int16_t)items[i];
}

void sound_update_music_timer(void)
{
}
