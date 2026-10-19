/* scumm5-amiga - console/debug output helpers
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "util.h"

static char line[160];
static uint16_t len;

void out_str(const char *s)
{
	while (*s && len < sizeof(line) - 1)
		line[len++] = *s++;
}

void out_num(uint32_t v)
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

void out_hex(uint32_t v)
{
	char buf[9];
	int i = sizeof(buf) - 1;
	buf[i] = 0;
	do {
		buf[--i] = "0123456789abcdef"[v & 15];
		v >>= 4;
	} while (v);
	out_str(buf + i);
}

void report(const char *s)
{
	out_str(s);
	line[len] = 0;
	plat_log(line);
	len = 0;
}

uint32_t parse_uint(const char **pp)
{
	const char *p = *pp;
	uint32_t v = 0;
	while (*p >= '0' && *p <= '9')
		v = v * 10 + (uint32_t)(*p++ - '0');
	*pp = p;
	return v;
}

const char *skip_spaces(const char *p)
{
	while (*p == ' ')
		p++;
	return p;
}
