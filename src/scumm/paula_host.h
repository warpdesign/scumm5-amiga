/* scumm5-amiga - Paula emulation writing a WAV file, for the host
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PAULA_HOST_H
#define PAULA_HOST_H

#include <stdint.h>

int paula_host_open(const char *path, uint32_t clock);
void paula_host_close(void);
void paula_host_tick(void);

#endif
