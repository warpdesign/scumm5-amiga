/* scumm5-amiga - headless interpreter run, for tracing and testing
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: scummrun PACK [FRAMES] [-t TRACE] [-r DEMO.REC] [-a ARENA_KB]
 *                 [-d DIR -f FIRST -e EVERY]   (host: dump frames as PPM)
 *                 [-c EVERY]   (print a main screen checksum every EVERY frames)
 *                 [-m MIDI]    (log the MIDI messages iMUSE sends)
 *                 [-s]         (start DEMO.REC in room 4, as ScummVM does)
 *                 [-w WAV -b BANK]  (host: play the music on emulated Paula)
 *                 [-k FRAME:KEYS]  (type KEYS from FRAME, one every 10 frames;
 *                                  \r is Enter, ~ a pause, @X,Y; a left click)
 * Boots the game and runs FRAMES main-loop iterations (default 2000)
 * without display or sound, optionally writing a ScummVM-format trace.
 * iMUSE runs on the game clock: before each frame, one tick for every
 * 10 ms of game time since the previous frame, as ScummVM's MIDI timer
 * does while the main loop waits.
 */
#include "scumm/scumm.h"
#include "scumm/gfx.h"
#include "scumm/imuse.h"
#include "util.h"

#ifndef AMIGA
#include "scumm/synth.h"
#include "scumm/paula_host.h"
#define PAL_CLOCK 3546895
#define SYNTH_POOL (256 * 1024)
static int synth_on;
#endif

static plat_file midi_log;
static uint32_t midi_ms;

static void log_hex(char *d, unsigned v)
{
	d[0] = "0123456789abcdef"[v >> 4 & 15];
	d[1] = "0123456789abcdef"[v & 15];
}

static void log_midi(uint32_t msg)
{
	char line[32];
	int n = 0, k;
	uint32_t t = midi_ms, div = 1000000000;

	while (div > 1 && t / div == 0)
		div /= 10;
	for (; div; div /= 10)
		line[n++] = (char)('0' + t / div % 10);
	for (k = 0; k < 3; k++) {
		line[n++] = ' ';
		log_hex(line + n, msg >> (k * 8) & 0xFF);
		n += 2;
	}
	line[n++] = '\n';
	plat_write(midi_log, line, (uint32_t)n);
}

static void midi_out(uint32_t msg)
{
	if (midi_log)
		log_midi(msg);
#ifndef AMIGA
	if (synth_on)
		synth_midi(msg);
#endif
}

static void music_tick(void)
{
#ifndef AMIGA
	if (synth_on)
		synth_tick_start();
	imuse_timer();
	if (synth_on) {
		synth_tick_end();
		paula_host_tick();
	}
#else
	imuse_timer();
#endif
}

static void warn_imuse(const char *msg, int a, int b)
{
	out_str("imuse: ");
	out_str(msg);
	out_str(" ");
	out_num((uint32_t)a);
	out_str(" ");
	out_num((uint32_t)b);
	report("");
}

#ifndef AMIGA
void display_host_dump(const char *dir, uint32_t first, uint32_t every);
#endif

int app_main(int argc, char **argv)
{
	const char *p, *pack = 0, *trace = 0, *rec = 0, *dump = 0, *midi = 0, *keys = 0, *wav = 0, *bank = 0;
	uint32_t key_frame = 0;
	uint32_t dump_first = 1, dump_every = 1, sum_every = 0, sum = 0;
	uint32_t frames = 2000, arena = 400, i, t0, jiffies = 0, ticks = 0;
	int n;

	for (n = 1; n < argc; n++) {
		if (argv[n][0] == '-' && argv[n][1] == 's' && !argv[n][2]) {
			playback_like_scummvm = 1;
		} else if (argv[n][0] == '-' && argv[n][1] && !argv[n][2] && n + 1 < argc) {
			char opt = argv[n][1];
			p = argv[++n];
			if (opt == 't')
				trace = p;
			else if (opt == 'r')
				rec = p;
			else if (opt == 'a')
				arena = parse_uint(&p);
			else if (opt == 'd')
				dump = p;
			else if (opt == 'f')
				dump_first = parse_uint(&p);
			else if (opt == 'e')
				dump_every = parse_uint(&p);
			else if (opt == 'c')
				sum_every = parse_uint(&p);
			else if (opt == 'm')
				midi = p;
			else if (opt == 'w')
				wav = p;
			else if (opt == 'b')
				bank = p;
			else if (opt == 'k') {
				key_frame = parse_uint(&p);
				keys = *p == ':' ? p + 1 : p;
			}
		} else if (!pack) {
			pack = argv[n];
		} else {
			p = argv[n];
			frames = parse_uint(&p);
		}
	}
	if (!pack) {
		report("usage: scummrun PACK [FRAMES] [-t TRACE] [-r DEMO.REC] [-a ARENA_KB]");
		return 20;
	}

	if (trace)
		trace_open(trace);
	imuse_warn = warn_imuse;
	if (midi)
		midi_log = plat_create(midi);
#ifndef AMIGA
	if (wav && bank) {
		if (synth_open(bank, PAL_CLOCK, SYNTH_POOL) || paula_host_open(wav, PAL_CLOCK))
			report("scummrun: cannot open the bank or the WAV file");
		else {
			synth_on = 1;
			synth_sound_active = sound_in_use;
			sound_prepare = synth_prepare;
		}
	}
#else
	(void)wav;
	(void)bank;
#endif
	imuse_midi_send = midi_out;
#ifndef AMIGA
	if (dump)
		display_host_dump(dump, dump_first, dump_every);
#else
	(void)dump;
	(void)dump_first;
	(void)dump_every;
#endif
	if (engine_init(pack, arena * 1024)) {
		report("scummrun: cannot open pack");
		return 20;
	}
	if (rec && playback_open(rec))
		report("scummrun: no playback stream, running without it");

	t0 = plat_ticks();
	engine_boot();
	for (i = 0; i < frames && !g.quit; i++) {
		jiffies += VAR(VAR_TIMER_NEXT) < 1 ? 1 : VAR(VAR_TIMER_NEXT);
		for (; ticks < jiffies * 5 / 3; ticks++) {
			midi_ms = ticks * 10;
			music_tick();
		}
		midi_ms = ticks * 10;
		if (keys && *keys && i >= key_frame && (i - key_frame) % 10 == 0) {
			if (keys[0] == '\\' && keys[1] == 'r') {
				input_key(13);
				keys += 2;
			} else if (*keys == '~') {
				keys++;
			} else if (*keys == '@') {
				uint32_t x, y;
				keys++;
				x = parse_uint(&keys);
				keys += *keys == ',';
				y = parse_uint(&keys);
				keys += *keys == ';';
				input_click((int)x, (int)y);
			} else {
				input_key(*keys++);
			}
		}
		engine_frame();
		if (sum_every) {
			const uint8_t *px = vs_pixels(&vscr[0], 0, 0);
			uint32_t k, len = (uint32_t)vscr[0].h * VS_PITCH;
			for (k = 0; k < len; k++)
				sum = (sum << 5 | sum >> 27) ^ px[k];
			if ((i + 1) % sum_every == 0) {
				out_str("sum ");
				out_num(i + 1);
				out_str(" ");
				out_num(sum);
				report("");
			}
		}
	}
	t0 = plat_ticks() - t0;

	trace_close();
	if (midi_log)
		plat_close(midi_log);
#ifndef AMIGA
	if (synth_on) {
		out_str("scummrun: instrument pool peak ");
		out_num(synth_pool_peak / 1024);
		out_str(" KB, notes without instrument ");
		out_num(synth_missing);
		report("");
		synth_close();
		paula_host_close();
	}
#endif
	out_str("scummrun: ");
	out_num(i);
	out_str(" frames, room ");
	out_num(g.current_room);
	out_str(", cache peak ");
	out_num(res_get_stats()->peak);
	out_str(" loads ");
	out_num(res_get_stats()->loads);
	report("");
	out_str("scummrun: game time ");
	out_num(jiffies / 60);
	out_str(" s, run time ");
	out_num(t0 / 50);
	out_str(".");
	out_num(t0 % 50 * 2);
	report(" s");
	res_close();
	return 0;
}
