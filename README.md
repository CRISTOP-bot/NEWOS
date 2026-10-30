<p align="center">
  <img src="brand/newos.svg" alt="NEWOS logo" width="112" height="112">
</p>

# NEWOS

A proprietary, Linux-independent operating system, designed and written from
scratch: kernel, ABI, syscall ABI, libc, init, shell, VFS, drivers,
toolchain, and a native package system — targeting x86_64 today (ARM64 and
RISC-V 64 configs exist as planning placeholders).

The system is built as a strictly layered, fully modular tree. Nothing is
copied from Linux; everything follows the layered order:
**Hardware → Arch → Kernel → Drivers → Syscalls → libc → Userspace.**

## Status

The x86_64 kernel reaches `BUILD=PASS`, `BOOT=PASS`, and `QEMU=PASS`:

- Builds clean under `-Werror` (host `gcc` or the `x86_64-elf` cross
  toolchain, `-mcmodel=kernel`, `-mgeneral-regs-only`).
- Boots in QEMU through **three independent paths**:
  - **PVH** (`qemu -kernel`, via the Xen ELF note), the default dev path.
  - **Multiboot2** (GRUB, via `make iso`).
  - **Limine** (BIOS+UEFI ISO, the only path with a framebuffer).
- Early long-mode setup: identity + high-half paging, GDT/TSS, IDT, PIC
  remap, serial console, PMM (bitmap + buddy over all usable RAM ranges),
  kheap, VFS with tmpfs / devfs plus read-only procfs and sysfs,
  initramfs, device-model core, PCI + PCnet / serial / PS/2 / PIT / CMOS
  drivers, framebuffer console.
- Interactive `nsh` shell (`/init`) with a native `/bin` toolbox, including
  recursive `find`,
  `.new` package manager (`newpkg`), and a C library (`libc.a`).
- A native `/bin/frotz` Z-machine interpreter (upstream Frotz 2.44, GPL-2+)
  for Infocom-style interactive fiction; see `docs/frotz.md`.
- Self-tests: 11 phase-1 kernel tests + 9 phase-2 userland/ABI tests —
  all pass (`make qemu-test`, QEMU exit `1` = PASS). `/proc` exposes
  version, uptime, meminfo, filesystems, mounts and cmdline; `/sys`
  exposes kernel identity and the PCI inventory.
- Complete static BusyBox 1.35.0 at `/bin/busybox` (402 applets) through the
  Linux-compatible x86-64 `syscall` entry, plus a built-in `/bin/glxgears`
  TinyGL demo and a five-window Win95-style desktop.
- `/bin/nano` — genuine GNU nano 8.4 on a static ncursesw, with colors, line
  numbers and syntax highlighting on the framebuffer console — and `/bin/tcc` —
  TinyCC 0.9.27, which compiles objects and can compile-and-run a freestanding
  program in place (`tcc -nostdlib -run /etc/tcc-hello.c`). Both are vendored
  forks under `ports/` (see `ports/nano/README.md`, `ports/tcc/README.md`).

## Build & run

For a fresh clone, `make` bootstraps every pinned recursive source submodule
and builds the cross toolchain, NEWOS kernel/system ISO, and the embedded
TinyGL demo from the original repository:

```sh
git clone https://github.com/CRISTOP-bot/NEWOS.git
cd NEWOS
make
```

All source checkouts stay inside this repository; generated files stay in
`build/` and `toolchain/out/`. The first full build may take 20-40 minutes.
Use `make fast` for a host-compiler system build that skips the cross
toolchain but still builds and embeds `/bin/glxgears`, or `make bootstrap` to
fetch sources without compiling.

The ISO defaults to a plain text `nsh` session. The Limine menu exposes a
separate **NEWOS (GUI)** entry (`gui=1`); `make qemu-limine-gui` boots
with `gui=1` directly for visual checks, and `/bin/desktop` can also be
started from `nsh`. `Ctrl+Q` (or closing the last window) returns to a
clean shell. The desktop has five draggable/resizable windows — System,
Files, Mouse, About and Log — plus a dock, live clock/memory readouts, a
`/bin` launcher and Midnight/Win95 themes. It owns the PS/2 keyboard while
active: `Tab` cycles windows, `1`–`5` focus one,
arrows select a program, `Enter` runs it and `?` shows the shortcut card.
Serial input stays with the shell, and `gui=0` (or `test_mode=1`) keeps a
plain text session. See [`docs/boot.md`](docs/boot.md)
and [`docs/abi.md`](docs/abi.md).

The desktop is self-contained NEWOS code: a clipped XRGB surface
(`desktop_surface`), caller-owned widgets (`desktop_widgets`) and a
damage-based compositor. The MIT [VOXEL](https://github.com/CRISTOP-bot/voxel)
software graphics stack is vendored under `ports/voxel` (built by
`scripts/build-voxel.sh`) and shipped as `/bin/voxel`, a standalone
classic-desktop demo; it is the graphics direction but not yet the session
shell. `glxgears` is built into the image at `/bin/glxgears`; the
standalone `build/userland/glxgears-newos.elf` and `.new` package remain
available for explicit port work.

`/bin/busybox` is the official static BusyBox 1.35.0 build (402 applets).
`nsh` runs unshadowed command names through `/bin/busybox <applet>` because
the VFS has no symlinks; see [`ports/busybox/README.md`](ports/busybox/README.md)
for provenance, the GPLv2 notice and known kernel-feature limits.

Requirements for the full build: `gcc`, `g++`, `make`, `binutils`, `nasm`,
`git`, `curl`, `python3`, `libgmp-dev`, `libmpfr-dev`, `libmpc-dev`, `flex`,
`bison`, and `texinfo`; `xorriso` is required for the Limine ISO. QEMU and
OVMF are required only for the boot tests. NEWOS does not install host
packages automatically.

```sh
make all            # cross toolchain + Limine ISO + glxgears.elf/.new
make fast           # host-compiler ISO (includes /bin/glxgears)
make bootstrap      # fetch all pinned first-party and nested submodules
make qemu           # boot in QEMU (serial console)  (BOOT=PASS)
make qemu-test      # boot, run self-tests, exit with a machine-checkable code
make qemu-limine    # text shell; choose NEWOS (GUI) in the Limine menu
make qemu-limine-gui   # boot with gui=1 directly (no menu wait)
make qemu-limine-test  # suite through the Limine ISO (graphics path)
make check-newpkg   # package system tests (no QEMU needed)
make help           # full target list (V=0 for a short log, V=1 verbose)
make clean
```

Every push to `main` runs the build, package-manager tests, and QEMU boot
tests. Passing commits publish a GitHub prerelease with the kernel, ISO images,
the standalone TinyGL demo and its `.new` package, and update the build
environment at `ghcr.io/cristop-bot/newos-build` (`latest` and `sha-*` tags). Pull requests
run the same checks without publishing. Pull and run the published build
environment with `docker compose pull newos` followed by
`docker compose run --rm newos`.

Automated test mode:

```sh
qemu-system-x86_64 -kernel build/images/newos-x86_64.elf \
  -serial stdio -no-reboot -m 256M \
  -device isa-debug-exit,iobase=0xf4 -append "test_mode=1"
```

The kernel runs its self-tests and writes the result to QEMU's
`isa-debug-exit` port: QEMU exits `1` when all tests pass, `>= 3` on failure.
`scripts/ci/run_qemu_test.sh` wraps this for CI.

## Boot protocol

- **Multiboot2** header, a **PVH ELF note**, and **Limine** requests coexist
  in the image. GRUB, QEMU (`-kernel`) and Limine each use the entry they
  understand and ignore the rest.
- The 32-bit trampoline builds early four-level paging (identity low 1 GiB +
  high-half kernel window), then long mode, and enters `arch_main`
  (Limine enters 64-bit directly via `limine_arch_main`).
- PVH start-info, multiboot2 info structures, and the Limine memory map are
  parsed into normalized RAM ranges that seed the PMM; MMIO holes are never
  handed out as free memory.

## Repository layout

See `docs/architecture.md` for design details.

| Area | Location |
| --- | --- |
| Boot trampoline / page tables | `arch/x86_64/boot/`, `arch/x86_64/memory/`, `arch/x86_64/include/` |
| GDT/TSS, IDT, PIC, context switch | `arch/x86_64/cpu/`, `arch/x86_64/interrupts/`, `arch/x86_64/threading/` |
| Kernel core (printk/panic/oops, cmdline, init, self-tests) | `core/` |
| PMM / kheap / VMM | `mm/` |
| VFS / tmpfs / devfs / initramfs | `fs/` |
| Processes, threads, ELF | `process/` |
| Syscall dispatcher | `syscall/` |
| Drivers | `drivers/`, `lib/kernel/` (`iru_*`), `ipc/` |
| User ABI + userland | `abi/`, `user/programs/*/` (shell + native `/bin` tools), `user/lib/nshlib` |
| Native packages | `user/programs/newpkg/`, `packages/*.newspec`, `tools/newpkg/` |
| C library | `libc/` (`libc.a`) |
| Build system | `Makefile`, `configs/x86_64/debug.config` |

## Roadmap

See [ROADMAP.md](ROADMAP.md): v0.3 Persistence → v0.4 Memory & processes →
v0.5 Network → v0.6 Packages v2 → v0.7 Hardware/SMP → v0.8 Ports → v1.0.
See [docs/porting-roadmap.md](docs/porting-roadmap.md) for the BoredOS port
inventory and the actual Nano, native compiler, and ARM64 prerequisites.

## Continuous integration

- `.github/workflows/ci-release.yml` — build all targets, validate the
  multiboot2 header, run the package and kernel tests, boot both ISOs in QEMU,
  and publish the tested assets.
- `.github/workflows/auto-merge.yml` — merge a PR once all checks are green
  and every pinned submodule commit is reachable from its default branch.
- `.github/workflows/pr-self-heal.yml` — repair failing PRs and merge
  conflicts with deterministic fixes and the free CodeRabbit OSS bot; see
  [`docs/self-healing.md`](docs/self-healing.md).

## License

MIT — see [LICENSE](LICENSE).
