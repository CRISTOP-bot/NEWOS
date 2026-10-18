# Building NEWOS

The **Makefile is the single build system**. There is no Kconfig/CMake/Kbuild;
configuration is one file per platform/flavor under `configs/`.

## Prerequisites

For the full default build: `git`, `gcc`, `g++`, `make`, `binutils`, `nasm`,
`curl`, `python3`, GMP/MPFR/MPC development headers, `flex`, `bison`, and
`texinfo`. `xorriso` is needed for the Limine ISO; `qemu-system-x86_64` and
OVMF are needed for boot tests. The build fetches pinned Git submodules and
boot assets over the network; host packages are not installed automatically.

## Full build and cross toolchain

`make` (or `make all`) is the single complete superproject entry point. It first
synchronizes and initializes every recursive submodule at the exact commits
pinned by NEWOS, then builds binutils 2.44 + GCC 16.1.0 + newlib for
`x86_64-elf` under `toolchain/out/`, builds the standalone kernel and Limine
system ISO with that cross toolchain, and builds the software-rendered TinyGL
demo. The first full build may take roughly 20-40 minutes and needs
GMP/MPFR/MPC development
headers, a host C/C++ toolchain, and the other prerequisites below. GCC 16.x
is used for the pinned target compiler.

```
git clone https://github.com/CRISTOP-bot/NEWOS.git
cd NEWOS
make                 # sources + cross toolchain + system ISO + TinyGL demo
```

All submodule checkouts stay inside this repository; build outputs and
third-party source archives stay under `build/` and `toolchain/out/`. The
build does not install the cross toolchain into the host system. Host
packages are prerequisites and are not installed automatically.

For a quicker kernel/system-image iteration after the modules have been
bootstrapped, use `make fast`. It deliberately uses host `gcc`/`ld` and skips
the cross-toolchain build and TinyGL demo. `make bootstrap` only fetches the
pinned recursive source modules; `make toolchain` bootstraps sources and
builds the cross compiler by itself.

## Userland libc

`libc/` is a freestanding C library (no floats, no locale, no threads):
`string` (mem*/str*), `stdlib` (abs, atoi, rand, `exit`), `stdio`
(unbuffered `putchar`/`puts`/`printf` with `%s %d %u %x %X %p %c %%`),
`malloc`/`free`/`calloc`/`realloc` over a static 256 KiB arena (no
`sbrk` yet: exhaustion returns NULL). Built as `build/userland/libc.a`;
programs opt in via an explicit link rule (see `ltest`). The lint knows
a `LIBC` class (`ABI` + self only); the kernel never imports it.

## Repository components and bootstrap

NEWOS is the superproject. Its required first-party modules are `kernel/`, `userland/`, `toolchain/`, and `ports/`. Top-level symlink shims (`abi/`, `arch/`, `core/`, `crypto/`, `drivers/`, `fs/`, `include/`, `ipc/`, `lib/`, `mm/`, `net/`, `process/`, `sys/`, `syscall/`, `user/`, and `libc/`) preserve the existing source paths used by the Makefile and C includes. The ports stage payload is stored compressed in `NEWOS-ports` and expanded automatically before packaging.

Fresh checkout and full build require no manual submodule commands:

```sh
git clone https://github.com/CRISTOP-bot/NEWOS.git
cd NEWOS
make                 # recursively fetches pinned sources and builds them
make check-newpkg    # optional test suite
```

`make bootstrap` can be run alone to fetch all first-party and nested source
modules without compiling. This includes the pinned GCC source checkout,
Nova, glxgears-newos, and TinyGL; submodules are checked out at recorded SHAs,
not updated to floating branch heads.

Nova is pinned as `ports/nova`; full `make` fetches that source but
intentionally does not compile the upstream compositor until its BoredOS-specific
SDK, framebuffer/VT, UNIX-socket, shared-memory, process, and threading interfaces
have real NEWOS adapters. The active desktop incorporates only the small NTK
color-rendering slice described in [`nova-porting.md`](nova-porting.md); Nova's
compositor, widgets, and window manager remain unported. Full `make` also
compiles `glxgears-newos` with its pinned TinyGL source to
`build/userland/glxgears-newos.elf`. That software-rendered framebuffer demo is
not GLX/X11 and is not part of the booted desktop.

## Targets

```
make / make all     fetch recursive modules, build x86_64-elf toolchain,
                    standalone kernel + Limine ISO and TinyGL demo
make bootstrap      fetch all pinned source modules into this checkout
make fast           host-tool system ISO only (skips cross toolchain and demo)
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

The system-image phase prints a configuration banner (ARCH, CONFIG,
toolchain, git hash/tree state). Failed recipes delete their half-written targets
(`.DELETE_ON_ERROR`), so a rerun never picks up corrupt artifacts.

Every `make all` first runs `scripts/ci/lint_layering.sh`.

## Artifact layout

`build/` is **generated only** and git-ignored:

```
build/
  obj/            per-source .o and .d (mirror of the source tree under build/obj)
  userland/       compiled userland objects/binaries before embedding, including glxgears-newos.elf
  images/         final artifacts: newos-x86_64.elf, -limine.elf, -limine.iso, .iso, .img
  limine-bins/    fetched Limine binaries + host `limine` tool (cached)
  limine-root/    ISO staging tree (kernel, limine.conf, EFI files)
  font8x8_basic.h fetched fbcon font (cached)
  splash.bmp  copied from brand/newos-splash.bmp (brand asset, not generated)
  test.ppm    generated test picture (scripts/gen-images.py)
```

The cross-toolchain build is kept separately under `toolchain/out/` (inside
the `toolchain` submodule); `make clean` removes `build/` but deliberately
keeps the downloaded toolchain and source modules for reuse.

`make clean` deletes the build tree; with generated paths ignored, the source
checkout remains unchanged (`git status --porcelain` empty).

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