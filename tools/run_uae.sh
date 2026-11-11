#!/bin/bash
# Usage: run_uae.sh <hd_dir> <max_seconds> <screenshot.png|-> [extra fs-uae options...]
# Boots an emulated stock A1200 (68EC020 14 MHz cycle-exact, 2 MB chip RAM, AGA, PAL, AROS ROM) from a
# host directory (its S/startup-sequence runs), then stops the emulator.
# Env: UAE_DONE=<file>       stop as soon as this file exists (the Amiga program writes it last)
#      UAE_SHOT_WHEN=<file>  take the screenshot UAE_SHOT_DELAY (default 3) seconds after this file exists
#      UAE_SHOT_AT=<seconds>  take the screenshot this many seconds after the emulator starts
#      UAE_STOP_AFTER_SHOT=1 stop the emulator right after the screenshot is taken
#      UAE_LOG=<path>        keep the full emulator log
#      UAE_KEYS="t1:key1 t2:key2"  send X key names (xdotool) at given seconds after UAE_SHOT_WHEN exists
HD="$(realpath "$1")"; SECS="$2"; SHOT="$3"; shift 3
for n in $(seq 90 140); do
  if [ ! -e /tmp/.X11-unix/X$n ] && mkdir /tmp/.uae-lock-$n 2>/dev/null; then DNUM=$n; break; fi
done
[ -z "$DNUM" ] && { echo "no free display"; exit 1; }
DISP=:$DNUM
Xvfb $DISP -screen 0 1024x768x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 1
export DISPLAY=$DISP
export SDL_AUDIODRIVER=dummy
export LP_NUM_THREADS=1
BASE=$(mktemp -d /tmp/fsuae-base.XXXXXX)
fs-uae --base_dir="$BASE" \
  --amiga_model=A1200 \
  --hard_drive_0="$HD" --hard_drive_0_label=SYS \
  --fullscreen=0 --window_width=752 --window_height=572 \
  --video_sync=0 --automatic_input_grab=0 --initial_input_grab=0 \
  --floppy_drive_volume=0 \
  "$@" > "$BASE/stdout.txt" 2>&1 &
UAE_PID=$!
T=0; SHOT_T=""; DONE_SHOT=0; DELAY=${UAE_SHOT_DELAY:-3}; MARK_T=""
while [ $T -lt "$SECS" ]; do
  sleep 1; T=$((T+1))
  if [ -n "$UAE_SHOT_WHEN" ] && [ -z "$MARK_T" ] && [ -e "$UAE_SHOT_WHEN" ]; then MARK_T=$T; SHOT_T=$((T+DELAY)); fi
  if [ -n "$UAE_SHOT_AT" ] && [ -z "$SHOT_T" ]; then SHOT_T=$UAE_SHOT_AT; fi
  if [ -n "$MARK_T" ] && [ -n "$UAE_KEYS" ]; then
    for kv in $UAE_KEYS; do
      kt=${kv%%:*}; kk=${kv#*:}
      if [ $((MARK_T+kt)) -eq $T ]; then xdotool mousemove 512 384 2>/dev/null; WID=$(xdotool search --onlyvisible --name "FS-UAE" 2>/dev/null | head -1); [ -n "$WID" ] && xdotool windowfocus $WID 2>/dev/null; xdotool keydown $kk; sleep 0.4; xdotool keyup $kk; fi
    done
  fi
  if [ -n "$SHOT_T" ] && [ $DONE_SHOT -eq 0 ] && [ $T -ge $SHOT_T ] && [ "$SHOT" != "-" ]; then
    import -display $DISP -window root "$SHOT" 2>/dev/null; DONE_SHOT=1
    [ -n "$UAE_STOP_AFTER_SHOT" ] && break
  fi
  if [ -n "$UAE_DONE" ] && [ -e "$UAE_DONE" ]; then sleep 1; break; fi
  kill -0 $UAE_PID 2>/dev/null || break
done
if [ $DONE_SHOT -eq 0 ] && [ -n "$SHOT" ] && [ "$SHOT" != "-" ]; then
  import -display $DISP -window root "$SHOT" 2>/dev/null || echo "screenshot failed"
fi
kill $UAE_PID 2>/dev/null; sleep 1; kill -9 $UAE_PID 2>/dev/null
kill $XVFB_PID 2>/dev/null
if [ -n "$UAE_LOG" ]; then cat "$BASE/stdout.txt" "$BASE/Cache/Logs/fs-uae.log.txt" > "$UAE_LOG" 2>/dev/null; fi
rm -rf "$BASE"; rmdir /tmp/.uae-lock-$DNUM 2>/dev/null
exit 0
