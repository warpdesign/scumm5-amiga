/* scumm5-amiga - the game launcher (Workbench window)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef LAUNCHER_H
#define LAUNCHER_H

/* Shows the installed games until one is started (1: pack and, for the
 * demo, its recording in rec, else "") or the launcher is closed (0). */
int launcher_run(char *pack, int pack_size, char *rec, int rec_size);

#endif
