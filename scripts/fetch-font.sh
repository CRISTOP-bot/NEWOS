#!/usr/bin/env bash
# Stage the vendored public-domain 8x8 console font used by fbcon.
#
# Usage: scripts/fetch-font.sh <dest-file>
set -euo pipefail

DEST="$1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE="$ROOT/bc/font8x8_basic.h"

if [ -f "$DEST" ]; then
    echo "  FONT cached $DEST"
    exit 0
fi

if [ ! -f "$SOURCE" ]; then
    echo "  FONT error: vendored source missing: $SOURCE" >&2
    exit 1
fi

echo "  FONT stage $SOURCE"
mkdir -p "$(dirname "$DEST")"
cp "$SOURCE" "$DEST.tmp"
mv "$DEST.tmp" "$DEST"
echo "  FONT cached -> $DEST"
