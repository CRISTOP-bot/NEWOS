# NEWOS user/kernel ABI

Public interface from userland. Owned by `abi/` and kept free of kernel
internals (no `core/*` types).

## Syscall mechanism

NEWOS user code uses `int $0x80` (a DPL-3 trap gate). The dispatcher
(`syscall/syscall_dispatch.c`) runs on the saved user frame; the value it
returns lands in the guest's `rax`. For compatibility with static Linux
binaries, the CPU also programs `STAR`/`LSTAR`/`FMASK` and provides a real
x86-64 `syscall` entry (`arch/x86_64/interrupts/x86_interrupt.S`) that shares
the same frame layout and dispatcher. `readv`/`writev` are handled by
`syscall/syscall_posix.c`.

| Register | Meaning |
| --- | --- |
| `rax` | syscall number |
| `rdi rsi rdx r10 r8 r9` | arguments (SysV order), copied from user memory |
| `rax` (return) | `>= 0` success, `< 0` error (negative errno) |
| `rcx`/`r11` | clobbered by `syscall`; preserved by `int $0x80` |

Addresses passed as arguments are validated with `copy_from_user`/
`copy_to_user` (range-checked against the current address space) before the
kernel touches them. The static Linux compatibility path currently returns
`-1` (`SYSCALL_RET_ERROR`) for most failures rather than a specific `-errno`;
successful BusyBox paths are exercised, but apps that inspect `errno` may see
an imprecise value.

## Syscall table

Numbers are the Linux x86_64 values for the compatible subset NEWOS
implements; the NEWOS-specific calls use the vendor block starting at 450.
The authoritative definitions live in `include/abi/syscall_abi.h`; dispatch
is split between `syscall/syscall_dispatch.c` (core and vendor calls) and
`syscall/syscall_posix.c` (Linux compatibility calls).

| Nr | Name | Notes |
| --- | --- | --- |
| 0 | `SYS_READ` | fd 0 blocks on the console; other fds read through the VFS; each call moves at most 512 bytes |
| 1 | `SYS_WRITE` | writes to the fd's file; each call moves at most 512 bytes |
| 2 | `SYS_OPEN` | only `O_READ`/`O_WRITE`/`O_CREATE`; per-process fd allocation |
| 3 | `SYS_CLOSE` | releases the fd |
| 4 | `SYS_STAT` | path metadata (`nsh_stat`) |
| 5 | `SYS_FSTAT` | metadata for an open fd |
| 6 | `SYS_LSTAT` | same as `stat`; symlinks are not implemented |
| 7 | `SYS_POLL` | console/pipe readiness with a millisecond timeout; VFS files are always ready |
| 8 | `SYS_LSEEK` | `SEEK_SET`/`SEEK_CUR`/`SEEK_END` |
| 9 | `SYS_MMAP` | anonymous mappings from the per-process bump cursor |
| 10 | `SYS_MPROTECT` | updates VMM permission flags |
| 11 | `SYS_MUNMAP` | unmaps an anonymous region |
| 12 | `SYS_BRK` | grows/shrinks the user heap |
| 16 | `SYS_IOCTL` | terminal size/mode queries; unknown requests fail |
| 19 | `SYS_READV` | scatter read through the existing per-fd read path |
| 20 | `SYS_WRITEV` | gather write through the existing per-fd write path |
| 21 | `SYS_ACCESS` | existence check for a user path |
| 22 | `SYS_PIPE` | creates a pipe fd pair |
| 23 | `SYS_SELECT` | legacy select on fds 0..63; timeval timeout, console/pipe readiness |
| 28 | `SYS_MADVISE` | accepted no-op |
| 32 | `SYS_DUP` | duplicates an fd into the first free slot |
| 33 | `SYS_DUP2` | duplicates into a specific slot |
| 35 | `SYS_SLEEP` | Linux `nanosleep`; deadline survives re-traps and yields to peers |
| 39 | `SYS_GETPID` | calling process's pid |
| 60 | `SYS_EXIT` | `__noreturn`; never returns |
| 61 | `SYS_WAITPID` | Linux `wait4` subset; returns the child's exit code |
| 62 | `SYS_KILL` | pid 0/1 are refused; no signal delivery beyond termination |
| 63 | `SYS_UNAME` | `NEWOS`, `NEWOS_VERSION`, `x86_64` |
| 72 | `SYS_FCNTL` | fd flags subset |
| 74/75 | `SYS_FSYNC`/`SYS_FDATASYNC` | no-op success (the VFS is RAM-backed) |
| 76 | `SYS_TRUNCATE` | path-based truncation for backends with `truncate` |
| 77 | `SYS_FTRUNCATE` | fd-based truncation |
| 79 | `SYS_GETCWD` | fails when the buffer is too small |
| 80 | `SYS_CHDIR` | per-process working directory |
| 82 | `SYS_RENAME` | writable in-memory trees only (tmpfs and VFS core directories) |
| 83 | `SYS_MKDIR` | one directory level |
| 87 | `SYS_UNLINK` | files and empty directories; refuses non-empty ones and synthetic trees |
| 89 | `SYS_READLINK` | always `-EINVAL`: the VFS has no symlinks (what `realpath` expects) |
| 90/91 | `SYS_CHMOD`/`SYS_FCHMOD` | records the mode on the inode; enforcement is future work |
| 92/93 | `SYS_CHOWN`/`SYS_FCHOWN` | accepted no-ops (single-user model) |
| 95 | `SYS_UMASK` | accepted; reports the fixed 022 mask |
| 96 | `SYS_GETTIMEOFDAY` | real CMOS wall clock |
| 100 | `SYS_TIMES` | process times subset |
| 102/104/107/108 | `SYS_GETUID/GID/EUID/EGID` | single-user model; all return 0 |
| 110 | `SYS_GETPPID` | parent pid |
| 111 | `SYS_GETPGRP` | reports the pid as the process group |
| 112 | `SYS_SETSID` | reports the pid; no session table yet |
| 131 | `SYS_SIGALTSTACK` | accepted no-op |
| 158 | `SYS_ARCH_PRCTL` | TLS base management |
| 217 | `SYS_GETDENTS64` | directory entries |
| 218 | `SYS_SET_TID_ADDRESS` | returns the pid |
| 228 | `SYS_CLOCK_GETTIME` | monotonic/realtime clock subset |
| 231 | `SYS_EXIT_GROUP` | same exit path as `SYS_EXIT` |
| 262 | `SYS_NEWFSTATAT` | `(dirfd, path, stat, flags)` metadata |
| 270 | `SYS_PSELECT6` | what musl's `select()` uses: fds 0..63, timespec timeout |
| 318 | `SYS_GETRANDOM` | xorshift-seeded kernel bytes |
| 450 | `SYS_SPAWN` | legacy spawn without argv |
| 451 | `SYS_SPAWN2` | spawn with real argc/argv (up to 16 x 128 chars) |
| 452 | `SYS_SPAWN3` | `spawn2` plus stdin/stdout fd redirection |
| 453 | `SYS_FBINFO` | panel geometry; `present == 0` on non-Limine boots |
| 454 | `SYS_FBWRITE` | blit XRGB8888 pixels at `(x, y, w, h)`, rows copied separately |
| 455 | `SYS_MOUSE_GET` | PS/2 pointer state |
| 456 | `SYS_PS` | process table records |
| 457 | `SYS_READDIR` | legacy packed `nsh_dirent` form |
| 458 | `SYS_GETTIME` | legacy CMOS `nsh_time` form |
| 459 | `SYS_SYSINFO` | total/free frames, uptime and tick rate |
| 460 | `SYS_KBD_GRAB` | foreground PS/2 keyboard ownership (`rdi=1` grab, `0` release) |
| 461 | `SYS_KBD_READ` | non-blocking bytes from the GUI keyboard queue |
| 462 | `SYS_CONSOLE_GUI` | suspend/resume fbcon and the legacy cursor for a framebuffer compositor |

`SYS_KBD_GRAB` is meant for full-screen sessions such as `desktop`. While a
process owns the grab, decoded PS/2 bytes go to its private queue instead of
the shell console; serial input keeps working normally. Process teardown
releases the grab automatically, so a crashed GUI cannot leave the keyboard
stolen. `SYS_KBD_READ` returns 0 when the queue is empty and never blocks.

`SYS_CONSOLE_GUI` takes `rdi=1` to claim the panel and `rdi=0` to release it.
A second owner is refused. While claimed, kernel `printk`/fbcon output and the
kernel's legacy mouse arrow do not touch the framebuffer (serial output and
klog still receive the text); this is what keeps `proc:` lines and a duplicate
cursor from appearing over `desktop` or `glxgears`. Text programs launched by
the desktop release the claim first. Process teardown releases it
automatically, including after a crash.

Not implemented: `fork` and `execve` return an error. `readlink` always
returns `-EINVAL` (there are no symlinks). Signal
setup calls are accepted as no-ops; the cooperative kernel never delivers a
signal. `poll`/`select`/`pselect6` report console and pipe readiness but are
not a general event engine (only fds 0..63, no epoll). There is no socket ABI
or network stack.
`SYS_RMDIR` is defined but not dispatched; userland removes empty
directories through `unlink`. Conventional file descriptors exist in
`abi/syscall_abi.h` (`STDOUT_FILENO = 1`); open flags (`O_READ`/`O_WRITE`/
`O_CREATE`) and mode bits (`MODE_DIR`/`MODE_REG`) are mirrored in
`user/lib/nshlib.h`.

## Memory model

- User text/data live in `[USER_SPACE_BASE, USER_SPACE_END)`, linked
  static at `0x400000` (`user/lib/app.ld`, no PIC/PIE, no shared objects).
- The user stack is a 1 MiB mapping just below `0x7fffffffe000`.
- `SYS_MMAP` supports anonymous mappings, with `MAP_FIXED` refused over an
  occupied range; file-backed mappings are not implemented. `SYS_BRK` grows
  or shrinks the image heap up to `PROCESS_MMAP_BASE`. `SYS_MUNMAP` and
  `SYS_MPROTECT` operate on mapped pages. The freestanding libc still uses its
  static 256 KiB arena rather than `sbrk`.

## ELF64 loader profile (kernel/process/elf_loader.c)

What the kernel accepts (`elf_validate` → `elf_load_segments` →
`elf_entry_valid`, the same path every exec-style spawn uses — `/bin/*`
toolbox tools, `busybox`, and anything `/bin/desktop` launches):

- ELF64 (`ELFCLASS64`), little-endian (`ELFDATA2LSB`), `EM_X86_64`,
  `ET_EXEC` **static** images only. Dynamic objects (`ET_DYN`, PIE or
  shared) and any image that carries `PT_INTERP` are rejected with an
  explicit console reason — NEWOS has no dynamic loader, so silently
  loading them would only fault later inside the process.
- Program headers: `PT_LOAD` only is mapped; each segment gets frames per
  page, the BSS tail (`p_memsz > p_filesz`) is zero-filled and pages are
  mapped `VMM_USER` + NX/W rules derived from `p_flags` (W → writable,
  no X → NX). `p_offset`/`p_filesz` are bounds-checked overflow-safe
  (`p_offset > size || p_filesz > size - p_offset`; the ELF data comes
  from VFS and must not be trusted). Entry must land on a present,
  user, executable page.
- Everything else present in the image (`PT_GNU_STACK`, `PT_GNU_RELRO`,
  `PT_NOTE`, …) is left alone.
- `PT_TLS` is detected and **warned, not rejected**: the loader ignores
  the header and the program is expected to set its own FS base with
  `SYS_ARCH_PRCTL` (158). Static musl binaries do exactly that for
  their self-contained TCB, which is how `busybox` runs. There is no
  kernel-side TLS allocation yet.
- Syscalls of loaded programs go through the ambient dispatcher: both
  the native `int $0x80` vendor block 450-462 and the Linux-number
  compatibility entry (`SYS_ARCH_PRCTL` 158 lives there) share
  `kernel/syscall/syscall_dispatch.c`; `SYS_EXECVE` (59) is a stub.
  Syscall numbers/records are statically asserted in
  `include/abi/syscall_abi.h`.

The loader is intentionally not a relocation engine, an `ld.so`, or a
shared-library loader: no dynamic linking, no `dlopen`, no
relocation processing.

## Program ABI (`user/programs/*`)

Every native `/bin` toolbox tool is freestanding (`-O2`, `-ffreestanding`,
`-mgeneral-regs-only`): one `user/programs/<name>/<name>_main.c` with a
standard `main(argc, argv)`, linked with the shared `user/lib/nshlib`
(thin `int $0x80` wrappers + string/output helpers) and the
`user/lib/start.S` entry stub (`_start` forwards `argc/argv` into `main`,
exits with its return code). The exceptions are the legacy `hello`
smoke-test, which carries its own entry point and syscall wrappers, and
`glxgears`, which is linked with FPU-enabled flags plus `libc/src/math.c`
for TinyGL. The build embeds each tool as an opaque blob
(`_binary_<name>_elf_start...`) and initramfs exposes it as `/bin/<name>`;
`newpkg` (`user/programs/newpkg/`) additionally links its portable `.new`
format core.

`/bin/busybox` is not built from the NEWOS ABI; it is the official static
Linux x86_64-linux-musl 1.35.0 binary, run through the `syscall` entry and
the Linux-number compatibility subset. The source URL, GPLv2 license and
SHA-256 checksums are recorded in `ports/busybox/`.

Running one:

```
$ make qemu            # serial console
root@newos:/# hello
Hello from ring 3!
ResidentPID: 6
```