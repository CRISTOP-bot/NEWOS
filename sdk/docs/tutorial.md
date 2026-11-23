# Tutorial: from `main()` to a running NEWOS program

## 0. Prerequisites

A NEWOS checkout with the submodules initialised and the host build tools from
`../docs/build.md` (GCC, binutils, Python 3). Run `make -C sdk check` once: it
fails loudly if the generated sysroot is missing.

## 1. Write a program

```c
/* hello.c */
#include <stdio.h>
#include <newos/nsh.h>

int main(int argc, char **argv)
{
    (void)argc;
    printf("hello, pid %ld\n", sys_getpid());
    return 0;
}
```

`main` is a normal function. The SDK `crt0` receives `(argc, argv)` from the
kernel, aligns the stack, calls `main`, and passes the return value to
`sys_exit`. Do not define your own `_start` unless you set `NEWOS_CRT0=none`.

## 2. Build it

```sh
sdk/bin/newos-cc -O2 -o /tmp/hello.elf hello.c
file /tmp/hello.elf
# ELF 64-bit LSB executable, x86-64, statically linked
readelf -l /tmp/hello.elf | grep LOAD
```

The `ld` warning about an RWX `LOAD` segment is expected: `app.ld` has no
`PHDRS` block. The kernel loader maps `PT_LOAD` segments with the flags it
derives from the ELF program headers.

To use the cross compiler instead of host GCC:

```sh
make CROSS_PREFIX=x86_64-elf- sdk
NEWOS_CC=x86_64-elf-gcc sdk/bin/newos-cc -o /tmp/hello.elf hello.c
```

## 3. Run it

Two supported paths:

1. **Build it into the image.** Add the program to `user/programs/<name>/` and
   register it in `PROG_NAMES` and `initramfs.c` (see the root `AGENTS.md`).
   That makes it a built-in `/bin/<name>`.
2. **Install it as a package.** Continue to step 4; this is the path for apps
   developed outside the OS tree.

Boot with `make qemu` (serial console) or `make qemu-limine-gui` (graphics)
and run the program from `nsh`:

```
root@newos:/# /tmp/hello
hello, pid 7
```

## 4. Package it

```sh
sdk/bin/newos-package /tmp/hello.elf \
    --name hello-sdk --version 1.0.0 \
    --desc "My first NEWOS app" \
    --out /tmp/hello-sdk-1.0.0-x86_64.new
```

In the guest:

```
newpkg info  /tmp/hello-sdk-1.0.0-x86_64.new
newpkg verify /tmp/hello-sdk-1.0.0-x86_64.new
newpkg install /tmp/hello-sdk-1.0.0-x86_64.new
hello-sdk
```

The default install destination is `/bin/<name>`; override it with `--dest`.
`newpkg` keeps a manifest in `/var/lib/newpkg/` so `remove` and upgrades stay
consistent. Details in `packaging.md`.

## 5. Debug checklist

| Symptom | Likely cause |
| --- | --- |
| `newos-cc: missing sysroot/...` | run `make sdk` (or `make -C sdk`) first |
| Loader says "dynamic/PIE image" | you dropped `-fno-pie`; let the driver set flags |
| Instant triple fault | `_start` clobbered the kernel's argc/argv frame; use the supplied crt0 |
| Program exits with a weird code | `main` fell off the end without `return`; `void main` is not supported |
| `no framebuffer in this boot` | PVH/GRUB boot; use Limine for a panel |
| `undefined reference to sys_*` | link with libnsh.a (the driver does this in link mode) |

## 6. Going further

- `04-sysinfo` shows the NEWOS vendor syscalls; `05-framebuffer` shows the GUI
  ownership protocol; `06-processes` shows spawning.
- Read `api.md` before reaching for a POSIX call that may be a no-op here.
- For a real port with floating point, see how DOOM and TinyGL are built in
  the root `Makefile` (`NEWOS_FPU=1` equivalent flags).
