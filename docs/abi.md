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
| 1 | `SYS_WRITE` | `write(fd, buf, len)` | writes to the fd's file (devfs console device); validates the flat buffer copy |
| 2 | `SYS_EXIT` | `exit(code)` | `__noreturn`; never returns |
| 3 | `SYS_GETPID` | `getpid()` | returns the calling process's pid |

Conventional file descriptors exist in `abi/syscall_abi.h` (`STDOUT_FILENO = 1`).

## Memory model

- User text/data live in `[USER_SPACE_BASE, USER_SPACE_END)`.
- The user stack is a cpu-instruction-normal 4 KiB-range kernel-allocated
  mapping just below the top of the user range.
- `heap`/`mmap` are not implemented; user binaries are small, position-static
  `-Ttext=0x400100` programs with no libc.

## Program ABI (`user/programs/hello`)

`hello_main.c` is freestanding (-O2, `-ffreestanding`, no libc). It
implements its own tiny `syscall0/1/2/3` inline wrappers and `_start`
that calls `main` and passes the result to `SYS_EXIT`. It is linked with
`-T user/programs/hello/linker.ld`, embedded into the kernel as an opaque
blob (`_binary_hello_elf_start...`), and exposed by initramfs as `/bin/hello`.

Running it:

```
$ make qemu            # serial console
Hello from ring 3!
ResidentPID: 2
NEWOS: boot complete.
```