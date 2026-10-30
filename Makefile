# NEWOS Build System
# ============================================================================
# A complete, layered build system for the NEWOS operating system.
#
# Primary targets (full list: make help):
#   make / make all  - fetch pinned submodules, build x86_64-elf toolchain,
#                      Limine system ISO, and the NEWOS TinyGL demo
#   make bootstrap   - fetch all pinned component and nested source submodules
#   make fast        - build only the system ISO with host tools (no GCC build)
#   make limine      - Limine-flavour kernel (BIOS+UEFI graphics path)
#   make iso         - GRUB multiboot2 ISO
#   make qemu-test   - automated test suite in QEMU (exit 1 = PASS)
#   make toolchain   - build the x86_64-elf cross toolchain under toolchain/out
#   make clean       - remove build/ artifacts (not source modules or toolchain)
#
# All source checkouts and generated files stay under this repository.
# The desktop is built from NEWOS code (desktop_surface/desktop_widgets);
# no external compositor or widget toolkit is fetched.
# Host packages listed in docs/build.md remain prerequisites.
#
# Options: V=0 (short log), V=1 (full commands, default),
#   ARCH=x86_64, CROSS_PREFIX=x86_64-elf-, CONFIG=path/to/config

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
ARCH            ?= x86_64
CONFIG          ?= configs/$(ARCH)/debug.config
# Single source of truth for the version string embedded in the kernel and
# userland (see VERSION). NEWOS_VERSION_ID drops the development suffix.
NEWOS_VERSION    := $(shell cat VERSION)
NEWOS_VERSION_ID := $(firstword $(subst -, ,$(NEWOS_VERSION)))
SUPPORTED_ARCHES := x86_64
ifeq ($(filter $(ARCH),$(SUPPORTED_ARCHES)),)
$(error ARCH='$(ARCH)' is not implemented yet; supported architecture: x86_64 (see docs/porting-roadmap.md))
endif
# Cross toolchain prefix for direct/low-level targets (empty = host gcc/ld).
# The complete `make all` pipeline builds and uses x86_64-elf- automatically;
# `make fast` deliberately uses the host compiler for a quicker system build.
CROSS_PREFIX    ?=
BUILD_DIR       := build
KERNEL_BIN      := $(BUILD_DIR)/images/newos-$(ARCH).elf
ISO_IMG         := $(BUILD_DIR)/images/newos-$(ARCH).iso
DISK_IMG        := $(BUILD_DIR)/images/newos-$(ARCH).img

# Limine boot flavour: the same kernel objects, linked high-half only
# (scripts/limine.ld) so Limine maps it without a slide.
LIMINE_BIN      := $(BUILD_DIR)/images/newos-$(ARCH)-limine.elf
LIMINE_ISO      := $(BUILD_DIR)/images/newos-$(ARCH)-limine.iso
LIMINE_ROOT     := $(BUILD_DIR)/limine-root
LIMINE_BINS     := $(BUILD_DIR)/limine-bins
# The kernel always boots nsh.  An empty cmdline therefore means a text
# session; the Limine menu also exposes a separate "NEWOS (GUI)" entry with
# gui=1, and users can run /bin/desktop from nsh.  The test target overrides
# this with test_mode=1 (which keeps the shell and never starts the desktop).
LIMINE_CMDLINE  ?=

CC              := $(CROSS_PREFIX)gcc
LD              := $(CROSS_PREFIX)ld
AR              := $(CROSS_PREFIX)ar
NASM            := nasm
QEMU            := qemu-system-x86_64
# Interactive QEMU targets use GTK without a host-drawn pointer: grab-on-hover
# hides the host cursor while the guest is focused and show-cursor=off stops
# QEMU from painting a second arrow over the NEWOS compositor cursor.
QEMU_DISPLAY    ?= -display gtk,grab-on-hover=on,show-cursor=off

# ---------------------------------------------------------------------------
# Verbosity: V=1 (default) prints every command in full; V=0 prints only
# short one-line tags ("  CC  file.c"). Usage: make all / make all V=0.
# ---------------------------------------------------------------------------
V ?= 1
ifeq ($(V),0)
Q := @
E := @echo
else
Q :=
E := @:
endif

CFLAGS          := -c -Wall -Wextra -Werror -Wno-unused-parameter -ffreestanding \
                   -nostdlib -fno-stack-protector -fno-pic -fno-pie -mno-red-zone \
                   -mno-sse -mno-mmx -fno-builtin -fno-exceptions \
                   -fno-asynchronous-unwind-tables -mgeneral-regs-only \
                   -mcmodel=kernel \
                   -DNEWOS_VERSION='"$(NEWOS_VERSION)"' \
                   -DNEWOS_VERSION_ID='"$(NEWOS_VERSION_ID)"' \
                   -O2 -g -MMD

ASFLAGS         := -f elf64
LDFLAGS         := -n --gc-sections -T arch/$(ARCH)/linker.ld

# ---------------------------------------------------------------------------
# Source discovery
#
# Explicit per-domain ledger. Every list here is verified against actual
# files at runtime by the build; do not add wildcards that match nothing
# (dead globs silently break the build).
# ---------------------------------------------------------------------------

# arch/<plat>: machine-facing implementation
ARCH_SRCS_C     := arch/$(ARCH)/boot/x86_boot.c arch/$(ARCH)/boot/x86_multiboot2.c \
                   arch/$(ARCH)/boot/x86_pvh.c arch/$(ARCH)/boot/limine.c \
                   arch/$(ARCH)/cpu/x86_cpuid.c \
                   arch/$(ARCH)/cpu/x86_fpu.c \
                   arch/$(ARCH)/cpu/x86_vendor.c arch/$(ARCH)/cpu/x86_gdt.c \
                   arch/$(ARCH)/interrupts/x86_idt.c \
                   arch/$(ARCH)/interrupts/x86_pic.c arch/$(ARCH)/interrupts/x86_irq.c \
                   arch/$(ARCH)/memory/x86_paging.c \
                   arch/$(ARCH)/memory/x86_devmap.c
ARCH_SRCS_ASM   := arch/$(ARCH)/boot/x86_entry.S arch/$(ARCH)/boot/limine_entry.S \
                   arch/$(ARCH)/interrupts/x86_interrupt.S \
                   arch/$(ARCH)/threading/x86_context.S \
                   arch/$(ARCH)/threading/x86_fpu.S

# core: kernel core (log, oops, init) and privileged subsystems
CORE_SRCS       := core/core_panic.c core/core_printk.c process/elf_loader.c \
                    core/core_cmdline.c core/core_init.c \
                    core/core_selftest.c core/core_oops.c \
                    core/core_console.c core/core_time.c \
                    process/proc_process.c syscall/syscall_dispatch.c \
                    syscall/syscall_posix.c \
                    process/proc_thread.c process/sched.c \
                    sys/wait_queue.c sys/work_queue.c sys/pty.c sys/tty.c

# mm: memory-management domain
MM_SRCS         := mm/mm_heap.c mm/mm_kmalloc.c mm/mm_debug.c \
                    mm/mm_slab/slab.c mm/mm_pmm_alloc.c mm/mm_pmm_bitmap.c \
                    mm/mm_pmm_buddy.c mm/mm_frame.c mm/mm_usercopy.c \
                    mm/mm_address_space.c mm/mm_vmm.c

# storage: VFS and backends
FS_SRCS         := fs/devfs/devfs.c fs/initramfs/initramfs.c fs/tmpfs/tmpfs.c \
                    fs/vfs/vfs_inode.c fs/vfs/vfs_core.c \
                    fs/ext4/ext4fs.c fs/fat32/fat32fs.c \
                    fs/iso9660/iso9660.c fs/romfs/romfs.c \
                    fs/procfs/procfs.c fs/sysfs/sysfs.c

# network stack: poll-mode IPv4 core over the pcnet driver
NET_SRCS        := net/net_stack.c net/nic.c

# drivers: hardware-facing services
DRV_SRCS        := drivers/console/tty_console.c drivers/console/vga_text_console.c \
                     drivers/core/device_core.c \
                     drivers/core/driver_core.c drivers/qemu/qemu_debug.c \
                     drivers/block_dev.c drivers/graphics/fb.c \
                     drivers/block/ata.c \
                     drivers/input/ps2_keyboard.c drivers/input/ps2_mouse.c \
                     drivers/pci/pci_bus.c drivers/pci/pci_chipset.c \
                     drivers/pci/pci_ids.c \
                     drivers/net/pcnet.c \
                     drivers/net/e1000.c \
                     drivers/audio/hda.c \
                     drivers/acpi/acpi.c drivers/ahci/ahci.c \
                      drivers/serial/serial_16550.c drivers/timer/pit_timer.c \
                      drivers/timer/cmos_rtc.c

# Console font for fbcon (fetched once, cached under build/).
FONT_HDR        := $(BUILD_DIR)/font8x8_basic.h

# ipc, lib/kernel: reusable kernel-side utilities
IPC_SRCS        := ipc/ipc_pipe.c
LIB_SRCS        := lib/kernel/iru_bitmap.c lib/kernel/iru_format.c \
                   lib/kernel/iru_lock.c lib/kernel/iru_memory.c \
                   lib/kernel/iru_ringbuf.c lib/kernel/iru_string.c

# libc: freestanding C library for userland (POSIX I/O, heap, mmap, termios).
# Built as a static archive; programs opt in (see the frotz link rule).
LIBC_SRCS       := libc/src/string.c libc/src/stdlib.c \
                   libc/src/stdio.c libc/src/malloc.c libc/src/unistd.c \
                   libc/src/syscall.c libc/src/mman.c libc/src/errno.c \
                   libc/src/signal.c libc/src/termios.c libc/src/ioctl.c \
                   libc/src/setjmp.c libc/src/locale.c
# Floating point is confined to these libc objects: the userland policy is
# integer-only (-mgeneral-regs-only), and they are the only exceptions, compiled
# with that flag filtered out exactly like the TinyGL port. Everything else in
# libc.a reaches them through plain integer-callable entry points
# (__fcvt, strtod, the math functions), so a program that never asks for a
# float conversion never executes an FP instruction. time.c is here for one
# reason: difftime returns a double, which needs an FP return register.
LIBC_FP_SRCS    := libc/src/math.c libc/src/printf_flo.c libc/src/strtod.c \
                   libc/src/time.c
FP_CFLAGS       = $(filter-out -mgeneral-regs-only,$(USER_CFLAGS))
LIBC_OBJS       := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(LIBC_SRCS) $(LIBC_FP_SRCS))
LIBC_HEADERS    := $(wildcard libc/include/*.h libc/include/sys/*.h \
                          libc/src/*.h)
LIBC_A          := $(BUILD_DIR)/userland/libc.a
# Top-level sysroot: the C SDK lives at sysroot/usr/{include,lib} (see
# sysroot/README.md).  `make sdk` (re)populates it in-tree; toolchain/
# newos-gcc drives `-L`/`-isystem`/link-script straight out of it.
SDK_ROOT        := sysroot
SDK_HEADERS     := $(SDK_ROOT)/usr/include/.newos-headers
SDK_LIBC        := $(SDK_ROOT)/usr/lib/libc.a
SDK_LIBNSH      := $(SDK_ROOT)/usr/lib/libnsh.a
SDK_CRT0        := $(SDK_ROOT)/usr/lib/crt0.o
SDK_LD          := $(SDK_ROOT)/usr/lib/app.ld

KERNEL_SRCS_C   := $(CORE_SRCS) $(MM_SRCS) $(FS_SRCS) $(DRV_SRCS) \
                   $(IPC_SRCS) $(LIB_SRCS) $(NET_SRCS) \
                   crypto/crypto_sha256.c crypto/crypto_chacha20.c

# ---------------------------------------------------------------------------
# Userland paths (definitions precede the object list, which uses them)
# ---------------------------------------------------------------------------
HELLO_DIR       := user/programs/hello
HELLO_OBJ       := $(BUILD_DIR)/userland/hello.o
HELLO_BIN       := $(BUILD_DIR)/userland/hello.elf
HELLO_EMBED     := $(BUILD_DIR)/obj/userland/hello.bin.o

INIT_DIR        := user/programs/init
INIT_OBJ        := $(BUILD_DIR)/userland/init.o
INIT_BIN        := $(BUILD_DIR)/userland/init.elf
INIT_EMBED      := $(BUILD_DIR)/obj/userland/init.bin.o

# Shared userland library + entry stub, linked into every /bin tool.
NSHLIB_SRC      := user/lib/nshlib.c
NSHLIB_OBJ      := $(BUILD_DIR)/userland/nshlib.o
START_OBJ       := $(BUILD_DIR)/userland/start.o
APP_LD          := user/lib/app.ld

# Generated test images (BMP + PPM), embedded into initramfs as /etc/*.
SPLASH_BMP      := $(BUILD_DIR)/splash.bmp
TEST_PPM        := $(BUILD_DIR)/test.ppm
SPLASH_EMBED    := $(BUILD_DIR)/obj/userland/splash.bin.o
PPM_EMBED       := $(BUILD_DIR)/obj/userland/testppm.bin.o

# The /bin toolbox: one directory per command, each holding <name>_main.c
# with a standard main(argc, argv). Installed into the root fs by initramfs.
PROG_NAMES      := ls cat echo mkdir rmdir rm mv touch pwd clear uname \
                    hostname whoami id date uptime ps kill sleep free df \
                    wc head grep find sort img vid about newfetch desktop newpkg frotz
PROG_OBJS       := $(patsubst %,$(BUILD_DIR)/userland/%.o,$(PROG_NAMES))
PROG_BINS       := $(patsubst %,$(BUILD_DIR)/userland/%.elf,$(PROG_NAMES))
PROG_EMBEDS     := $(patsubst %,$(BUILD_DIR)/obj/userland/%.bin.o,$(PROG_NAMES))
# Host-built static Linux/musl programs (same model as /bin/busybox). Their
# sources live in ports/tcc and ports/nano; the checked-in binaries keep a
# normal image build offline. See scripts/build-tcc.sh / build-nano.sh.
NANO_BIN        := ports/nano/nano
NANO_EMBED      := $(BUILD_DIR)/obj/userland/nano.bin.o
TCC_BIN         := ports/tcc/tcc
TCC_EMBED       := $(BUILD_DIR)/obj/userland/tcc.bin.o
# VOXEL (MIT): software graphics stack + classic desktop. Built from the
# vendored source with the host compiler against the NEWOS sysroot.
VOXEL_DIR       := ports/voxel
VOXEL_SRCS      := $(wildcard $(VOXEL_DIR)/demo/*.c $(VOXEL_DIR)/src/*/*.c \
                     $(VOXEL_DIR)/src/*/*/*.c $(VOXEL_DIR)/apps/*/*.c)
VOXEL_BIN       := $(BUILD_DIR)/voxel.elf
VOXEL_EMBED     := $(BUILD_DIR)/obj/userland/voxel.bin.o

# newpkg carries a second translation unit: the portable `.new` format core
# (shared with the host test driver). Linked explicitly with libc.a.
NEWPKG_FORMAT_OBJ := $(BUILD_DIR)/userland/newpkg_format.o

# Desktop drawing layer: one clipped XRGB surface plus caller-owned widgets.
# Both are NEWOS code (integer-only, allocation-free) and are compiled by the
# desktop pattern rule below.
DESKTOP_UI_SRCS   := desktop_surface.c desktop_widgets.c
DESKTOP_UI_HEADERS := user/programs/desktop/desktop_surface.h \
                      user/programs/desktop/desktop_widgets.h

# Desktop modules. desktop_main.c stays the entry point; the rest is split by
# responsibility so the compositor, shell and applications can evolve without
# growing one translation unit.
DESKTOP_DIR       := user/programs/desktop
DESKTOP_SRCS      := desktop_state.c desktop_gfx.c desktop_wm.c desktop_theme.c \
                     desktop_shell.c desktop_apps.c desktop_input.c \
                     desktop_compositor.c $(DESKTOP_UI_SRCS)
DESKTOP_OBJS      := $(patsubst %.c,$(BUILD_DIR)/userland/%.o,$(DESKTOP_SRCS))

# Sample `.new` package embedded for live QEMU exercises (/tmp/*.new).
# Built from the already-linked hello ELF through the host builder script.
NEWPKG_SAMPLE_NEWSPEC := packages/hello-new.newspec
NEWPKG_PKGDIR         := $(BUILD_DIR)/packages
NEWPKG_HELLO_NEW      := $(NEWPKG_PKGDIR)/hello-new-1.0.0-x86_64.new
NEWPKG_SAMPLE_NEW     := $(BUILD_DIR)/sample.new
SAMPLE_NEW_EMBED      := $(BUILD_DIR)/obj/userland/sample_new.bin.o

# Demo Z-code story checked in with the frotz port (tools/frotz/tests),
# installed by initramfs as /usr/share/zcode/mini.z3 (demo: `frotz /usr/
# share/zcode/mini.z3`; real-game story files are user-supplied). Declared
# before OBJS on purpose: OBJS expands immediately (see AGENT.md).
MINI_Z3_SRC      := tools/frotz/tests/mini.z3
MINI_Z3_EMBED    := $(BUILD_DIR)/obj/userland/mini_z3.bin.o

# Shell boot script (nsh runs /etc/nsh.rc before the prompt). Commented-out
# by default so a normal boot stays interactive; edit it to script a session,
# or point `make qemu-rc` at a generated one for automated runs.
NSH_RC_SRC        := user/rootfs/nsh.rc
NSH_RC            := $(BUILD_DIR)/nsh.rc
NSH_RC_EMBED      := $(BUILD_DIR)/obj/userland/nsh_rc.bin.o

# System nano configuration. nano's compiled-in SYSCONFDIR is /usr/etc, so
# initramfs installs this blob as /usr/etc/nanorc (syntax colors + defaults).
NANORC_SRC        := user/rootfs/nanorc
NANORC            := $(BUILD_DIR)/nanorc
NANORC_EMBED      := $(BUILD_DIR)/obj/userland/nanorc.bin.o

# --- GNU tools (host-built, static musl) ---------------------------------
# The optional upstream GNU package is cross-built on the host against musl.
# These static Linux ET_EXEC files can be mapped by elf_loader.c, but their
# Linux syscall-instruction/TLS ABI is not implemented by NEWOS; installation
# under /usr/bin is packaging only and does not make them runnable yet.
# scripts/build-gnu.sh stages them under $(GNU_STAGE)/<tool>, and
# scripts/pack-gnu.sh turns that stage into one `.new` archive.
#
# One archive rather than one embed per tool: 68 coreutils are 10 MiB, and
# shipping them through the package manager is both smaller in the build
# tree and the thing actually being tested.
#
# Declared BEFORE OBJS on purpose: `OBJS :=` expands immediately, so a
# later definition would silently drop the embeds (see AGENT.md).
GNU_STAGE       := ports/stage
GNU_STAGE_INPUTS := $(wildcard $(GNU_STAGE)/*)
GNU_PKG_VERSION := 9.7
GNU_PKG         := $(BUILD_DIR)/gnu-coreutils.new
GNU_PKG_EMBED   := $(BUILD_DIR)/obj/userland/gnu_coreutils.bin.o

# NEWOS C SDK package: the generated sysroot (libc headers, libc.a, libnsh.a,
# crt0.o, app.ld) plus TinyCC's own include/ directory, packed with the same
# `.new` builder the package manager uses and staged at /tmp. Installing it
# is what turns `/bin/tcc -c` into a full on-board compile+link:
#   newpkg install /tmp/newos-sdk.new
SDK_PKG         := $(BUILD_DIR)/newos-sdk.new
SDK_PKG_EMBED   := $(BUILD_DIR)/obj/userland/newos_sdk.bin.o

# Complete upstream BusyBox (official 1.35.0 x86_64-linux-musl build,
# 402 applets).  It is a static ET_EXEC and now runs on NEWOS through the
# Linux-compatible SYSCALL entry in arch/x86_64/interrupts/x86_interrupt.S.
# The binary is checked in so a build never needs network access; BusyBox is
# GPLv2, so ports/busybox/ records the exact source URL, SHA-256 and license.
BUSYBOX_BIN     := ports/busybox/busybox
BUSYBOX_EMBED   := $(BUILD_DIR)/obj/userland/busybox.bin.o

# TinyGL gears demo, built with the host compiler and NEWOS libc so it is
# part of the normal image.  The port's own Makefile remains available for
# cross-toolchain builds, but no user has to run it to get /bin/glxgears.
GLXGEARS_DIR    := ports/glxgears-newos
TINYGL_DIR      := $(GLXGEARS_DIR)/vendor/tinygl
GLXGEARS_PORT_SRCS := glxgears_newos.c allocator.c
GLXGEARS_TINY_SRCS := api.c arrays.c clear.c clip.c get.c image_util.c \
                      init.c light.c list.c matrix.c misc.c msghandling.c \
                      select.c specbuf.c texture.c vertex.c zbuffer.c \
                      zline.c zmath.c zpostprocess.c zraster.c ztext.c \
                      ztriangle.c
GLXGEARS_PORT_OBJS := $(patsubst %.c,$(BUILD_DIR)/userland/glxgears/%.o,$(GLXGEARS_PORT_SRCS))
GLXGEARS_TINY_OBJS := $(patsubst %.c,$(BUILD_DIR)/userland/glxgears/tinygl_%.o,$(GLXGEARS_TINY_SRCS))
GLXGEARS_MATH_OBJ  := $(BUILD_DIR)/userland/glxgears/math.o
GLXGEARS_BIN       := $(BUILD_DIR)/userland/glxgears.elf
GLXGEARS_EMBED     := $(BUILD_DIR)/obj/userland/glxgears.bin.o

# Package the checked-in staged tools directly; `make gnu` refreshes this
# same stage from upstream. They install under /usr/bin, preserving native
# NEWOS commands under /bin.
$(GNU_PKG): $(GNU_STAGE_INPUTS) scripts/pack-gnu.sh tools/newpkg/newpkg-build.py | dirs ports-assets
	$(Q)bash scripts/pack-gnu.sh $(GNU_STAGE) $@ $(GNU_PKG_VERSION)

# The blob symbol is derived from the operand name: gnu-coreutils.new ->
# _binary_gnu_coreutils_new_{start,end}.
$(GNU_PKG_EMBED): $(GNU_PKG)
	$(Q)mkdir -p $(dir $@)
	$(E) "  GEMB $(notdir $<)"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/gnu_coreutils.bin.o gnu-coreutils.new

# SDK archive: depends on the generated sysroot, so `make` populates it first.
$(SDK_PKG): $(SDK_HEADERS) $(SDK_LIBC) $(SDK_LIBNSH) $(SDK_CRT0) $(SDK_LD) \
    scripts/pack-sdk.sh tools/newpkg/newpkg-build.py | dirs
	$(E) "  SDK $(notdir $@)"
	$(Q)bash scripts/pack-sdk.sh $(SDK_ROOT) $@ $(NEWOS_VERSION)

$(SDK_PKG_EMBED): $(SDK_PKG)
	$(Q)mkdir -p $(dir $@)
	$(E) "  EMB newos-sdk.new"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/newos_sdk.bin.o newos-sdk.new

# The binary is an opaque ET_EXEC blob (not linked into the kernel); only the
# initramfs copies it to /bin/busybox.  Keep the embed name stable for the
# _binary_busybox_elf_* symbols used by the VFS initializer.
$(BUSYBOX_EMBED): $(BUSYBOX_BIN) | dirs
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $(BUSYBOX_BIN) $(BUILD_DIR)/busybox.elf
	$(E) "  EMB busybox ($(notdir $(BUSYBOX_BIN)))"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/busybox.bin.o busybox.elf

gnu: | dirs ports-assets
	$(E) "Building GNU tools with musl (Docker) ..."
	$(Q)bash scripts/build-gnu.sh stage ports/stage
	$(Q)rm -f $(GNU_PKG)

gnu-pkg: $(GNU_PKG)
	$(Q)ls -la $(GNU_PKG)

.PHONY: gnu gnu-pkg ports-assets

# Expand the compact binary payload kept in NEWOS-ports before packaging.
# This is a no-op when the staged marker and pinned source archive are present.
ports-assets:
	$(Q)test -x ports/scripts/restore-assets.sh || { echo "ports submodule is missing; run: git submodule update --init ports" >&2; exit 1; }
	$(Q)test -f ports/stage/.built -a -f ports/distfiles/coreutils-9.7.tar.xz || bash ports/scripts/restore-assets.sh

# Keep the linked tool ELFs: they chain into embeds through a pattern rule
# and make would otherwise delete them as intermediates after every build.
# $(KERNEL_BIN) goes the same way: `make all` reaches it through the ISO
# chain, and a plain `make all` would otherwise remove the -kernel image
# that `make qemu`/scripts/qemu-shell.py boot.
.SECONDARY: $(PROG_BINS) $(GLXGEARS_BIN) $(KERNEL_BIN)

OBJS_C          := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(KERNEL_SRCS_C) $(ARCH_SRCS_C))
OBJS_ASM        := $(patsubst %.S,$(BUILD_DIR)/obj/%.o,$(ARCH_SRCS_ASM))
OBJS            := $(OBJS_C) $(OBJS_ASM) $(HELLO_EMBED) $(INIT_EMBED) \
                   $(PROG_EMBEDS) $(SPLASH_EMBED) $(PPM_EMBED) \
                   $(SAMPLE_NEW_EMBED) $(NSH_RC_EMBED) $(GNU_PKG_EMBED) \
                   $(NANO_EMBED) $(BUSYBOX_EMBED) $(GLXGEARS_EMBED) \
                   $(TCC_EMBED) $(VOXEL_EMBED) $(SDK_PKG_EMBED) $(MINI_Z3_EMBED) \
                   $(NANORC_EMBED)

INCLUDE         := -Iinclude -I. -Ilib/kernel -Iarch/$(ARCH)/include \
                   -I$(BUILD_DIR)

# ---------------------------------------------------------------------------
# Userland build + embed
# ---------------------------------------------------------------------------

# Userland is freestanding but has access to the full instruction set
# (no -mno-red-zone, the program runs with a normal SysV stack).
USER_CFLAGS     := -c -O2 -ffreestanding -nostdlib -fno-pic -fno-pie \
                   -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables \
                   -fno-exceptions -mgeneral-regs-only -Wall -Wextra -Werror \
                   -DNEWOS_VERSION='"$(NEWOS_VERSION)"' \
                   -DNEWOS_VERSION_ID='"$(NEWOS_VERSION_ID)"' \
                   -Ilibc/include -Iuserland/user

$(HELLO_OBJ): $(HELLO_DIR)/hello_main.c
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $<"
	$(Q)$(CC) $(USER_CFLAGS) -o $@ $<

$(HELLO_BIN): $(HELLO_OBJ) $(HELLO_DIR)/linker.ld
	$(Q)mkdir -p $(dir $@)
	$(E) "  ULD $@"
	$(Q)$(LD) -n -static -nostdlib -T $(HELLO_DIR)/linker.ld -o $@ $(HELLO_OBJ)

# Embed hello.elf as an opaque blob. The linker derives the exported symbols
# from the input file name, so the link runs from $(BUILD_DIR) with the file
# named exactly `hello.elf` -> _binary_hello_elf_{start,end,size}.
$(HELLO_EMBED): $(HELLO_BIN)
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $(HELLO_BIN) $(BUILD_DIR)/hello.elf
	$(E) "  EMB $<"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/hello.bin.o hello.elf

$(INIT_OBJ): $(INIT_DIR)/shell_main.c user/lib/nshlib.h
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $<"
	$(Q)$(CC) $(USER_CFLAGS) -o $@ $<

$(INIT_BIN): $(INIT_OBJ) $(INIT_DIR)/linker.ld $(NSHLIB_OBJ) $(START_OBJ)
	$(Q)mkdir -p $(dir $@)
	$(E) "  ULD $@"
	$(Q)$(LD) -n -static -nostdlib -T $(INIT_DIR)/linker.ld -o $@ \
	  $(INIT_OBJ) $(NSHLIB_OBJ) $(START_OBJ)

# Roaming init shell, embedded as `init.elf` -> _binary_init_elf_{start,end,size}.
$(INIT_EMBED): $(INIT_BIN)
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $(INIT_BIN) $(BUILD_DIR)/init.elf
	$(E) "  EMB $<"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/init.bin.o init.elf

# --- /bin toolbox ------------------------------------------------------
# Each tool is a standard main() linked with the shared lib + entry stub,
# then embedded as _binary_<name>_elf_{start,end,size} for initramfs.

$(NSHLIB_OBJ): $(NSHLIB_SRC) user/lib/nshlib.h
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $<"
	$(Q)$(CC) $(USER_CFLAGS) -o $@ $<

$(START_OBJ): user/lib/start.S
	$(Q)mkdir -p $(dir $@)
	$(E) "  UAS $<"
	$(Q)$(NASM) -f elf64 -o $@ $<

$(BUILD_DIR)/userland/%.elf: $(BUILD_DIR)/userland/%.o \
    $(NSHLIB_OBJ) $(START_OBJ) $(APP_LD)
	$(Q)mkdir -p $(dir $@)
	$(E) "  ULD $@"
	$(Q)$(LD) -n -static -nostdlib -T $(APP_LD) -o $@ $(filter %.o,$^)

# One explicit compile rule per tool (a pattern with two stems is not
# expressible: the source lives at user/programs/<name>/<name>_main.c).
define PROG_COMPILE_RULE
$(BUILD_DIR)/userland/$(1).o: user/programs/$(1)/$(1)_main.c user/lib/nshlib.h
	$(Q)mkdir -p $$(dir $$@)
	$(E) "  UCC $$<"
	$(Q)$$(CC) $$(USER_CFLAGS) -o $$@ $$<
endef
$(foreach p,$(PROG_NAMES),$(eval $(call PROG_COMPILE_RULE,$(p))))

$(BUILD_DIR)/userland/desktop.o: $(DESKTOP_UI_HEADERS) \
    user/programs/desktop/font8x8.h user/programs/desktop/desktop.h

$(BUILD_DIR)/userland/desktop_%.o: $(DESKTOP_DIR)/desktop_%.c \
    $(DESKTOP_DIR)/desktop.h $(DESKTOP_UI_HEADERS) \
    user/programs/desktop/font8x8.h
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $< (desktop module)"
	$(Q)$(CC) $(USER_CFLAGS) -o $@ $<

$(BUILD_DIR)/userland/desktop.elf: $(BUILD_DIR)/userland/desktop.o \
    $(DESKTOP_OBJS) $(NSHLIB_OBJ) $(START_OBJ) $(APP_LD)
	$(Q)mkdir -p $(dir $@)
	$(E) "  ULD $@"
	$(Q)$(LD) -n -static -nostdlib -T $(APP_LD) -o $@ \
	  $(BUILD_DIR)/userland/desktop.o $(DESKTOP_OBJS) \
	  $(NSHLIB_OBJ) $(START_OBJ)

# --- TinyGL glxgears ---------------------------------------------------
# The demo uses floating point and TinyGL, so it cannot use the normal
# -mgeneral-regs-only userland flags.  It still links the NEWOS libc and the
# local math.c instead of a host libm.
GLXGEARS_CFLAGS := $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) \
                   -std=gnu99 -Wno-error -Wno-implicit-fallthrough \
                   -Iuserland/user/lib \
                   -I$(TINYGL_DIR)/include -I$(TINYGL_DIR)/src \
                   -DNO_DEBUG_OUTPUT -DTGL_FEATURE_RENDER_BITS=32

$(BUILD_DIR)/userland/glxgears/%.o: $(GLXGEARS_DIR)/src/%.c
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $< (glxgears)"
	$(Q)$(CC) $(GLXGEARS_CFLAGS) -o $@ $<

$(BUILD_DIR)/userland/glxgears/tinygl_%.o: $(TINYGL_DIR)/src/%.c
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $< (TinyGL)"
	$(Q)$(CC) $(GLXGEARS_CFLAGS) -o $@ $<

$(GLXGEARS_MATH_OBJ): libc/src/math.c libc/include/math.h
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $< (libm)"
	$(Q)$(CC) $(GLXGEARS_CFLAGS) -o $@ $<

$(GLXGEARS_BIN): $(GLXGEARS_PORT_OBJS) $(GLXGEARS_TINY_OBJS) \
    $(GLXGEARS_MATH_OBJ) $(NSHLIB_OBJ) $(START_OBJ) $(LIBC_A) $(APP_LD)
	$(Q)mkdir -p $(dir $@)
	$(E) "  ULD $@ +TinyGL"
	$(Q)$(LD) -n -static -nostdlib -T $(APP_LD) -o $@ \
	  $(filter %.o,$^) $(filter %.a,$^)

$(BUILD_DIR)/obj/userland/%.bin.o: $(BUILD_DIR)/userland/%.elf
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $< $(BUILD_DIR)/$*.elf
	$(E) "  EMB $<"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/$*.bin.o $*.elf

# GNU nano and tcc are checked-in static Linux/musl binaries built from the
# vendored forks in ports/. They are data blobs to the kernel: initramfs
# exposes them as /bin/nano and /bin/tcc, and they run through the same
# Linux-compatible SYSCALL path as /bin/busybox.
$(NANO_EMBED): $(NANO_BIN)
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $< $(BUILD_DIR)/nano.elf
	$(E) "  EMB nano ($(notdir $(NANO_BIN)))"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/nano.bin.o nano.elf

$(TCC_EMBED): $(TCC_BIN)
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $< $(BUILD_DIR)/tcc.elf
	$(E) "  EMB tcc ($(notdir $(TCC_BIN)))"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/tcc.bin.o tcc.elf

# VOXEL is NEWOS-native source (no libc dependency of its own): compile it
# against the sysroot and expose the demo as /bin/voxel.
$(VOXEL_BIN): $(VOXEL_SRCS) scripts/build-voxel.sh | $(SDK_LIBC) $(SDK_LIBNSH) $(SDK_CRT0) $(SDK_LD)
	$(E) "  VOXEL $(notdir $@)"
	$(Q)CC="$(CC)" bash scripts/build-voxel.sh $@

$(VOXEL_EMBED): $(VOXEL_BIN)
	$(Q)mkdir -p $(dir $@)
	$(E) "  EMB voxel"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/voxel.bin.o voxel.elf

# --- newpkg: toolbox tool + portable format core -------------------------
# newpkg_main.c comes from the data-driven rule above; the format core is
# an extra object linked explicitly (explicit rules win over the pattern
# rule, same mechanism as frotz+libc below).

$(NEWPKG_FORMAT_OBJ): user/programs/newpkg/newpkg_format.c \
    user/programs/newpkg/newpkg_format.h user/lib/nshlib.h
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $<"
	$(Q)$(CC) $(USER_CFLAGS) -o $@ $<

$(BUILD_DIR)/userland/newpkg.elf: $(BUILD_DIR)/userland/newpkg.o \
    $(NEWPKG_FORMAT_OBJ) $(NSHLIB_OBJ) $(START_OBJ) $(APP_LD)
	$(Q)mkdir -p $(dir $@)
	$(E) "  ULD $@ +newpkg_format"
	$(Q)$(LD) -n -static -nostdlib -T $(APP_LD) -o $@ $(filter %.o,$^)

# Sample `.new` package (host builder, deterministic bytes) + embed as an
# opaque blob for live exercises: sample.new -> _binary_sample_new_*.
# The distributable artifact keeps its real file name under
# build/packages/; the embed is a copy of those exact bytes.
$(NEWPKG_HELLO_NEW): $(HELLO_BIN) $(NEWPKG_SAMPLE_NEWSPEC) \
    tools/newpkg/newpkg-build.py | dirs
	$(Q)mkdir -p $(dir $@)
	$(E) "  NEWPKG $@"
	$(Q)python3 tools/newpkg/newpkg-build.py $(NEWPKG_SAMPLE_NEWSPEC) \
	  -o $@ --root .

$(NEWPKG_SAMPLE_NEW): $(NEWPKG_HELLO_NEW)
	$(Q)cp $< $@

$(SAMPLE_NEW_EMBED): $(NEWPKG_SAMPLE_NEW)
	$(Q)mkdir -p $(dir $@)
	$(E) "  EMB $<"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/sample_new.bin.o sample.new

# Demo story blob: mini.z3 -> _binary_mini_z3_{start,end}. Installed by
# initramfs at /usr/share/zcode/mini.z3.
$(MINI_Z3_EMBED): $(MINI_Z3_SRC) | dirs
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $(MINI_Z3_SRC) $(BUILD_DIR)/mini.z3
	$(E) "  EMB mini.z3 (demo story)"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/mini_z3.bin.o mini.z3

# --- libc (static archive for userland) --------------------------------
# Compiled with the userland flags (NOT the kernel -mcmodel=kernel ones).

$(BUILD_DIR)/obj/libc/%.o: libc/%.c $(LIBC_HEADERS) | dirs
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $<"
	$(Q)$(CC) $(USER_CFLAGS) -o $@ $<

# The FP-capable libc objects (math, float printf, strtod, time) override the
# userland integer policy. The target must be the object, not the source.
define LIBC_FP_RULE
$(BUILD_DIR)/obj/$(1:.c=.o): $(1) $(LIBC_HEADERS) | dirs
	$(Q)mkdir -p $$(dir $$@)
	$(E) "  UFCC $$<"
	$(Q)$$(CC) $$(FP_CFLAGS) -o $$@ $$<
endef
$(foreach f,$(LIBC_FP_SRCS),$(eval $(call LIBC_FP_RULE,$(f))))

$(LIBC_A): $(LIBC_OBJS) | dirs
	$(Q)mkdir -p $(dir $@)
	$(E) "  AR  $@"
	$(Q)$(AR) rcs $@ $(LIBC_OBJS)

# C application SDK: cross compiler users link against NEWOS libc and the
# syscall/startup shim, never the host/newlib syscall layer.
$(SDK_HEADERS): $(LIBC_HEADERS) include/abi/syscall_abi.h
	$(Q)mkdir -p $(SDK_ROOT)/usr/include/newos
	$(Q)cp -R libc/include/. $(SDK_ROOT)/usr/include/
	$(Q)cp include/abi/syscall_abi.h $(SDK_ROOT)/usr/include/newos/syscall_abi.h
	$(Q)touch $@

$(SDK_LIBC): $(LIBC_A)
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $< $@

$(SDK_LIBNSH): $(NSHLIB_OBJ)
	$(Q)mkdir -p $(dir $@)
	$(Q)rm -f $@
	$(Q)$(AR) rcs $@ $<

$(SDK_CRT0): $(START_OBJ)
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $< $@

$(SDK_LD): user/lib/app.ld
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $< $@

.PHONY: sdk
sdk: $(SDK_HEADERS) $(SDK_LIBC) $(SDK_LIBNSH) $(SDK_CRT0) $(SDK_LD)
	$(E) "NEWOS sysroot ready: $(SDK_ROOT)"

# `.new` archive of the same sysroot, embedded at /tmp for the on-board tcc.
sdk-pkg: $(SDK_PKG)
	$(Q)ls -la $(SDK_PKG)

.PHONY: sdk-pkg

# --- frotz: Z-machine interpreter (upstream Frotz 2.44, GPL-2+) -----------
# Ported source lives in user/programs/frotz/ (one TU per upstream file);
# frotz_main.c is the NEWOS toolbox entry, frotz_run.c is the renamed
# upstream main.c. It links NEWOS libc.a explicitly.
# gnu99: upstream is ANSI C (typedef's bool, K&R-era syntax); the default
# -std=c23 breaks it. -Wno-error=unterminated-string-initialization keeps
# the upstream exact-6-byte game serial records intact. -fcommon: upstream
# 2.44 relies on commons merging for the duplicated `f_setup`/`do_more_
# prompts` definitions in the dumb driver (pre-gcc10 default).
FROTZ_CFLAGS   := $(USER_CFLAGS) -std=gnu99 \
                  -Wno-error=unterminated-string-initialization -fcommon
FROTZ_DIR      := user/programs/frotz
FROTZ_SRCS     := buffer.c err.c fastmem.c files.c frotz_run.c hotkey.c \
                  input.c math.c object.c process.c quetzal.c random.c \
                  redirect.c screen.c sound.c stream.c table.c text.c \
                  variable.c dumb_init.c dumb_input.c dumb_output.c \
                  dumb_pic.c
FROTZ_OBJS     := $(patsubst %.c,$(BUILD_DIR)/userland/frotz/%.o,$(FROTZ_SRCS))
FROTZ_HEADERS  := $(FROTZ_DIR)/frotz.h $(FROTZ_DIR)/setup.h \
                  $(FROTZ_DIR)/dumb_frotz.h

$(BUILD_DIR)/userland/frotz.o $(FROTZ_OBJS): $(FROTZ_HEADERS) $(LIBC_HEADERS)

$(BUILD_DIR)/userland/frotz/%.o: $(FROTZ_DIR)/%.c
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $< (frotz)"
	$(Q)$(CC) $(FROTZ_CFLAGS) -o $@ $<

$(BUILD_DIR)/userland/frotz.elf: $(BUILD_DIR)/userland/frotz.o $(FROTZ_OBJS) \
    $(NSHLIB_OBJ) $(START_OBJ) $(LIBC_A) $(APP_LD)
	$(Q)mkdir -p $(dir $@)
	$(E) "  ULD $@ +frotz"
	$(Q)$(LD) -n -static -nostdlib -T $(APP_LD) -o $@ \
	  $(filter %.o,$^) $(filter %.a,$^)

# --- generated assets: console font + test images ----------------------
# The font is fetched once and cached; the images are generated locally
# with python3 (no external image tools needed).

$(FONT_HDR): bc/font8x8_basic.h scripts/fetch-font.sh
	$(Q)bash scripts/fetch-font.sh $(FONT_HDR)

$(TEST_PPM): scripts/gen-images.py
	$(Q)mkdir -p $(dir $(TEST_PPM))
	$(E) "  GEN test images"
	$(Q)python3 scripts/gen-images.py $(TEST_PPM)

# The boot splash is a brand asset, not a generated placeholder: brand/ is
# the single source of truth. It is copied into $(BUILD_DIR) under the fixed
# name so the blob symbol below keeps its expected name.
$(SPLASH_BMP): brand/newos-splash.bmp
	$(Q)mkdir -p $(dir $(SPLASH_BMP))
	$(E) "  BRAND $<"
	$(Q)cp $< $@

# Embed the images as opaque blobs for initramfs (/etc/splash.bmp,
# /etc/test.ppm). The linker derives symbol names from the operand
# string, so the link runs from $(BUILD_DIR): splash.bmp ->
# _binary_splash_bmp_{start,end}, test.ppm -> _binary_test_ppm_*
# (the generated files already carry exactly those names).
$(SPLASH_EMBED): $(SPLASH_BMP)
	$(Q)mkdir -p $(dir $@)
	$(E) "  EMB $<"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/splash.bin.o splash.bmp

$(PPM_EMBED): $(TEST_PPM)
	$(Q)mkdir -p $(dir $@)
	$(E) "  EMB $<"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/testppm.bin.o test.ppm

# The boot script is copied under its fixed name for the same reason as the
# splash: the blob symbol is derived from the operand, nsh.rc ->
# _binary_nsh_rc_{start,end}.
$(NSH_RC): $(NSH_RC_SRC)
	$(Q)mkdir -p $(dir $@)
	$(E) "  RC  $<"
	$(Q)cp $< $@

$(NSH_RC_EMBED): $(NSH_RC)
	$(Q)mkdir -p $(dir $@)
	$(E) "  EMB $<"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/nsh_rc.bin.o nsh.rc

# System nanorc: copied under its fixed name so the blob symbol stays
# nanorc -> _binary_nanorc_{start,end}.
$(NANORC): $(NANORC_SRC)
	$(Q)mkdir -p $(dir $@)
	$(E) "  RC  $<"
	$(Q)cp $< $@

$(NANORC_EMBED): $(NANORC)
	$(Q)mkdir -p $(dir $@)
	$(E) "  EMB $<"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/nanorc.bin.o nanorc

# fb.c needs the generated font header.
$(BUILD_DIR)/obj/drivers/graphics/fb.o: $(FONT_HDR)

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------
.PHONY: all bootstrap bootstrap-fast fast clean qemu qemu-debug qemu-test qemu-network qemu-disk iso \
        qemu-limine qemu-limine-gui qemu-limine-test limine toolchain sdk check-config dirs ports-assets \
        lint-layers check-newpkg check-frotz check-web web brand qemu-shell \
        build-info help system-image-build \
        vbox vbox-setup vbox-test FORCE

.DEFAULT_GOAL := all

# Delete half-written targets when a recipe fails (no corrupt artifacts).
.DELETE_ON_ERROR:

# `make` is the complete superproject entry point. Bootstrap is a separate
# prerequisite with no source-file prerequisites, so it runs before a nested
# make expands the source ledger from the initialized component submodules.
# `git submodule update` checks out only the commits pinned by this repo.
all: bootstrap
	$(Q)$(MAKE) --no-print-directory toolchain
	$(Q)PATH="$(CURDIR)/toolchain/out/bin:$$PATH" $(MAKE) --no-print-directory CROSS_PREFIX=x86_64-elf- system-image-build
	$(Q)PATH="$(CURDIR)/toolchain/out/bin:$$PATH" $(MAKE) --no-print-directory CROSS_PREFIX=x86_64-elf- glxgears-newos glxgears-pkg

# Initialize every first-party and nested source repository at its pinned SHA.
# Build products are kept in build/ and toolchain/out/ inside this checkout.
bootstrap:
	$(Q)git -C "$(CURDIR)" rev-parse --show-toplevel >/dev/null 2>&1 || { echo "ERROR: run make from a Git checkout of NEWOS" >&2; exit 1; }
	$(Q)git submodule sync --recursive
	$(Q)git submodule update --init --recursive

# Fast local alternative: bootstrap only the sources the system image uses,
# then build the Limine ISO with host gcc/ld. It deliberately skips the cross
# toolchain (including toolchain/upstream-gcc, whose repository is gigabytes)
# and the optional graphics demo.
bootstrap-fast:
	$(Q)git -C "$(CURDIR)" rev-parse --show-toplevel >/dev/null 2>&1 || { echo "ERROR: run make from a Git checkout of NEWOS" >&2; exit 1; }
	$(Q)git submodule sync --recursive -- kernel userland ports
	$(Q)git submodule update --init --recursive -- kernel userland ports

fast: bootstrap-fast
	$(Q)$(MAKE) --no-print-directory CROSS_PREFIX= system-image-build

# Actual system image rules are reached through recursive make after bootstrap.
system-image-build: build-info $(KERNEL_BIN) $(LIMINE_ISO)

# One-line build configuration banner (always printed for `all`).
build-info:
	@echo "NEWOS build: ARCH=$(ARCH) CONFIG=$(CONFIG) V=$(V) (V=0: short log)"
	@echo "  CC='$(CC)' LD='$(LD)' AR='$(AR)' CROSS_PREFIX='$(CROSS_PREFIX)'"
	@echo "  git: $$(git rev-parse --short HEAD 2>/dev/null || echo nogit)/$$(git diff --quiet 2>/dev/null && echo clean || echo dirty)"

help:
	@echo "NEWOS build targets:"
	@echo "  make / make all        bootstrap sources, build x86_64-elf, kernel + ISO + TinyGL demo"
	@echo "  make bootstrap         fetch all pinned recursive submodules into this checkout"
	@echo "  make fast              host-compiler system ISO only (skips toolchain and demo)"
	@echo "  make bootstrap-fast    fetch only kernel/userland/ports (no GCC sources)"
	@echo "  make build/images/newos-x86_64.elf          standalone PVH/multiboot2 kernel"
	@echo "  make limine            Limine-flavour kernel"
	@echo "  make iso               GRUB multiboot2 ISO"
	@echo "  make build/images/newos-x86_64-limine.iso   BIOS+UEFI Limine ISO"
	@echo "  make qemu              boot in QEMU (serial console)"
	@echo "  make qemu-debug        boot with GDB server on :1234"
	@echo "  make qemu-test         automated test suite in QEMU (exit 1 = PASS)"
	@echo "  make qemu-limine       boot Limine (text shell; GUI is a menu entry)"
	@echo "  make qemu-limine-gui   boot Limine with gui=1"
	@echo "  make qemu-limine-test  suite through the Limine ISO"
	@echo "  make vbox              boot the Limine ISO in VirtualBox (GUI)"
	@echo "  make vbox-test         headless VirtualBox boot + serial check"
	@echo "  make qemu-network      boot with user-mode networking"
	@echo "  make qemu-disk         boot from a 64 MiB raw disk image"
	@echo "  make check-newpkg      package system tests (no QEMU needed)"
	@echo "  make sdk               populate sysroot/ (headers, libc.a, crt0.o)"
	@echo "  make sdk-pkg           pack the SDK as the .new embedded at /tmp"
	@echo "  make glxgears-pkg      package the TinyGL demo as a native .new"
	@echo "  make check-frotz       frotz port host tests (mini.z3 story)"
	@echo "  make check-web         validate website links and assets"
	@echo "  make web               serve web/ at http://localhost:8000"
	@echo "  make brand             regenerate the brand/ SVG family"
	@echo "  make qemu-shell        run a scripted nsh session (CMDS=file)"
	@echo "  make lint-layers       layering contract check only"
	@echo "  make toolchain         bootstrap sources and build x86_64-elf under toolchain/out"
	@echo "  make CROSS_PREFIX=x86_64-elf- sdk  populate sysroot/usr C SDK"
	@echo "  make clean             remove build/ entirely"
	@echo "Options: V=0 (short log), V=1 (full commands, default),"
	@echo "  ARCH=x86_64, CROSS_PREFIX=x86_64-elf-, CONFIG=path/to/config"

dirs:
	$(Q)mkdir -p $(BUILD_DIR)/obj

check-config:
	$(Q)test -f $(CONFIG) && echo "Using config: $(CONFIG)" || { \
	  echo "ERROR: config $(CONFIG) not found"; exit 1; }

lint-layers:
	$(Q)bash scripts/ci/lint_layering.sh

# check-newpkg: native package system tests (no QEMU needed).
#  1. C unit driver for the portable format core (CRC, header, metadata,
#     manifest, paths, versions, deps, arch gate): 80+ assertions.
#  2. End-to-end suite driving the REAL newpkg_main.c (compiled for the
#     host against a syscall jail): install/remove/list/files, conflicts,
#     deps incl. circular, corrupt/truncated/arch-mismatch packages.
check-newpkg: $(NEWPKG_SAMPLE_NEW) | dirs
	$(Q)gcc -DNEWPKG_HOST -Wall -Wextra -Werror \
	  -o $(BUILD_DIR)/newpkg-host-test \
	  user/programs/newpkg/newpkg_format.c tools/newpkg/host-test.c
	$(Q)$(BUILD_DIR)/newpkg-host-test
	$(Q)gcc -DNEWPKG_HOST -Wall -Wextra -Werror -Iuserland/user \
	  -o $(BUILD_DIR)/newpkg-host \
	  user/programs/newpkg/newpkg_format.c \
	  user/programs/newpkg/newpkg_main.c tools/newpkg/posix-shim.c
	$(Q)NEWPKG_HOST_BIN=$(BUILD_DIR)/newpkg-host \
	  python3 tools/newpkg/tests/test_newpkg.py

# check-frotz: host functional test for the /bin/frotz port (upstream
# Frotz 2.44, GPL-2+). Compiles the ported units against the host libc
# and drives the committed mini.z3 story: banner, one line read and
# deterministic regeneration. No QEMU needed.
check-frotz: | dirs
	$(Q)bash tools/frotz/check-frotz.sh $(BUILD_DIR)/frotz-check

# The static site has no build step; this validates every local link/asset
# and catches unreferenced files under web/assets.
check-web:
	$(Q)python3 scripts/check-web.py

web: check-web
	$(E) "Serving the NEWOS site at http://localhost:8000 (Ctrl+C to stop)"
	$(Q)cd web && python3 -m http.server 8000

# Regenerate the whole brand SVG family from its single source script and
# refresh the copies the static website serves.
brand:
	$(Q)python3 scripts/gen-brand.py
	$(Q)mkdir -p web/assets/app-icons
	$(Q)cp brand/app-icons/*.svg web/assets/app-icons/
	$(Q)cp brand/newos-1024.png web/assets/newos-1024.png
	$(E) "Regenerated brand/ and refreshed web/assets"

$(BUILD_DIR)/obj/%.o: %.c | dirs
	$(Q)mkdir -p $(dir $@)
	$(E) "  CC  $<"
	$(Q)$(CC) $(CFLAGS) $(INCLUDE) -o $@ $<

$(BUILD_DIR)/obj/%.o: %.S | dirs
	$(Q)mkdir -p $(dir $@)
	$(E) "  AS  $<"
	$(Q)$(NASM) $(ASFLAGS) -o $@ $<

$(KERNEL_BIN): $(OBJS) lint-layers | check-config dirs
	$(Q)mkdir -p $(dir $@)
	$(E) "  LD  $(KERNEL_BIN)"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJS)
	$(E) "  SIZE"
	$(Q)size $@
	$(E) "=== Build complete: $(KERNEL_BIN) ==="

iso: $(KERNEL_BIN)
	$(Q)mkdir -p $(dir $(ISO_IMG)) $(BUILD_DIR)/iso/boot/grub
	$(Q)cp $(KERNEL_BIN) $(BUILD_DIR)/iso/boot/newos.elf
	$(Q)printf 'set timeout=0\nset default=0\nserial --unit=0 --speed=115200 --stop=1\nterminal_input serial\nterminal_output serial\nmenuentry "NEWOS (text shell)" {\n  multiboot2 /boot/newos.elf\n  boot\n}\nmenuentry "NEWOS (GUI)" {\n  multiboot2 /boot/newos.elf gui=1\n  boot\n}\nmenuentry "NEWOS (test mode)" {\n  multiboot2 /boot/newos.elf test_mode=1\n  boot\n}\n' \
	  > $(BUILD_DIR)/iso/boot/grub/grub.cfg
	$(Q)grub-mkrescue --arcs-boot -o $(ISO_IMG) $(BUILD_DIR)/iso 2>/dev/null || \
	  xorriso -as mkisofs -b boot/grub/eltorito.img -no-emul-boot \
	  -boot-load-size 4 -boot-info-table --grub2-boot-info \
	  --grub2-mbr /usr/lib/grub/i386-pc/boot.img \
	  -o $(ISO_IMG) $(BUILD_DIR)/iso
	$(Q)python3 scripts/iso/fixup_iso.py $(ISO_IMG)
	$(E) "ISO created: $(ISO_IMG)"

# Limine bootloader binaries (bios + uefi CD boot images), cached under
# $(BUILD_DIR)/limine-bins. The release tag can be overridden.
LIMINE_RELEASE  ?= v12.9.0

$(LIMINE_ROOT)/limine.conf: FORCE | dirs
	$(Q)mkdir -p $(LIMINE_ROOT)
	$(Q)printf 'timeout: 2\nserial: yes\ninterface_resolution: 1280x800\nwallpaper: boot():/boot/newos-limine.png\nwallpaper_style: stretched\nbackdrop: 000000\nterm_margin: 64\ninterface_branding: NEWOS\ninterface_branding_colour: ff7a3c\n\n/NEWOS (text shell)\n    protocol: limine\n    path: boot():/boot/newos-limine.elf\n' > $@
	$(Q)if [ -n "$(LIMINE_CMDLINE)" ]; then \
	  printf '    cmdline: $(LIMINE_CMDLINE)\n' >> $@; fi
	$(Q)printf '\n/NEWOS (GUI)\n    protocol: limine\n    path: boot():/boot/newos-limine.elf\n    cmdline: gui=1\n' >> $@

$(LIMINE_ROOT)/boot/newos-limine.png: brand/newos-limine.png | dirs
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $< $@

$(LIMINE_ROOT)/boot/newos-limine.elf: $(LIMINE_BIN) | dirs
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $(LIMINE_BIN) $@

$(LIMINE_BINS)/limine-bios-cd.bin: $(LIMINE_BINS)/limine
	$(E) "  LIMINE binaries present in $(LIMINE_BINS)"

$(LIMINE_BINS)/limine:
	$(Q)bash scripts/fetch-limine.sh $(LIMINE_RELEASE) $(LIMINE_BINS)

$(LIMINE_BIN): $(OBJS) lint-layers | check-config dirs
	$(Q)mkdir -p $(dir $@)
	$(E) "  LD  $(LIMINE_BIN)"
	$(Q)$(LD) -n --gc-sections -T scripts/limine.ld -o $@ $(OBJS)
	$(E) "  SIZE"
	$(Q)size $@

$(LIMINE_ISO): $(LIMINE_BIN) $(LIMINE_ROOT)/limine.conf \
               $(LIMINE_ROOT)/boot/newos-limine.elf \
               $(LIMINE_ROOT)/boot/newos-limine.png \
               $(LIMINE_BINS)/limine-bios-cd.bin
	$(Q)mkdir -p $(dir $(LIMINE_ISO)) $(LIMINE_ROOT)/boot $(LIMINE_ROOT)/EFI/BOOT
	$(Q)cp $(LIMINE_BINS)/limine-bios-cd.bin $(LIMINE_ROOT)/boot/
	$(Q)cp $(LIMINE_BINS)/limine-uefi-cd.bin $(LIMINE_ROOT)/boot/
	$(Q)cp $(LIMINE_BINS)/limine-bios.sys $(LIMINE_ROOT)/
	$(Q)cp $(LIMINE_BINS)/BOOTX64.EFI $(LIMINE_ROOT)/EFI/BOOT/
	$(Q)rm -f $(LIMINE_ISO)
	$(Q)xorriso -as mkisofs -R -r -J \
	  -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
	  -boot-info-table -hfsplus -apm-block-size 2048 \
	  --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
	  --protective-msdos-label -volid NEWOS \
	  -o $(LIMINE_ISO) $(LIMINE_ROOT)
	$(Q)$(LIMINE_BINS)/limine bios-install $(LIMINE_ISO)
	$(E) "Limine ISO created: $(LIMINE_ISO)"

limine: $(LIMINE_BIN)

# Cross toolchain component: builds binutils+GCC+newlib for x86_64-elf under
# toolchain/out/. The full `make all` pipeline uses this toolchain by default.
toolchain: bootstrap
	$(Q)bash toolchain/build.sh

# Userspace graphics demo. The normal image already contains /bin/glxgears;
# the target is kept as a direct build entry point for the standalone port.
.PHONY: glxgears-newos glxgears-pkg
glxgears-newos: $(GLXGEARS_BIN)
	$(E) "glxgears: $(GLXGEARS_BIN)"

# The demo ships as a native .new package too, so the spec under packages/
# has a real build target instead of sitting unused.
GLXGEARS_PKG := $(BUILD_DIR)/packages/glxgears-newos-0.1.0-x86_64.new
glxgears-pkg: glxgears-newos
	$(Q)mkdir -p $(dir $(GLXGEARS_PKG))
	$(E) "  NEWPKG $(GLXGEARS_PKG)"
	$(Q)python3 tools/newpkg/newpkg-build.py \
	  packages/glxgears-newos.newspec -o $(GLXGEARS_PKG) --root .

qemu-limine: $(LIMINE_ISO)
	$(E) "Booting NEWOS (Limine, text shell; choose GUI in the menu) ..."
	$(Q)$(QEMU) -cdrom $(LIMINE_ISO) -boot order=d -serial stdio -no-reboot \
	  -m 256M $(QEMU_DISPLAY)

# Same image, but boot the GUI entry directly for quick visual checks.
qemu-limine-gui: LIMINE_CMDLINE := gui=1
qemu-limine-gui: $(LIMINE_ISO)
	$(E) "Booting NEWOS (Limine, gui=1) in QEMU ..."
	$(Q)$(QEMU) -cdrom $(LIMINE_ISO) -boot order=d -serial stdio -no-reboot \
	  -m 256M $(QEMU_DISPLAY)

# Target-specific variable: LIMINE_CMDLINE reaches the ISO via the FORCE-prereq
# limine.conf, adding a `cmdline:` option to the boot entry (test_mode).
qemu-limine-test: LIMINE_CMDLINE := test_mode=1
qemu-limine-test: $(LIMINE_ISO)
	$(E) "Running automated test suite through Limine in QEMU ..."
	$(Q)status=0; $(QEMU) -cdrom $(LIMINE_ISO) -boot order=d -serial stdio -no-reboot -m 256M \
	  -display none -device isa-debug-exit,iobase=0xf4 -device pcnet || status=$$?; \
	echo "QEMU exit status: $$status"; \
	if [ "$$status" -ne 1 ]; then \
	  echo "qemu-limine-test: FAIL (expected QEMU exit status 1)"; exit 1; \
	fi; \
	echo "qemu-limine-test: PASS"

# --- Oracle VM VirtualBox --------------------------------------------------
# The Limine ISO boots on VirtualBox's legacy BIOS with a VMSVGA display:
# Limine picks a VESA mode, the kernel drives the linear framebuffer (fbcon)
# and PS/2 keyboard/mouse work out of the box. `vbox-setup` creates the VM
# once; `vbox` opens it with a window, `vbox-test` runs it headless and
# checks the serial log for the green suite result.
VBOX_VM     ?= NEWOS-Limine
VBOX_SERIAL ?= /tmp/newos-vbox-serial.log

vbox-setup: $(LIMINE_ISO)
	$(E) "Creating VirtualBox VM $(VBOX_VM) ..."
	$(Q)VBoxManage createvm --name $(VBOX_VM) --ostype Linux_64 --register \
	  --basefolder "$$HOME/VirtualBox VMs" >/dev/null 2>&1 || true
	$(Q)VBoxManage modifyvm $(VBOX_VM) --memory 512 --vram 16 \
	  --firmware bios --boot1 dvd --boot2 none --acpi on --chipset piix3 \
	  --mouse ps2 --keyboard ps2 --graphicscontroller vmsvga \
	  --audio-driver null --nic1 none --uart1 0x3F8 4 \
	  --uart-mode1 file $(VBOX_SERIAL)
	$(Q)VBoxManage storagectl $(VBOX_VM) --name IDE --add ide \
	  --controller PIIX4 >/dev/null 2>&1 || true
	$(Q)VBoxManage storageattach $(VBOX_VM) --storagectl IDE --port 1 \
	  --device 0 --type dvddrive --medium $(CURDIR)/$(LIMINE_ISO)

vbox: vbox-setup
	$(E) "Starting $(VBOX_VM) in VirtualBox ..."
	$(Q)VBoxManage startvm $(VBOX_VM)

vbox-test: vbox-setup
	$(E) "Headless VirtualBox boot test (serial log: $(VBOX_SERIAL)) ..."
	$(Q)VBoxManage controlvm $(VBOX_VM) poweroff >/dev/null 2>&1 || true
	$(Q)rm -f $(VBOX_SERIAL)
	$(Q)VBoxManage startvm $(VBOX_VM) --type headless >/dev/null
	$(Q)for i in $$(seq 1 40); do \
	  grep -q '=== 11 tests, 0 failures ===' $(VBOX_SERIAL) 2>/dev/null && break; \
	  sleep 1; \
	done; \
	VBoxManage controlvm $(VBOX_VM) poweroff >/dev/null 2>&1; \
	if grep -q '=== 11 tests, 0 failures ===' $(VBOX_SERIAL); then \
	  echo "VBox: boot complete, 11/11 phase-1 kernel tests PASS"; \
	else \
	  echo "VBox: FAILED - see $(VBOX_SERIAL)"; exit 1; \
	fi

qemu: all
	$(E) "Booting NEWOS in QEMU (serial console) ..."
	$(Q)$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 256M \
	  $(QEMU_DISPLAY)

qemu-debug: all
	$(E) "Booting NEWOS in QEMU with GDB server on port 1234 ..."
	$(Q)$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 256M \
	  $(QEMU_DISPLAY) -s -S -d guest_errors

# `all` deliberately produces only the ISO, so the PVH suite must ask for
# its own kernel: without this dependency QEMU ran over a missing (or
# stale) ELF and the `exit 1 = PASS` convention reported a false green.
# Audio devices ride along like the CI runner does (probing the recorder
# backend name, "file" vs "wav" across QEMU builds); when no recorder
# exists the hda ktest still self-verifies its DMA round through LPIB.
# The WAV payload check on the host lives in scripts/ci/run_qemu_test.sh.
qemu-test: $(KERNEL_BIN)
	$(E) "Running automated test suite in QEMU ..."
	$(Q)adrv=none; \
	  for cand in file wav; do \
	    if $(QEMU) -audiodev help 2>/dev/null | grep -qw $$cand; then adrv=$$cand; break; fi; \
	  done; \
	  wava=$$(mktemp /tmp/newos-hda-XXXXXX.wav); \
	  astr=""; \
	  if [ "$$adrv" != "none" ]; then \
	    astr="-audiodev driver=$$adrv,id=aud0,path=$$wava,out.frequency=48000,out.channels=2,out.format=s16"; \
	  fi; \
	  $(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 256M \
	  -display none -device isa-debug-exit,iobase=0xf4 \
	  -netdev user,id=net0 -device pcnet,netdev=net0 \
	  -netdev user,id=net1 -device e1000,netdev=net1 \
	  $$astr -device intel-hda -device hda-duplex,audiodev=aud0 \
	  -append "test_mode=1"; \
	  rc=$$?; \
	  rm -f $$wava; \
	  echo "QEMU exit status: $$rc"; \
	  if [ $$rc -ne 1 ]; then \
	    echo "qemu-test: FAIL (isa-debug-exit maps 1 to this status only when every test passed)"; \
	    exit 1; \
	  fi; \
	  echo "qemu-test: PASS"

# Non-interactive session: the image runs /etc/nsh.rc (built from
# NSH_RC_SRC) and the whole transcript lands in build/nsh.log. Driving the
# shell over serial instead is racy - QEMU only feeds the emulated UART as
# fast as the guest drains it, so scripted boot is how a session gets
# verified without dropping keystrokes.
qemu-rc: $(KERNEL_BIN)
	$(E) "Running scripted nsh session in QEMU ..."
	$(Q)rm -f $(BUILD_DIR)/nsh.log
	$(Q)status=0; timeout $(or $(RC_TIMEOUT),60) $(QEMU) -kernel $(KERNEL_BIN) \
	  -m 512M -display none -no-reboot \
	  -device isa-debug-exit,iobase=0xf4 \
	  -serial file:$(BUILD_DIR)/nsh.log \
	    >/dev/null 2>&1 || status=$$?; \
	cat $(BUILD_DIR)/nsh.log; \
	if grep -q 'USER EXCEPTION' $(BUILD_DIR)/nsh.log; then \
	  echo "qemu-rc: FAIL (userspace exception)"; exit 1; \
	fi; \
	if [ "$$status" -ne 0 ] && [ "$$status" -ne 124 ]; then \
	  echo "qemu-rc: FAIL (QEMU exit $$status)"; exit 1; \
	fi

# Drive a scripted nsh session over the serial console. CMDS is a file with
# one command per line ('#' comments); the harness waits for each prompt.
CMDS ?= user/rootfs/nsh.rc
MEM  ?= 512
qemu-shell: $(KERNEL_BIN)
	$(E) "Driving nsh from $(CMDS) over the serial console ..."
	$(Q)python3 scripts/qemu-shell.py $(KERNEL_BIN) $(CMDS) --mem $(MEM)

qemu-network: all
	$(E) "Booting NEWOS with user-mode networking ..."
	$(Q)$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 256M \
	  $(QEMU_DISPLAY) -netdev user,id=net0 -device virtio-net-pci,netdev=net0

qemu-disk: $(DISK_IMG)
	$(E) "Booting NEWOS from disk image ..."
	$(Q)$(QEMU) -drive file=$(DISK_IMG),format=raw,if=ide -serial stdio -no-reboot \
	  -m 256M $(QEMU_DISPLAY)

$(DISK_IMG): all
	$(Q)mkdir -p $(dir $@)
	$(E) "Creating disk image ..."
	$(Q)dd if=/dev/zero of=$(DISK_IMG) bs=1M count=64 2>/dev/null

clean:
	$(Q)rm -rf $(BUILD_DIR)
	$(E) "Cleaned."

-include $(OBJS:.o=.d)
