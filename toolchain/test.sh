#!/usr/bin/env bash
# Smoke-test the installed cross toolchain (no full OS build):
# compilers exist, freestanding objects compile with kernel and userland
# flags, and a userland static link works.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/toolchain/out/bin"
CC="$BIN/x86_64-elf-gcc"
LD="$BIN/x86_64-elf-ld"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

[ -x "$CC" ] || { echo "FAIL: $CC missing (run toolchain/build.sh)"; exit 1; }
[ -x "$LD" ] || { echo "FAIL: $LD missing"; exit 1; }
"$CC" --version | head -n 1

KFLAGS="-c -O2 -ffreestanding -nostdlib -fno-pic -fno-pie \
  -mno-red-zone -mno-sse -mno-mmx -fno-builtin -mgeneral-regs-only \
  -mcmodel=kernel -Wall -Werror"
UFLAGS="-c -O2 -ffreestanding -nostdlib -fno-pic -fno-pie \
  -fno-builtin -mgeneral-regs-only -Wall -Werror"

cat > "$TMP/k.c" <<'EOF'
__attribute__((section(".text")))
long kadd(long a, long b) { return a + b; }
EOF
cat > "$TMP/u.c" <<'EOF'
long uadd(long a, long b) { return a + b; }
int main(void) { return (int)uadd(40, 2); }
EOF
# shellcheck disable=SC2086
$CC $KFLAGS -o "$TMP/k.o" "$TMP/k.c"
# shellcheck disable=SC2086
$CC $UFLAGS -o "$TMP/u.o" "$TMP/u.c"
$LD -n -static -nostdlib -e main -o "$TMP/u.elf" "$TMP/u.o"
test -s "$TMP/u.elf"

echo "toolchain smoke test: PASS"
