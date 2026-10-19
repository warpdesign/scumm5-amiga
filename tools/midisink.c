/* scumm5-amiga - macOS virtual MIDI destination that swallows everything
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ScummVM's CoreMIDI driver needs a destination to open; with --dump-midi
 * it then logs every message it sends, which is the iMUSE reference.
 *   cc -O1 -o build/host/midisink tools/midisink.c -framework CoreMIDI -framework CoreFoundation
 *   build/host/midisink [SECONDS]
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <stdio.h>
#include <stdlib.h>

static void on_packets(const MIDIPacketList *list, void *ref, void *src)
{
	(void)list;
	(void)ref;
	(void)src;
}

int main(int argc, char **argv)
{
	MIDIClientRef client;
	MIDIEndpointRef dest;
	double secs = argc > 1 ? atof(argv[1]) : 3600;

	if (MIDIClientCreate(CFSTR("scumm5 midisink"), NULL, NULL, &client) != noErr ||
	    MIDIDestinationCreate(client, CFSTR("scumm5 sink"), on_packets, NULL, &dest) != noErr) {
		fprintf(stderr, "midisink: cannot create the destination\n");
		return 1;
	}
	printf("midisink: destination \"scumm5 sink\" up\n");
	fflush(stdout);
	CFRunLoopRunInMode(kCFRunLoopDefaultMode, secs, false);
	MIDIEndpointDispose(dest);
	MIDIClientDispose(client);
	return 0;
}
