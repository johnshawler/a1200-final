#!/bin/bash
# Usage: tools/setup.sh
# Installs what tools/build.sh and tools/bench.sh need on Ubuntu 24.04: the FS-UAE emulator, a virtual
# display, ImageMagick, and the amigadev/crosstools compiler image. Safe to run more than once.
# If it fails, the fault is in this machine or its network, never in a port: run it again.
set -e
SUDO=""; [ "$(id -u)" != 0 ] && SUDO="sudo"
IMAGE=amigadev/crosstools:m68k-amigaos
FALLBACK=http://mirrors.edge.kernel.org/ubuntu      # a full Ubuntu mirror that is not under ubuntu.com
T_INDEX=300; T_FILES=1800; T_IMAGE=1800             # seconds allowed: package index, packages, compiler image
if [ -n "$CI" ]; then T_INDEX=60; T_FILES=150; T_IMAGE=300; fi      # a CI machine is fast: give up early

fail() {
  echo "setup: FAILED: $1"
  for h in archive.ubuntu.com mirrors.edge.kernel.org registry-1.docker.io github.com; do
    if timeout 10 getent hosts "$h" >/dev/null 2>&1; then echo "setup:   $h resolves"; else echo "setup:   $h DOES NOT RESOLVE"; fi
  done
  echo "setup: the fault is in this machine or its network, not in the port. Run tools/setup.sh again; in CI, re-run the job."
  exit 1
}

# Every download has a time limit and a second try. A package mirror that stops answering otherwise stalls
# apt for as long as the job lasts: it happens on GitHub's hosted runners (actions/runner-images issue 15040).
APT_OPTS=""; LIST=""
trap '[ -z "$LIST" ] || rm -f "$LIST"' EXIT           # the second try's temporary source list
apt_get() { $SUDO env DEBIAN_FRONTEND=noninteractive timeout "$1" apt-get -q $APT_OPTS "${@:2}"; }
download() {                                        # "first": no index, no waiting for packages: go to the second try
  apt_get $T_INDEX update || { [ "$1" != first ] || return 1; }
  apt_get $T_FILES install -y --download-only $NEED
}

NEED=""
for p in fs-uae xvfb imagemagick xdotool libgl1-mesa-dri; do
  dpkg -s "$p" >/dev/null 2>&1 || NEED="$NEED $p"
done
if [ -n "$NEED" ]; then
  echo "setup: installing$NEED"
  if ! download first; then
    echo "setup: the package servers did not deliver. Trying once more."
    sleep 20
    CODENAME=$(. /etc/os-release; [ "$ID" = ubuntu ] && echo "$VERSION_CODENAME" || true)
    if [ -n "$CODENAME" ] && [ "$(dpkg --print-architecture)" = amd64 ]; then
      echo "setup: asking $FALLBACK this time, not this machine's own mirrors"
      LIST=$(mktemp)
      for s in "" -updates -security; do
        echo "deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] $FALLBACK $CODENAME$s main universe"
      done > "$LIST"
      APT_OPTS="-o Dir::Etc::SourceList=$LIST -o Dir::Etc::SourceParts=- -o APT::Get::List-Cleanup=0"
    fi
    download || fail "could not download$NEED"
  fi
  $SUDO env DEBIAN_FRONTEND=noninteractive apt-get -q $APT_OPTS install -y $NEED
fi

if ! docker info >/dev/null 2>&1; then              # cloud sessions: the Docker daemon may not be running yet
  $SUDO sh -c 'nohup dockerd >/tmp/dockerd.log 2>&1 &'
  for i in $(seq 1 30); do docker info >/dev/null 2>&1 && break; sleep 1; done
fi
docker info >/dev/null 2>&1 || fail "Docker is not available. Install Docker, or start its daemon"
if ! docker image inspect $IMAGE >/dev/null 2>&1; then
  echo "setup: pulling $IMAGE"
  timeout $T_IMAGE docker pull $IMAGE || { sleep 20; timeout $T_IMAGE docker pull $IMAGE; } || fail "could not pull $IMAGE from Docker Hub"
fi
echo "setup: ready ($(fs-uae --version 2>/dev/null | head -1), $(docker run --rm $IMAGE m68k-amigaos-gcc --version | head -1))"
