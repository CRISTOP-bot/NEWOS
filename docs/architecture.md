# NEWOS architecture

NEWOS is a monolithic hobby OS for x86_64. The kernel is built as a single
freestanding ELF loaded as a PVH payload (QEMU's `-kernel`), by a
multiboot2 boot loader (GRUB), or by Limine (BIOS/UEFI ISO). It
boots, sets up its own paging, runs kernel self-tests, and launches one or
more userland programs in ring 3.

## Source layout

Every source file has one home directory; the directory is its **domain**.
Filenames carry the domain prefix; the same concept is never spelled under
two names (see `docs/development/layering.md` for the naming rules).

| Domain | Files | Contents |
| --- | --- | --- |
| `abi/` | `syscall_abi.h`, `multiboot2.h` | Public ABI: syscall numbers/registers, multiboot2 boot structures. Self-contained (`<stdint.h>`), usable by userland. |
| `lib/kernel/` | `iru_*` | Generic kernel utilities: string, memory, bitmap, intrusive list, lock, ring buffer, printf-style formatter. |
| `libc/` | `string`, `stdlib`, `stdio`, `malloc` | Freestanding C library for userland (no floats/threads); static `libc.a`, self-contained + ABI. |
| `arch/x86_64/` | `x86_*` | Machine-facing code: boot trampoline + entry, page tables, GDT/TSS, IDT, PIC, paging, context switch. Public arch headers live flat in `arch/x86_64/include/`. |
| `drivers/` | `serial_16550`, `tty_console`, `vga_text_console`, `fb`, `driver_core`, `device_core`, `qemu_debug`, `pci_*`, `pcnet`, `ps2_*`, `pit_timer`, `cmos_rtc` | Hardware-facing services behind the device model: serial/VGA/fb consoles, PCI bus, PCnet NIC, PS/2 input, timers. |
| `core/` | `core_printk`, `core_panic`, `core_oops`, `core_cmdline`, `core_init`, `core_selftest` | Kernel core: logging, panic/oops, cmdline parsing, boot orchestration, self-tests. |
| `mm/` | `mm_pmm_{alloc,bitmap,buddy}`, `mm_frame`, `mm_address_space`, `mm_vmm`, `mm_heap`, `mm_kmalloc`, `mm_usercopy`, `mm_debug` | Physical memory manager (bitmap + buddy), 4-level page tables, VMM address spaces, kernel heap, user copy helpers. |
| `fs/` | `vfs_{core,inode}`, `tmpfs`, `devfs`, `initramfs` | VFS plus in-memory filesystem backends and the built-in root filesystem. |
| `process/` | `proc_process`, `proc_thread`, `elf_loader`, `sched` | Processes, threads (kernel-stack layout, context save/restore), cooperative+tick scheduler, ELF loading into a user address space. |
| `ipc/` | `ipc_pipe` | Inter-process primitives (pipes). |
| `syscall/` | `syscall_dispatch` | The `int $0x80` dispatcher switching on `rax` (syscalls 0-21, incl. `FBINFO`/`FBWRITE`). |
| `user/` | `programs/*/*_main.c` + `lib/nshlib` | Freestanding userland: `init` (nsh shell) plus the 30-tool `/bin` toolbox (`ls`, `cat`, …, `img`, `vid`, `newpkg`), linked against the ABI only. |
| `packages/` + `tools/newpkg/` | `*.newspec`, `newpkg-build.py`, tests | Native `.new` packages: host builder, install database at `/var/lib/newpkg`, first package `hello-new` (see `docs/packages.md`). |

## Dependency contract

Layering is upward-only and enforced by `scripts/ci/lint_layering.sh` on every
build:

```
L0 lib/kernel   {LIB,CORE}              base building blocks
L1 abi          {ABI}                   public, self-contained
L2 arch/*       permissive              boot orchestration
L3 core/        permissive              init orchestration
L4 drivers  ->  {LIB,ARCH,CORE,MM}      can use the heap, not the VFS
L5 mm       ->  {LIB,ARCH,CORE,MM}      never reaches drivers/fs/process
L6 fs       ->  {LIB,CORE,MM,DRV,FS}    devfs may register a device
L7 process  ->  {LIB,ARCH,CORE,MM,FS,PROC}
L8 ipc      ->  {LIB,CORE,IPC}
L9 syscall  ->  {ABI,LIB,ARCH,CORE,MM,FS,PROC,SYSC}
L10 user    ->  {ABI,LIB}                userland knows only the ABI
```

A domain may import its own tier and any tier below. arch/ and core/ are
permissive because boot and init exist only to wire the whole system
together; they are the exception, not the rule.

## Boot flow

1. QEMU/GRUB/Limine loads the ELF. On the native path the 32-bit
   trampoline (`x86_entry.S`) builds early four-level paging (identity
   low 1 GiB + high-half kernel window), switches to long mode and jumps
   to `arch_main` (`x86_boot.c`); on the Limine path
   (`limine_arch_main`) the bootloader already provides long mode and
   paging plus HHDM, memory map, command line and framebuffer.
2. `arch_main` reads the boot info: a multiboot2 info struct
   (`x86_multiboot2.c`) or the PVH start-info (`x86_pvh.c`), producing a
   normalized RAM map.
3. `pmm_allocator_init` seeds the physical allocator from that map; GDT/TSS,
   IDT and the serial console come up; the VMM kernel address space is built
   (direct map at `0xfffffe0000000000`).
4. On Limine boots the panel is mapped uncacheable into a private window
   and the fbcon text backend registers next to serial/VGA text.
5. `core_init` mounts the root filesystem (initramfs over tmpfs, devfs),
   initializes processes/threads, registers the syscall table, runs the
   in-kernel self-tests (6 phase-1 + 8 phase-2) and finally launches
   `/init` (interactive shell) or `/bin/hello` in `test_mode`.

See `docs/boot.md` for details and `docs/abi.md` for the user interface.

## Address map (x86_64)

- Kernel image: `0xffffffff80000000` high-half.
- Direct map / kernel address space: `0xfffffe0000000000` (phys + KDIRECT).
- Framebuffer UC window: `0xfffffd8000000000` (PML4[507], 2 MiB pages).
- User space: `[USER_SPACE_BASE, USER_SPACE_END)`, stack just below the top.

## Shell and graphics

- `nsh` (`/init`): UTF-8 line editing with history and `Ctrl+A/C/D/E/K/L/U/V/W/Y`
  (kill-ring clipboard for cut/paste), builtins plus spawned `/bin` tools.
- `img /etc/splash.bmp|*.ppm`: native BMP (24/32-bit) + PPM viewer on the
  Limine framebuffer (`FBINFO`/`FBWRITE`); `vid`: procedural animation
  player. JPEG/PNG need a userspace codec (no heap/FPU yet) and are
  refused with a message.

## Why this layout (decision record)

A physical reorganization into `kernel/`, `sched/`, `graphics/`, `gui/`,
`libc/`, `shell/`, `commands/` or `rootfs/` directories was audited and
**rejected**: the domain system above (prefix per directory, layering
enforced **by path** in `scripts/ci/lint_layering.sh`, explicit ledger in
the Makefile with immediate `:=` expansion, relative
`../../lib/nshlib.h` includes in the 30 userland tools) makes bulk moves
high-risk churn with no decoupling gain. Multi-topic files (`sched.c`,
`x86_boot.c`, `syscall_dispatch.c`, `vfs_core.c`) are only correct together
with their neighbors; empty `gui/`/`libc/`/`storage/` trees would be
placeholder content against `docs/development/layering.md`. The rootfs is
code-generated at boot (`fs/initramfs/`), not a directory. Revisit only
with a committed tree and per-file risk acceptance.