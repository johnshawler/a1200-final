#!/bin/bash
# Usage: tools/shot_at.sh <tick> <out.png> [seconds] - debug: run the bench input, freeze the picture at <tick>,
# and take a screenshot <seconds> (default: an estimate) after the emulator starts.
cd "$(dirname "$0")/.."
. ./port.conf
T="$1"; OUT="$2"; AT="${3:-$(( 14 + T * 3 / 100 ))}"
CFLAGS="$CFLAGS -DHOLD_AT=$T" tools/build.sh dist/llb_hold $SOURCES >/dev/null || exit 1
D=$(mktemp -d /tmp/hold.XXXXXX); mkdir -p "$D/S"; cp dist/llb_hold "$D/"
printf 'llb_hold bench %s\n' $(( T + 10 )) > "$D/S/startup-sequence"
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
UAE_SHOT_AT="$AT" UAE_STOP_AFTER_SHOT=1 tools/run_uae.sh "$D" $(( AT + 5 )) "$OUT"
rm -rf "$D"
