#!/bin/bash
# Usage: tools/build.sh <output-exe> <source files: .c .s .asm ...>
# Run from the project directory; the output and all sources must be inside it (only this directory is
# visible to the compiler). incbin paths are looked up from the project directory first. Cross-compiles for a 68020 Amiga inside the amigadev/crosstools Docker image
# (m68k-amigaos-gcc 6.5.0b + vasm). First use:  docker pull amigadev/crosstools:m68k-amigaos
# Extra flags: CFLAGS=... ASFLAGS=...
OUT="$1"; shift
docker run --rm -v "$PWD":/work -w /work -e CFLAGS="$CFLAGS" -e ASFLAGS="$ASFLAGS" amigadev/crosstools:m68k-amigaos bash -c '
set -e
OUT="'"$OUT"'"; OBJS=""
mkdir -p "$(dirname "$OUT")" /tmp/obj
for f in '"$*"'; do
  o=/tmp/obj/$(echo "$f" | tr "/." "__").o
  case "$f" in
    *.c)       m68k-amigaos-gcc -m68020 -O2 -fomit-frame-pointer -noixemul -Wall $CFLAGS -c "$f" -o "$o" ;;
    *.s|*.asm) vasmm68k_mot -quiet -Fhunk -m68020 -I. -I"$(dirname "$f")" $ASFLAGS -o "$o" "$f" ;;
  esac
  OBJS="$OBJS $o"
done
m68k-amigaos-gcc -m68020 -noixemul $OBJS -o "$OUT"
m68k-amigaos-size "$OUT" | tail -1
'
