#!/usr/bin/env bash
# check-frotz.sh: host functional test for the NEWOS Frotz port.
#
# Compiles the ported translation units against the host libc (no
# freestanding flags), links tools/frotz/host-test.c and drives the
# committed mini.z3 story through the real interpreter:
#
#   1. run with no input: banner must appear, exit 0
#   2. run with one input line: second banner must appear, exit 0
#   3. regenerating mini.z3 must reproduce the committed bytes
#
# Usage: check-frotz.sh <workdir-out>   (e.g. build/frotz-check)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FROTZ="$ROOT/userland/user/programs/frotz"
TESTS="$ROOT/tools/frotz/tests"
OUT="${1:-$ROOT/build/frotz-check}"
mkdir -p "$OUT"

SRCS="buffer.c err.c fastmem.c files.c frotz_run.c hotkey.c input.c \
math.c object.c process.c quetzal.c random.c redirect.c screen.c \
sound.c stream.c table.c text.c variable.c dumb_init.c dumb_input.c \
dumb_output.c dumb_pic.c"

HF="-O2 -std=gnu99 -fcommon -fno-stack-protector -Wall -Wextra -Werror \
-Wno-error=unterminated-string-initialization \
-Wno-unterminated-string-initialization"

# port translation units match the NEWOS build: same flags minus the
# freestanding/nostdlib set (host libc provides everything the port uses).
for s in $SRCS; do
    gcc $HF -c -o "$OUT/${s%.c}.o" "$FROTZ/$s"
done
gcc $HF -c -o "$OUT/host-test.o" "$ROOT/tools/frotz/host-test.c"
gcc -o "$OUT/frotz-host" "$OUT"/*.o
binary="$OUT/frotz-host"
story="$TESTS/mini.z3"

fails=0
check() { # check <name> <cmd...>
    local name="$1"; shift
    if "$@"; then
	echo "check-frotz: ok $name"
    else
	echo "check-frotz: FAIL $name" >&2
	fails=$((fails + 1))
    fi
}

# 1) banner without input (stdin closed)
out1="$("$binary" -Z2 "$story" </dev/null)" || exit 1
check "banner-only output" \
    bash -c "echo \"\$0\" | grep -q 'frotz mini demo works'" "$out1"

# 2) one input line reaches the read and the follow-up print
out2="$(printf 'hello\n' | "$binary" -Z2 "$story")" || exit 1
check "read path output" \
    bash -c "echo \"\$0\" | grep -q 'read ok'" "$out2"

# check each story half kept its own status line render
check "status line rendering" \
    bash -c "echo \"\$0\" | grep -q 'Score: 0'" "$out2"

# 3) committed mini.z3 equals the deterministic generator output
python3 "$TESTS/gen_mini_z3.py" "$OUT/mini.z3.gen" >/dev/null
check "mini.z3 reproducible" cmp -s "$story" "$OUT/mini.z3.gen"

if [ "$fails" -gt 0 ]; then
    echo "check-frotz: $fails failure(s)" >&2
    exit 1
fi
echo "check-frotz: all checks passed"
