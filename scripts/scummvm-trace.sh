#!/bin/sh
# Record a reference opcode trace with a SCUMM-enabled ScummVM build, headless.
#   scripts/scummvm-trace.sh OUT.txt [SECONDS]
# $SCUMMVM: ScummVM binary (default ~/Documents/Dev/scummvm-build/scummvm).
# ScummVM runs in real time, so SECONDS bounds how far the trace gets.
# Enhancements are off and the music driver is null, matching scummrun.
set -e
cd "$(dirname "$0")/.."

OUT=${1:?usage: scummvm-trace.sh OUT.txt [SECONDS]}
SECS=${2:-120}
SCUMMVM=${SCUMMVM:-$HOME/Documents/Dev/scummvm-build/scummvm}
GAME_DIR=$(pwd)/${DATA:-scumm-data/mi2demo}
TMP=$(mktemp -d)

cat > "$TMP/scummvm.ini" <<INI
[scummvm]
gfx_mode=surfacesdl

[monkey2]
gameid=monkey2
path=$GAME_DIR
enhancements=0
music_driver=null
subtitles=true
INI

SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
	"$SCUMMVM" --config="$TMP/scummvm.ini" --random-seed=1 --debugflags=OPCODES,SCRIPTS -d 1 monkey2 \
	> "$TMP/log.txt" 2>/dev/null &
PID=$!
sleep "$SECS"
kill $PID 2>/dev/null || true
wait $PID 2>/dev/null || true
grep -E '^(Script|runScript)' "$TMP/log.txt" > "$OUT"
rm -rf "$TMP"
wc -l "$OUT"
