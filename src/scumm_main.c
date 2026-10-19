/* scumm5-amiga - the player: runs the game on the AGA display in real time
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: scumm5 PACK [-r DEMO.REC] [-a ARENA_KB] [-n FRAMES]
 * For now input comes only from a DEMO.REC playback; the left mouse button
 * quits.
 */
#include "amiga.h"
#include "scumm/scumm.h"
#include "scumm/gfx.h"
#include "util.h"

APTR GfxBase;

int display_open(void);
void display_close(void);
uint32_t display_jiffies(void);
void display_wait_vbl(void);
uint32_t display_lines(void);
extern void (*prof_hook)(int phase);

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

int app_main(int argc, char **argv)
{
	const char *p, *pack = 0, *rec = 0;
	uint32_t arena = 400, next, frames = 0, late = 0, max_frames = 0, busy = 0, t0, start = 0, ideal = 0, slow = 0, slow_j = 0, worst = 0, worst_frame = 0, d;
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
		} else if (!pack) {
			pack = argv[n];
		}
	}
	if (!pack) {
		report("usage: scumm5 PACK [-r DEMO.REC] [-a ARENA_KB]");
		return 20;
	}

	GfxBase = OpenLibrary("graphics.library", 39);
	if (!GfxBase) {
		report("scumm: graphics.library V39 (AGA) required");
		return 20;
	}
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

	prof_hook = prof;
	res_clock = display_lines;
	engine_boot();
	start = next = display_jiffies();
	while (!g.quit && (CIAA_PRA & 0x40) && (!max_frames || frames < max_frames)) {
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
		display_sync();
		memcpy(prof_before, prof_acc, sizeof(prof_acc));
		engine_frame();
		d = display_jiffies() - t0;
		busy += d;
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
	for (n = 0; n < NUM_PHASES; n++) {
		out_str("  ");
		out_str(prof_names[n]);
		out_str(": ");
		out_num(prof_acc[n] / 313);
		out_str(" frames (worst frame ");
		out_num(prof_worst[n] / 313);
		report(")");
	}
	res_close();
	CloseLibrary(GfxBase);
	return rc;
}
