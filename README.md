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
  kheap, VFS with tmpfs / devfs, initramfs, device-model core, PCI +
  PCnet / serial / PS/2 / PIT / CMOS drivers, framebuffer console.
- Interactive `nsh` shell (`/init`) with a native `/bin` toolbox, including
  recursive `find`,
  `.new` package manager (`newpkg`), and a C library (`libc.a`).
- Self-tests: 7 phase-1 kernel tests + 9 phase-2 userland/ABI tests —
  all pass (`make qemu-test`, QEMU exit `1` = PASS).

## Build & run

Requirements: `gcc`, `make`, `nasm`, `ld`, `qemu-system-x86_64`, `python3`
(plus `grub-mkrescue`/`xorriso` for the ISOs).

```sh
make all            # build build/images/newos-x86_64.elf  (BUILD=PASS)
make qemu           # boot in QEMU (serial console)  (BOOT=PASS)
make qemu-test      # boot, run self-tests, exit with a machine-checkable code
make qemu-limine-test  # suite through the Limine ISO (graphics path)
make check-newpkg   # package system tests (no QEMU needed)
make help           # full target list (V=0 for a short log, V=1 verbose)
make clean
```

Every push to `main` runs the build, package-manager tests, and QEMU boot
tests. Passing commits publish a GitHub prerelease with the kernel and ISO
images, and update the build environment at
`ghcr.io/cristop-bot/newos-build` (`latest` and `sha-*` tags). Pull requests
run the same checks without publishing. Pull and run the published build
environment with `docker compose pull newos` followed by
`docker compose run --rm newos`.

Automated test mode:

```sh
qemu-system-x86_64 -kernel build/images/newos-x86_64.elf \
  -serial stdio -no-reboot -m 128M \
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

- `.github/workflows/build.yml` — build all targets, validate the multiboot2
  header, run the package system tests (`make check-newpkg`).
- `.github/workflows/qemu-test.yml` — `BUILD=PASS`, `QEMU=PASS`,
  multiboot2-ISO `BOOT=PASS`.
- `.github/workflows/static-analysis.yml` — warnings-as-errors build,
  layering contract, and whitespace checks.

## License

MIT — see [LICENSE](LICENSE).
