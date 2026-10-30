# Userland ports and ARM64 roadmap

This document records what is already available in NEWOS, what was found in
`/home/cristopher/BoredOS`, and the prerequisites for the requested editor,
compiler, and second architecture. A directory or config file is not evidence
that an architecture or program can boot and run.

## Requested programs

### Nano

`/bin/nano` is a genuine GNU nano (8.4, GPL-3.0-or-later) port, not an alias:
the vendored fork lives in `ports/nano`, the binary is a static musl
`ET_EXEC` built with a static ncursesw (fallback `linux`/`xterm` terminfo
compiled in), and it runs through the Linux `syscall` entry like
`/bin/busybox`. The terminal work it needed is in the kernel: `pselect6`/
`select`/`poll` readiness with timeouts, `fsync`/`fdatasync` as no-op
successes, `chmod`/`fchmod` recorded on the inode, and `readlink` returning
`-EINVAL` so realpath works without symlinks. fbcon implements the ANSI/VT100
subset ncurses emits (SGR 16/256 colors, bold, reverse, cursor movement,
erase/insert/delete line and chars) and paints a real reverse-video block
cursor. GNU nano replaces the retired Kilo editor at `/bin/nano`; initramfs
installs `/usr/etc/nanorc` with autoindent, line numbers and C/C++/shell/
Makefile/nanorc highlighting. Limits: no spell checker and no external
filters (`fork`/`execve` do not exist). See `ports/nano/README.md`.

### Compilers

`toolchain/` builds a host-executed `x86_64-elf` cross compiler used to build
NEWOS itself. It is not an executable that runs in NEWOS, and it is the only
GCC in the project. Its GCC source is pinned as the optional, shallow
`toolchain/upstream-gcc` submodule at the GCC 16.1.0 release commit;
`toolchain/build.sh` uses it when initialized and version-matched, otherwise
it retains the release-tarball fallback.

The on-system compiler is **TinyCC**, not GCC: `/bin/tcc` is TinyCC 0.9.27
(LGPL-2.1-or-later) built from the vendored fork in `ports/tcc`. `tcc -c`
writes x86-64 ELF objects, and `tcc -nostdlib -run /etc/tcc-hello.c` compiles,
relocates and runs a freestanding program **in the same process** — no
`fork`/`execve` needed. TCC's x86-64 image base (`0x400000`) matches the NEWOS
user load address. A full `tcc hello.c -o hello` link needs the NEWOS SDK
(headers, `crt0.o`, `libc.a`) on board; that package is built by `make sdk-pkg`
and embedded at `/tmp/newos-sdk.new`. TCC is not GCC: no C++/Objective-C, no
full libtcc1, and no programs that need libc without the SDK.

A native GCC port is **not planned**: it needs a hosted target toolchain and
substantial OS support (reliable `fork`/`exec`, large address spaces, signals,
a hosted libc), while TCC already compiles and runs code on the machine. See
`ports/tcc/README.md`.

## BoredOS port inventory

| BoredOS component | NEWOS status | Next useful action |
| --- | --- | --- |
| Kilo | Retired; GNU nano 8.4 is `/bin/nano` | No action (see `ports/nano/README.md`) |
| Lua | `user/programs/lua/` exists | Audit runtime coverage and package integration |
| TinyGL | `ports/glxgears-newos` builds a real 300-frame demo; `/bin/glxgears` is embedded in every ISO | Keep the NEWOS math/libc shim small; audit TinyGL for larger demos |
| GNU coreutils | Static musl package installs under `/usr/bin` and runs through the Linux `syscall` entry (tested `echo`, `arch`, `true`); individual tools may still need missing Linux interfaces | Document per-tool limits; a real NEWOS libc port remains the long-term path |
| TCC | Ported: `/bin/tcc` 0.9.27 (LGPL-2.1), `-c` + in-process `-run` | SDK package (headers/crt0/libc.a) for full links; keep license/source with `ports/tcc` |
| GNU nano | Ported: `/bin/nano` 8.4 (GPL-3.0+), static ncursesw, kernel select/fsync/chmod support, fbcon colors and `/usr/etc/nanorc` syntax highlighting | Spell checker and external filters stay out until fork/exec exists |
| AHCI and ext4 | NEWOS has AHCI and ext4 source trees, but storage is not a complete persistent user-facing system | Finish block-device/VFS integration and persistence before relying on disk-backed toolchains |
| e1000, RTL8139, VirtIO and lwIP | NEWOS has a PCnet driver and loopback self-test, but no IP stack or socket ABI | Port concepts and tests selectively; implement the network stack before promising BoredOS-compatible networking |

BoredOS is GPL-3.0. Its kernel and userland code must not be copied into the
proprietary NEWOS codebase without resolving the project's licensing model.
For each candidate, check its own license rather than assuming the enclosing
repository's license applies. Reimplementing an interface or porting a
permissively licensed standalone component is a separate decision.

## ARM64 status and implementation sequence

`configs/arm64/debug.config` is only a config placeholder. There is currently
no `arch/arm64/` implementation and the Makefile's boot, assembly, linker and
QEMU paths are x86-64-specific. ARM64 compatibility is therefore not
implemented yet.

An ARM64 port needs, at minimum:

1. Architecture-neutral kernel interfaces for CPU state, interrupt control,
   context switching, timers, page tables, atomics and boot memory maps.
2. AArch64 boot entry and linker layout, exception vectors, EL setup, MMU,
   interrupt controller/timer, context switch and user syscall entry/return.
3. A boot protocol and QEMU `virt` machine path, initially serial-only.
4. Architecture-specific compiler flags and user ELF ABI, plus ARM64 CI/QEMU
   boot and syscall tests.
5. Drivers and graphics after a stable serial console, memory, timer and
   interrupt path work.

Keep x86-64 as the working reference until ARM64 can independently build,
boot, run the kernel self-tests, and launch the same shell. Do not enable
`ARCH=arm64` as a supported build before those gates pass.
