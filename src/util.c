/* scumm5-amiga - console/debug output helpers
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "util.h"

static char line[160];
static UWORD len;

static void uae_log(const char *s)
{
	typedef LONG (*uaelib_fn)(LONG, const char *);
	uaelib_fn uae = (uaelib_fn)0xf0ff60;
	UWORD op = *(volatile UWORD *)0xf0ff60;
	if (op == 0x4eb9 || op == 0xa00e)
		uae(86, s);
}

void out_str(const char *s)
{
	while (*s && len < sizeof(line) - 1)
		line[len++] = *s++;
}

void out_num(ULONG v)
{
	char buf[11];
	int i = sizeof(buf) - 1;
	buf[i] = 0;
	do {
		buf[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	out_str(buf + i);
}

void report(const char *s)
{
	out_str(s);
	line[len] = 0;
	PutStr(line);
	PutStr("\n");
	uae_log(line);
	len = 0;
}

ULONG parse_uint(const char **pp)
{
	const char *p = *pp;
	ULONG v = 0;
	while (*p >= '0' && *p <= '9')
		v = v * 10 + (ULONG)(*p++ - '0');
	*pp = p;
	return v;
}

const char *skip_spaces(const char *p)
{
	while (*p == ' ')
		p++;
	return p;
}
