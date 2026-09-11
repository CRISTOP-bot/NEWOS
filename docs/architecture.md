# NEWOS architecture

NEWOS is a monolithic hobby OS for x86_64. The kernel is built as a single
freestanding ELF loaded either by a multiboot2 boot loader (GRUB) or directly
as a PVH payload (QEMU's `-kernel` with the PVH start-info struct). It
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
| `arch/x86_64/` | `x86_*` | Machine-facing code: boot trampoline + entry, page tables, GDT/TSS, IDT, PIC, paging, context switch. Public arch headers live flat in `arch/x86_64/include/`. |
| `drivers/` | `serial_16550`, `tty_console`, `driver_core`, `device_core`, `qemu_debug` | Hardware-facing services behind the device model. |
| `core/` | `core_printk`, `core_panic`, `core_oops`, `core_cmdline`, `core_init`, `core_selftest` | Kernel core: logging, panic/oops, cmdline parsing, boot orchestration, self-tests. |
| `mm/` | `mm_pmm_{alloc,bitmap,buddy}`, `mm_frame`, `mm_address_space`, `mm_vmm`, `mm_heap`, `mm_kmalloc`, `mm_usercopy`, `mm_debug` | Physical memory manager (bitmap + buddy), 4-level page tables, VMM address spaces, kernel heap, user copy helpers. |
| `fs/` | `vfs_{core,inode}`, `tmpfs`, `devfs`, `initramfs` | VFS plus in-memory filesystem backends and the built-in root filesystem. |
| `process/` | `proc_process`, `proc_thread`, `elf_loader` | Processes, threads (kernel-stack layout, context save/restore), ELF loading into a user address space. |
| `ipc/` | `ipc_pipe` | Inter-process primitives (pipes). |
| `syscall/` | `syscall_dispatch` | The `int $0x80` dispatcher switching on `rax`. |
| `user/` | `programs/hello/hello_main.c` | Userland programs (freestanding, linked against the ABI). |

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

1. QEMU/GRUB loads the ELF. The 32-bit trampoline (`x86_entry.S`) builds
   early four-level paging (identity low 1 GiB + high-half kernel window),
   switches to long mode and jumps to `arch_main` (`x86_boot.c`).
2. `arch_main` reads the boot info: either a multiboot2 info struct
   (`x86_multiboot2.c`) or the PVH start-info (`x86_pvh.c`), producing a
   normalized RAM map.
3. `pmm_allocator_init` seeds the physical allocator from that map; GDT/TSS,
   IDT and the serial console come up; the VMM kernel address space is built
   (direct map at `0xfffffe0000000000`).
4. `core_init` mounts the root filesystem (initramfs over tmpfs, devfs),
   initializes processes/threads, registers the syscall table, runs the
   in-kernel self-tests and finally launches `/bin/hello` in ring 3.

See `docs/boot.md` for details and `docs/abi.md` for the user interface.

## Address map (x86_64)

- Kernel image: `0xffffffff80000000` high-half.
- Direct map / kernel address space: `0xfffffe0000000000` (phys + KDIRECT).
- User space: `[USER_SPACE_BASE, USER_SPACE_END)`, stack just below the top.