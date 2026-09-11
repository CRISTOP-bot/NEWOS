# Booting x86_64 NEWOS

The kernel is a freestanding x86_64 ELF. It can be reached through two
boot protocols, both parsed at runtime with no firmware dependency beyond
what the boot manager already did:

- **multiboot2** — GRUB (`multiboot2 /boot/newos.elf`, see the `iso` target).
  Dword 0 of the image must be the magic `0xe85250d6`; `arch_main` receives
  `(eax, ebx)` = (magic, physical address of the info struct).
- **PVH** — QEMU's `-kernel` path constructs a `hvm_start_info`; `arch_main`
  probes physical memory for the magic `0x336ec578` in `start_info` and falls
  back to multiboot2 when it is not found.

## Stage 1: 32-bit trampoline (`x86_entry.S`)

Starts at `_start` (the linker entry). Runs BSP-only, before C:

1. Generates early four-level paging in `.early_bss` (BSS, not in the ELF
   file): identity map of the low 1 GiB, plus a high-half window so the
   kernel image at `0xffffffff80000000` is visible.
2. Compares `eax` against the multiboot2 magic; stores magic + info address
   on the early stack for `arch_main`.
3. Enables PAE + long mode (CR4.PAE, EFER.LMA), loads the early PML4 into
   CR3, enables paging, and `ret`-far jumps to the 64-bit C entry point.

## Stage 2: `arch_main` (`x86_boot.c`)

Runs on the pre-built early stack and boot page tables:

1. `x86_get_loaders()` — detects multiboot2 vs PVH and copies the memory map
   (`x86_multiboot2.c` / `x86_pvh.c`) into a normalized bootinfo.
2. `pmm_allocator_init(mem_start, mem_end)` — seeds the physical allocator;
   the loader map only hands out usable RAM.
3. `x64_gdt_init` / `x64_tss_install` — sets up GDT + TSS (RSP0 for syscalls).
4. `x86_idt_init` — installs the IDT with gate types for trap/int, including
   the `int $0x80` user syscall gate.
5. `x64_paging_init` — builds the kernel address space:
   - direct map of physical memory at `0xfffffe0000000000`
   - kernel image mapped at its link address `0xffffffff80000000`
   - pages marked NX; final `invlpg`/`mov cr3` switches to it.
6. `mm_heap_init()` — early 4 MiB arena for the kernel heap.
7. `core_init` — mounts `initramfs` (a `/bin/hello` + `/init` hierarchy built
   into the image), devfs and tmpfs, sets up processes/threads, registers the
   syscall table, runs self-tests, and finally launches `/bin/hello`.

## Terminal boot log

```
NEWOS x86_64 boot: entry(magic=..., info=...)
boot: PVH protocol detected ...
PMM: tracking N frames (M MiB) from physical 100000
VMM: kernel address space ready (CR3=..., direct map at 0xfffffe0000000000)
...
=== 5 tests, 0 failures ===
userland: entering hello (pid 2) at ring 3
Hello from ring 3!
ResidentPID: 2
NEWOS: boot complete.
```

## Multiboot2 header

Located in `.multiboot2` (`x86_boot.S`): magic, architecture 0, header length,
checksum, and a `multiboot2_header_tag_info_request` so the loader always
provides a memory map. `grub-file --is-x86-multiboot2 build/images/newos-x86_64.elf`
validates it in CI.