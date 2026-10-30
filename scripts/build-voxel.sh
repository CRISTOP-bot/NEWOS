#!/usr/bin/env bash
# Build the VOXEL classic-desktop demo for NEWOS with the host C compiler.
#
# VOXEL upstream builds through toolchain/newos-gcc, which needs the full
# x86_64-elf cross toolchain. NEWOS itself builds userland with the host
# compiler against the generated sysroot (crt0.o, app.ld, libc.a, libnsh.a),
# so the same recipe is enough here and keeps a plain `make fast` offline.
#
# Usage: scripts/build-voxel.sh [output-elf]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/ports/voxel"
SDK="$ROOT/sysroot"
OUT="${1:-$ROOT/build/voxel.elf}"
CC="${CC:-gcc}"

case "$OUT" in
    /*) ;;
    *) OUT="$PWD/$OUT" ;;
esac

for f in "$SDK/usr/lib/crt0.o" "$SDK/usr/lib/app.ld" "$SDK/usr/lib/libnsh.a" "$SDK/usr/lib/libc.a"; do
    if [ ! -f "$f" ]; then
        echo "build-voxel: missing $f; run 'make sdk' first" >&2
        exit 1
    fi
done

if [ ! -f "$SRC/demo/voxel_demo.c" ]; then
    echo "build-voxel: $SRC is missing; run 'git submodule update --init ports'" >&2
    exit 1
fi

CORE_SRCS="src/graphics/surface.c src/graphics/compositor.c \
src/theme/classic.c src/graphics/bmp.c"
APP_SRCS="apps/about/about.c apps/explorer/explorer.c apps/settings/settings.c \
apps/run/run.c apps/terminal/term.c apps/helloworld/helloworld.c \
apps/editor/editor.c apps/imageview/imageview.c apps/common/app_registry.c \
apps/common/virtual_keyboard.c"
BACKEND_SRCS="src/platform/newos/framebuffer.c src/platform/newos/input.c \
src/platform/newos/syscall.c src/platform/newos/apps.c"

mkdir -p "$(dirname "$OUT")"
cd "$SRC"

# -mgeneral-regs-only: VOXEL is integer-only; NEWOS userland forbids SSE.
# -Werror: the vendored copy carries patches/0001, the minimum needed for
# current GCC to accept the tree; keep it warning-clean from here on.
# The RWX LOAD warning comes from upstream's app.ld layout (code+data in one
# segment) and is expected; do not turn it into an error.
"$CC" -std=gnu11 -O2 -ffreestanding -fno-pic -fno-pie \
    -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables \
    -fno-unwind-tables -mgeneral-regs-only \
    -Wall -Wextra -Werror -Iinclude \
    -nostdlib -static -no-pie \
    -Wl,-T,"$SDK/usr/lib/app.ld" -Wl,--build-id=none -Wl,-e,_start \
    -o "$OUT" \
    demo/voxel_demo.c $CORE_SRCS $APP_SRCS $BACKEND_SRCS \
    "$SDK/usr/lib/crt0.o" \
    -L"$SDK/usr/lib" -Wl,--start-group -lnsh -lc -Wl,--end-group -lgcc

echo "build-voxel: wrote $OUT ($(stat -c %s "$OUT") bytes)"
