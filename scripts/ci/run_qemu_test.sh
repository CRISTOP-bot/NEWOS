#!/usr/bin/env bash
# NEWOS in-QEMU automated test runner.
#
# Boots the kernel in test mode (test_mode=1) via the PVH path, which runs
# the kernel self-tests and then writes to QEMU's isa-debug-exit device.
# QEMU exits with status 1 on success and >= 3 on failure.
#
# Exit status 0 from this script means the kernel booted cleanly and all
# self-tests passed. Non-zero means a kernel or test failure.
set -euo pipefail

cd "$(dirname "$0")/../.."

KERNEL="build/newos-x86_64.elf"
TIMEOUT="${TIMEOUT:-120}"

make all

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

set +e
timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -serial stdio \
    -no-reboot \
    -m 128M \
    -display none \
    -device isa-debug-exit,iobase=0xf4 \
    -append "test_mode=1" >"$log" 2>&1
rc=$?
set -e

cat "$log"

# timeout reached -> kernel stalled and never exited
if [ "$rc" -eq 124 ]; then
    echo "FAIL: QEMU timed out ($TIMEOUT s); kernel did not finish." >&2
    exit 1
fi

# isa-debug-exit: pass = 1, fail = >= 3
if [ "$rc" -ne 1 ]; then
    echo "FAIL: QEMU exited with status $rc (expected 1 = tests passed)." >&2
    exit 1
fi

if ! grep -qE "[0-9]+ tests, 0 failures" "$log"; then
    echo "FAIL: self-test summary does not show zero failures." >&2
    exit 1
fi

echo "CI test PASS"