# Building NEWOS

The **Makefile is the single build system**. There is no Kconfig/CMake/Kbuild;
configuration is one file per platform/flavor under `configs/`.

## Prerequisites

- gcc, binutils (ld), nasm, qemu-system-x86_64, python3
- grub-mkrescue/xorriso for `make iso`; xorriso for the Limine ISO
- network once: `scripts/fetch-limine.sh` (bootloader binaries) and
  `scripts/fetch-font.sh` (fbcon font) cache under `build/`

## Cross toolchain (opt-in)

`toolchain/build.sh` builds binutils 2.44 + GCC 16.1.0 (C only) for
`x86_64-elf` under `toolchain/out/` (~20-40 min, needs GMP/MPFR/MPC
headers). GCC 16.x is required: older sources miscompile under a gcc-16
host (`libcody` char8_t errors, no `--disable-libcody` switch).

```
bash toolchain/build.sh --jobs 2   # or: make toolchain
export PATH="$PWD/toolchain/out/bin:$PATH"
make CROSS_PREFIX=x86_64-elf- all
```

The default build keeps using host `gcc`/`ld` (`CROSS_PREFIX` empty);
`nasm` is always the host one. Validate a cross build with
`make CROSS_PREFIX=x86_64-elf- qemu-test` before relying on it.

## Userland libc

`libc/` is a freestanding C library (no floats, no locale, no threads):
`string` (mem*/str*), `stdlib` (abs, atoi, rand, `exit`), `stdio`
(unbuffered `putchar`/`puts`/`printf` with `%s %d %u %x %X %p %c %%`),
`malloc`/`free`/`calloc`/`realloc` over a static 256 KiB arena (no
`sbrk` yet: exhaustion returns NULL). Built as `build/userland/libc.a`;
programs opt in via an explicit link rule (see `ltest`). The lint knows
a `LIBC` class (`ABI` + self only); the kernel never imports it.

## Targets

```
make all            build the kernel       -> build/images/newos-x86_64.elf
make limine         Limine-flavour kernel  -> build/images/newos-x86_64-limine.elf
make build/images/newos-x86_64-limine.iso  BIOS+UEFI Limine ISO (complete system)
make iso            bootable multiboot2 ISO-> build/images/newos-x86_64.iso (needs grub)
make qemu           boot in QEMU (serial console)
make qemu-debug     boot with GDB server on :1234 (-s -S)
make qemu-test      run the in-kernel self-tests, isa-debug-exit (exit 1 = PASS)
make qemu-limine-test  self-tests through the Limine ISO (exit 1 = PASS)
make qemu-limine    boot the Limine ISO interactively
make qemu-network   boot with user-mode networking
make qemu-disk      boot from a 64 MiB raw disk image
make check-newpkg   package system tests (C unit driver + end-to-end suite)
make lint-layers    enforce the dependency contract only
make help           list targets and options
make check-config   verify configs/<arch>/debug.config exists
make clean          remove build/ entirely (source tree is pristine again)
```

Build verbosity: `V=1` (default) prints every command in full;
`V=0` prints only short tags (`CC`, `UCC`, `ULD`, `EMB`, `LD`, ...):

```
make all       # full gcc/ld/nasm command lines
make all V=0   # short tags only
```

`make all` starts with a configuration banner (ARCH, CONFIG, toolchain,
git hash/tree state). Failed recipes delete their half-written targets
(`.DELETE_ON_ERROR`), so a rerun never picks up corrupt artifacts.

Every `make all` first runs `scripts/ci/lint_layering.sh`.

## Artifact layout

`build/` is **generated only** and git-ignored:

```
build/
  obj/            per-source .o and .d (mirror of the source tree under build/obj)
  userland/       compiled userland objects/binaries before embedding
  images/         final artifacts: newos-x86_64.elf, -limine.elf, -limine.iso, .iso, .img
  limine-bins/    fetched Limine binaries + host `limine` tool (cached)
  limine-root/    ISO staging tree (kernel, limine.conf, EFI files)
  font8x8_basic.h fetched fbcon font (cached)
  splash.bmp  copied from brand/newos-splash.bmp (brand asset, not generated)
  test.ppm    generated test picture (scripts/gen-images.py)
```

`make clean` deletes all of it; the source tree is then untouched
(`git status --porcelain` empty).

## Source ledger

The kernel's source list is an explicit per-domain ledger in the Makefile
(`ARCH_SRCS_*`, `CORE_SRCS`, `MM_SRCS`, `FS_SRCS`, `DRV_SRCS`, `IPC_SRCS`,
`LIB_SRCS`). Dead globs are forbidden: every listed file must exist.
Adding a source file means adding it to its domain's list (and keeping the
layering contract, see `docs/development/layering.md`).

Userland tools are data-driven: one `user/programs/<name>/<name>_main.c`
plus the name in `PROG_NAMES` (build + embed) and in `bin_table`
(`fs/initramfs/initramfs.c`, installs into `/bin`). `make` variables use
`:=` (immediate expansion), so embed variables must be defined *before*
the `OBJS` line that consumes them.

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