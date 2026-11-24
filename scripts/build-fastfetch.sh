#!/usr/bin/env bash
# build-fastfetch.sh: build the upstream fastfetch port for NEWOS.
#
# fastfetch v2.72 requires C23, so the build uses the host GCC 14 (the musl
# cross GCC 11 used by nano/tcc is too old). The result is a static Linux
# ET_EXEC that runs through NEWOS's Linux-compatible SYSCALL entry — the same
# model as ports/busybox, ports/nano and ports/tcc. All optional native
# graphics/desktop libraries are disabled: NEWOS has an XRGB8888 framebuffer,
# not X11/Wayland/DRM/EGL.
#
# Usage:
#   bash scripts/build-fastfetch.sh [output]
#
# Requires a C23-capable host gcc (GCC 14+).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/ports/fastfetch"
OUT="${1:-$ROOT/ports/fastfetch/fastfetch}"
BUILD="$ROOT/ports/build/fastfetch-newos"

# Prefer the C23-capable musl cross toolchain (matches nano/tcc: a static
# musl ET_EXEC runs cleanly on NEWOS; static glibc does not). Fall back to
# the host gcc, which also supports C23 on modern distros.
CROSS="$ROOT/ports/build/x86-64--musl--bleeding-edge-2026.08-1"
if [ -x "$CROSS/bin/x86_64-buildroot-linux-musl-gcc" ]; then
    CC="$CROSS/bin/x86_64-buildroot-linux-musl-gcc"
    STRIP="$CROSS/bin/x86_64-buildroot-linux-musl-strip"
elif [ -x "$CROSS/bin/x86_64-linux-musl-gcc" ]; then
    CC="$CROSS/bin/x86_64-linux-musl-gcc"
    STRIP="$CROSS/bin/x86_64-linux-musl-strip"
else
    CC="${CC:-gcc}"
    STRIP="${STRIP:-strip}"
fi

command -v "$CC" >/dev/null || { echo "build-fastfetch: missing compiler $CC" >&2; exit 1; }
[ -d "$SRC/src" ] || { echo "build-fastfetch: missing source tree $SRC" >&2; exit 1; }

mkdir -p "$BUILD" "$(dirname "$OUT")"

# Static binary, every optional native library OFF. BINARY_LINK_TYPE=static
# compiles out dlopen (FF_DISABLE_DLOPEN), so no shared objects are needed at
# runtime. ENABLE_LIBZFS is the exact option name in this release.
cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static" \
    -DBINARY_LINK_TYPE=static \
    -DBUILD_FLASHFETCH=OFF \
    -DENABLE_VULKAN=OFF -DENABLE_WAYLAND=OFF -DENABLE_XCB=OFF \
    -DENABLE_XCB_RANDR=OFF -DENABLE_XRANDR=OFF -DENABLE_X11=OFF \
    -DENABLE_DRM=OFF -DENABLE_VADRM=OFF -DENABLE_VAX11=OFF \
    -DENABLE_VDPAU=OFF -DENABLE_GIO=OFF -DENABLE_DCONF=OFF \
    -DENABLE_EET=OFF -DENABLE_DBUS=OFF -DENABLE_SQLITE3=OFF \
    -DENABLE_RPM=OFF -DENABLE_IMAGEMAGICK6=OFF -DENABLE_IMAGEMAGICK7=OFF \
    -DENABLE_CHAFA=OFF -DENABLE_EGL=OFF -DENABLE_GLX=OFF \
    -DENABLE_OPENCL=OFF -DENABLE_FREETYPE=OFF -DENABLE_ELF=OFF \
    -DENABLE_LUA=OFF -DENABLE_ZLIB=OFF -DENABLE_LIBZFS=OFF \
    -DENABLE_THREADS=OFF -DENABLE_IMAGE_LOGO=OFF -DENABLE_PCIIDS=OFF \
    -DENABLE_AMDGPUIDS=OFF -DENABLE_WORDEXP=OFF -DENABLE_SYSTEM_YYJSON=OFF

cmake --build "$BUILD" -j"$(nproc)"

cp "$BUILD/fastfetch" "$OUT"
"$STRIP" --strip-all "$OUT"
echo "build-fastfetch: $OUT ($(stat -c %s "$OUT") bytes)"
