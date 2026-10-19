#!/bin/sh
# Run the resource loader test under Copperline (A1200, 2 MB chip, no fast RAM)
# and exit non-zero unless it passes.
#   scripts/run-restest.sh [ARENA_KB [ACCESSES]]
set -e
cd "$(dirname "$0")/.."

PACK=${PACK:-mi2demo.s5p}
COPPERLINE=${COPPERLINE:-/Applications/Copperline.app/Contents/MacOS/copperline}

make >/dev/null

out=$("$COPPERLINE" --model A1200 --chip 2M --fast 0 --noaudio --run build/amiga/restest \
	--run-args "$PACK ${1:-256} ${2:-4000}" --exit-on-return $COPPERLINE_ARGS 2>&1 |
	sed -n 's/^DBG: //p')
echo "$out"
case $out in *"restest: PASS"*) ;; *) exit 1 ;; esac
