# Building NEWOS

The **Makefile is the single build system**. There is no Kconfig/CMake/Kbuild;
configuration is one file per platform/flavor under `configs/`.

## Prerequisites

- gcc, binutils (ld), nasm, qemu-system-x86_64
- optional: grub-mkrescue/xorriso for `make iso`

## Targets

```
make all            build the kernel       -> build/images/newos-x86_64.elf
make iso            bootable multiboot2 ISO-> build/images/newos-x86_64.iso
make qemu           boot in QEMU (serial console)
make qemu-debug     boot with GDB server on :1234 (-s -S)
make qemu-test      run the in-kernel self-tests, isa-debug-exit (exit 1 = PASS)
make qemu-network   boot with user-mode networking
make qemu-disk      boot from a 64 MiB raw disk image
make lint-layers    enforce the dependency contract only
make check-config   verify configs/<arch>/debug.config exists
make clean          remove build/ entirely (source tree is pristine again)
```

Every `make all` first runs `scripts/ci/lint_layering.sh`.

## Artifact layout

`build/` is **generated only** and git-ignored:

```
build/
  obj/            per-source .o and .d (mirror of the source tree under build/obj)
  userland/       compiled userland objects/binaries before embedding
  images/         final artifacts: newos-x86_64.elf, .iso, .img
```

`make clean` deletes all of it; the source tree is then untouched
(`git status --porcelain` empty).

## Source ledger

The kernel's source list is an explicit per-domain ledger in the Makefile
(`ARCH_SRCS_*`, `CORE_SRCS`, `MM_SRCS`, `FS_SRCS`, `DRV_SRCS`, `IPC_SRCS`,
`LIB_SRCS`). Dead globs are forbidden: every listed file must exist.
Adding a source file means adding it to its domain's list (and keeping the
layering contract, see `docs/development/layering.md`).

## Determinism

Builds are reproducible: two `make clean && make all` runs produce a
byte-identical ELF (checked by hashing during development). Reordering the
ledger or adding files changes the image, so the ledger order is part of the
build contract.

## Configuration

`configs/x86_64/debug.config` is the debug flavor; `configs/{arm64,riscv64}/
debug.config` track the other ports' planned debug settings. The file is
currently informational; the build reads no CONFIG_* symbols yet.

## Continuous integration

- `build.yml` — matrix build, validates the multiboot2 header
  (`grub-file --is-x86-multiboot2`).
- `qemu-test.yml` — `BUILD=PASS` (+ lint), `QEMU=PASS`
  (`scripts/ci/run_qemu_test.sh`), multiboot2-ISO `BOOT=PASS`.
- `static-analysis.yml` — warnings-as-errors build and `git diff --check`.