#!/usr/bin/env bash
# Fetch the Limine bootloader binaries for a given release tag, build the
# portable `limine` host tool, and expose the files needed for a hybrid
# (BIOS + UEFI) ISO boot.
#
# Usage: scripts/fetch-limine.sh <release-tag> <dest-dir>
set -euo pipefail

RELEASE="$1"
DEST="$2"

URL="https://github.com/Limine-Bootloader/Limine/releases/download/${RELEASE}/limine-binary.zip"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "  LIMINE fetch ${RELEASE}"
curl -fsSL -o "$TMP/limine-binary.zip" "$URL"
unzip -qq -o "$TMP/limine-binary.zip" -d "$TMP/x"

SRC="$(find "$TMP/x" -maxdepth 2 -type d -name 'limine-binary' | head -n1)"
test -n "$SRC" || { echo "ERROR: limine-binary dir not found in archive" >&2; exit 1; }

mkdir -p "$DEST"
# The directory that contains the binaries and the buildable host tool.
rm -rf "$DEST/limine-binary"
cp -r "$SRC" "$DEST/limine-binary"

# Surface the files the ISO target copies into the image root.
cp "$DEST/limine-binary/limine-bios-cd.bin" "$DEST/"
cp "$DEST/limine-binary/limine-uefi-cd.bin" "$DEST/"
cp "$DEST/limine-binary/limine-bios.sys"   "$DEST/"
cp "$DEST/limine-binary/BOOTX64.EFI"       "$DEST/"

# Build the portable host tool (limine.c + Makefile) for bios-install.
echo "  LIMINE build host tool"
make -C "$DEST/limine-binary" limine >/dev/null
cp "$DEST/limine-binary/limine" "$DEST/"

test -x "$DEST/limine" && echo "  LIMINE fetched + tool built -> $DEST"