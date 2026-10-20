/* scumm5-amiga - the player: runs the game on the AGA display in real time
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: scumm5 [PACK [-r DEMO.REC] [-a ARENA_KB] [-n FRAMES] [-p BOOT_PARAM] [-l SLOT]]
 * Without a pack, the launcher lists the installed games and installs more.
 * Mouse and keyboard play the game, or DEMO.REC drives it with -r (then the
 * left mouse button quits). F5 opens the game's menu (save, load, quit);
 * games are saved next to the pack, as monkey2.s01 to .s99 for monkey2.s5p.
 * Ctrl-C or Alt-X quits. Music plays if the instrument bank (PACK with .s5b
 * instead of .s5p) is next to the pack.
 */
#include "amiga.h"
#include "scumm/scumm.h"
#include "scumm/gfx.h"
#include "scumm/imuse.h"
#include "scumm/synth.h"
#include "util.h"
#include "launcher.h"

APTR GfxBase;

int display_open(void);
void display_close(void);
uint32_t display_jiffies(void);
void display_wait_vbl(void);
uint32_t display_lines(void);
extern void (*prof_hook)(int phase);
int audio_open(void);
void audio_close(void);
uint32_t audio_paula_clock(void);
extern uint32_t audio_ticks, audio_lines_imuse, audio_lines_synth;
int input_open(int pointer);
void input_close(void);
int input_poll(void);

#define NUM_PHASES 14
static uint32_t prof_acc[NUM_PHASES], prof_last;
static int prof_phase = -1;
static uint32_t prof_before[NUM_PHASES], prof_worst[NUM_PHASES];
static const char *const prof_names[NUM_PHASES] = {
	"input/vars", "scripts", "walk/camera/text", "bg+objects", "actors(flags)", "mouseover/effects", "palette",
	"display copy", "wait+sound", "reset_actor_bgs", "actor misc", "costume decode", "costume flush", "animate"
};

static void prof(int phase)
{
	uint32_t now = display_lines();
	if (prof_phase >= 0)
		prof_acc[prof_phase] += now - prof_last;
	prof_phase = phase;
	prof_last = now;
}

#define SYNTH_POOL_KB 256
#define ARENA_RESERVE_KB (600 + SYNTH_POOL_KB)

int app_main(int argc, char **argv)
{
	const char *p, *pack = 0, *rec = 0;
	char bank[128];
	int music = 0;
	uint32_t arena = 0, next, frames = 0, late = 0, max_frames = 0, busy = 0, t0, start = 0, ideal = 0, slow = 0, slow_j = 0, over = 0, chip_free = 0, worst = 0, worst_frame = 0, d;
	int32_t boot_param = 0;
	uint32_t load_slot = 0;
	int n, rc = 20;

	for (n = 1; n < argc; n++) {
		if (argv[n][0] == '-' && argv[n][1] && !argv[n][2] && n + 1 < argc) {
			char opt = argv[n][1];
			p = argv[++n];
			if (opt == 'r')
				rec = p;
			else if (opt == 'a')
				arena = parse_uint(&p);
			else if (opt == 'n')
				max_frames = parse_uint(&p);
			else if (opt == 'l')
				load_slot = parse_uint(&p);
			else if (opt == 'p')
				boot_param = *p == '-' ? (p++, -(int32_t)parse_uint(&p)) : (int32_t)parse_uint(&p);
		} else if (!pack) {
			pack = argv[n];
		}
	}
	if (!pack && argc > 1) {
		report("usage: scumm5 [PACK [-r DEMO.REC] [-a ARENA_KB]]");
		return 20;
	}
	if (!pack) {
		static char lpack[256], lrec[256];
		if (!launcher_run(lpack, sizeof(lpack), lrec, sizeof(lrec)))
			return 0;
		pack = lpack;
		if (lrec[0])
			rec = lrec;
	}

	if (!arena) {
		/* leave room for the display, actor scratch and sound samples */
		uint32_t largest = AvailMem(MEMF_CHIP | MEMF_LARGEST) / 1024;
		arena = largest > ARENA_RESERVE_KB + 400 ? largest - ARENA_RESERVE_KB : 400;
	}
	out_str("scumm: cache ");
	out_num(arena);
	report(" KB");
	GfxBase = OpenLibrary("graphics.library", 39);
	if (!GfxBase) {
		report("scumm: graphics.library V39 (AGA) required");
		return 20;
	}
	n = (int)strlen(pack);
	if (n > 4 && n < (int)sizeof(bank)) {
		memcpy(bank, pack, (size_t)n + 1);
		bank[n - 1] = 'b';
		if (!synth_open(bank, audio_paula_clock(), SYNTH_POOL_KB * 1024)) {
			music = 1;
			imuse_midi_send = synth_midi;
			synth_sound_active = sound_in_use;
			sound_prepare = synth_prepare;
		}
	}
	if (!music)
		report("scumm: no instrument bank, no music");
	if (engine_init(pack, arena * 1024)) {
		report("scumm: cannot open pack");
		goto out;
	}
	if (rec && playback_open(rec))
		report("scumm: no playback stream");
	if (display_open()) {
		report("scumm: AGA display not available");
		goto out;
	}

	if (input_open(!rec))
		report("scumm: no input.device handler");
	if (music && audio_open())
		report("scumm: no CIA-B timer for the music");
	chip_free = AvailMem(MEMF_CHIP) / 1024;
	prof_hook = prof;
	res_clock = display_lines;
	engine_boot(boot_param);
	if (load_slot && !engine_load((int)load_slot))
		report("scumm: no saved game in that slot");
	start = next = display_jiffies();
	while (!g.quit && (!rec || (CIAA_PRA & 0x40)) && (!max_frames || frames < max_frames)) {
		int delta = VAR(VAR_TIMER_NEXT) < 1 ? 1 : VAR(VAR_TIMER_NEXT);
		next += (uint32_t)delta;
		ideal += (uint32_t)delta;
		while ((int32_t)(display_jiffies() - next) < 0)
			display_wait_vbl();
		if ((int32_t)(display_jiffies() - next) > delta) {
			late++;
			next = display_jiffies();
		}
		t0 = display_jiffies();
		if (input_poll())
			break;
		display_sync();
		memcpy(prof_before, prof_acc, sizeof(prof_acc));
		engine_frame();
		d = display_jiffies() - t0;
		busy += d;
		if (d > (uint32_t)delta)
			over++;
		if (d > 2 * (uint32_t)delta) {
			slow++;
			slow_j += d;
		}
		if (d > worst) {
			worst = d;
			worst_frame = frames;
			for (n = 0; n < NUM_PHASES; n++)
				prof_worst[n] = prof_acc[n] - prof_before[n];
		}
		frames++;
	}
	rc = 0;

out:
	audio_close();
	input_close();
	display_close();
	out_str("scumm: ");
	out_num(frames);
	out_str(" frames, ");
	out_num(late);
	out_str(" late, busy ");
	out_num(busy);
	out_str(" of ");
	out_num(display_jiffies() - start);
	out_str(" jiffies, ideal ");
	out_num(ideal);
	report("");
	out_str("  frames over budget: ");
	out_num(over);
	out_str(", chip free after setup ");
	out_num(chip_free);
	out_str(" KB, cache peak ");
	out_num(res_get_stats()->peak / 1024);
	report(" KB");
	out_str("  frames over twice their budget: ");
	out_num(slow);
	out_str(", taking ");
	out_num(slow_j);
	out_str(" jiffies; worst ");
	out_num(worst);
	out_str(" at frame ");
	out_num(worst_frame);
	report("");
	out_str("  pack reads: ");
	out_num(res_get_stats()->loads);
	out_str(" loads, ");
	out_num(res_get_stats()->read_bytes);
	out_str(" bytes in ");
	out_num(res_get_stats()->read_time / 313);
	report(" frames");
	out_str("  music: ");
	out_num(audio_ticks);
	out_str(" ticks, iMUSE ");
	out_num(audio_lines_imuse / 313);
	out_str(" frames, synth ");
	out_num(audio_lines_synth / 313);
	report(" frames");
	for (n = 0; n < NUM_PHASES; n++) {
		out_str("  ");
		out_str(prof_names[n]);
		out_str(": ");
		out_num(prof_acc[n] / 313);
		out_str(" frames (worst frame ");
		out_num(prof_worst[n] / 313);
		report(")");
	}
	synth_close();
	res_close();
	CloseLibrary(GfxBase);
	return rc;
}
