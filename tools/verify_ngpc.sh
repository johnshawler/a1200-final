#!/bin/bash
# Usage: tools/verify_ngpc.sh [ticks]
# The proof of the translation. Builds the host test twice from the same machine and frame model: once with the
# code tools/recomp900.py generated, once with the interpreter. Both write work RAM and the K2GE's memory after
# every tick; the two files must be equal byte for byte. Prints the first tick that differs, if any.
set -e
cd "$(dirname "$0")/.."
N=${1:-3000}
OUT=${TMPDIR:-/tmp}/verify_ngpc.$$
mkdir -p "$OUT"
COMMON="src/ngpc/host_test.c src/ngpc/logic.c src/ngpc/ngp.c src/ngpc/k2ge.c src/coro_host.c"
cc -O2 -Isrc -Isrc/ngpc $COMMON src/ngpc/run_c.c src/ngpc/gen_dispatch.c src/ngpc/gen_[0-9]*.c -o "$OUT/translated"
cc -O2 -Isrc -Isrc/ngpc $COMMON src/ngpc/run_interp.c -o "$OUT/interpreted"
"$OUT/translated" "$N" -r "$OUT/t.bin" | tail -1
"$OUT/interpreted" "$N" -r "$OUT/i.bin" | tail -1
if cmp -s "$OUT/t.bin" "$OUT/i.bin"; then
  echo "verify: translation = interpreter after every one of $N ticks ($(stat -c %s "$OUT/t.bin") bytes compared)"
  rm -rf "$OUT"; exit 0
fi
BYTE=$(cmp "$OUT/t.bin" "$OUT/i.bin" | sed 's/.*\(byte\|char\) \([0-9]*\).*/\2/')
echo "verify: DIFFERENT. First difference in tick $(( (BYTE - 1) / 32768 + 1 )), offset $(( (BYTE - 1) % 32768 )) of that tick's 32768 bytes"
echo "        (0-16383 = RAM from 0x4000, 16384-32767 = K2GE from 0x8000). The two dumps are in $OUT"
exit 1
