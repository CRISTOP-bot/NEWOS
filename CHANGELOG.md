# Changelog

All notable changes to NEWOS are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- Native `/bin/find` command with recursive directory traversal, basename
  wildcard matching, file/directory filters and depth bounds; `/bin` and
  shell help now expose it. The userland library also exports `sys_stat()`.
- Syscall ABI `SYS_OPEN` (7) and `SYS_CLOSE` (8) (`abi/syscall_abi.h`):
  per-process fd allocation backed by the VFS, with user-pointer validation
  and `O_CREATE` support; `SYS_READ` now serves any open fd through the VFS
  while stdin keeps its blocking console semantics. Covered by the new
  phase-2 `open-close` test (open/read/close + `O_CREATE` write/read-back
  round trip + error cases, all through the real dispatcher).
- GCC 16 (C23) build fix: `bool`/`true`/`false` in `core_types.h` now defer
  to the compiler builtins instead of redefining the keywords.
- Multi-run RAM registration (`pmm_allocator_init_ranges()`): boot parsers
  now hand every RAM E820 region (clamped to the direct-map window) to the
  PMM instead of one collapsed interval, so no MMIO hole is ever handed out
  as free memory and all free frames stay within the identity map.
- VGA text-mode console (`drivers/console/vga_text_console.c`): the kernel
  mirrors every `printk` line to the 80x25 screen (`0xB8000`) in addition to
  serial, so a VGA-only machine (e.g. VirtualBox default BIOS) displays the
  boot log.
- Robust GRUB config for the ISO: both the console (VGA) and the serial
  terminal are enabled; a missing serial port falls back to the screen.
- Reproducible ISO: `make iso` pins every time-derived byte (volume
  timestamps, the xorriso `.uuid` marker, the `efi.img` FAT volume serial)
  via `scripts/iso/fixup_iso.py`, so two clean ISO builds are byte-identical.
- Usage documentation: `docs/virtualbox-and-hardware.md` (VirtualBox legacy
  BIOS + real hardware + validation matrix).
- Native `.new` package system (`docs/packages.md`, `docs/new-format.md`,
  `docs/newpkg.md`): deterministic `NEW1` container (header + `key: value`
  metadata + sorted manifest + raw payload, CRC32 throughout), userspace
  `/bin/newpkg` (`info/verify/install/remove/list/files`, dependency
  checks with cycle detection, conflict scan, install rollback), host
  builder (`tools/newpkg/newpkg-build.py`, `packages/*.newspec`), first
  real package `hello-new-1.0.0-x86_64.new` (staged at
  `/tmp/hello-new-1.0.0-x86_64.new`, installs runnable `/bin/hello-new`),
  and `make check-newpkg` (C unit driver + end-to-end suite driving the
  shipped installer code).
- Brand identity (`brand/`) now feeds every asset the build consumes:
  `/etc/splash.bmp` installs from `brand/newos-splash.bmp` instead of a
  generated gradient placeholder (`scripts/gen-images.py` is reduced to the
  PPM decoder test image), the website serves the brand mark as favicon,
  header and footer logo (mono variant, so it follows the site theme), and
  `README.md` opens with `brand/newos.svg`. The per-tool icons under
  `brand/app-icons/` and `brand/newos-1024.png` still have no consumer.

### Fixed
- `desktop` microfont glyph rows were read least-significant-bit first while
  the embedded glyphs are stored most-significant-bit first, mirroring text.
  The corrected table has unique character mappings and a `?` fallback;
  desktop labels now render at readable scale. Window painting is clipped to
  each frame, layout uses the reported framebuffer dimensions, cursor rendering
  avoids trails, and the taskbar reserves space for its live clock.
- Desktop click routing now honors the topmost window before its controls,
  preventing covered windows from receiving close/minimize actions. Files
  entries remain sourced from `/bin`, with visible-row selection and wheel
  scrolling; live memory changes trigger refreshes.
- Memory-map parsers previously collapsed all RAM between the lowest and
  highest map entries, feeding MMIO holes to the physical allocator. The
  PMM now receives strictly the RAM runs and rejects nothing else.
- `vfs_unlink()` in `split_parent()` dropped the first basename character
  of nested paths (`/bin/x` was looked up as `ello`), so unlinking anything
  below the root always failed (this broke `rm` on nested paths and would
  have blocked package removal). Covered by new `vfs-tmpfs` unlink
  round-trip assertions (nested + single-char names).
- `make qemu-test` was a false green: `all` builds only the Limine ISO, so the
  PVH kernel the suite boots was never a prerequisite and never existed. A
  missing kernel made QEMU exit 1, which the `exit 1 = PASS` convention
  reported as success. The target now depends on the kernel it boots and
  fails unless QEMU exits with the pass status.
- The PVH kernel did not link: `fb.c` wrote the framebuffer PML4 slot through
  `x64_pml4`, whose `.early_bss` address is out of reach of a high-half
  RIP-relative displacement (`-mcmodel=kernel` truncated `R_X86_64_PC32`).
  It now reaches the table through the direct map, as `x86_paging.c` does.
- `test_mode=1` only terminated the run on failure; a green run fell through
  into the interactive shell, so the suite's verdict depended on serial EOF.
  Success now prints a summary and exits through `isa-debug-exit` too.
- The `open-close` self-test failed since the ABI switched to Linux `O_*`
  flags: it still passed internal `VFS_O_*` values, so `O_CREATE` was never
  decoded and the create round trip was skipped. The test now speaks the
  syscall ABI, and `SYS_OPEN` rejects the reserved `O_ACCMODE` value 3
  instead of silently treating it as read-write.

### Verified
- `BOOT=PASS` - ISO boots to `NEWOS: boot complete.` under SeaBIOS (legacy
  BIOS path) and under OVMF/UEFI; PVH still passes the automated suite.
- `REPRO`     - `make clean && make iso` twice is byte-for-byte identical
  (ISO `6c84433a...`; ELF `d60467ba...`).

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
