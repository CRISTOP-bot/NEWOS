# Changelog

All notable changes to NEWOS are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.2.0-pre-alpha] - 2026-09-10

Source-layout rework: every subsystem moved under a canonical, layering-owned
tree (`abi/`, `arch/*/`, `core/`, `drivers/`, `fs/`, `include/`, `ipc/`,
`lib/kernel/`, `mm/`, `process/`, `syscall/`, `user/`), the build was
replaced with a single explicit-source `Makefile`, and the output hierarchy
was moved under `build/`.

### Added
- Layering contract lint (`scripts/ci/lint_layering.sh`, `make lint-layers`)
  enforced on every build and in CI; no cross-domain `#include` shows up.
- Documentation set: `docs/architecture.md`, `docs/build.md`, `docs/boot.md`,
  `docs/abi.md`, `docs/development/layering.md`.
- Explicit `PHDRS` in the x86_64 linker script so no PT_LOAD is RWE.

### Changed
- Tree restructured to the layered layout; files renamed to prefixed,
  self-describing names (`restructure`).
- Build unified on one `Makefile` with an explicit per-domain source ledger
  (`cmake/kbuild` removed); artifacts consolidated under `build/images/`.
- `include/core/core_types.h` uses the real `<stdint.h>` types; `size_t`
  widened to 64-bit and `intptr_t`/`uintptr_t` fixed.
- CI workflows (`build`, `qemu-test`, `static-analysis`) reconciled with the
  new layout and lint rule.
- `.early_bss` declared `nobits` in the entry assembly (NASM warning gone).

### Fixed
- `abi/multiboot2.h` is self-contained (no longer depends on kernel types).

### Verified
- `BUILD=PASS`  - two clean builds are byte-for-byte identical.
- `BOOT=PASS`   - PVH boot reaches `NEWOS: boot complete.` plus userland.
- `SELF-TEST`   - all kernel self-tests and the userland hello pass.
- `QEMU=PASS`   - `scripts/ci/run_qemu_test.sh` exits 0 only on success.

## [0.1.0-pre-alpha] - 2026-09-06

Phase 1 milestone: the x86_64 kernel builds clean under `-Werror`, boots in
QEMU through two independent boot-loading paths, and passes its kernel
self-tests with an automated, machine-checkable test exit code.

### Added
- Bootable ISO target (`make iso`) via GRUB (multiboot2).
- Automated in-QEMU test runner with the `isa-debug-exit` device
  (`ci/qemu/run_test.sh`, `make qemu-test`) so CI observes a real exit status.
- GitHub Actions workflows: `build`, `qemu-test`, `static-analysis`.
- `LICENSE` (MIT), `VERSION`, `CHANGELOG.md`, `README.md`.

### Changed
- `lib/libk/lock.h`: `spinlock_unlock` now uses a plain store instead of
  `lock; movl` (LOCK-prefixed MOV is an invalid `#UD` instruction).
- `arch/x86_64/gdt/gdt.c`: TSS descriptor rewritten to the byte-accurate
  Intel layout (access byte at offset 5) so `ltr` no longer faults.
- `arch/x86_64/idt/idt.c`: legacy 8259 PIC remapped off the CPU exception
  vectors (IRQ0 no longer collides with the double-fault gate); all PIC lines
  masked until drivers claim them.
- `arch/x86_64/boot/*`: PVH start-info parsing now keys off populated fields
  instead of QEMU's (unset) flag bits; memory-map parsing spans all available
  RAM regions above the kernel load base.
- `fs/vfs/vfs.c`: `vfs_mkdir`/`vfs_create` strip leading slashes on
  single-component paths (`/etc` created as `etc`).
- Build flags add `-mcmodel=kernel`; `printk` drops the `format(printf,...)`
  attribute in favor of `u64`-friendly `%p`.

### Fixed
- Early high-half kernel map: six-megabyte PTE window at
  `0xffffffff80000000` now maps kernel physical memory 1:1 (was mapping VA
  window to physical frame 0, jumping into zeros).
- MV scoped fixes otherwise recorded across PMM/heap/VFS boot integration.

### Verified
- `BUILD=PASS`  - links `build/newos-x86_64.elf` under `-Werror`.
- `BOOT=PASS`   - boots via PVH (QEMU `-kernel`) and via multiboot2 (GRUB
  under OVMF/UEFI), serial console active, 126 MiB / 125 MiB RAM tracked.
- `SELF-TEST`   - bitmap, heap-rw, pmm-alloc, pipe, vfs-tmpfs all pass.
- `QEMU=PASS`   - `ci/qemu/run_test.sh` exits clean only when tests pass.