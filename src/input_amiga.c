/* scumm5-amiga - mouse and keyboard from an input.device handler
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The handler runs ahead of Intuition and takes the raw mouse and key
 * events, so nothing reaches the Workbench hidden under the display. The
 * main loop picks up what arrived once per frame.
 */
#include "amiga.h"
#include "scumm/scumm.h"

void display_pointer(int x, int y);

#define IECLASS_NULL 0
#define IECLASS_RAWKEY 1
#define IECLASS_RAWMOUSE 2
#define IECODE_UP 0x80
#define IECODE_LBUTTON 0x68
#define IECODE_RBUTTON 0x69
#define IEQUAL_SHIFT 0x0003
#define IEQUAL_CONTROL 0x0008
#define IEQUAL_ALT 0x0030
#define IEQUAL_REPEAT 0x0200
#define IND_ADDHANDLER 9
#define IND_REMHANDLER 10
#define IO_COMMAND 28
#define IO_DATA 40
#define IOSTD_SIZE 48
#define KEYQ 16

struct InputEvent {
	struct InputEvent *next;
	UBYTE cls, subclass;
	UWORD code, qualifier;
	WORD x, y;
	ULONG time[2];
};

void input_stub(void);

static APTR port, io;
static struct Interrupt handler;
static volatile WORD mouse_x = SCREEN_WIDTH / 2, mouse_y = SCREEN_HEIGHT / 2;
static UBYTE follow;
static volatile UBYTE lclicks, rclicks, ldown, rdown, quit;
static volatile UWORD keyq[KEYQ];
static volatile UBYTE khead, ktail;

/* US layout, raw key codes 0x00-0x3f */
static const char raw_ascii[64] =
	"`1234567890-=\\\0000"
	"qwertyuiop[]\000123"
	"asdfghjkl;'\000\000456"
	"\000zxcvbnm,./\000.789";

static UWORD translate(UWORD code, UWORD qual)
{
	UWORD c = 0;

	if (code < 64)
		c = (UBYTE)raw_ascii[code];
	else if (code == 0x40)
		c = ' ';
	else if (code == 0x41)
		c = 8;
	else if (code == 0x43 || code == 0x44)
		c = 13;
	else if (code == 0x45)
		c = 27;
	else if (code >= 0x50 && code <= 0x59)
		return (UWORD)(315 + code - 0x50);	/* F1-F10 as DOS SCUMM numbers them */
	if (c >= 'a' && c <= 'z' && (qual & IEQUAL_SHIFT))
		c = (UWORD)(c - 32);
	return c;
}

struct InputEvent *input_filter(struct InputEvent *list)
{
	struct InputEvent *e;

	for (e = list; e; e = e->next) {
		if (e->cls == IECLASS_RAWMOUSE) {
			if (e->x || e->y) {
				int x = mouse_x + e->x, y = mouse_y + e->y;
				mouse_x = (WORD)(x < 0 ? 0 : x > SCREEN_WIDTH - 1 ? SCREEN_WIDTH - 1 : x);
				mouse_y = (WORD)(y < 0 ? 0 : y > SCREEN_HEIGHT - 1 ? SCREEN_HEIGHT - 1 : y);
				if (follow)
					display_pointer(mouse_x, mouse_y);
			}
			if (e->code == IECODE_LBUTTON) {
				lclicks++;
				ldown = 1;
			} else if (e->code == (IECODE_LBUTTON | IECODE_UP)) {
				ldown = 0;
			} else if (e->code == IECODE_RBUTTON) {
				rclicks++;
				rdown = 1;
			} else if (e->code == (IECODE_RBUTTON | IECODE_UP)) {
				rdown = 0;
			}
			e->cls = IECLASS_NULL;
		} else if (e->cls == IECLASS_RAWKEY) {
			UWORD k = (UWORD)(e->code & 0x7f);
			if (!(e->code & IECODE_UP) && !(e->qualifier & IEQUAL_REPEAT)) {
				if (((e->qualifier & IEQUAL_CONTROL) && k == 0x33) ||
				    ((e->qualifier & IEQUAL_ALT) && k == 0x32))
					quit = 1;	/* Ctrl-C, Alt-X */
				else if ((k = translate(k, e->qualifier)) && (UBYTE)(khead - ktail) < KEYQ)
					keyq[khead++ % KEYQ] = k;
			}
			e->cls = IECLASS_NULL;
		}
	}
	return list;
}

/* With pointer set, the mouse plays the game and moves the pointer sprite. */
int input_open(int pointer)
{
	port = CreateMsgPort();
	io = port ? CreateIORequest(port, IOSTD_SIZE) : 0;
	if (!io || OpenDevice("input.device", 0, io, 0)) {
		if (io)
			DeleteIORequest(io);
		if (port)
			DeleteMsgPort(port);
		io = port = 0;
		return -1;
	}
	follow = (UBYTE)pointer;
	handler.is_Node.ln_Type = NT_INTERRUPT;
	handler.is_Node.ln_Pri = 100;
	handler.is_Node.ln_Name = "scumm5 input";
	handler.is_Code = input_stub;
	*(UWORD *)((UBYTE *)io + IO_COMMAND) = IND_ADDHANDLER;
	*(APTR *)((UBYTE *)io + IO_DATA) = &handler;
	DoIO(io);
	return 0;
}

void input_close(void)
{
	if (!io)
		return;
	*(UWORD *)((UBYTE *)io + IO_COMMAND) = IND_REMHANDLER;
	*(APTR *)((UBYTE *)io + IO_DATA) = &handler;
	DoIO(io);
	CloseDevice(io);
	DeleteIORequest(io);
	DeleteMsgPort(port);
	io = port = 0;
}

/* Once per frame, before the engine reads the input. */
int input_poll(void)
{
	WORD x, y;
	UBYTE lc, rc, ld, rd;

	if (!io)
		return 0;
	Disable();
	x = mouse_x;
	y = mouse_y;
	lc = lclicks;
	rc = rclicks;
	lclicks = rclicks = 0;
	ld = ldown;
	rd = rdown;
	Enable();

	if (follow)
		input_mouse(x, y, lc, ld, rc, rd);
	if (follow && khead != ktail)
		input_key(keyq[ktail++ % KEYQ]);
	return quit;
}
