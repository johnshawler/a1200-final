#!/bin/bash
# Usage: tools/debug_run.sh <outdir> <ticks> [extra CFLAGS] - debug build with a checksum after every tick
# (trace.bin, big-endian longs) and a bench run that keeps everything the Amiga wrote in <outdir>.
cd "$(dirname "$0")/.."
. ./port.conf
OUT="$1"; N="$2"; shift 2
CFLAGS="$CFLAGS -DLOGIC_TRACE=8192 $*" tools/build.sh dist/llb_dbg $SOURCES src/prof68k.s >/dev/null || exit 1
rm -rf "$OUT"; mkdir -p "$OUT/S"; cp dist/llb_dbg "$OUT/"
printf 'llb_dbg bench %s\n' "$N" > "$OUT/S/startup-sequence"
UAE_DONE="$OUT/bench.txt" tools/run_uae.sh "$OUT" $(( N / 20 + 90 )) "$OUT/shot.png" $UAE_OPTS     # e.g. UAE_OPTS=--fast_memory=4096
cat "$OUT/bench.txt" 2>/dev/null || echo "no bench.txt"
