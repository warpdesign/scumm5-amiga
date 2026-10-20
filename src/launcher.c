/* scumm5-amiga - the game launcher (Workbench window)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lists the games installed in PROGDIR:Games. "Add game" asks for a
 * folder of DOS game files, recognises the game and converts it into
 * PROGDIR:Games/<id>/ with a progress window that can stop it; the game is
 * listed only once its pack is complete and checked. The instrument bank
 * shipped in PROGDIR:Banks is copied next to the pack.
 *
 * Uses GadTools (V39) and asl.library (V38), so Kickstart 3.0 or later.
 */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/asl.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include "platform.h"
#include "convert.h"
#include "detect.h"
#include "launcher.h"
#include "version.h"

struct IntuitionBase *IntuitionBase;
struct Library *GadToolsBase, *AslBase;

#define GAMES_DIR "PROGDIR:Games"
#define BANKS_DIR "PROGDIR:Banks"
#define PATH_MAX_LEN 256
#define MAX_LISTED 8
#define TITLE "SCUMM5"

enum { G_LIST = 1, G_START, G_ADD, G_REMOVE, G_QUIT, G_STATUS, G_STOP, G_WHAT, G_TIME };

static struct Screen *scr;
static APTR vi;
static struct DrawInfo *dri;
static struct Window *win;
static struct Menu *menus;
static struct Gadget *glist, *g_list, *g_start, *g_remove, *g_status;
static struct List games;
static struct Node nodes[MAX_LISTED];
static const struct game_def *listed[MAX_LISTED];
static int nlisted, sel = -1;
static char status[96];
static char last_dir[PATH_MAX_LEN];

/* ---- small strings ---- */

static void str_cat(char *d, int size, const char *s)
{
	int n = (int)strlen(d);

	while (*s && n < size - 1)
		d[n++] = *s++;
	d[n] = 0;
}

static void num_cat(char *d, int size, ULONG v, int width)
{
	char b[12];
	int i = sizeof(b) - 1;

	b[i] = 0;
	do {
		b[--i] = (char)('0' + v % 10);
		v /= 10;
		width--;
	} while (v || width > 0);
	str_cat(d, size, b + i);
}

static void game_path(char *out, const struct game_def *gd, const char *ext)
{
	out[0] = 0;
	str_cat(out, PATH_MAX_LEN, GAMES_DIR "/");
	str_cat(out, PATH_MAX_LEN, gd->id);
	if (ext) {
		str_cat(out, PATH_MAX_LEN, "/");
		str_cat(out, PATH_MAX_LEN, ext[0] == '.' ? gd->id : "");
		str_cat(out, PATH_MAX_LEN, ext);
	}
}

static int exists(const char *path)
{
	BPTR l = Lock((CONST_STRPTR)path, ACCESS_READ);

	if (!l)
		return 0;
	UnLock(l);
	return 1;
}

/* ---- requesters ---- */

static LONG ask(const char *body, const char *buttons, APTR args)
{
	struct EasyStruct es;

	es.es_StructSize = sizeof(es);
	es.es_Flags = 0;
	es.es_Title = (UBYTE *)TITLE;
	es.es_TextFormat = (UBYTE *)body;
	es.es_GadgetFormat = (UBYTE *)buttons;
	return EasyRequestArgs(win, &es, NULL, (RAWARG)args);
}

static void tell(const char *body, APTR args)
{
	ask(body, "OK", args);
}

enum { M_ABOUT = 1, M_QUIT };

static struct NewMenu menu_def[] = {
	{ NM_TITLE, (CONST_STRPTR)"Project", 0, 0, 0, 0 },
	{ NM_ITEM, (CONST_STRPTR)"About...", (CONST_STRPTR)"?", 0, 0, (APTR)M_ABOUT },
	{ NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
	{ NM_ITEM, (CONST_STRPTR)"Quit", (CONST_STRPTR)"Q", 0, 0, (APTR)M_QUIT },
	{ NM_END, 0, 0, 0, 0, 0 },
};

static void about(void)
{
	ULONG args[2];

	args[0] = (ULONG)SCUMM5_VERSION;
	args[1] = (ULONG)SCUMM5_REPO;
	tell(SCUMM5_REPO[0]
	     ? TITLE " for AGA Amigas\nVersion %s\n\n"
	       "A SCUMM v5 interpreter adapted from ScummVM.\n"
	       "Free software under the GNU GPL, version 3 or later.\n\n%s"
	     : TITLE " for AGA Amigas\nVersion %s\n\n"
	       "A SCUMM v5 interpreter adapted from ScummVM.\n"
	       "Free software under the GNU GPL, version 3 or later.", args);
}

/* ---- the game list ---- */

static void set_status(void)
{
	status[0] = 0;
	if (!nlisted) {
		str_cat(status, sizeof(status), "No game installed yet: choose Add game.");
	} else {
		num_cat(status, sizeof(status), (ULONG)nlisted, 0);
		str_cat(status, sizeof(status), nlisted == 1 ? " game installed." : " games installed.");
	}
	if (g_status)
		GT_SetGadgetAttrs(g_status, win, NULL, GTTX_Text, (IPTR)status, TAG_DONE);
}

static void update_buttons(void)
{
	GT_SetGadgetAttrs(g_start, win, NULL, GA_Disabled, sel < 0, TAG_DONE);
	GT_SetGadgetAttrs(g_remove, win, NULL, GA_Disabled, sel < 0, TAG_DONE);
}

static void fill_list(const struct game_def *select)
{
	char path[PATH_MAX_LEN];
	int i;

	if (win)
		GT_SetGadgetAttrs(g_list, win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
	games.lh_Head = (struct Node *)&games.lh_Tail;
	games.lh_Tail = NULL;
	games.lh_TailPred = (struct Node *)&games.lh_Head;
	nlisted = 0;
	sel = -1;
	for (i = 0; i < game_count() && nlisted < MAX_LISTED; i++) {
		const struct game_def *gd = game_at(i);
		game_path(path, gd, ".s5p");
		if (!gd->supported || !exists(path))
			continue;
		if (gd == select || (!select && nlisted == 0))
			sel = nlisted;
		listed[nlisted] = gd;
		nodes[nlisted].ln_Name = (char *)gd->name;
		AddTail(&games, &nodes[nlisted]);
		nlisted++;
	}
	if (win) {
		GT_SetGadgetAttrs(g_list, win, NULL, GTLV_Labels, (IPTR)&games, GTLV_Selected, sel < 0 ? ~0UL : (ULONG)sel,
				  TAG_DONE);
		update_buttons();
	}
	set_status();
}

/* What a stopped or interrupted install left behind. */
static void clean_leftovers(void)
{
	char path[PATH_MAX_LEN];
	int i;

	for (i = 0; i < game_count(); i++) {
		game_path(path, game_at(i), ".tmp");
		DeleteFile((CONST_STRPTR)path);
		game_path(path, game_at(i), ".s5p");
		if (exists(path))
			continue;
		game_path(path, game_at(i), 0);
		DeleteFile((CONST_STRPTR)path);	/* only goes if empty */
	}
}

/* ---- the progress window ---- */

static struct {
	struct Window *w;
	struct Gadget *glist, *what, *time;
	WORD bx, by, bw, bh;
	ULONG t0, last;
	ULONG fill;
	UWORD phase, item;
	char what_s[64], time_s[64];
	int stop;
} pw;

static const char *const phase_names[] = { "Rooms", "Scripts", "Costumes", "Sounds", "Charsets", "Checking" };

static int open_progress(const char *title)
{
	struct NewGadget ng;
	struct Gadget *g;
	WORD fh = scr->Font->ta_YSize, left = scr->WBorLeft + 10, top = scr->WBorTop + fh + 1 + 8, w = 380;

	memset(&pw, 0, sizeof(pw));
	memset(&ng, 0, sizeof(ng));
	ng.ng_TextAttr = scr->Font;
	ng.ng_VisualInfo = vi;
	g = CreateContext(&pw.glist);
	ng.ng_LeftEdge = left;
	ng.ng_TopEdge = top;
	ng.ng_Width = w;
	ng.ng_Height = fh + 4;
	ng.ng_GadgetID = G_WHAT;
	pw.what = g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (IPTR)"Starting", TAG_DONE);
	pw.bx = left;
	pw.by = top + fh + 8;
	pw.bw = w;
	pw.bh = fh + 4;
	ng.ng_TopEdge = pw.by + pw.bh + 6;
	ng.ng_GadgetID = G_TIME;
	pw.time = g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (IPTR)"", TAG_DONE);
	ng.ng_LeftEdge = left + (w - 100) / 2;
	ng.ng_TopEdge += fh + 10;
	ng.ng_Width = 100;
	ng.ng_Height = fh + 6;
	ng.ng_GadgetText = (UBYTE *)"_Stop";
	ng.ng_GadgetID = G_STOP;
	g = CreateGadget(BUTTON_KIND, g, &ng, GT_Underscore, '_', TAG_DONE);
	if (!g)
		goto fail;
	pw.w = OpenWindowTags(NULL, WA_PubScreen, (IPTR)scr, WA_Title, (IPTR)title,
			      WA_Left, win->LeftEdge + 20, WA_Top, win->TopEdge + 20,
			      WA_InnerWidth, w + 20, WA_InnerHeight, ng.ng_TopEdge + ng.ng_Height + 8 - (scr->WBorTop + fh + 1),
			      WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
			      WA_AutoAdjust, TRUE, WA_Gadgets, (IPTR)pw.glist,
			      WA_IDCMP, BUTTONIDCMP | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY, TAG_DONE);
	if (!pw.w)
		goto fail;
	GT_RefreshWindow(pw.w, NULL);
	DrawBevelBox(pw.w->RPort, pw.bx, pw.by, pw.bw, pw.bh, GT_VisualInfo, (IPTR)vi, GTBB_Recessed, TRUE, TAG_DONE);
	pw.t0 = plat_ticks();
	return 1;
fail:
	FreeGadgets(pw.glist);
	pw.glist = 0;
	return 0;
}

static void close_progress(void)
{
	if (pw.w)
		CloseWindow(pw.w);
	FreeGadgets(pw.glist);
	pw.w = 0;
	pw.glist = 0;
}

static void time_cat(char *d, int size, ULONG secs)
{
	num_cat(d, size, secs / 60, 1);
	str_cat(d, size, ":");
	num_cat(d, size, secs % 60, 2);
}

static void draw_progress(const struct conv_status *st)
{
	struct RastPort *rp = pw.w->RPort;
	ULONG secs = (plat_ticks() - pw.t0) / 50, fill;

	if (st->phase != pw.phase || st->item != pw.item || !pw.what_s[0]) {
		pw.phase = st->phase;
		pw.item = st->item;
		pw.what_s[0] = 0;
		str_cat(pw.what_s, sizeof(pw.what_s), phase_names[st->phase < 6 ? st->phase : 5]);
		if (st->phase != CONV_PHASE_VERIFY && st->items) {
			str_cat(pw.what_s, sizeof(pw.what_s), ": ");
			num_cat(pw.what_s, sizeof(pw.what_s), st->item, 0);
			str_cat(pw.what_s, sizeof(pw.what_s), " of ");
			num_cat(pw.what_s, sizeof(pw.what_s), st->items, 0);
		} else if (st->phase == CONV_PHASE_VERIFY) {
			str_cat(pw.what_s, sizeof(pw.what_s), " the converted data");
		}
		GT_SetGadgetAttrs(pw.what, pw.w, NULL, GTTX_Text, (IPTR)pw.what_s, TAG_DONE);
	}
	pw.time_s[0] = 0;
	str_cat(pw.time_s, sizeof(pw.time_s), "Elapsed ");
	time_cat(pw.time_s, sizeof(pw.time_s), secs);
	if (secs >= 20 && st->done > st->total / 50) {
		ULONG left = secs * ((st->total - st->done) / 64) / (st->done / 64 + 1);
		str_cat(pw.time_s, sizeof(pw.time_s), ", about ");
		time_cat(pw.time_s, sizeof(pw.time_s), left + 1);
		str_cat(pw.time_s, sizeof(pw.time_s), " left");
	}
	GT_SetGadgetAttrs(pw.time, pw.w, NULL, GTTX_Text, (IPTR)pw.time_s, TAG_DONE);
	fill = st->total ? (ULONG)(pw.bw - 4) * (st->done / 256) / (st->total / 256 + 1) : 0;
	if (fill > (ULONG)(pw.bw - 4))
		fill = (ULONG)(pw.bw - 4);
	if (fill != pw.fill) {
		SetAPen(rp, dri->dri_Pens[FILLPEN]);
		if (fill > pw.fill)
			RectFill(rp, pw.bx + 2 + pw.fill, pw.by + 1, pw.bx + 1 + fill, pw.by + pw.bh - 2);
		pw.fill = fill;
	}
}

/* The converter's poll: redraws ten times a second, and listens to Stop. */
static int progress_poll(const struct conv_status *st, void *ctx)
{
	struct IntuiMessage *m;
	ULONG now = plat_ticks();

	(void)ctx;
	while ((m = GT_GetIMsg(pw.w->UserPort))) {
		ULONG cls = m->Class;
		UWORD code = m->Code;
		APTR addr = m->IAddress;
		GT_ReplyIMsg(m);
		if (cls == IDCMP_CLOSEWINDOW || (cls == IDCMP_GADGETUP && ((struct Gadget *)addr)->GadgetID == G_STOP) ||
		    (cls == IDCMP_VANILLAKEY && (code == 's' || code == 'S' || code == 27)))
			pw.stop = 1;
		else if (cls == IDCMP_REFRESHWINDOW) {
			GT_BeginRefresh(pw.w);
			GT_EndRefresh(pw.w, TRUE);
		}
	}
	if (st && (now - pw.last >= 5 || !pw.last)) {
		pw.last = now ? now : 1;
		draw_progress(st);
	}
	return pw.stop;
}

/* ---- installing ---- */

static int copy_file(const char *from, const char *to)
{
	static UBYTE buf[16384];
	plat_file in = plat_open(from), out;
	int32_t n;
	int rc = CONV_OK;

	if (!in)
		return CONV_ERR_READ;
	out = plat_create(to);
	if (!out) {
		plat_close(in);
		return CONV_ERR_WRITE;
	}
	while ((n = plat_read(in, buf, sizeof(buf))) > 0) {
		if (plat_write(out, buf, (uint32_t)n) != n) {
			rc = CONV_ERR_WRITE;
			break;
		}
		if (progress_poll(0, 0)) {
			rc = CONV_STOPPED;
			break;
		}
	}
	if (n < 0)
		rc = CONV_ERR_READ;
	plat_close(in);
	plat_close(out);
	if (rc)
		plat_delete(to);
	return rc;
}

static int install(const struct game_def *gd, const char *dir)
{
	char gdir[PATH_MAX_LEN], tmp[PATH_MAX_LEN], pack[PATH_MAX_LEN], bank_from[PATH_MAX_LEN], bank[PATH_MAX_LEN];
	char extra_from[PATH_MAX_LEN], extra[PATH_MAX_LEN], title[80];
	static char data_path[GAME_FILES][PATH_MAX_LEN];
	const char *data[GAME_FILES];
	int n = 0, k, rc, has_bank;

	game_path(gdir, gd, 0);
	game_path(tmp, gd, ".tmp");
	game_path(pack, gd, ".s5p");
	game_path(bank, gd, ".s5b");
	bank_from[0] = 0;
	str_cat(bank_from, sizeof(bank_from), BANKS_DIR "/");
	str_cat(bank_from, sizeof(bank_from), gd->id);
	str_cat(bank_from, sizeof(bank_from), ".s5b");
	has_bank = exists(bank_from);
	extra[0] = extra_from[0] = 0;
	if (gd->extra) {
		game_path(extra, gd, gd->extra);
		join_path(extra_from, sizeof(extra_from), dir, gd->extra);
	}
	for (k = 1; k < GAME_FILES && gd->files[k]; k++) {
		join_path(data_path[n], PATH_MAX_LEN, dir, gd->files[k]);
		data[n] = data_path[n];
		n++;
	}
	join_path(data_path[n], PATH_MAX_LEN, dir, gd->files[0]);

	plat_mkdir(GAMES_DIR);
	plat_mkdir(gdir);
	title[0] = 0;
	str_cat(title, sizeof(title), "Installing ");
	str_cat(title, sizeof(title), gd->name);
	if (!open_progress(title)) {
		DeleteFile((CONST_STRPTR)gdir);
		return CONV_ERR_MEMORY;
	}
	rc = convert_game(data_path[n], data, n, tmp, progress_poll, 0);
	if (!rc && has_bank)
		rc = copy_file(bank_from, bank);
	if (!rc && gd->extra)
		rc = copy_file(extra_from, extra);
	if (!rc && plat_rename(tmp, pack))
		rc = CONV_ERR_WRITE;
	close_progress();
	if (rc) {
		DeleteFile((CONST_STRPTR)tmp);
		if (has_bank)
			DeleteFile((CONST_STRPTR)bank);
		if (gd->extra)
			DeleteFile((CONST_STRPTR)extra);
		DeleteFile((CONST_STRPTR)gdir);	/* only goes if empty */
	}
	return rc;
}

static void add_game(void)
{
	struct FileRequester *fr;
	const struct game_def *gd = 0;
	char path[PATH_MAX_LEN];
	ULONG args[3], bytes = 0;
	int k, rc;

	if (!AslBase) {
		tell("Adding a game needs asl.library\n(version 38 or later) in LIBS:.", NULL);
		return;
	}
	fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (IPTR)win, ASLFR_SleepWindow, TRUE,
				 ASLFR_TitleText, (IPTR)"Choose the folder with the DOS game files",
				 ASLFR_PositiveText, (IPTR)"Use", ASLFR_DrawersOnly, TRUE,
				 last_dir[0] ? ASLFR_InitialDrawer : TAG_IGNORE, (IPTR)last_dir, TAG_DONE);
	if (!fr)
		return;
	if (!AslRequest(fr, NULL)) {
		FreeAslRequest(fr);
		return;
	}
	last_dir[0] = 0;
	str_cat(last_dir, sizeof(last_dir), (const char *)fr->fr_Drawer);
	FreeAslRequest(fr);

	SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
	rc = detect_game(last_dir, &gd);
	SetWindowPointer(win, TAG_DONE);
	args[0] = (ULONG)last_dir;
	switch (rc) {
	case DETECT_NONE:
		tell("No game this program knows is in\n%s\n\n"
		     "Choose the folder that holds the game's\n"
		     "files, such as MONKEY2.000 and MONKEY2.001.", args);
		return;
	case DETECT_UNSUPPORTED:
		args[0] = (ULONG)gd->name;
		tell("%s was found,\nbut it is not supported yet.", args);
		return;
	case DETECT_UNKNOWN_VERSION:
		args[0] = (ULONG)gd->name;
		tell("%s was found, but not in a version\nthis program knows (its files differ).", args);
		return;
	}
	game_path(path, gd, ".s5p");
	if (exists(path)) {
		args[0] = (ULONG)gd->name;
		tell("%s is already installed.", args);
		return;
	}
	for (k = 0; k < GAME_FILES && gd->files[k]; k++)
		bytes += gd->sizes[k];
	args[0] = (ULONG)gd->name;
	args[1] = bytes / 1150000 + 1;	/* MI2: 9 MB in 8 minutes on an A1200 */
	args[2] = bytes / 1000000 * 2 + 2;
	if (!ask("Install %s?\n\n"
		 "Its files are converted for the Amiga, once:\n"
		 "about %ld minutes on an A1200, and %ld MB in\n"
		 "the Games folder. You can stop at any time.", "Install|Cancel", args))
		return;

	SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
	rc = install(gd, last_dir);
	SetWindowPointer(win, TAG_DONE);
	if (rc == CONV_STOPPED) {
		tell("Installation stopped. Nothing was kept.", NULL);
	} else if (rc) {
		args[0] = (ULONG)gd->name;
		args[1] = (ULONG)convert_error(rc);
		tell("%s could not be installed:\n%s.\nNothing was kept.", args);
	}
	fill_list(gd);
}

/* Deletes the game's folder with everything in it (saved games too). */
static int remove_dir(const char *dir)
{
	static char names[32][108];
	struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
	char path[PATH_MAX_LEN];
	int n, i, rounds;

	if (!fib)
		return 0;
	for (rounds = 0; rounds < 64; rounds++) {
		BPTR l = Lock((CONST_STRPTR)dir, ACCESS_READ);
		n = 0;
		if (!l)
			break;
		if (Examine(l, fib))
			while (n < 32 && ExNext(l, fib)) {
				names[n][0] = 0;
				str_cat(names[n], sizeof(names[n]), (const char *)fib->fib_FileName);
				n++;
			}
		UnLock(l);
		for (i = 0; i < n; i++)
			if (join_path(path, sizeof(path), dir, names[i]))
				DeleteFile((CONST_STRPTR)path);
		if (n < 32)
			break;
	}
	FreeDosObject(DOS_FIB, fib);
	return DeleteFile((CONST_STRPTR)dir) != 0;
}

static void remove_game(void)
{
	const struct game_def *gd;
	char gdir[PATH_MAX_LEN];
	ULONG args[2];

	if (sel < 0 || sel >= nlisted)
		return;
	gd = listed[sel];
	game_path(gdir, gd, 0);
	args[0] = (ULONG)gd->name;
	args[1] = (ULONG)gdir;
	if (!ask("Remove %s?\n\nThe converted files and the saved games\nin %s are deleted.", "Remove|Cancel", args))
		return;
	SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
	if (!remove_dir(gdir))
		tell("Some files in %s\ncould not be deleted.", args + 1);
	SetWindowPointer(win, TAG_DONE);
	fill_list(0);
}

/* ---- the main window ---- */

static int open_main(void)
{
	struct NewGadget ng;
	struct Gadget *g;
	WORD fh = scr->Font->ta_YSize, left = scr->WBorLeft + 8, top = scr->WBorTop + fh + 1 + 6;
	WORD w = 380, bh = fh + 6, lvh = fh * 6 + 4, y, bw[4], x, sum = 0;
	static const char *const labels[4] = { "_Start", "_Add game...", "_Remove", "_Quit" };
	int i;

	/* each button fits its label, the spare width shared out */
	for (i = 0; i < 4; i++) {
		const char *t = labels[i] + 1;
		bw[i] = (WORD)(TextLength(&scr->RastPort, (CONST_STRPTR)t, (ULONG)strlen(t)) + 16);
		sum += bw[i];
	}
	if (sum + 3 * 6 > w)
		w = (WORD)(sum + 3 * 6);
	for (i = 0; i < 4; i++)
		bw[i] += (WORD)((w - 3 * 6 - sum) / 4);

	memset(&ng, 0, sizeof(ng));
	ng.ng_TextAttr = scr->Font;
	ng.ng_VisualInfo = vi;
	g = CreateContext(&glist);
	ng.ng_LeftEdge = left;
	ng.ng_TopEdge = top;
	ng.ng_Width = w;
	ng.ng_Height = lvh;
	ng.ng_GadgetID = G_LIST;
	g_list = g = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (IPTR)&games, GTLV_ShowSelected, 0,
				  GTLV_Selected, sel < 0 ? ~0UL : (ULONG)sel, TAG_DONE);
	y = top + lvh + 4;
	ng.ng_TopEdge = y;
	ng.ng_Height = fh + 4;
	ng.ng_GadgetID = G_STATUS;
	g_status = g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (IPTR)status, TAG_DONE);
	y += fh + 8;
	for (i = 0, x = left; i < 4; x += bw[i] + 6, i++) {
		if (i == 3)
			bw[i] = (WORD)(left + w - x);
		ng.ng_LeftEdge = x;
		ng.ng_TopEdge = y;
		ng.ng_Width = bw[i];
		ng.ng_Height = bh;
		ng.ng_GadgetText = (UBYTE *)labels[i];
		ng.ng_GadgetID = G_START + i;
		g = CreateGadget(BUTTON_KIND, g, &ng, GT_Underscore, '_',
				 GA_Disabled, (i == 0 || i == 2) && sel < 0, TAG_DONE);
		if (i == 0)
			g_start = g;
		else if (i == 2)
			g_remove = g;
	}
	if (!g)
		return 0;
	win = OpenWindowTags(NULL, WA_PubScreen, (IPTR)scr, WA_Title, (IPTR)TITLE " - Game launcher",
			     WA_Left, (scr->Width - w - 16) / 2, WA_Top, (scr->Height - y - bh) / 3,
			     WA_InnerWidth, w + 16, WA_InnerHeight, y + bh + 8 - (scr->WBorTop + fh + 1),
			     WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
			     WA_AutoAdjust, TRUE, WA_Gadgets, (IPTR)glist,
			     WA_NewLookMenus, TRUE,
			     WA_IDCMP, LISTVIEWIDCMP | BUTTONIDCMP | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
					   IDCMP_VANILLAKEY | IDCMP_MENUPICK, TAG_DONE);
	if (!win)
		return 0;
	if ((menus = CreateMenusA(menu_def, NULL))) {
		if (LayoutMenus(menus, vi, GTMN_NewLookMenus, TRUE, TAG_DONE))
			SetMenuStrip(win, menus);
		else {
			FreeMenus(menus);
			menus = 0;
		}
	}
	GT_RefreshWindow(win, NULL);
	return 1;
}

/* 1: start the selected game; 0: quit */
static int run(void)
{
	ULONG last_s = 0, last_m = 0;
	int last_sel = -1;

	for (;;) {
		struct IntuiMessage *m;
		Wait(1UL << win->UserPort->mp_SigBit);
		while ((m = GT_GetIMsg(win->UserPort))) {
			ULONG cls = m->Class, secs = m->Seconds, mics = m->Micros;
			UWORD code = m->Code;
			APTR addr = m->IAddress;
			int id = 0;
			GT_ReplyIMsg(m);
			if (cls == IDCMP_CLOSEWINDOW)
				return 0;
			if (cls == IDCMP_REFRESHWINDOW) {
				GT_BeginRefresh(win);
				GT_EndRefresh(win, TRUE);
				continue;
			}
			if (cls == IDCMP_MENUPICK) {
				while (code != MENUNULL) {
					struct MenuItem *item = ItemAddress(menus, code);
					if (!item)
						break;
					if (GTMENUITEM_USERDATA(item) == (APTR)M_QUIT)
						return 0;
					if (GTMENUITEM_USERDATA(item) == (APTR)M_ABOUT)
						about();
					code = item->NextSelect;
				}
				continue;
			}
			if (cls == IDCMP_GADGETUP)
				id = ((struct Gadget *)addr)->GadgetID;
			if (cls == IDCMP_VANILLAKEY) {
				code |= 0x20;
				id = code == 's' || code == ('\r' | 0x20) ? G_START : code == 'a' ? G_ADD
				     : code == 'r' ? G_REMOVE : code == 'q' || code == (27 | 0x20) ? G_QUIT : 0;
			}
			switch (id) {
			case G_LIST:
				sel = code < nlisted ? code : -1;
				update_buttons();
				if (sel >= 0 && sel == last_sel && DoubleClick(last_s, last_m, secs, mics))
					return 1;
				last_sel = sel;
				last_s = secs;
				last_m = mics;
				break;
			case G_START:
				if (sel >= 0)
					return 1;
				break;
			case G_ADD:
				add_game();
				break;
			case G_REMOVE:
				remove_game();
				break;
			case G_QUIT:
				return 0;
			}
		}
	}
}

int launcher_run(char *pack, int pack_size, char *rec, int rec_size)
{
	char path[PATH_MAX_LEN];
	int go = 0;

	pack[0] = rec[0] = 0;
	IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
	GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 39);
	AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38);
	if (!IntuitionBase || !GfxBase || !GadToolsBase)
		goto out;
	scr = LockPubScreen(NULL);
	if (!scr)
		goto out;
	vi = GetVisualInfoA(scr, NULL);
	dri = GetScreenDrawInfo(scr);
	clean_leftovers();
	fill_list(0);
	if (vi && dri && open_main())
		go = run();
	if (go && sel >= 0 && sel < nlisted) {
		const struct game_def *gd = listed[sel];
		game_path(path, gd, ".s5p");
		pack[0] = 0;
		str_cat(pack, pack_size, path);
		if (gd->extra) {
			game_path(path, gd, gd->extra);
			str_cat(rec, rec_size, path);
		}
	} else {
		go = 0;
	}
out:
	if (win) {
		if (menus)
			ClearMenuStrip(win);
		CloseWindow(win);
	}
	win = 0;
	FreeMenus(menus);
	menus = 0;
	FreeGadgets(glist);
	glist = 0;
	if (dri)
		FreeScreenDrawInfo(scr, dri);
	if (vi)
		FreeVisualInfo(vi);
	if (scr)
		UnlockPubScreen(NULL, scr);
	dri = 0;
	vi = 0;
	scr = 0;
	if (AslBase)
		CloseLibrary(AslBase);
	if (GadToolsBase)
		CloseLibrary(GadToolsBase);
	if (GfxBase)
		CloseLibrary((struct Library *)GfxBase);
	if (IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
	AslBase = GadToolsBase = 0;
	GfxBase = 0;
	IntuitionBase = 0;
	return go;
}
