#!/bin/sh
# Run the room viewer under Copperline on an unexpanded A1200 (2 MB chip, no fast RAM).
#   scripts/run-roomview.sh [ROOM]            interactive window, LMB exits
#   scripts/run-roomview.sh [ROOM] SHOT.png   headless, screenshot after $SHOT_SECS
# Extra roomview arguments go in $ROOMVIEW_ARGS ("FRAMES X", e.g. "0 100" to
# hold column 100); extra Copperline flags (e.g. a Kickstart ROM) in $COPPERLINE_ARGS.
set -e
cd "$(dirname "$0")/.."

ROOM=${1:-7}
SHOT=$2
PACK=${PACK:-mi2demo.s5p}
COPPERLINE=${COPPERLINE:-/Applications/Copperline.app/Contents/MacOS/copperline}

make >/dev/null

set -- --model A1200 --chip 2M --fast 0 --run build/amiga/roomview --run-args "$PACK $ROOM${ROOMVIEW_ARGS:+ $ROOMVIEW_ARGS}" $COPPERLINE_ARGS
if [ -n "$SHOT" ]; then
	exec "$COPPERLINE" "$@" --noaudio --screenshot-after "${SHOT_SECS:-20}" "$SHOT"
fi
exec "$COPPERLINE" "$@"
