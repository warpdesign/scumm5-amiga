/* scumm5-amiga - script trace in ScummVM's debug format, fatal errors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lines match ScummVM's DEBUG_OPCODES and DEBUG_SCRIPTS output
 * (engines/scumm/script.cpp) so the two traces can be compared directly.
 */
#include "scumm.h"
#include "util.h"

int trace_on;
static plat_file trace_file;
static char buf[4096];
static uint32_t buf_len;

void trace_open(const char *path)
{
	trace_file = plat_create(path);
	trace_on = trace_file != 0;
}

void trace_flush(void)
{
	if (trace_file && buf_len) {
		plat_write(trace_file, buf, buf_len);
		buf_len = 0;
	}
}

void trace_close(void)
{
	trace_flush();
	if (trace_file)
		plat_close(trace_file);
	trace_file = 0;
	trace_on = 0;
}

static void put_str(const char *s)
{
	while (*s) {
		if (buf_len == sizeof(buf))
			trace_flush();
		buf[buf_len++] = *s++;
	}
}

static void put_num(int32_t v)
{
	char tmp[12];
	int i = sizeof(tmp) - 1;
	uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;

	tmp[i] = 0;
	do {
		tmp[--i] = (char)('0' + u % 10);
		u /= 10;
	} while (u);
	if (v < 0)
		tmp[--i] = '-';
	put_str(tmp + i);
}

static void put_hex(uint32_t v, int upper)
{
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char tmp[9];
	int i = sizeof(tmp) - 1;

	tmp[i] = 0;
	do {
		tmp[--i] = digits[v & 15];
		v >>= 4;
	} while (v);
	put_str(tmp + i);
}

void trace_text(const char *s)
{
	if (!trace_on)
		return;
	put_str(s);
	put_str("\n");
}

/* Room scripts are addressed inside the ROOM block, which the pack
 * rebuilds with converted images; report offsets as in the original block,
 * using the S5OF table of top-level child offsets. */
static uint32_t original_offset(const struct script_slot *ss, uint32_t offs)
{
	const uint8_t *room, *t;
	int n, i;

	if (ss->where != WIO_ROOM && ss->where != WIO_LOCAL)
		return offs;
	room = res_peek(RT_ROOM, g.room_resource);
	t = room ? res_find(TAG('S', '5', 'O', 'F'), room) : 0;
	if (!t)
		return offs;
	n = rd_be16(t + 8);
	for (i = n - 1; i >= 0; i--) {
		uint32_t nw = rd_be32(t + 10 + i * 8);
		if (offs >= nw)
			return offs - nw + rd_be32(t + 14 + i * 8);
	}
	return offs;
}

void trace_opcode(void)
{
	const struct script_slot *ss = &g.slot[g.current_script];

	put_str("Script ");
	put_num(ss->number);
	put_str(", offset 0x");
	put_hex(original_offset(ss, (uint32_t)(g.script_ptr - g.script_org)), 0);
	put_str(": [");
	put_hex(g.opcode, 1);
	put_str("] ");
	put_str(opcode_name(g.opcode));
	put_str("()\n");
}

void trace_run_script(int script, int local)
{
	int from = g.current_script != 0xFF ? g.slot[g.current_script].number : 0;

	put_str("runScript(");
	if (!local)
		put_str("Global-");
	put_num(script);
	put_str(") from ");
	put_num(from);
	put_str("-");
	put_num(g.room_resource);
	put_str("\n");
}

void fatal(const char *msg, int a, int b)
{
	trace_close();
	out_str("fatal: ");
	out_str(msg);
	out_str(" (");
	out_num((uint32_t)a);
	out_str(", ");
	out_num((uint32_t)b);
	out_str(")");
	if (g.current_script != 0xFF) {
		const struct script_slot *ss = &g.slot[g.current_script];
		out_str(" in script ");
		out_num(ss->number);
		out_str(" offset 0x");
		out_hex(original_offset(ss, (uint32_t)(g.script_ptr - g.script_org)));
	}
	out_str(" room ");
	out_num(g.current_room);
	out_str(" frame ");
	out_num(g.frame);
	report("");
	plat_exit(30);
}
