/* scumm5-amiga - no audio output; the caller runs imuse_timer() itself
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "imuse.h"

void audio_lock(void)
{
}

void audio_unlock(void)
{
}

void audio_pause(int on)
{
	(void)on;
}
