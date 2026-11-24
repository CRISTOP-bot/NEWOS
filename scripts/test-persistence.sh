#!/usr/bin/env bash
# test-persistence.sh: prove v0.3 persistence — install a .new package,
# reboot the machine with the same disk image, and verify the package is
# still installed from the FAT32 database on /fat/NEWPKG.
#
# Usage:
#   bash scripts/test-persistence.sh [kernel.elf] [pkg.new]
#
# Requires the musl-built hello-new package (make check-newpkg builds it).
# The disk image is FAT32-formatted in a temp dir and reused across both
# QEMU boots so a real power-cycle is simulated.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${1:-$ROOT/build/images/newos-x86_64.elf}"
PKG="${2:-$ROOT/build/packages/hello-new-1.0.0-x86_64.new}"
QEMU="${QEMU:-qemu-system-x86_64}"
DISK="$(mktemp /tmp/newos-persist-XXXXXX.img)"

cleanup() { rm -f "$DISK"; pkill -9 qemu-system-x86_64 2>/dev/null || true; }
trap cleanup EXIT

# 64 MiB FAT32 (fat32_parse_geometry needs >= 65525 clusters).
dd if=/dev/zero of="$DISK" bs=1M count=64 2>/dev/null
MKFS="$(command -v mkfs.fat || command -v /sbin/mkfs.fat || \
        command -v mkfs.vfat || command -v /sbin/mkfs.vfat)"
"$MKFS" -F 32 -s 1 "$DISK" > /dev/null 2>&1

[ -f "$KERNEL" ] || { echo "no kernel at $KERNEL" >&2; exit 1; }
[ -f "$PKG" ] || { echo "no package at $PKG (run make check-newpkg)" >&2; exit 1; }

# Copy the package into the guest via a temp file that the embedded initramfs
# already ships: /tmp/hello-new-1.0.0-x86_64.new is present in the image.
# We install it by name from the embedded repo instead (offline friendly).

run_guest() {
    local cmds="$1" log="$2"
    timeout 150 python3 "$ROOT/scripts/qemu-shell.py" "$KERNEL" "$cmds" \
        --mem 256 -drive "file=$DISK,format=raw,if=ide" \
        > "$log" 2>/dev/null || true
}

LOG1="$(mktemp)"; LOG2="$(mktemp)"
trap 'rm -f "$DISK" "$LOG1" "$LOG2"; pkill -9 qemu-system-x86_64 2>/dev/null || true' EXIT

cat > /tmp/newos-persist-1.txt <<'EOF'
echo PERSIST-START
newpkg install /tmp/hello-new-1.0.0-x86_64.new
newpkg list
echo PERSIST-END
EOF
cat > /tmp/newos-persist-2.txt <<'EOF'
echo REBOOT-START
newpkg list
newpkg info hello-new
echo REBOOT-END
EOF

run_guest /tmp/newos-persist-1.txt "$LOG1"
grep -q "install: hello-new 1.0.0: OK" "$LOG1" || {
    echo "FAIL: package did not install in first boot"; tail -10 "$LOG1"; exit 1; }

run_guest /tmp/newos-persist-2.txt "$LOG2"
grep -q "hello-new 1.0.0" "$LOG2" || {
    echo "FAIL: package gone after reboot"; tail -10 "$LOG2"; exit 1; }

echo "persistence: PASS (hello-new survived a full reboot on FAT32)"
