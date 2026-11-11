#!/bin/bash
# Usage: tools/setup.sh
# Installs what tools/build.sh and tools/bench.sh need on Ubuntu 24.04: the FS-UAE emulator, a virtual
# display, ImageMagick, and the amigadev/crosstools compiler image. Safe to run more than once.
set -e
SUDO=""; [ "$(id -u)" != 0 ] && SUDO="sudo"
NEED=""
for p in fs-uae xvfb imagemagick xdotool libgl1-mesa-dri; do
  dpkg -s "$p" >/dev/null 2>&1 || NEED="$NEED $p"
done
if [ -n "$NEED" ]; then
  $SUDO apt-get update -q
  $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -q $NEED
fi
if ! docker info >/dev/null 2>&1; then                 # cloud sessions: the Docker daemon may not be running yet
  $SUDO sh -c 'nohup dockerd >/tmp/dockerd.log 2>&1 &'
  for i in $(seq 1 30); do docker info >/dev/null 2>&1 && break; sleep 1; done
fi
docker info >/dev/null 2>&1 || { echo "setup: Docker is not available. Install Docker, or start its daemon, then run this again."; exit 1; }
docker image inspect amigadev/crosstools:m68k-amigaos >/dev/null 2>&1 || docker pull amigadev/crosstools:m68k-amigaos
echo "setup: ready ($(fs-uae --version 2>/dev/null | head -1), $(docker run --rm amigadev/crosstools:m68k-amigaos m68k-amigaos-gcc --version | head -1))"
