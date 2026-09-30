#!/usr/bin/env bash
# build-gnu.sh: host builder for the GNU tools NEWOS runs as /bin programs.
#
# Why on the host: NEWOS carries no C toolchain on board and its libc is a
# deliberately small subset (no gnulib, no locale, no wide chars), so
# upstream GNU sources cannot compile against it.
# Why musl: musl emits Linux syscall numbers, which is exactly the numbering
# NEWOS's ABI follows (include/abi/syscall_abi.h), and `-static` yields the
# plain ET_EXEC image process/elf_loader.c maps. The result is genuine GNU
# programs, not reimplementations.
#
# Usage:
#   bash scripts/build-gnu.sh stage <dir>   # build, then drop ELFs into <dir>
#   bash scripts/build-gnu.sh list          # print the curated tool list
#
# Artifacts cache under ports/ (tarballs in ports/distfiles, build tree in
# ports/build, staged ELFs in ports/stage), so re-running is cheap.
# Requires docker or podman (override with GNU_CONTAINER=...).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORTS="$ROOT/ports"
DIST="$PORTS/distfiles"
BUILD="$PORTS/build"
STAGE="$PORTS/stage"

COREUTILS_VER=9.7
COREUTILS_URL="https://ftp.gnu.org/gnu/coreutils/coreutils-${COREUTILS_VER}.tar.xz"
COREUTILS_SUM="e8bb26ad0293f9b5a1fc43fb42ba970e312c66ce92c1b0b16713d7500db251bf"

# Curated subset: tools that only need file I/O, directory traversal and
# clocks. Anything that forks or execs (env, nice, nohup, stdbuf, chroot,
# timeout) is excluded until the kernel implements SYS_FORK/SYS_EXECVE.
TOOLS='arch base64 basename cat chcon cksum comm cp cut date df du echo
expr false fmt fold groups head id link ln ls md5sum mkdir mkfifo mknod
mv nl nproc od paste pr printenv printf ptx pwd readlink realpath relpath
rm rmdir seq sha1sum sha224sum sha256sum sha384sum sha512sum shred shuf
sleep sort split stat sum sync tac tail tr true truncate tsort uname
unexpand uniq unlink users wc whoami yes'

cmd="${1:-stage}"
if [ "$cmd" = list ]; then
  printf '%s\n' $TOOLS
  exit 0
fi
stage_dir="${2:-$ROOT/build/gnu}"

mkdir -p "$DIST" "$BUILD" "$STAGE" "$stage_dir"

if [ ! -f "$DIST/coreutils-${COREUTILS_VER}.tar.xz" ]; then
  echo "build-gnu: downloading coreutils ${COREUTILS_VER} from ftp.gnu.org"
  curl -fsSL -o "$DIST/coreutils-${COREUTILS_VER}.tar.xz" "$COREUTILS_URL"
fi
echo "${COREUTILS_SUM}  $DIST/coreutils-${COREUTILS_VER}.tar.xz" \
  | sha256sum --check --status \
  || { echo "build-gnu: distfile checksum mismatch"; exit 1; }

printf '%s\n' $TOOLS > "$PORTS/tools.txt"

# Cache stamp: the container build is ~10 minutes, and nothing about it
# changes unless the coreutils version or the curated tool list does. GNU_REBUILD=1
# forces it.
stamp="$(sha256sum "$PORTS/tools.txt" | cut -d' ' -f1)-${COREUTILS_VER}"
if [ -f "$STAGE/.built" ] && [ "$(cat "$STAGE/.built")" = "$stamp" ] \
   && [ -z "${GNU_REBUILD:-}" ]; then
  cp -f "$STAGE"/* "$stage_dir"/ 2>/dev/null || true
  rm -f "$stage_dir"/*.new 2>/dev/null || true
  count=$(find "$stage_dir" -maxdepth 1 -type f ! -name '.built' | wc -l)
  echo "build-gnu: reusing cached stage ($count ELF(s) in $stage_dir)"
  exit 0
fi

"${GNU_CONTAINER:-docker}" run --rm \
  -e FORCE_UNSAFE_CONFIGURE=1 \
  -v "$ROOT":/src -w /src/ports/build \
  debian:trixie-slim /bin/sh -euc '
    apt-get update -qq >/dev/null 2>&1
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
      --no-install-recommends musl musl-dev musl-tools gcc make \
      patch diffutils xz-utils curl ca-certificates coreutils \
      >/dev/null
    rm -rf /src/ports/build/coreutils-*
    tar -xf /src/ports/distfiles/coreutils-9.7.tar.xz \
        -C /src/ports/build
    cd /src/ports/build/coreutils-9.7
    # Static musl: no NLS, no ACL/xattr/libcap/selinux (none exist here).
    ./configure CC=musl-gcc CFLAGS="-static -O2 -pipe" LDFLAGS="-static" \
      --disable-nls --without-acl --without-xattr --without-selinux \
      --build=x86_64-linux --host=x86_64-linux --prefix=/usr \
      > /tmp/cfg.log 2>&1 || { echo "--- configure tail ---"; tail -30 /tmp/cfg.log; exit 1; }
    make -j"$(nproc)" > /tmp/mk.log 2>&1 || { echo "--- make tail ---"; tail -40 /tmp/mk.log; exit 1; }
    mkdir -p /src/ports/stage
    kept=0
    while read -r t; do
      # `arch` and `relpath` have no binary of their own in coreutils; they
      # are install-time aliases of uname and realpath. NEWOS has no
      # symlinks, so copy the real thing under the alias name.
      case "$t" in
        arch)    src=uname    ;;
        relpath) src=realpath ;;
        *)       src=$t       ;;
      esac
      [ -x "src/$src" ] || continue
      cp "src/$src" "/src/ports/stage/$t"
      kept=$((kept+1))
    done < /src/ports/tools.txt
    # Line tables only; the package is embedded in the kernel image.
    strip --strip-all /src/ports/stage/* 2>/dev/null || true
    echo "build-gnu: built and staged $kept tool(s)"
  '

cp -f "$STAGE"/* "$stage_dir"/ 2>/dev/null || true
rm -f "$stage_dir"/*.new 2>/dev/null || true
printf '%s' "$stamp" > "$STAGE/.built"

count=$(find "$stage_dir" -maxdepth 1 -type f ! -name '*.elf' | wc -l)
echo "build-gnu: $count ELF(s) in $stage_dir"
[ "$count" -gt 0 ] || { echo "build-gnu: nothing staged"; exit 1; }
