/* scumm5-amiga - sound command queue and iMUSE glue
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/sound.cpp (startSound,
 * processSoundQueues, triggerSound, isSoundRunning, isSoundInUse,
 * stopSound, stopAllSounds, soundKludge, updateMusicTimer), ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff. Music goes to iMUSE; there is
 * no sampled sound effect playback yet. ScummVM is the legal property of
 * its developers, whose names are too numerous to list here; see the
 * COPYRIGHT file distributed with the ScummVM source.
 */
#include "scumm.h"
#include "imuse.h"

#define MIDI_QUEUE_SIZE 0x100

static int16_t midi_queue[MIDI_QUEUE_SIZE];
static uint16_t midi_queue_pos;

void (*sound_prepare)(int sound);

void sound_init(void)
{
	audio_lock();
	imuse_init(1);
	audio_unlock();
}

void sound_start(int sound)
{
	VAR(VAR_LAST_SOUND) = sound;
	if (g.sound_queue_pos >= sizeof(g.sound_queue) / sizeof(g.sound_queue[0]))
		fatal("sound queue overflow", sound, 0);
	g.sound_queue[g.sound_queue_pos++] = (uint16_t)sound;
}

static void trigger_sound(int sound)
{
	if (!res_get(RT_SOUND, (uint16_t)sound))
		return;
	if (sound_prepare)
		sound_prepare(sound);
	audio_lock();
	imuse_start_sound(sound);
	audio_unlock();
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
			/* sounds iMUSE will start by itself, at a marker or after a delay */
			if (sound_prepare && data[0] == 0x10F && data[1] == 8)
				sound_prepare(data[2]);
			else if (sound_prepare && data[0] == 20 && data[2] == 8)
				sound_prepare(data[3]);
			audio_lock();
			VAR(VAR_SOUNDRESULT) = (int16_t)imuse_do_command(num, data);
			audio_unlock();
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

static int music_status(int sound, int ignore_fadeouts)
{
	int r;

	audio_lock();
	r = imuse_sound_status(sound, ignore_fadeouts);
	audio_unlock();
	return r;
}

int sound_is_running(int sound)
{
	if (sound_in_queue(sound))
		return 1;
	if (sound <= 0 || sound >= res_count(RT_SOUND) || !res_loaded(RT_SOUND, (uint16_t)sound))
		return 0;
	return music_status(sound, 1);
}

/* Keeps a sound's resource from expiring; unlike isSoundRunning, a sound
 * that is fading out still counts. */
int sound_in_use(int sound)
{
	if (sound_in_queue(sound))
		return 1;
	if (!res_loaded(RT_SOUND, (uint16_t)sound))
		return 0;
	return music_status(sound, 0) != 0;
}

void sound_stop(int sound)
{
	unsigned i;

	audio_lock();
	imuse_stop_sound(sound);
	audio_unlock();
	for (i = 0; i < sizeof(g.sound_queue) / sizeof(g.sound_queue[0]); i++)
		if (g.sound_queue[i] == sound)
			g.sound_queue[i] = 0;
}

void sound_stop_all(void)
{
	g.sound_queue_pos = 0;
	memset(g.sound_queue, 0, sizeof(g.sound_queue));
	audio_lock();
	imuse_stop_all();
	audio_unlock();
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
	audio_lock();
	VAR(VAR_MUSIC_TIMER) = (int16_t)imuse_music_timer();
	audio_unlock();
}
