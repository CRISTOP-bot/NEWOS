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

| Nr | Name | Signature | Notes |
| --- | --- | --- | --- |
| 0 | `SYS_READ` | `read(fd, buf, len)` | fd 0 blocks on the console; other fds read through the VFS; each call moves at most 512 bytes |
| 1 | `SYS_WRITE` | `write(fd, buf, len)` | writes to the fd's file; each call moves at most 512 bytes |
| 2 | `SYS_EXIT` | `exit(code)` | `__noreturn`; never returns |
| 3 | `SYS_GETPID` | `getpid()` | returns the calling process's pid |
| 4 | `SYS_SLEEP` | `sleep(ms)` | deadline survives re-traps; yields while peers run |
| 5 | `SYS_SPAWN` | `spawn(path, name)` | legacy spawn without argv (see `SPAWN2`) |
| 6 | `SYS_WAITPID` | `waitpid(pid)` | returns the child's exit code; unknown pids fail instead of hanging |
| 7 | `SYS_OPEN` | `open(path, flags)` | only `O_READ`/`O_WRITE`/`O_CREATE`; per-process fd allocation |
| 8 | `SYS_CLOSE` | `close(fd)` | releases the fd |
| 9 | `SYS_MKDIR` | `mkdir(path)` | creates one directory (see `mkdir -p` in userland) |
| 10 | `SYS_READDIR` | `readdir(path, buf, bytes)` | returns the child count (capped by the buffer) |
| 11 | `SYS_CHDIR` | `chdir(path)` | per-process working directory |
| 12 | `SYS_GETCWD` | `getcwd(buf, size)` | fails when the buffer is too small |
| 13 | `SYS_UNLINK` | `unlink(path)` | removes files and empty directories; refuses non-empty ones |
| 14 | `SYS_SPAWN2` | `spawn2(path, argv, argc)` | spawn with real argc/argv (up to 16 x 128 chars) |
| 15 | `SYS_PS` | `ps(buf, bytes)` | returns the process count |
| 16 | `SYS_KILL` | `kill(pid)` | pid 0/1 (init) are refused |
| 17 | `SYS_UNAME` | `uname(struct nsh_uname *)` | `NEWOS`, release, `x86_64` |
| 18 | `SYS_GETTIME` | `gettime(struct nsh_time *)` | real CMOS wall clock, 24h |
| 19 | `SYS_SYSINFO` | `sysinfo(struct nsh_sysinfo *)` | total/free frames, uptime, tick rate |
| 20 | `SYS_FBINFO` | `fbinfo(struct nsh_fbinfo *)` | panel geometry; `present == 0` on non-Limine boots |
| 21 | `SYS_FBWRITE` | `fbwrite(const struct nsh_fbwrite *)` | blit caller XRGB8888 pixels at `(x, y, w, h)`; rows copied separately, bounds-checked |
| 22 | `SYS_RENAME` | `rename(oldpath, newpath)` | Linux number 82; moves or replaces, in-memory trees only (tmpfs, devfs, sysfs) |

Not implemented (fail as unknown): `lseek`/`stat`/`truncate`,
`mmap`/`sbrk`, sockets, signals. Conventional file descriptors exist in
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