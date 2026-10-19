#!/bin/sh
# Run the interpreter headless on the host with the demo recording and
# compare its opcode trace with a ScummVM reference trace.
#   scripts/trace-compare.sh REFERENCE.txt [FRAMES]
set -e
cd "$(dirname "$0")/.."

REF=${1:?usage: trace-compare.sh REFERENCE.txt [FRAMES]}
FRAMES=${2:-4000}
DATA=${DATA:-scumm-data/mi2demo}

make build/host/scummrun build/amiga/mi2demo.s5p >/dev/null
build/host/scummrun build/amiga/mi2demo.s5p "$FRAMES" -t build/host/trace.txt -r "$DATA/DEMO.REC"
python3 -I tools/tracecmp.py "$REF" build/host/trace.txt
