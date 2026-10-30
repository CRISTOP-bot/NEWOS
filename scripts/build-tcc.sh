#!/usr/bin/env bash
# build-tcc.sh: build the NEWOS tcc port (ports/tcc) on the host.
#
# Why on the host: NEWOS has no on-board toolchain and its own libc is a
# deliberately small freestanding subset. TCC is built here as a static
# x86_64-linux-musl ET_EXEC (no PT_INTERP) so the kernel ELF loader can map
# it; it then runs in ring 3 through the Linux-compatible SYSCALL entry,
# exactly like the checked-in BusyBox. The resulting binary is committed so
# a normal `make` never needs network access or a cross compiler.
#
# Usage:
#   bash scripts/build-tcc.sh [output]
#
# Toolchain: MUSL_CROSS must point at a x86_64-linux-musl cross prefix
# (obtained from https://musl.cc/x86_64-linux-musl-cross.tgz and unpacked
# under ports/build/x86_64-linux-musl-cross). Nothing is installed on the
# host and no root is required.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/ports/tcc/src"
OUT="${1:-$ROOT/ports/tcc/tcc}"
CROSS="${MUSL_CROSS:-$ROOT/ports/build/x86_64-linux-musl-cross}"
BUILD="$ROOT/ports/build/tcc-newos"
CC="$CROSS/bin/x86_64-linux-musl-gcc"
STRIP="$CROSS/bin/x86_64-linux-musl-strip"

if [ ! -x "$CC" ]; then
    echo "build-tcc: missing musl cross compiler at $CC" >&2
    echo "build-tcc: set MUSL_CROSS=<prefix> or unpack musl.cc under" >&2
    echo "build-tcc: ports/build/x86_64-linux-musl-cross" >&2
    exit 1
fi
export PATH="$CROSS/bin:$PATH"
[ -d "$SRC" ] || { echo "build-tcc: missing source tree $SRC" >&2; exit 1; }

rm -rf "$BUILD"
mkdir -p "$BUILD"
cp -a "$SRC"/. "$BUILD"/
cd "$BUILD"

# Static, non-PIE, no host libc, installed paths matching the NEWOS root
# filesystem. -no-pie is required: the kernel loader maps ET_EXEC only and
# rejects ET_DYN/PIE images. --tccdir is where the program looks for its
# headers/libtcc1.a at run time; the image ships the NEWOS SDK under
# /usr/include and /usr/lib instead.
./configure \
    --cc="$CC" \
    --extra-cflags="-static -O2 -no-pie -pipe" \
    --extra-ldflags="-static -no-pie" \
    --prefix=/usr \
    --tccdir=/usr/lib/tcc \
    --config-musl \
    > /tmp/newos-tcc-configure.log 2>&1 \
    || { echo "--- configure tail ---"; tail -20 /tmp/newos-tcc-configure.log; exit 1; }

# --config-musl drops lib/bcheck.c (glibc __malloc_hook), so the default
# target also produces libtcc1.a: the runtime helpers TCC-generated code
# needs (64-bit division, varargs register save). It is shipped next to the
# compiler at /usr/lib/tcc/libtcc1.a by the SDK package.
make -j"$(nproc)" > /tmp/newos-tcc-make.log 2>&1 \
    || { echo "--- make tail ---"; tail -30 /tmp/newos-tcc-make.log; exit 1; }

"$STRIP" -s tcc
cp tcc "$OUT"
cp libtcc1.a "$ROOT/ports/tcc/libtcc1.a"
chmod 0755 "$OUT"

file "$OUT" 2>/dev/null || true
echo "build-tcc: wrote $OUT ($(stat -c %s "$OUT") bytes)"
echo "build-tcc: wrote ports/tcc/libtcc1.a ($(stat -c %s "$ROOT/ports/tcc/libtcc1.a") bytes)"
