#!/usr/bin/env bash
# Fetch the public-domain 8x8 console font used by the framebuffer text
# console (fbcon). Cached under build/ so later builds work offline.
#
# Usage: scripts/fetch-font.sh <dest-file>
set -euo pipefail

DEST="$1"
URL="https://raw.githubusercontent.com/dhepper/font8x8/master/font8x8_basic.h"

if [ -f "$DEST" ]; then
    echo "  FONT cached $DEST"
    exit 0
fi

echo "  FONT fetch $URL"
mkdir -p "$(dirname "$DEST")"
curl -fsSL -o "$DEST.tmp" "$URL"
mv "$DEST.tmp" "$DEST"
echo "  FONT cached -> $DEST"
