#!/usr/bin/env bash
# build-nano.sh: build the NEWOS GNU nano port (ports/nano) on the host.
#
# Same model as ports/busybox and scripts/build-tcc.sh: nano is a static
# x86_64-linux-musl ET_EXEC that runs through the Linux-compatible SYSCALL
# entry. It links a static ncursesw whose fallback terminfo entries are
# compiled in, because NEWOS ships no terminfo database (`nano` itself falls
# back to TERM=linux when the shell passes no environment).
#
# Usage:
#   bash scripts/build-nano.sh [output]
#
# Requires MUSL_CROSS (see scripts/build-tcc.sh) and downloads the ncurses
# release tarball into ports/distfiles (git-ignored) on first use.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/ports/nano/src"
OUT="${1:-$ROOT/ports/nano/nano}"
CROSS="${MUSL_CROSS:-$ROOT/ports/build/x86_64-linux-musl-cross}"
BUILD="$ROOT/ports/build/nano-newos"
DIST="$ROOT/ports/distfiles"
PREFIX="$BUILD/ncurses-prefix"
CC="$CROSS/bin/x86_64-linux-musl-gcc"
STRIP="$CROSS/bin/x86_64-linux-musl-strip"
NCURSES_VER=6.5
NCURSES_URL="https://invisible-mirror.net/archives/ncurses/ncurses-${NCURSES_VER}.tar.gz"

if [ ! -x "$CC" ]; then
    echo "build-nano: missing musl cross compiler at $CC" >&2
    echo "build-nano: set MUSL_CROSS=<prefix> or unpack musl.cc under" >&2
    echo "build-nano: ports/build/x86_64-linux-musl-cross" >&2
    exit 1
fi
export PATH="$CROSS/bin:$PATH"
[ -d "$SRC" ] || { echo "build-nano: missing source tree $SRC" >&2; exit 1; }

mkdir -p "$DIST" "$BUILD"
if [ ! -f "$DIST/ncurses-${NCURSES_VER}.tar.gz" ]; then
    echo "build-nano: downloading ncurses ${NCURSES_VER}"
    curl -fsSL -o "$DIST/ncurses-${NCURSES_VER}.tar.gz" "$NCURSES_URL"
fi

# --- ncursesw, static, with compiled-in fallback entries --------------------
if [ ! -f "$PREFIX/lib/libncursesw.a" ]; then
    rm -rf "$BUILD/ncurses-${NCURSES_VER}"
    tar -xf "$DIST/ncurses-${NCURSES_VER}.tar.gz" -C "$BUILD"
    cd "$BUILD/ncurses-${NCURSES_VER}"
    PATH="$CROSS/bin:$PATH" CC="$CC" \
    ./configure \
        --host=x86_64-linux-musl \
        --with-build-cc=gcc \
        --without-shared --without-debug --without-ada \
        --without-manpages --without-tests \
        --enable-widec --enable-sigwinch \
        --with-fallbacks=linux,xterm,xterm-256color,vt220 \
        --prefix="$PREFIX" \
        > /tmp/newos-ncurses-configure.log 2>&1 \
        || { echo "--- ncurses configure tail ---"; tail -20 /tmp/newos-ncurses-configure.log; exit 1; }
    make -j"$(nproc)" > /tmp/newos-ncurses-make.log 2>&1 \
        || { echo "--- ncurses make tail ---"; tail -30 /tmp/newos-ncurses-make.log; exit 1; }
    make install > /tmp/newos-ncurses-install.log 2>&1 \
        || { echo "--- ncurses install tail ---"; tail -20 /tmp/newos-ncurses-install.log; exit 1; }
fi

# --- nano ------------------------------------------------------------------
rm -rf "$BUILD/nano-8.4"
cp -a "$SRC" "$BUILD/nano-8.4"
cd "$BUILD/nano-8.4"

# NEWOS patch 0001 trims doc/ and po/ out of configure.ac, so the vendored
# autotools inputs look stale and `make` (which configure itself invokes for
# `showinfo`) would try to re-run aclocal/automake/autoconf. Build hosts do
# not always carry the matching automake version, so refresh the generated
# files' timestamps in dependency order instead: aclocal.m4 after
# configure.ac, configure/config.h.in after aclocal.m4, and every Makefile.in
# after its Makefile.am. configure then writes fresh Makefiles from them.
touch aclocal.m4
touch configure config.h.in
find . -name 'Makefile.in' -exec touch {} +

PATH="$PREFIX/bin:$PATH" ./configure \
    --host=x86_64-linux-musl \
    --disable-nls \
    --prefix=/usr \
    CFLAGS="-static -O2 -pipe -fno-pie" \
    CPPFLAGS="-I$PREFIX/include -I$PREFIX/include/ncursesw" \
    LDFLAGS="-static -no-pie -L$PREFIX/lib" \
    > /tmp/newos-nano-configure.log 2>&1 \
    || { echo "--- nano configure tail ---"; tail -20 /tmp/newos-nano-configure.log; exit 1; }

make -j"$(nproc)" > /tmp/newos-nano-make.log 2>&1 \
    || { echo "--- nano make tail ---"; tail -30 /tmp/newos-nano-make.log; exit 1; }

"$STRIP" -s src/nano
cp src/nano "$OUT"
chmod 0755 "$OUT"

file "$OUT" 2>/dev/null || true
echo "build-nano: wrote $OUT ($(stat -c %s "$OUT") bytes)"
