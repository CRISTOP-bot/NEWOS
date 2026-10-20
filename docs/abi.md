# NEWOS user/kernel ABI

Public interface from userland. Owned by `abi/` and kept free of kernel
internals (no `core/*` types).

## Syscall mechanism

`int $0x80` (a DPL-3 trap gate). The dispatcher (`syscall/syscall_dispatch.c`)
runs on the saved user frame; the value it returns lands in the guest's `rax`.

| Register | Meaning |
| --- | --- |
| `rax` | syscall number |
| `rdi rsi rdx r10 r8 r9` | arguments (SysV order), copied from user memory |
| `rax` (return) | `>= 0` success, `< 0` error (negative errno) |

`rcx`/`r11` are clobbered by the instruction itself; all other registers are
preserved. Addresses passed as arguments are validated with
`copy_from_user`/`copy_to_user` (range-checked against the current address
space) before the kernel touches them.

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
| 7 | `SYS_POLL` | compatibility no-op: reports timeout (0), no readiness engine |
| 8 | `SYS_LSEEK` | `SEEK_SET`/`SEEK_CUR`/`SEEK_END` |
| 9 | `SYS_MMAP` | anonymous mappings from the per-process bump cursor |
| 10 | `SYS_MPROTECT` | updates VMM permission flags |
| 11 | `SYS_MUNMAP` | unmaps an anonymous region |
| 12 | `SYS_BRK` | grows/shrinks the user heap |
| 16 | `SYS_IOCTL` | terminal size/mode queries; unknown requests fail |
| 21 | `SYS_ACCESS` | existence check for a user path |
| 22 | `SYS_PIPE` | creates a pipe fd pair |
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
| 76 | `SYS_TRUNCATE` | path-based truncation for backends with `truncate` |
| 77 | `SYS_FTRUNCATE` | fd-based truncation |
| 79 | `SYS_GETCWD` | fails when the buffer is too small |
| 80 | `SYS_CHDIR` | per-process working directory |
| 82 | `SYS_RENAME` | writable in-memory trees only (tmpfs and VFS core directories) |
| 83 | `SYS_MKDIR` | one directory level |
| 87 | `SYS_UNLINK` | files and empty directories; refuses non-empty ones and synthetic trees |
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

Not implemented: `fork`, `execve` and `readlink` return an error. Signal
setup calls are accepted as no-ops; the cooperative kernel never delivers a
signal. `poll` only reports timeout. There is no socket ABI or network stack.
`SYS_RMDIR` is defined but not dispatched; userland removes empty
directories through `unlink`. Conventional file descriptors exist in
`abi/syscall_abi.h` (`STDOUT_FILENO = 1`); open flags (`O_READ`/`O_WRITE`/
`O_CREATE`) and mode bits (`MODE_DIR`/`MODE_REG`) are mirrored in
`user/lib/nshlib.h`.

## Memory model

- User text/data live in `[USER_SPACE_BASE, USER_SPACE_END)`, linked
  static at `0x400000` (`user/lib/app.ld`, no PIC/PIE, no shared objects).
- The user stack is a 1 MiB mapping just below `0x7fffffffe000`.
- `heap`/`mmap` syscalls are not implemented; user binaries are small
  static programs. Userland heap exists inside `libc/` (static 256 KiB
  arena, no `sbrk` yet).

## Program ABI (`user/programs/*`)

Every `/bin` toolbox tool is freestanding (`-O2`, `-ffreestanding`,
`-mgeneral-regs-only`): one `user/programs/<name>/<name>_main.c` with a
standard `main(argc, argv)`, linked with the shared `user/lib/nshlib`
(thin `int $0x80` wrappers + string/output helpers) and the
`user/lib/start.S` entry stub (`_start` forwards `argc/argv` into `main`,
exits with its return code). The only exception is the legacy `hello`
smoke-test, which carries its own entry point and syscall wrappers and
links with its own `user/programs/hello/linker.ld`. The build embeds each
tool as an opaque blob (`_binary_<name>_elf_start...`) and initramfs
exposes it as `/bin/<name>`; `newpkg` (`user/programs/newpkg/`)
additionally links its portable `.new` format core.

Running one:

```
$ make qemu            # serial console
root@newos:/# hello
Hello from ring 3!
ResidentPID: 6
```