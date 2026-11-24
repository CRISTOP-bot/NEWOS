#!/usr/bin/env bash
# test-installer.sh: prove the guided installer + boot-time restore work.
#
#   bash scripts/test-installer.sh [kernel.elf]
#
# Boots the same FAT32 image twice: first runs `newinstall` with the embedded
# answer file, then reboots and checks that /sbin/sysrestore replayed the
# installed system (hostname, user, session, packages) before the shell.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${1:-$ROOT/build/images/newos-x86_64.elf}"
QEMU="${QEMU:-qemu-system-x86_64}"
DISK="$(mktemp /tmp/newos-installer-XXXXXX.img)"
LOG1="$(mktemp)"
LOG2="$(mktemp)"
CMD1="$(mktemp)"
CMD2="$(mktemp)"

cleanup() {
    rm -f "$DISK" "$LOG1" "$LOG2" "$CMD1" "$CMD2"
    pkill -9 qemu-system-x86_64 2>/dev/null || true
}
trap cleanup EXIT

dd if=/dev/zero of="$DISK" bs=1M count=64 2>/dev/null
MKFS="$(command -v mkfs.fat || command -v /sbin/mkfs.fat || \
        command -v mkfs.vfat || command -v /sbin/mkfs.vfat)"
"$MKFS" -F 32 -s 1 "$DISK" > /dev/null 2>&1

[ -f "$KERNEL" ] || { echo "no kernel at $KERNEL" >&2; exit 1; }

run_guest() {
    local cmds="$1" log="$2"
    NEWOS_QEMU_TIMEOUT=120 NEWOS_QEMU_KEEP_OUTPUT=1 \
        timeout 240 python3 -u "$ROOT/scripts/qemu-shell.py" \
        "$KERNEL" "$cmds" --mem 256 -drive "file=$DISK,format=raw,if=ide" \
        > "$log" 2>/dev/null || true
}

cat > "$CMD1" <<'EOF'
echo INSTALL-START
newinstall /etc/installer.answers
echo INSTALL-END
cat /etc/hostname
cat /etc/newos.conf
grep installer:x:1000 /etc/passwd
newpkg list
EOF

cat > "$CMD2" <<'EOF'
echo REBOOT-START
cat /etc/hostname
cat /etc/newos.conf
grep installer:x:1000 /etc/passwd
newpkg list
echo REBOOT-END
EOF

run_guest "$CMD1" "$LOG1"
grep -q "newinstall: OK" "$LOG1" || {
    echo "FAIL: installer did not write the snapshot"; tail -25 "$LOG1"; exit 1; }
grep -q "install: hello-new 1.0.0: OK" "$LOG1" || {
    echo "FAIL: selected package was not installed"; tail -25 "$LOG1"; exit 1; }

run_guest "$CMD2" "$LOG2"
grep -qE "sysrestore: [1-9][0-9]* files restored" "$LOG2" || {
    echo "FAIL: no snapshot restored after reboot"; tail -25 "$LOG2"; exit 1; }
grep -q "newos-installed" "$LOG2" || {
    echo "FAIL: installed hostname did not survive reboot"; tail -25 "$LOG2"; exit 1; }
grep -q "session=text" "$LOG2" || {
    echo "FAIL: installed session did not survive reboot"; tail -25 "$LOG2"; exit 1; }
grep -q "installer:x:1000" "$LOG2" || {
    echo "FAIL: installed user did not survive reboot"; tail -25 "$LOG2"; exit 1; }
grep -q "hello-new 1.0.0" "$LOG2" || {
    echo "FAIL: installed package did not survive reboot"; tail -25 "$LOG2"; exit 1; }

echo "installer: PASS (snapshot written, rebooted and restored from /fat)"
