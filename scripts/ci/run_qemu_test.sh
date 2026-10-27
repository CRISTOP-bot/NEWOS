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

KERNEL="build/images/newos-x86_64.elf"
TIMEOUT="${TIMEOUT:-120}"

make all

log="$(mktemp)"
wav="$(mktemp /tmp/newos-hda-XXXXXX.wav)"
trap 'rm -f "$log" "$wav"' EXIT

# The audio recorder backend is named "wav" in minimal QEMU builds and
# "file" in others; probe so the invocation works on both (and degrade
# to "none" — the DMA round still self-verifies in-kernel via LPIB).
adrv="none"
for cand in file wav; do
    if qemu-system-x86_64 -audiodev help 2>/dev/null | grep -qw "$cand"; then
        adrv="$cand"
        break
    fi
done
adrvargs=""
if [ "$adrv" != "none" ]; then
    # The recorder's property is spelled `path` (upstream audiodev
    # naming), not `out.filename`, and the recorder ignores the codec
    # side's requested rate: pin 48 kHz / stereo / S16 explicitly.
    adrvargs="-audiodev driver=$adrv,id=aud0,path=$wav,out.frequency=48000,out.channels=2,out.format=s16"
fi

set +e
timeout "$TIMEOUT" qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -serial stdio \
    -no-reboot \
    -m 128M \
    -display none \
    -device isa-debug-exit,iobase=0xf4 \
    -netdev user,id=net0 -device pcnet,netdev=net0 \
    -netdev user,id=net1 -device e1000,netdev=net1 \
    $adrvargs \
    -device intel-hda \
    -device hda-duplex,audiodev=aud0 \
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

# The hda ktest drove 0.25 s of a 440 Hz sine (48 kHz / S16 / stereo)
# through the codec; when a WAV backend exists QEMU wrote it out and we
# verify the container and the payload instead of trusting the kernel's
# own LPIB-based pass alone.
if [ "$adrv" = "none" ]; then
    echo "note: no WAV-capable audiodev here; hda WAV check skipped" >&2
elif [ -s "$wav" ] && command -v python3 >/dev/null 2>&1; then
    if ! python3 - "$wav" <<'PYEOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
assert d[:4] == b"RIFF" and d[8:12] == b"WAVE", "not a RIFF/WAVE file"
i, fmt, data = 12, None, None
while i + 8 <= len(d):
    cid, sz = d[i:i+4], struct.unpack("<I", d[i+4:i+8])[0]
    if sz == 0 and cid == b"data":
        body, i = d[i+8:], len(d)
    else:
        body, i = d[i+8:i+8+sz], i + 8 + sz + (sz & 1)
    if cid == b"fmt ":
        fmt = body
    elif cid == b"data":
        data = body
assert fmt is not None and data is not None, "missing fmt/data chunks"
_, ch, rate, _br, _ba, bits = struct.unpack("<HHIIHH", fmt[:16])
assert (rate, ch, bits) == (48000, 2, 16), (rate, ch, bits)
n = len(data) // 2
assert n >= 20000, f"payload too small: {n} samples"
v = struct.unpack(f"<{n}h", data[:2*n])
nz = sum(1 for x in v if x != 0)
assert nz > n // 2, "payload is mostly silence"
peak = max(abs(x) for x in v)
assert 12000 < peak < 32768, f"peak amplitude {peak} out of range"
assert any(x > 5000 for x in v) and any(x < -5000 for x in v), \
    "waveform is one-sided"
print(f"hda WAV ok: {n} samples, peak {peak}")
PYEOF
    then
        echo "FAIL: hda WAV validation failed" >&2
        exit 1
    fi
elif [ -s "$wav" ]; then
    echo "note: python3 unavailable; WAV presence-checked only" >&2
else
    echo "FAIL: recorder backend exists but no WAV was produced" >&2
    exit 1
fi

echo "CI test PASS"