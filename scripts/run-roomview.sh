#!/bin/sh
# Run the room viewer under Copperline on an unexpanded A1200 (2 MB chip, no fast RAM).
#   scripts/run-roomview.sh [ROOM]            interactive window, LMB exits
#   scripts/run-roomview.sh [ROOM] SHOT.png   headless, screenshot after $SHOT_SECS
# Extra Copperline flags (e.g. a Kickstart ROM path) can be passed in $COPPERLINE_ARGS.
set -e
cd "$(dirname "$0")/.."

ROOM=${1:-7}
SHOT=$2
COPPERLINE=${COPPERLINE:-/Applications/Copperline.app/Contents/MacOS/copperline}

make ROOM="$ROOM" >/dev/null

set -- --model A1200 --chip 2M --fast 0 --run build/amiga/roomview --run-args "room$ROOM.pln" $COPPERLINE_ARGS
if [ -n "$SHOT" ]; then
	exec "$COPPERLINE" "$@" --noaudio --screenshot-after "${SHOT_SECS:-20}" "$SHOT"
fi
exec "$COPPERLINE" "$@"
