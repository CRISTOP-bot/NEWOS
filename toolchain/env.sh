#!/usr/bin/env bash
# Source this to use the NEWOS cross toolchain in the current shell:
#   source toolchain/env.sh
# Requires one prior `bash toolchain/build.sh` run.
_TC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="$_TC_ROOT/toolchain/out/bin:$PATH"
export CROSS_PREFIX="x86_64-elf-"
unset _TC_ROOT
