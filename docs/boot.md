# Booting x86_64 NEWOS

The kernel is a freestanding x86_64 ELF. It can be reached through three
boot protocols, all parsed at runtime with no firmware dependency beyond
what the boot manager already did:

- **multiboot2** — GRUB (`multiboot2 /boot/newos.elf`, see the `iso` target).
  Dword 0 of the image must be the magic `0xe85250d6`; `arch_main` receives
  `(eax, ebx)` = (magic, physical address of the info struct).
- **PVH** — QEMU's `-kernel` path constructs a `hvm_start_info`; `arch_main`
  probes physical memory for the magic `0x336ec578` in `start_info` and falls
  back to multiboot2 when it is not found.
- **Limine** — the `newos-x86_64-limine.elf` variant (separate link via
  `scripts/limine.ld`) requests entry point, HHDM, memory map, command
  line and **framebuffer**; `limine_arch_main` (`arch/x86_64/boot/limine.c`)
  takes over paging (Limine is already in long mode), copies the usable
  runs, snapshots the first framebuffer and rejoins the common
  `kernel_boot_tail`. Only this path enables graphics (`fbcon` +
  `img`/`vid`); the other two keep VGA text + serial.

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
   (`x86_multiboot2.c` / `x86_pvh.c`) into normalized RAM ranges.
2. `pmm_allocator_init_ranges(runs, nruns)` — seeds the physical allocator
   from every usable run; MMIO holes are never handed out as free memory.
   (The single-range `pmm_allocator_init()` remains only as a thin wrapper.)
3. `x64_gdt_init` / `x64_tss_install` — sets up GDT + TSS (RSP0 for syscalls).
4. `x86_idt_init` — installs the IDT with gate types for trap/int, including
   the `int $0x80` user syscall gate.
5. `x64_paging_init` — builds the kernel address space:
   - direct map of physical memory at `0xfffffe0000000000`
   - kernel image mapped at its link address `0xffffffff80000000`
   - pages marked NX; final `invlpg`/`mov cr3` switches to it.
6. `mm_heap_init()` — early 4 MiB arena for the kernel heap.
7. `core_init` — mounts `initramfs` (the `/bin` toolbox + `/init` hierarchy
   built into the image, plus `/var/lib/newpkg` and a sample `.new` staged
   under `/tmp`), devfs and tmpfs, sets up processes/threads, registers the
   syscall table, runs self-tests, and finally launches `/init`
   (interactive shell) — or `/bin/hello` in `test_mode`.

## Terminal boot log

```
NEWOS x86_64 boot: entry(magic=..., info=...)
boot: PVH protocol detected ...
PMM: tracking N frames (M MiB) from physical 100000
VMM: kernel address space ready (CR3=..., direct map at 0xfffffe0000000000)
...
=== 6 tests, 0 failures ===
=== 8 tests, 0 failures ===
userland: entering init (pid 4) at ring 3
root@newos:/#
NEWOS: boot complete.
```

On Limine the log starts with `NEWOS x86_64 boot: Limine boot protocol`
plus `limine: framebuffer WxH ...` and `fb: ... UC window ...` lines, and
the same text is mirrored on the graphics screen by fbcon.

## Multiboot2 header

Located in `.multiboot` (`x86_entry.S`): magic, architecture 0, header length,
checksum, and a `multiboot2_header_tag_info_request` so the loader always
provides a memory map. `grub-file --is-x86-multiboot2 build/images/newos-x86_64.elf`
validates it in CI.