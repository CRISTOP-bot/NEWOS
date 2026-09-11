# NEWOS

A proprietary, Linux-independent operating system, designed and written from
scratch: kernel, ABI, syscall ABI, libc, dynamic linker, init, shell, VFS,
drivers, toolchain, and GUI — targeting x86, x86_64, ARM, ARM64, RISC-V, and
RISC-V64.

The system is built as a strictly layered, fully modular tree. Nothing is
copied from Linux; everything follows the layered order:
**Hardware → Arch → Kernel → Drivers → Syscalls → libc → Userspace.**

## Status (Phase 1)

The x86_64 kernel reaches `BUILD=PASS`, `BOOT=PASS`, and `QEMU=PASS`:

- Builds clean under `-Werror` with `gcc -mcmodel=kernel`.
- Boots in QEMU through **two independent paths**:
  - **PVH** (`qemu -kernel`, via the Xen ELF note), the default dev path.
  - **Multiboot2** (GRUB, via `make iso`).
- Early long-mode setup: identity + high-half paging, GDT/TSS, IDT, PIC
  remap, serial console, PMM (bitmap), kheap (first-fit), VFS with tmpfs /
  devfs, initramfs, first device-model core.
- Kernel self-tests: bitmap, heap-rw, pmm-alloc, pipe, vfs-tmpfs — all pass.

## Build & run

Requirements: `gcc`, `make`, `nasm`, `ld`, `qemu-system-x86_64`
(plus `grub-mkrescue`/`xorriso` for the ISO).

```sh
make all        # build build/images/newos-x86_64.elf  (BUILD=PASS)
make qemu       # boot in QEMU (serial console)  (BOOT=PASS)
make qemu-test  # boot, run self-tests, exit with a machine-checkable code
make iso        # GRUB multiboot2 ISO
make qemu-debug # boot with GDB server on :1234
make clean
```

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

- **Multiboot2** header and a **PVH ELF note** coexist in `_start`. GRUB and
  QEMU each use the one they understand and ignore the other.
- The 32-bit trampoline builds early four-level paging (identity low 1 GiB +
  high-half kernel window), then long mode, and enters `arch_main`.
- PVH start-info and multiboot2 info structures are parsed to seed the PMM
  with the real RAM map; single low chunk skipped, fragmented regions spanned.

## Repository layout

See `docs/architecture/` for design details. Phase-1 working set:

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
| User ABI + userland | `abi/`, `user/programs/hello/` |
| Build system | `Makefile`, `configs/x86_64/debug.config` |

## Continuous integration

- `.github/workflows/build.yml` — build all targets, validate the multiboot2
  header.
- `.github/workflows/qemu-test.yml` — `BUILD=PASS`, `QEMU=PASS`,
  multiboot2-ISO `BOOT=PASS`.
- `.github/workflows/static-analysis.yml` — warnings-as-errors build and
  whitespace checks.

## License

MIT — see [LICENSE](LICENSE).