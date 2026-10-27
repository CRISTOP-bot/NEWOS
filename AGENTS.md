# AGENTS.md — NEWOS agent guide

Linux-independent, from-scratch x86_64 OS: monolithic freestanding kernel, own
libc, `nsh` shell + native `/bin`, native package manager `newpkg`
(`.new`/`.newspec`).
Strict layers: `lib/kernel (iru_*)` < `abi` < `arch`/`core` < `drivers`/`mm`/`fs`
< `process`/`ipc` < `syscall` < `user`. Repo language/docs are English; the
maintainer converses in Spanish.

Deep subsystem "do-not-break" notes live in `AGENT.md` (Spanish) — read it
before touching kernel internals.

## Repo shape (the #1 gotcha)

- `kernel/`, `userland/`, `toolchain/`, `ports/` are git **submodules**. The
  top-level domain dirs are symlinks into them: `arch`→`kernel/arch`,
  `core`→`kernel/core`, … , `libc`→`userland/libc`, `user`→`userland/user`.
  Editing `core/foo.c` edits `kernel/core/foo.c`.
- Commits inside a submodule belong to that submodule's repo
  (`NEWOS-kernel`, `NEWOS-userland`, …): commit there, push, then bump the pin
  from the root repo (`git add kernel`). CI auto-merge refuses to merge while a
  pinned submodule SHA is not reachable from that repo's default branch
  (including the nested `ports/glxgears-newos` pin).
- Every `make` runs `bootstrap` (`git submodule update --init --recursive`),
  which re-detaches submodule HEADs to the pinned SHAs. Land submodule commits
  and bump the pin **before** rerunning full `make`/`bootstrap` to avoid losing
  your HEAD position (commits remain in reflog).
- `toolchain/upstream-gcc` is gigabytes; `make bootstrap-fast` fetches only
  `kernel`, `userland`, `ports`.

## Commands (from repo root; full list: `make help`)

```sh
make all        # bootstrap + x86_64-elf cross toolchain + kernel/Limine ISO + TinyGL demo (~20-40 min first time)
make fast       # host-compiler kernel+ISO build; quickest full-image iteration
make qemu-test  # automated in-QEMU suite (11 phase-1 + 16 phase-2 tests); must be 0 failures
make lint-layers            # layering contract only
make check-newpkg           # package system tests, no QEMU (host build)
make check-nova / check-web # component/apps + website checks (CI runs both)
```

Automated test mode (what `make qemu-test` runs; `run_qemu_test.sh` uses a
`-m 128M` variant of the same invocation):

```sh
qemu-system-x86_64 -kernel build/images/newos-x86_64.elf \
  -serial stdio -no-reboot -m 256M -display none \
  -device isa-debug-exit,iobase=0xf4 \
  -netdev user,id=net0 -device pcnet,netdev=net0 \
  -audiodev driver=wav,id=aud0,path=/tmp/hda.wav,out.frequency=48000,out.channels=2,out.format=s16 \
  -device intel-hda -device hda-duplex,audiodev=aud0 \
  -append "test_mode=1"
```

- `make qemu-test` probes for a WAV recorder backend (`-audiodev
  driver=file` vs `driver=wav` naming differs per QEMU build; the
  recorder's property is `path`, and it ignores the codec side's rate,
  so 48 kHz/stereo/S16 are pinned on the audiodev). Without one the
  phase-2 `hda` ktest skips cleanly (the DMA round self-verifies via
  LPIB); with one, `scripts/ci/run_qemu_test.sh` additionally
  validates the recorded 0.25 s A4 sine on the host.

- Kernel calls `qemu_debug_exit(0)` on pass → QEMU exits **1** (the port maps
  `(code << 1) | 1`); failures exit **>= 3**. Without `test_mode=1` the kernel
  boots into interactive `nsh` (interactive targets need a display/GTK).
- The serial console is authoritative for CI; VGA/fbcon is visual only.
  Screenshot/input recipes (QEMU monitor `screendump`, PS/2 `mouse_move`) and
  headless debugging steps are in `AGENT.md`.

## Layering (`scripts/ci/lint_layering.sh` — runs on every kernel build; details: `docs/development/layering.md`)

- Verified allow-lists: `SYSC`: no `<drivers/...>` (framebuffer/mouse backend
  prototypes live in `include/syscall/syscall.h` for this reason).
  `DRV`→{LIB,ARCH,CORE,MM}; `FS`→ adds DRV; `PROC`→ adds FS,IPC; `IPC`→CORE,MM;
  `libc`/`lib/kernel` are self-contained (+ABI); `user` side may use ABI + LIB.
- `include/*` and system headers are unclassified/free; quoted includes are local.

## Conventions

- One file = one domain; filename prefix by subsystem (`pcnet_`, `vfs_`).
- Kernel **and all userland** compile `-Wall -Wextra -Werror` and
  `-mgeneral-regs-only` (no SSE/FPU; integer-only userland; the glxgears/TinyGL
  port strips `-mgeneral-regs-only`). Freestanding; libc (`libc.a`) has no
  threads/floating-point printf, 256 KiB malloc arena.
- Syscalls: `int $0x80`; numbers/structs in `include/abi/syscall_abi.h`
  (single source of truth; keep Linux x86_64 numbers where reused, NEWOS vendor
  block 450-462; record layouts are size/offset statically asserted). Kernel
  dispatch in `syscall/syscall_dispatch.c`; userland wrappers are named
  `sys_*` in `user/lib/nshlib.{h,c}` (thin `int $0x80` impls).
- New userland program: `userland/user/programs/<cmd>/<cmd>_main.c`, register
  in `PROG_NAMES` (root `Makefile`, rules are generated per name) **and** in
  `bin_table` (`kernel/fs/initramfs/initramfs.c`) so it ships embedded in `/bin`.
- New `*_EMBED` build variables must be declared BEFORE `OBJS :=` in the
  Makefile — it is immediately expanded.
- Version comes from single source `VERSION` (Makefile embeds it kernel+userland).
- Static VFS: no symlinks, so `nsh` runs unshadowed commands as
  `/bin/busybox <applet>` (static BusyBox 1.35.0 via the Linux-compatible
  LSTAR/SYSCALL entry). Optional cross toolchain: `toolchain/build.sh` →
  `toolchain/out` (binutils+GCC x86_64-elf); use with `make CROSS_PREFIX=x86_64-elf-`.

## Verified kernel gotchas (details in AGENT.md)

- Direct map `0xfffffe0000000000` is built ONLY from 2 MiB pages — never 1 GiB:
  VirtualBox hides PDPE1GB and a 1 GiB page triple-faults there. Don't
  "optimize" to 1 GiB pages; tests must pass under QEMU-TCG, VirtualBox and HW.
- Scheduler (`kernel/process/sched.c`): cooperative + tick. After a yield
  handoff is published, return to the interrupt epilogue immediately
  (`if (sched_yield(f)) return 0;`); spinning after it corrupts rip / re-runs
  the epilogue twice (`YIELD-REWIND`, `SLOT-MISMATCH` guards). Read the file
  before touching sleep/blocking paths.
- `copy_user_string` copies byte-by-byte; fixed-size chunk reads over-read
  past the argv area into unmapped pages under `USER_STACK_TOP`.
- PS/2 mouse probe masks IRQ1 while reading the config byte, else the keyboard
  handler steals it ("no mouse detected"). Device Y axis points up; wheel data
  uses 4-byte Intellimouse packets.
- Framebuffer: private 1 GiB window at PML4[507] (`0xFFFFFD8000000000`),
  2 MiB pages with PS bit, PCD|PWT; Limine fb address = HHDM virtual
  (subtract `hhdm->offset`); builtin font is LSB-first CC0 font8x8.
- PIT at 1000 Hz ticks coalesce under QEMU-TCG (uptime/sleep stretch ~20x in
  emulation, correct on real HW). Do not re-fashion the PIT driver for this.

## CI / workflow expectations

- Every push: `git diff --check` (no trailing whitespace/conflict markers),
  lint-layers, check-newpkg, check-nova, check-web, full cross build, multiboot2
  header validation, QEMU boot tests (`.github/workflows/ci-release.yml`).
  PRs run the same checks without publishing.
- The maintainer's working style from `AGENT.md` worth honoring: audit before
  touching (read the real code, never assume APIs), small changes, keep
  `-Werror` + lint-layers green, run `make qemu-test` plus a visual check
  (screenshot) when output changes. Don't commit unless asked; don't leave
  TODO stubs as "implementation".
- Honest scope (don't promise in PR prose): no USB/AHCI/NVMe, no
  MSI/IOAPIC/LAPIC/ACPI, no IP network stack (PCnet loopback only), no codecs
  beyond BMP24/32 + PPM P6 in img/vid (JPEG/PNG rejected).

## Where to look

- `AGENT.md` — verified subsystem deep notes (Spanish), debug/QEMU recipes.
- `docs/build.md` (prereqs, build flow), `docs/architecture.md`,
  `docs/boot.md` (boot protocols), `docs/abi.md`,
  `docs/development/layering.md`, `docs/nova-porting.md` (GUI porting limits),
  `ROADMAP.md` (what not to duplicate work on).
