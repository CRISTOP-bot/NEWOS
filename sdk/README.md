# NEWOS Application SDK

Everything a host developer needs to write, build and package a native
userspace program for NEWOS. It is a thin, honest layer over the real build
inputs — no hidden runtime, no second libc, no fake compatibility shim:

| Piece | Source of truth |
| --- | --- |
| C headers | `libc/include` + `include/abi/syscall_abi.h` (copied into `sysroot/usr/include`) |
| `libc.a` | `libc/src` built with the exact userland flags |
| `libnsh.a` | `user/lib/nshlib.c` + `nshfmt.c` (the `sys_*` syscall wrappers) |
| `crt0` | `sdk/crt0/crt0.S`, functionally identical to `user/lib/start.S` |
| Link script | `user/lib/app.ld` (snapshotted into `sysroot/usr/lib`) |
| Compiler driver | `sdk/bin/newos-cc` |
| Packager | `sdk/bin/newos-package` → `tools/newpkg/newpkg-build.py` |

This directory lives in the superproject, not in a submodule, so changing it
does not require a submodule commit/pin bump.

## Quickstart (host)

```sh
make -C sdk check          # populates sysroot/ and builds all 6 examples
ls sdk/build/*.elf
```

Build your own program:

```sh
cp -r sdk/template ~/myapp
cd ~/myapp
make NEWOS_SDK=/home/cristopher/newos/sdk
make NEWOS_SDK=/home/cristopher/newos/sdk package
```

Or use the driver directly:

```sh
sdk/bin/newos-cc -O2 -o /tmp/myapp main.c
sdk/bin/newos-cc -c main.c -o main.o      # compile only
```

Copy the resulting static ELF to the target (for example via `newpkg install
/tmp/myapp.new` from `nsh`) and run it. `main`'s return value becomes the
process exit code.

## Layout

```
sdk/
├── README.md                 this file
├── bin/
│   ├── newos-cc              host compiler driver (GCC by default)
│   └── newos-package         .new archive builder for finished programs
├── crt0/crt0.S               GAS entry stub (no nasm needed)
├── include/newos/nsh.h       public syscall API umbrella
├── examples/
│   ├── 01-hello              stdio hello world
│   ├── 02-args               argc/argv + strtol
│   ├── 03-files              fopen/fprintf/fgets/stat/remove
│   ├── 04-sysinfo            uname, sysinfo, RTC, getpid (libnsh)
│   ├── 05-framebuffer        FBINFO/FBWRITE/CONSOLE_GUI
│   └── 06-processes          SPAWN3 + WAITPID
├── template/                 copyable standalone app + Makefile + packaging
└── docs/
    ├── tutorial.md           write → build → run → package, step by step
    ├── api.md                libc + syscall API reference
    └── packaging.md          the .new format and installation workflow
```

## Compiler driver

`newos-cc` injects the flags the in-tree userland already uses:

```
-O2 -ffreestanding -nostdlib -fno-pic -fno-pie -fno-stack-protector
-fno-builtin -fno-asynchronous-unwind-tables -fno-exceptions
-mgeneral-regs-only -Wall -Wextra -isystem <sysroot>/usr/include
```

In link mode it adds `crt0.o`, `-T app.ld`, `libnsh.a` and `libc.a`. Useful
knobs:

| Variable | Effect |
| --- | --- |
| `NEWOS_SYSROOT` | override the SDK tree (default `<repo>/sysroot`) |
| `NEWOS_CC` | compiler binary (default `${CROSS_PREFIX:-}gcc`) |
| `CROSS_PREFIX` | e.g. `x86_64-elf-` when using the built cross toolchain |
| `NEWOS_CRT0` | alternate crt0 source, or `none` to define `_start` yourself |
| `NEWOS_FPU=1` | drop `-mgeneral-regs-only` for math-heavy ports |
| `NEWOS_OPT` | optimisation flag (default `-O2`) |

`newos-cc --help`, `--print-sysroot` and `--print-flags` report the effective
setup.

## On-board compilation

The SDK is also packaged as a `.new` archive and embedded in the image at
`/tmp/newos-sdk.new`. Install it in a booted system, then use `/bin/tcc`:

```
newpkg install /tmp/newos-sdk.new
tcc -static -nostdlib -I/usr/lib/tcc/include -I/usr/include \
    -o /tmp/hello /usr/lib/crt0.o hello.c /usr/lib/libnsh.a /usr/lib/libc.a
```

`make sdk-pkg` regenerates that archive. The on-board path and the host path
share the same headers and libraries, so an app can be developed either way.

## What NOT to expect

NEWOS is a small freestanding OS. The SDK deliberately refuses to paper over
these limits:

- Static `ET_EXEC` ELF64 only — no PIE, shared objects, `dlopen` or dynamic
  loader.
- No `fork`/`execve`; use `sys_spawn3()`/`sys_waitpid()`.
- Integer-only by default (`-mgeneral-regs-only`); floats need `NEWOS_FPU=1`
  or the on-board compiler.
- Single-user VFS with no symlinks and no permission enforcement.
- No signals, no sockets/IP stack, no USB/AHCI/NVMe.
- Each `read`/`write` syscall moves at most 512 bytes; libc loops so `fread`/
  `fwrite` are fine.
- No framebuffer on PVH boots; `SYS_FBINFO` reports `present == 0`.

See `docs/tutorial.md` to start, `docs/api.md` for the callable surface, and
`../docs/abi.md` for the raw syscall contract.
