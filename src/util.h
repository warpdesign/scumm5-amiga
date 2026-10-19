/* scumm5-amiga - console/debug output helpers
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef UTIL_H
#define UTIL_H

#include "platform.h"

/* Builds a line in a static buffer; report() prints it to the console and
 * the emulator log, then starts a new line. */
void out_str(const char *s);
void out_num(uint32_t v);
void out_hex(uint32_t v);
void report(const char *s);

uint32_t parse_uint(const char **pp);
const char *skip_spaces(const char *p);

#endif
