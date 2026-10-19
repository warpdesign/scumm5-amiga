#!/bin/sh
# Record a ScummVM reference of the MIDI stream iMUSE sends to a General
# MIDI device, interleaved with the opcode trace, headless.
#   scripts/scummvm-midi.sh OUT.txt [SECONDS]
# Needs build/host/midisink (macOS CoreMIDI): ScummVM's CoreMIDI driver
# sends to that virtual destination, and --dump-midi logs each message.
set -e
cd "$(dirname "$0")/.."

OUT=${1:?usage: scummvm-midi.sh OUT.txt [SECONDS]}
SECS=${2:-120}
SCUMMVM=${SCUMMVM:-$HOME/Documents/Dev/scummvm-build/scummvm}
GAME_DIR=$(pwd)/${DATA:-scumm-data/mi2demo}
TMP=$(mktemp -d)

[ -x build/host/midisink ] || cc -O1 -o build/host/midisink tools/midisink.c \
	-framework CoreMIDI -framework CoreFoundation 2>/dev/null

cat > "$TMP/scummvm.ini" <<INI
[scummvm]
gfx_mode=surfacesdl

[monkey2]
gameid=monkey2
path=$GAME_DIR
enhancements=0
music_driver=coremidi_scumm5 sink
native_mt32=false
enable_gs=false
multi_midi=false
subtitles=true
music_volume=255
autosave_period=0
INI

build/host/midisink $((SECS + 30)) > /dev/null &
SINK=$!
sleep 1
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
	"$SCUMMVM" --config="$TMP/scummvm.ini" --random-seed=1 --dump-midi \
	--debugflags=OPCODES,SCRIPTS -d 1 monkey2 > "$TMP/log.txt" 2>/dev/null &
PID=$!
sleep "$SECS"
kill $PID 2>/dev/null || true
wait $PID 2>/dev/null || true
kill $SINK 2>/dev/null || true
# The MIDI timer thread writes each dump line in pieces, so script trace
# lines from the main thread can land inside one: split them back out.
python3 -I - "$TMP/log.txt" > "$OUT" <<'PY'
import re, sys
keep = re.compile(r'(Script \d|runScript\()')
pending = ''
for line in open(sys.argv[1], errors='replace'):
    line = line.rstrip('\n')
    if pending:
        m = keep.search(line)
        if m and m.start() == 0:
            print(line)
            continue
        line = pending + line
        pending = ''
    if line.startswith('MIDI :'):
        m = keep.search(line)
        if m:
            print(line[m.start():])
            pending = line[:m.start()]
            continue
        if 'message(' not in line or not line.rstrip().endswith(')'):
            pending = line
            continue
        print(line)
    elif keep.match(line):
        print(line)
PY
rm -rf "$TMP"
grep -c '^MIDI :' "$OUT"
