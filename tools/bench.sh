#!/bin/bash
# Usage: bench.sh <dist-dir> <exe-name> <frames> [screenshot.png|-] [extra fs-uae options...]
# Boots an emulated stock A1200 from a copy of <dist-dir>, runs "<exe-name> bench <frames>", prints the
# bench.txt it writes. The screenshot is taken SHOT_AT (default 20) seconds after the emulator starts.
DIST="$(realpath "$1")"; EXE="$2"; FRAMES="$3"; SHOT="${4:--}"; shift 4 2>/dev/null || shift $#
D=$(mktemp -d /tmp/uaebench.XXXXXX)
cp -r "$DIST"/. "$D"/; mkdir -p "$D/S"; printf '%s bench %s\n' "$EXE" "$FRAMES" > "$D/S/startup-sequence"
rm -f "$D/bench.txt"
SECS=$(( FRAMES / 25 + 60 ))
UAE_DONE="$D/bench.txt" UAE_SHOT_AT="${SHOT_AT:-20}" "$(dirname "$0")/run_uae.sh" "$D" "$SECS" "$SHOT" "$@"
if [ -e "$D/bench.txt" ]; then cat "$D/bench.txt"; else echo "NO bench.txt (crashed, hung, or did not finish in $SECS s)"; fi
rm -rf "$D"
