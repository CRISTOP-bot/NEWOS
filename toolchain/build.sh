#!/usr/bin/env bash
# Build the NEWOS cross toolchain: binutils + GCC (C only) + newlib for
# x86_64-elf.
#
# The OS builds fine with the host gcc/ld, but a pinned cross toolchain
# removes host-version drift (the kernel relies on -mcmodel=kernel and
# freestanding codegen staying stable). The result lives in
# toolchain/out/ (git-ignored) and is used opt-in:
#
#   toolchain/build.sh            # one-time, ~20-40 min on 2 cores
#   export PATH="$PWD/toolchain/out/bin:$PATH"
#   make CROSS_PREFIX=x86_64-elf- all
#
# The newlib stage is what lets real upstream C software be compiled for
# NEWOS: without a target libc the cross gcc is freestanding-only (even
# #include <stdio.h> fails). newlib is configured WITHOUT its own syscall
# stubs -- NEWOS supplies them against the int $0x80 ABI in its own
# support library, see libc/ + syscall/.
#
# Usage: toolchain/build.sh [--jobs N]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$ROOT/toolchain/out"
SRC="$PREFIX/src"
LOG="$PREFIX/build.log"
TARGET="x86_64-elf"
BINUTILS_VER="2.44"
# gcc 16.x: 14.x/15.1 sources miscompile under a gcc-16 host (libcody
# char8_t errors, no --disable-libcody switch); 16.1 matches the host
# generation and builds cleanly there.
GCC_VER="16.1.0"
# newlib is a datestamped release train (upstream only ships 4.6.0.20260123,
# not a bare 4.6.0); the .0 suffix of the date is part of the version string
# and of the extracted directory name.
NEWLIB_VER="4.6.0.20260123"
JOBS="$(nproc)"
mkdir -p "$SRC"

while [ $# -gt 0 ]; do
    case "$1" in
        --jobs) JOBS="$2"; shift 2 ;;
        *) echo "usage: $0 [--jobs N]" >&2; exit 1 ;;
    esac
done

MIRRORS="https://ftp.gnu.org/gnu https://mirrors.edge.kernel.org/gnu"
# newlib is not GNU: it is published on SourceWare only (no ftp.gnu.org
# copy, and the kernel.org GNU mirror does not carry it either). The
# second entry is the same archive without the www hop, which has been
# observed to fail on some networks.
NEWLIB_MIRRORS="https://www.sourceware.org/pub/newlib https://sourceware.org/pub/newlib"

fetch() { # $1 = subpath, $2 = dest file, $3 = mirror list (optional)
    local sub="$1" dest="$2" base
    local mirrors="${3:-$MIRRORS}"
    base="$(basename "$sub")"
    if [ -f "$dest" ]; then
        echo "cached $base"
        return 0
    fi
    for m in $mirrors; do
        echo "fetch $m/$sub"
        if curl -fsSL --retry 3 -o "$dest.tmp" "$m/$sub"; then
            mv "$dest.tmp" "$dest"
            return 0
        fi
        rm -f "$dest.tmp"
    done
    echo "ERROR: could not download $sub from any mirror" >&2
    return 1
}

check_archive() { # $1 = archive: refuse to extract a truncated download
    local a="$1"
    case "$a" in
        *.xz) xz -t "$a" 2>/dev/null ;;
        *.gz) gzip -t "$a" 2>/dev/null ;;
        *) echo "ERROR: unknown archive type $a" >&2; return 1 ;;
    esac || { echo "ERROR: corrupt archive $a, delete it and retry" >&2; exit 1; }
}

# Installed-tool markers, used to make every stage resumable.
have_binutils() { [ -x "$PREFIX/bin/$TARGET-ld" ]; }
have_gcc()      { [ -x "$PREFIX/bin/$TARGET-gcc" ]; }
have_newlib()   { [ -f "$PREFIX/$TARGET/lib/libc.a" ] && \
                  [ -f "$PREFIX/$TARGET/include/stdio.h" ]; }

{
echo "=== NEWOS toolchain build: $TARGET (binutils $BINUTILS_VER, gcc $GCC_VER, newlib $NEWLIB_VER)"
echo "=== prefix: $PREFIX jobs: $JOBS"

BINUTILS_TBZ="$SRC/binutils-$BINUTILS_VER.tar.xz"
GCC_TBZ="$SRC/gcc-$GCC_VER.tar.xz"
NEWLIB_TGZ="$SRC/newlib-$NEWLIB_VER.tar.gz"

if have_binutils && have_gcc; then
    echo "=== binutils + gcc already installed, skipping"
    "$PREFIX/bin/$TARGET-gcc" --version | head -n 1
else
    fetch "binutils/binutils-$BINUTILS_VER.tar.xz" "$BINUTILS_TBZ"
    fetch "gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz" "$GCC_TBZ"

    # Always extract fresh: a run killed mid-tar leaves a partial tree that
    # a mere "configure exists?" check cannot detect.
    for a in "$BINUTILS_TBZ" "$GCC_TBZ"; do
        check_archive "$a"
    done
    rm -rf "$SRC/binutils-$BINUTILS_VER" "$SRC/gcc-$GCC_VER"
    tar -xf "$BINUTILS_TBZ" -C "$SRC"
    tar -xf "$GCC_TBZ" -C "$SRC"
    test -x "$SRC/binutils-$BINUTILS_VER/configure" || \
        { echo "ERROR: binutils sources missing" >&2; exit 1; }
    test -f "$SRC/gcc-$GCC_VER/move-if-change" || \
        { echo "ERROR: gcc sources incomplete" >&2; exit 1; }
    rm -rf "$SRC/build-binutils" "$SRC/build-gcc"
    mkdir -p "$SRC/build-binutils" "$SRC/build-gcc"

    if ! have_binutils; then
    echo "=== binutils"
    cd "$SRC/build-binutils"
    "$SRC/binutils-$BINUTILS_VER/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --disable-nls --disable-docs --disable-multilib \
        --disable-werror > /dev/null
    make -j"$JOBS" > /dev/null
    make install > /dev/null
    else
    echo "=== binutils already installed, skipping"
    fi
fi
export PATH="$PREFIX/bin:$PATH"

if ! have_gcc; then
    echo "=== gcc (c + target libgcc)"
    cd "$SRC/build-gcc"
    "$SRC/gcc-$GCC_VER/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --disable-nls --disable-docs --disable-multilib \
        --without-headers --with-newlib \
        --enable-languages=c > /dev/null
    make -j"$JOBS" all-gcc > /dev/null
    make -j"$JOBS" all-target-libgcc > /dev/null
    make install-gcc > /dev/null
    make install-target-libgcc > /dev/null
else
    echo "=== gcc already installed, skipping"
fi

if have_newlib; then
    echo "=== newlib $NEWLIB_VER already installed, skipping"
else
    # --disable-newlib-supplied-syscalls: newlib's own syscall layer must not
    #   be baked into libc.a, NEWOS provides it itself through the int $0x80
    #   ABI. With this flag libc.a/libm.a end up referencing the plain POSIX
    #   set (read, write, open, close, lseek, fstat, stat, isatty, kill,
    #   getpid, sbrk, link, unlink, fork, wait, times, gettimeofday,
    #   getentropy, execve and _exit -- only _exit keeps its underscore),
    #   which is exactly what the NEWOS stub library has to define. libgloss
    #   still builds libnosys.a as a separate archive; it is never on the
    #   default link line.
    # --disable-newlib-multithread: no userland threads in NEWOS, so the
    #   reentrant-locking machinery (and its size) is dead weight. Note the
    #   upstream spelling is "multithread", not "multithreading".
    # --enable-newlib-mb: compiles the Unicode width tables in, otherwise
    #   wcwidth() degenerates to 1/0/-1 and never reports CJK double width.
    # --disable-multilib: matches the binutils/gcc stages; a multilib newlib
    #   would also build a 32-bit variant this compiler has no libs for.
    echo "=== newlib $NEWLIB_VER (libc + libm for $TARGET)"
    fetch "newlib-$NEWLIB_VER.tar.gz" "$NEWLIB_TGZ" "$NEWLIB_MIRRORS"
    check_archive "$NEWLIB_TGZ"
    rm -rf "$SRC/newlib-$NEWLIB_VER"
    tar -xf "$NEWLIB_TGZ" -C "$SRC"
    test -x "$SRC/newlib-$NEWLIB_VER/configure" || \
        { echo "ERROR: newlib sources missing" >&2; exit 1; }
    rm -rf "$SRC/build-newlib"
    mkdir -p "$SRC/build-newlib"
    cd "$SRC/build-newlib"
    "$SRC/newlib-$NEWLIB_VER/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --disable-nls --disable-multilib \
        --disable-newlib-supplied-syscalls \
        --disable-newlib-multithread \
        --enable-newlib-mb > /dev/null
    make -j"$JOBS" > /dev/null
    make install > /dev/null
fi

echo "=== verify"
"$PREFIX/bin/$TARGET-gcc" --version | head -n 1
echo 'int main(void){return 0;}' > "$SRC/smoke.c"
"$PREFIX/bin/$TARGET-gcc" -ffreestanding -nostdlib -c \
    "$SRC/smoke.c" -o "$SRC/smoke.o"
echo '#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
int main(void){char b[64];snprintf(b,sizeof b,"%8.3f %s",(double)3.0/2.0,"x");return b[0];}' \
    > "$SRC/smoke-libc.c"
# -nostartfiles: no crt0 for x86_64-elf (libgloss has no x86_64 BSP), NEWOS
# supplies its own. -specs=nosys.specs (installed by libgloss next to
# libnosys.a) only stands in for the NEWOS syscall stubs so the link resolves
# -- libnosys is never part of the default link line.
"$PREFIX/bin/$TARGET-gcc" -static -nostartfiles -Wl,-e,main \
    -specs=nosys.specs -D_XOPEN_SOURCE=700 \
    -o "$SRC/smoke-libc.elf" "$SRC/smoke-libc.c" > /dev/null 2>&1 \
    || { echo "ERROR: libc link test failed (newlib not usable)" >&2; exit 1; }
echo "libc smoke link ok: $SRC/smoke-libc.elf"
"$PREFIX/bin/$TARGET-ld" --version | head -n 1
echo "=== toolchain ready: $PREFIX/bin/$TARGET-{gcc,ld} + newlib $NEWLIB_VER"
} 2>&1 | tee "$LOG"
