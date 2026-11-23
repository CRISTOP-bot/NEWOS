# NEWOS userspace API reference

This is a map of what is actually callable. The headers are the contract:
`<...>` names below resolve inside `sysroot/usr/include`. For the raw syscall
numbers and record layouts see `../../docs/abi.md` and
`<newos/syscall_abi.h>`.

## libc (`libc.a`)

Freestanding, single-threaded, mostly integer-only. No host libc symbols leak
in; `printf` is NEWOS's own formatter.

| Header | What you get |
| --- | --- |
| `<stdio.h>` | `printf/fprintf/sprintf/snprintf/v*`, `puts/putchar`, unbuffered `FILE` streams (`fopen/fread/fwrite/fgets/fseek/ftell/fflush/...`), `perror`, `getline`, `remove/rename` |
| `<stdlib.h>` | `malloc/calloc/realloc/free`, `atoi/atol/strtol/strtoul`, `rand/srand`, `abs`, `div`, `atexit`, `exit/abort`, `getenv/setenv`, `system` (fails honestly — no process creation in libc) |
| `<string.h>` | full `mem*`/`str*` family incl. `strdup`, `strtok`, `strcasecmp` |
| `<unistd.h>` | `read/write/close/lseek/access/sbrk`, `STD*_FILENO`, `isatty` (always true) |
| `<fcntl.h>` | `open(path, flags, ...)` with Linux `O_*` values |
| `<sys/stat.h>` | `stat/lstat/fstat/mkdir`, Linux-layout `struct stat`, `S_IS*` macros |
| `<time.h>` | `time/gettimeofday/clock_gettime/clock`, `gmtime/localtime/mktime/strftime`, `difftime` |
| `<sys/time.h>`, `<sys/types.h>` | `timeval`, `ssize_t`, `off_t`, `size_t` |
| `<errno.h>` | global `errno` + common `E*` codes |
| `<ctype.h>`, `<assert.h>`, `<setjmp.h>`, `<locale.h>`, `<signal.h>` | the usual small surface; signals are accepted but never delivered |
| `<math.h>` | available, but compiled with FP flags — see "Floating point" below |
| `<sys/mman.h>`, `<sys/ioctl.h>`, `<termios.h>` | thin syscall facades |

Notes that matter in practice:

- `FILE` has no user-space buffer. `fwrite` maps to `write`, which moves at
  most 512 bytes per syscall; libc loops so you do not have to.
- The heap is a fixed arena (`sbrk` exists but libc does not switch to it).
  Do not assume unbounded `malloc`.
- `stdin` is the console; there is no environment block at startup, so
  `getenv()` starts empty.
- `system()` and `signal()` are stubs that report failure rather than
  pretending to work.

## Syscalls (`libnsh.a`, `<newos/nsh.h>`)

Thin `int $0x80` wrappers. Most return `>= 0` on success and a negative value
on error; check `< 0`, not just `-1`.

### Console and files

```
sys_read(fd, buf, len)        sys_write(fd, buf, len)
sys_open(path, flags)         sys_close(fd)
sys_lseek(fd, off, whence)    sys_stat(path, &nsh_stat)
sys_readdir(path, &nsh_dirent, bytes)
sys_chdir(path)               sys_getcwd(buf, size)
sys_mkdir(path)               sys_unlink(path)
sys_rename(old, new)          sys_pipe(fds[2])
```

`nshlib.h` mirrors a smaller `O_READ/O_WRITE/O_CREATE` set for its own
helpers; libc headers expose the full Linux `O_*` values. Use one style
consistently.

### Processes and time

```
sys_getpid()                  sys_sleep(ms)
sys_spawn2(path, argv, argc)  sys_spawn3(path, argv, argc, fdin, fdout)
sys_waitpid(pid)              sys_kill(pid)
sys_ps(buf, bytes)            sys_uname(&nsh_uname)
sys_gettime(&nsh_time)        sys_sysinfo(&nsh_sysinfo)
```

`fork` and `execve` are not implemented. `spawn2/3` loads a static ELF
directly from the VFS. The kernel returns the child's exit code from
`sys_waitpid`.

### Graphics and input (vendor block 450-462)

```
sys_fbinfo(&nsh_fbinfo)       sys_fbwrite(&nsh_fbwrite)
sys_mouse_get(&nsh_mouse)
sys_kbd_grab(1|0)             sys_kbd_read(buf, len)
sys_console_gui(1|0)
```

Protocol:

1. `sys_fbinfo` — `present == 0` on PVH/GRUB boots. Only 24/32 bpp panels are
   supported by the NEWOS graphics stack; `sys_fbwrite` always takes XRGB8888
   input and blits rows separately.
2. `sys_console_gui(1)` — claim the panel so the kernel's text console and
   legacy cursor stop painting. A second owner is refused. Always release
   with `sys_console_gui(0)`; process teardown also releases it.
3. `sys_kbd_grab(1)` — route decoded PS/2 bytes into your private queue;
   `sys_kbd_read` is non-blocking (returns 0 when empty). Release on exit.
4. `sys_mouse_get` returns a snapshot; `seq` increments per hardware packet,
   device Y points up, and the record carries wheel delta.

The panel is an uncacheable window with no double buffer, so tearing on
partial repaints is inherent.

### nshlib string/output helpers

If you do not want libc, `libnsh.a` also provides `nstrlen/nstrcmp/nstrcpy/...`,
`nmemcpy/nmemset`, `natoi/natou`, `nputc/nputs/putdec/puthex/putf/fputf` and
`fail(prog, msg)`. These are what the built-in `/bin` tools use.

## Floating point

The default SDK flags include `-mgeneral-regs-only`: no SSE/FPU instructions.
Integer code, `printf("%d")`, `strtol`, `malloc` etc. are unaffected.

If you need `math.h`, `%f` formatting or `strtod`:

```sh
NEWOS_FPU=1 sdk/bin/newos-cc -o app.elf app.c
```

`libc.a` already contains FP-enabled objects for `math.c`, `printf_flo.c`,
`strtod.c` and `time.c`; they are only linked when referenced. For a large
port, study the DOOM/TinyGL rules in the root `Makefile` — they use the same
relaxation. Programs with FPU state must be prepared for the kernel's
cooperative scheduler; ordinary SDK apps should stay integer-only.

## ABI stability

`<newos/syscall_abi.h>` is copied from `include/abi/syscall_abi.h`, the single
source of truth. It statically asserts record sizes and offsets (`struct
nsh_stat == 144`, `nsh_fbwrite == 48`, ...). If you add a syscall, edit that
header, bump the consumer headers by re-running `make sdk`, and keep the
kernel dispatcher in sync.
