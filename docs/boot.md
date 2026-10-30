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
  `kernel_boot_tail`. Only this path enables graphics (`fbcon`, `img`, `vid`,
  `glxgears` and `desktop`); the other two keep VGA text + serial. The Limine
  menu shows a 2-second branded screen at 1280x800 with the NEWOS/Win95
  wallpaper and `interface_branding`, with two entries: **NEWOS (text
  shell)** (the default) and **NEWOS (GUI)**.

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
6. `mm_heap_init()` — early 48 MiB arena for the kernel heap.
7. `kernel_boot_tail` — mounts `initramfs` (the `/bin` toolbox + `/init`
   hierarchy built into the image, plus `/var/lib/newpkg` and a sample
   `.new` staged under `/tmp`), devfs, tmpfs, procfs and sysfs; `core_init`
   then sets up processes/threads, registers the syscall table, runs
   self-tests, and finally launches `/bin/sh` (interactive shell) with the
   `--session` flag — or runs only the suites in `test_mode`.

## Graphical session (`gui=1`)

Both entries boot the same kernel. The default **NEWOS (text shell)** entry
passes no graphical flag, so the system always starts in `nsh`. The **NEWOS
(GUI)** entry passes `gui=1`; `make qemu-limine-gui` builds and boots that
entry directly, and a user can also type `/bin/desktop` in the text shell.
`make qemu-limine-test` overrides the command line with `test_mode=1`. The
kernel still enters the shell, which reads `/proc/cmdline` and, only for the
boot session (`--session`) with `gui=1`, spawns `/bin/desktop` and waits for
it. Closing the last window or pressing **Ctrl+Q** returns to a clean `nsh`
prompt. A missing framebuffer or a failed spawn leaves the shell in place, so
graphics can never strand the machine.

The desktop owns the PS/2 keyboard through `SYS_KBD_GRAB`/`SYS_KBD_READ`
while it runs. `Tab` cycles windows, `1`–`5` focus one, arrows move through
`/bin`, `Enter` runs the selected program, `?` toggles the help card and
`Ctrl+Q` exits. Mouse drag, focus, resize, maximize, minimize, close, wheel
scrolling and the `FILES` launcher work as before.
Serial input remains available to the shell at all times. The compositor only
repaints and uploads dirty rectangles; startup, theme changes and structural
layout changes are the only full-screen redraws. The cursor is a staging-buffer
overlay; old and new cursor rectangles are always uploaded before the current
sprite is stamped, so pointer motion cannot leave a trail.

While the desktop is active it also claims the framebuffer text console with
`SYS_CONSOLE_GUI`. Kernel logs and child stdout still reach the serial console,
but fbcon and the legacy kernel mouse arrow do not paint over compositor
pixels. Text programs launched from the dock or Files temporarily release the
claim so their output is visible; graphical programs such as `/bin/glxgears`
keep it. Process teardown releases both claims, so a crashed GUI cannot leave
the keyboard or console hidden.

From a plain `nsh` session the built-in demo can be started directly:

```sh
glxgears             # 300 frames, then returns to nsh
glxgears --frames 60 # shorter run
```

`/bin/busybox --list` reports the 402 embedded applets. Unshadowed names are
dispatched as `/bin/busybox <applet>` because NEWOS has no symlinks.

## Terminal boot log

```
NEWOS x86_64 boot: entry(magic=..., info=...)
boot: PVH protocol detected ...
PMM: tracking N frames (M MiB) from physical 100000
VMM: kernel address space ready (CR3=..., direct map at 0xfffffe0000000000)
...
=== 11 tests, 0 failures ===
=== 9 tests, 0 failures ===
userland: entering sh (pid 4) at ring 3
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