# Adding code to NEWOS

These rules keep the tree coherent; the CI runs them on every change.

## Naming

- One home directory per concept; the directory is the domain.
- Filenames carry the domain prefix: `iru_*` (lib/kernel), `x86_*`
  (arch/x86_64), `mm_*` (mm), `proc_*` (process), `core_*` (core), `vfs_*`
  (fs/vfs), `tmpfs`/`devfs`/`initramfs` (fs backends), plus the flat names
  already in use for drivers and ipc.
- **Forbidden** file names: `utils.c`, `common.c`, `misc.c`, `helper.c`,
  `manager.c`, `system.c`, and bare `main.c`/`init.c` (the only `_start`
  entry is the userland program; boot stays `arch_main`).
- Reserved until implemented: the `sched_*` and `libc_*` prefixes, and the
  `scheduler/`, `net/`, `libc/`, `tests/` directories. Do not create
  placeholder directories for planned subsystems; record the plan in
  `docs/` instead.

## Headers

- Public ABI (`abi/`) uses only `<stdint.h>`; never include `core/*` there.
- Kernel-internal shared headers live in `include/<domain>/` with the same
  prefixed basename as the concept they describe.
- Arch headers that everything may include live flat in
  `arch/x86_64/include/` (one file per concept: `x86_mmu.h`, `x86_gdt.h`,
  ...). No cross-file stub directory (`arch/x86_64/{cpu,io,mm,mmu}.h`
  forwards were removed in the rework).

## Layering

`scripts/ci/lint_layering.sh` enforces the contract from
`docs/architecture.md` on every `make all` and `make lint-layers`. When you
add an include, keep these outcomes:

- production domains never reach a layer above them (no `drivers/` code
  including `fs/*`, no `mm/` code including `process/*`, ...);
- `libc/` is userland-side only (lint class `LIBC`: self + `ABI`); the
  kernel never imports it, and userland programs reach it by relative
  include (ignored by the lint like the other local includes);
- everything that needs integer types uses `core/core_types.h` (or
  `<stdint.h>` for ABI/boot headers), never a hand-rolled duplicate
  (`libc` carries its own `stddef.h` because it cannot use either);
- system headers (`<stdint.h>`/`<stddef.h>`) and same-directory quoted
  includes are allowed and ignored by the lint.

If a cross-domain include is genuinely required, change the contract in
`lint_layering.sh` and in `docs/architecture.md` with a comment explaining
why, instead of weakening the classes.

## Build

Add new sources to their domain's list in the Makefile ledger (never a
glob). Keep the ledger order meaningful: it is part of the reproducible
build. Run, at minimum:

```
make clean && make all          # -Werror build + layering lint
scripts/ci/run_qemu_test.sh     # exit 0, "CI test PASS"
```