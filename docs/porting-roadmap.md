# Userland ports and ARM64 roadmap

This document records what is already available in NEWOS, what was found in
`/home/cristopher/BoredOS`, and the prerequisites for the requested editor,
compiler, and second architecture. A directory or config file is not evidence
that an architecture or program can boot and run.

## Requested programs

### Nano

`nano` currently resolves to the existing Kilo editor ELF in the build. It is
not GNU nano. Kilo offers basic terminal editing, but the alias must not be
presented as a GNU nano port. BoredOS contains Kilo (BSD-2-Clause), not GNU
nano. A genuine nano port needs a terminal/termios compatibility layer,
signals and a sufficiently complete libc; NEWOS currently has only a limited
subset. GNU nano is GPL-3.0-or-later, so its source and notices must be kept
with the port and distributed in a way compatible with that license.

### GCC

`toolchain/` builds a host-executed `x86_64-elf` cross compiler for kernel
development. It is not an executable that runs in NEWOS. A native GCC port
needs a hosted target toolchain and substantial OS support: reliable process
creation and `exec`, large per-process virtual address spaces, `mmap`, signals,
filesystem operations, terminal support, and a usable hosted libc. NEWOS is
missing several of these pieces. The practical bootstrap order is:

1. Complete process and virtual-memory interfaces and broaden the libc.
2. Port a small self-hosted compiler such as TCC as an intermediate compiler.
3. Build a NEWOS-targeting binutils and GCC cross toolchain on the host.
4. Only then attempt native GCC, its assembler/linker tools, and compiler test
   suite inside NEWOS.

BoredOS has TCC, which is a credible intermediate port candidate. Its local
manifest identifies it as LGPLv2; preserve the upstream license, notices, and
any source modifications with the port. This does not make TCC equivalent to
GCC, and it is not yet integrated into NEWOS.

## BoredOS port inventory

| BoredOS component | NEWOS status | Next useful action |
| --- | --- | --- |
| Kilo | Existing editor; `nano` is a build alias for it | Keep the distinction explicit; add genuine GNU nano after terminal/libc work |
| Lua | `user/programs/lua/` exists | Audit runtime coverage and package integration |
| TinyGL | `user/programs/tinygl/` exists | Audit rendering and desktop integration |
| GNU coreutils | Experimental host-built Linux ELF package installs under `/usr/bin`; runtime fails on Linux `syscall`/TLS setup (`#UD` at `syscall`) | Add a real NEWOS libc/runtime port or explicitly implement the required Linux ABI before calling these tools usable |
| TCC | Not ported | Best first on-device compiler candidate; audit LGPLv2 source and libc/ELF needs |
| AHCI and ext4 | NEWOS has AHCI and ext4 source trees, but storage is not a complete persistent user-facing system | Finish block-device/VFS integration and persistence before relying on disk-backed toolchains |
| e1000, RTL8139, VirtIO and lwIP | NEWOS has network protocol/driver scaffolding, not BoredOS-compatible drivers | Port concepts and tests selectively; do not copy kernel code across incompatible driver/VFS APIs |
| Nova compositor, desktop apps and games | No direct port identified | Revisit after a stable windowing, input and graphics application ABI exists |

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
