#!/bin/bash
# Usage: tools/check_kit.sh
# Fails if any kit file differs from the released kit (KIT_VERSION). Ports must not edit the kit:
# copy kit/main.c to src/main.c and change the copy.
cd "$(dirname "$0")/.."
if sha256sum --quiet -c tools/kit.sha256; then echo "kit: unchanged (version $(cat KIT_VERSION))"; else echo "kit: CHANGED - restore the files listed above"; exit 1; fi
