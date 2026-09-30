# NEWOS cross toolchain

Pinned `x86_64-elf` (bare-metal) toolchain so the OS build does not drift
with the host compiler: **binutils 2.44 + GCC 16.1.0 (C only) + newlib
4.6.0.20260123**.

- gcc 16.x (not older): 14.x/15.1 sources miscompile under a gcc-16
  host (`libcody` char8_t errors, no `--disable-libcody` switch);
  16.1 matches the host generation and builds cleanly there.
- newlib (the target libc) is pinned to the datestamped release
  `newlib-4.6.0.20260123.tar.gz`. Upstream only publishes that form,
  there is no bare `4.6.0` tarball.
- Downloads land in `toolchain/out/src/` (cached); the install prefix
  is `toolchain/out/` (git-ignored, survives `make clean`).

## Build (one time, ~20-40 min on 2 cores; the newlib stage itself is ~4 min)

```sh
bash toolchain/build.sh --jobs 2   # or: make toolchain
```

Resumable: re-running skips finished stages (installed tools win).
Needs GMP/MPFR/MPC headers plus flex/bison on the host. newlib is not
on the GNU mirrors: it comes from SourceWare
(`https://www.sourceware.org/pub/newlib`, fallback without `www`), which
`build.sh` keeps as its own mirror list.

## Use

```sh
export PATH="$PWD/toolchain/out/bin:$PATH"
make CROSS_PREFIX=x86_64-elf- all        # kernel + userland
make CROSS_PREFIX=x86_64-elf- qemu-test  # must end: QEMU exit status: 1
```

`CROSS_PREFIX` empty (default) keeps host `gcc`/`ld`; `nasm` is always
the host assembler. `bash toolchain/test.sh` smoke-tests the install
without building the whole tree.

This is a **host-side cross toolchain** for building NEWOS. Its GCC and
binutils executables run on Linux and emit `x86_64-elf` code. To compile a
static C application against the NEWOS ABI and libc, use the accompanying
driver after building the cross compiler:

```sh
make toolchain
toolchain/newos-gcc -o build/hello.elf toolchain/examples/hello.c
readelf -h build/hello.elf | grep 'Type:'   # EXEC (Executable file)
```

The driver stages the NEWOS headers, C runtime entry, syscall shim, and
`libc.a` under `build/newos-sdk/`. Its output is a static ELF executable
for the current x86_64 NEWOS loader; it is not a Linux program. Install the
ELF into an NEWOS filesystem/package to run it. The underlying GCC and
binutils still run on the host. A compiler that itself runs inside NEWOS is
a separate bootstrap stage requiring more process, virtual-memory, and
hosted-libc support.

## Target libc (newlib)

GCC is still configured `--without-headers --with-newlib`, so newlib is
built as a separate stage afterwards; without it the cross compiler is
freestanding-only (even `#include <stdio.h>` fails) and no real upstream
C software can be compiled. The stage installs into the same prefix:

- headers -> `toolchain/out/x86_64-elf/include/` (found automatically by
  `x86_64-elf-gcc`, it is on its system include path)
- libraries -> `toolchain/out/x86_64-elf/lib/`: `libc.a`, `libm.a`,
  `libg.a`, plus `libnosys.a` and `nosys.specs` from libgloss.

Configure line used by the stage (the two `--disable-newlib-*` flags are
the ones that matter, `x86_64-elf` is accepted as-is by newlib's
configure, which normalizes it to `x86_64-pc-elf`):

```sh
../newlib-4.6.0.20260123/configure --target=x86_64-elf --prefix=toolchain/out \
    --disable-nls --disable-multilib \
    --disable-newlib-supplied-syscalls --disable-newlib-multithread \
    --enable-newlib-mb
```

- `--disable-newlib-supplied-syscalls`: newlib must not ship its own
  syscall layer in `libc.a`. NEWOS supplies the stubs itself, written
  against the NEWOS `int $0x80` ABI, in its own support library (that
  glue is not part of the toolchain). What `libc.a`/`libm.a` reference
  once this flag is on is the plain POSIX set, *not* the historic
  `_read`/`_write` spelling (only `_exit` keeps its underscore):
  `close, execve, _exit, fork, fstat, getentropy, getpid, gettimeofday,
  isatty, kill, link, lseek, open, read, sbrk, stat, times, unlink,
  wait, write`. Until that glue exists, `-specs=nosys.specs` (libgloss's
  `libnosys.a`, which is never on the default link line) resolves them
  to "always fails" placeholders so the link itself can be tested.
- `--disable-newlib-multithread`: NEWOS userland has no threads, so the
  reentrant locking machinery is dead weight. Note the upstream spelling
  is `multithread`; `--disable-newlib-multithreading` is silently
  ignored by `configure`.
- `--enable-newlib-mb`: compiles the Unicode width tables into the wide
  character layer. Without it `wcwidth()` still exists but degenerates
  to 1/0/-1 (printable/control/non-printable) and never reports the
  double width of CJK characters.
- `--disable-multilib`: matches the binutils and gcc stages (the cross
  gcc reports a single multilib anyway).
- Float `printf` is left enabled (no `--disable-newlib-io-float`), so
  `%f/%e/%g` and `_dtoa_r` are in `libc.a`; `-lm` is *not* on the
  default link line, add it explicitly.

Two caveats for anything being ported:

- No `crt0.o`: libgloss has no x86_64-elf BSP and newlib installs no crt
  objects, so `x86_64-elf-gcc -static prog.c` fails with `cannot find
  crt0.o`. NEWOS will provide its own startup; until then link with
  `-nostartfiles -Wl,-e,main`.
- Feature-test macros: newlib exports only the baseline surface by
  default (`__XSI_VISIBLE` is 0 unless you ask for more), so things like
  `wcwidth()` are present in `libc.a` but declared only when compiling
  with `-D_GNU_SOURCE` or `-D_XOPEN_SOURCE=700` (`-D_DEFAULT_SOURCE` is
  not enough). GCC 16 makes an implicit declaration a hard error, so
  ported software has to pass the feature macros its build system
  normally uses.

Probe a real link without touching the OS tree:

```sh
printf '#include <stdio.h>\nint main(void){printf("%%8.3f\\n",3.0/2.0);}\n' > /tmp/p.c
x86_64-elf-gcc -static -nostartfiles -Wl,-e,main -specs=nosys.specs \
    -o /tmp/p /tmp/p.c -lm
readelf -h /tmp/p | grep Type:      # EXEC (Executable file), not PIE
```
