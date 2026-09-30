# NEWOS Build System
# ============================================================================
# A complete, layered build system for the NEWOS operating system.
#
# Primary targets (full list: make help):
#   make all          - build the kernel
#   make limine       - Limine-flavour kernel (BIOS+UEFI graphics path)
#   make iso          - GRUB multiboot2 ISO
#   make qemu         - boot the kernel in QEMU (serial console)
#   make qemu-test    - automated test suite in QEMU (exit 1 = PASS)
#   make qemu-limine-test - suite through the Limine ISO
#   make check-newpkg - package system tests (no QEMU needed)
#   make toolchain    - build the x86_64-elf cross toolchain
#   make clean        - remove all build artifacts
#
# Options: V=0 (short log), V=1 (full commands, default),
#   ARCH=x86_64, CROSS_PREFIX=x86_64-elf-, CONFIG=path/to/config

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
ARCH            ?= x86_64
CONFIG          ?= configs/$(ARCH)/debug.config
# Cross toolchain prefix (empty = host gcc/ld). Build it once with
#   tools/toolchain/build.sh
# then use: make CROSS_PREFIX=x86_64-elf- all
# (needs toolchain/out/bin on PATH).
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

CC              := $(CROSS_PREFIX)gcc
LD              := $(CROSS_PREFIX)ld
AR              := $(CROSS_PREFIX)ar
NASM            := nasm
QEMU            := qemu-system-x86_64

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
                   arch/$(ARCH)/memory/x86_paging.c
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
                    fs/ext4/ext4fs.c fs/procfs/procfs.c fs/sysfs/sysfs.c

# drivers: hardware-facing services
DRV_SRCS        := drivers/console/tty_console.c drivers/console/vga_text_console.c \
                     drivers/core/device_core.c \
                     drivers/core/driver_core.c drivers/qemu/qemu_debug.c \
                     drivers/block_dev.c drivers/graphics/fb.c \
                     drivers/input/ps2_keyboard.c drivers/input/ps2_mouse.c \
                     drivers/pci/pci_bus.c drivers/pci/pci_chipset.c \
                     drivers/pci/pci_ids.c \
                     drivers/net/pcnet.c \
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

# libc: freestanding C library for userland (string/stdlib/stdio/malloc).
# Built as a static archive; programs opt in (see ltest link rule).
LIBC_SRCS       := libc/src/string.c libc/src/stdlib.c \
                   libc/src/stdio.c libc/src/malloc.c libc/src/unistd.c \
                   libc/src/syscall.c libc/src/errno.c libc/src/time.c \
                   libc/src/signal.c libc/src/termios.c libc/src/ioctl.c
LIBC_OBJS       := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(LIBC_SRCS))
LIBC_A          := $(BUILD_DIR)/userland/libc.a

KERNEL_SRCS_C   := $(CORE_SRCS) $(MM_SRCS) $(FS_SRCS) $(DRV_SRCS) \
                   $(IPC_SRCS) $(LIB_SRCS)

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
                    wc head grep sort img vid ltest about newfetch desktop newpkg kilo \
                    lua tinygl
PROG_OBJS       := $(patsubst %,$(BUILD_DIR)/userland/%.o,$(PROG_NAMES))
PROG_BINS       := $(patsubst %,$(BUILD_DIR)/userland/%.elf,$(PROG_NAMES))
PROG_EMBEDS     := $(patsubst %,$(BUILD_DIR)/obj/userland/%.bin.o,$(PROG_NAMES))

# newpkg carries a second translation unit: the portable `.new` format core
# (shared with the host test driver). Linked explicitly like ltest+libc.
NEWPKG_FORMAT_OBJ := $(BUILD_DIR)/userland/newpkg_format.o

# Sample `.new` package embedded for live QEMU exercises (/tmp/*.new).
# Built from the already-linked hello ELF through the host builder script.
NEWPKG_SAMPLE_NEWSPEC := packages/hello-new.newspec
NEWPKG_PKGDIR         := $(BUILD_DIR)/packages
NEWPKG_HELLO_NEW      := $(NEWPKG_PKGDIR)/hello-new-1.0.0-x86_64.new
NEWPKG_SAMPLE_NEW     := $(BUILD_DIR)/sample.new
SAMPLE_NEW_EMBED      := $(BUILD_DIR)/obj/userland/sample_new.bin.o

# Shell boot script (nsh runs /etc/nsh.rc before the prompt). Commented-out
# by default so a normal boot stays interactive; edit it to script a session,
# or point `make qemu-rc` at a generated one for automated runs.
NSH_RC_SRC        := user/rootfs/nsh.rc
NSH_RC            := $(BUILD_DIR)/nsh.rc
NSH_RC_EMBED      := $(BUILD_DIR)/obj/userland/nsh_rc.bin.o

# --- GNU tools (host-built, static musl) ---------------------------------
# Upstream GNU programs cannot be compiled on board (NEWOS ships no C
# toolchain and its libc is a deliberately small subset), so they are built
# on the HOST against musl: musl emits Linux syscall numbers, which is the
# numbering this kernel's ABI follows, and `-static` yields the plain
# ET_EXEC image elf_loader.c can map. scripts/build-gnu.sh stages the
# resulting ELFs under $(GNU_STAGE)/<tool>, and scripts/pack-gnu.sh turns
# whatever is staged into ONE `.new` archive that the board installs with
# `newpkg install /tmp/gnu-coreutils.new`.
#
# One archive rather than one embed per tool: 68 coreutils are 10 MiB, and
# shipping them through the package manager is both smaller in the build
# tree and the thing actually being tested.
#
# Declared BEFORE OBJS on purpose: `OBJS :=` expands immediately, so a
# later definition would silently drop the embeds (see AGENT.md).
GNU_STAGE       := $(BUILD_DIR)/gnu
GNU_PKG_VERSION := 9.7
GNU_PKG         := $(BUILD_DIR)/gnu-coreutils.new
GNU_PKG_EMBED   := $(BUILD_DIR)/obj/userland/gnu_coreutils.bin.o

# No prerequisites beyond the directory: the archive is rebuilt when `make
# gnu` invalidates it, and an unstaged tree yields a zero-length blob the
# kernel skips, so `make all` stays green without Docker (and CI works).
$(GNU_PKG): | dirs
	$(Q)if ! bash scripts/pack-gnu.sh $(GNU_STAGE) $@ $(GNU_PKG_VERSION) >/dev/null 2>&1; then \
	  echo "  GNU pkg: nothing staged (run 'make gnu') -> empty placeholder"; \
	  : > $@; \
	fi

# The blob symbol is derived from the operand name: gnu-coreutils.new ->
# _binary_gnu_coreutils_new_{start,end}.
$(GNU_PKG_EMBED): $(GNU_PKG)
	$(Q)mkdir -p $(dir $@)
	$(E) "  GEMB $(notdir $<)"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/gnu_coreutils.bin.o gnu-coreutils.new

gnu: | dirs
	$(E) "Building GNU tools with musl (Docker) ..."
	$(Q)bash scripts/build-gnu.sh stage $(GNU_STAGE)
	$(Q)rm -f $(GNU_PKG)

gnu-pkg: $(GNU_PKG)
	$(Q)ls -la $(GNU_PKG)

.PHONY: gnu gnu-pkg

# Keep the linked tool ELFs: they chain into embeds through a pattern rule
# and make would otherwise delete them as intermediates after every build.
# $(KERNEL_BIN) goes the same way: `make all` reaches it through the ISO
# chain, and a plain `make all` would otherwise remove the -kernel image
# that `make qemu`/scripts/qemu-shell.py boot.
.SECONDARY: $(PROG_BINS) $(KERNEL_BIN)

OBJS_C          := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(KERNEL_SRCS_C) $(ARCH_SRCS_C))
OBJS_ASM        := $(patsubst %.S,$(BUILD_DIR)/obj/%.o,$(ARCH_SRCS_ASM))
OBJS            := $(OBJS_C) $(OBJS_ASM) $(HELLO_EMBED) $(INIT_EMBED) \
                   $(PROG_EMBEDS) $(SPLASH_EMBED) $(PPM_EMBED) \
                   $(SAMPLE_NEW_EMBED) $(NSH_RC_EMBED) $(GNU_PKG_EMBED)

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
                   -Ilibc/include

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

$(BUILD_DIR)/obj/userland/%.bin.o: $(BUILD_DIR)/userland/%.elf
	$(Q)mkdir -p $(dir $@)
	$(Q)cp $< $(BUILD_DIR)/$*.elf
	$(E) "  EMB $<"
	$(Q)cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/$*.bin.o $*.elf

# --- newpkg: toolbox tool + portable format core -------------------------
# newpkg_main.c comes from the data-driven rule above; the format core is
# an extra object linked explicitly (explicit rules win over the pattern
# rule, same mechanism as ltest+libc below).

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

# --- libc (static archive for userland) --------------------------------
# Compiled with the userland flags (NOT the kernel -mcmodel=kernel ones).

$(BUILD_DIR)/obj/libc/%.o: libc/%.c | dirs
	$(Q)mkdir -p $(dir $@)
	$(E) "  UCC $<"
	$(Q)$(CC) $(USER_CFLAGS) -o $@ $<

$(LIBC_A): $(LIBC_OBJS) | dirs
	$(Q)mkdir -p $(dir $@)
	$(E) "  AR  $@"
	$(Q)$(AR) rcs $@ $(LIBC_OBJS)

# ltest proves libc: same recipe as the toolbox pattern rule plus libc.a
# (explicit rules win over pattern rules).
$(BUILD_DIR)/userland/ltest.elf: $(BUILD_DIR)/userland/ltest.o \
    $(NSHLIB_OBJ) $(START_OBJ) $(LIBC_A) $(APP_LD)
	$(Q)mkdir -p $(dir $@)
	$(E) "  ULD $@ +libc"
	$(Q)$(LD) -n -static -nostdlib -T $(APP_LD) -o $@ $(filter %.o,$^) $(filter %.a,$^)
$(BUILD_DIR)/userland/kilo.elf: $(BUILD_DIR)/userland/kilo.o \
    $(NSHLIB_OBJ) $(START_OBJ) $(LIBC_A) $(APP_LD)
	$(Q)mkdir -p $(dir $@)
	$(E) "  ULD $@ +libc"
	$(Q)$(LD) -n -static -nostdlib -T $(APP_LD) -o $@ $(filter %.o,$^) $(filter %.a,$^)

# --- generated assets: console font + test images ----------------------
# The font is fetched once and cached; the images are generated locally
# with python3 (no external image tools needed).

$(FONT_HDR):
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

# fb.c needs the generated font header.
$(BUILD_DIR)/obj/drivers/graphics/fb.o: $(FONT_HDR)

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------
.PHONY: all clean qemu qemu-debug qemu-test qemu-network qemu-disk iso \
        qemu-limine qemu-limine-test limine toolchain check-config dirs \
        lint-layers check-newpkg build-info help \
        vbox vbox-setup vbox-test FORCE

.DEFAULT_GOAL := all

# Delete half-written targets when a recipe fails (no corrupt artifacts).
.DELETE_ON_ERROR:

# Default build: the Limine ISO only. The kernel ELF lives inside the ISO
# (boot/newos-limine.elf); no standalone OS ELF is produced by `make all`.
all: build-info $(LIMINE_ISO)

# One-line build configuration banner (always printed for `all`).
build-info:
	@echo "NEWOS build: ARCH=$(ARCH) CONFIG=$(CONFIG) V=$(V) (V=0: short log)"
	@echo "  CC='$(CC)' LD='$(LD)' AR='$(AR)' CROSS_PREFIX='$(CROSS_PREFIX)'"
	@echo "  git: $$(git rev-parse --short HEAD 2>/dev/null || echo nogit)/$$(git diff --quiet 2>/dev/null && echo clean || echo dirty)"

help:
	@echo "NEWOS build targets:"
	@echo "  make all               Limine ISO -> build/images/newos-x86_64-limine.iso"
	@echo "  make build/images/newos-x86_64.elf          standalone PVH/multiboot2 kernel"
	@echo "  make limine            Limine-flavour kernel"
	@echo "  make iso               GRUB multiboot2 ISO"
	@echo "  make build/images/newos-x86_64-limine.iso   BIOS+UEFI Limine ISO"
	@echo "  make qemu              boot in QEMU (serial console)"
	@echo "  make qemu-debug        boot with GDB server on :1234"
	@echo "  make qemu-test         automated test suite in QEMU (exit 1 = PASS)"
	@echo "  make qemu-limine-test  suite through the Limine ISO"
	@echo "  make vbox              boot the Limine ISO in VirtualBox (GUI)"
	@echo "  make vbox-test         headless VirtualBox boot + serial check"
	@echo "  make qemu-network      boot with user-mode networking"
	@echo "  make qemu-disk         boot from a 64 MiB raw disk image"
	@echo "  make check-newpkg      package system tests (no QEMU needed)"
	@echo "  make lint-layers       layering contract check only"
	@echo "  make toolchain         build x86_64-elf cross toolchain"
	@echo "  make clean             remove build/ entirely"
	@echo "Options: V=0 (short log), V=1 (full commands, default),"
	@echo "  ARCH=x86_64, CROSS_PREFIX=x86_64-elf-, CONFIG=path/to/config"

dirs:
	$(Q)mkdir -p $(BUILD_DIR)/obj

check-config:
	$(Q)test -f $(CONFIG) && echo "Using config: $(CONFIG)" || { \
	  echo "ERROR: config $(CONFIG) not found"; exit 1; }

lint-layers:
	$(Q)scripts/ci/lint_layering.sh

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
	$(Q)gcc -DNEWPKG_HOST -Wall -Wextra -Werror \
	  -o $(BUILD_DIR)/newpkg-host \
	  user/programs/newpkg/newpkg_format.c \
	  user/programs/newpkg/newpkg_main.c tools/newpkg/posix-shim.c
	$(Q)NEWPKG_HOST_BIN=$(BUILD_DIR)/newpkg-host \
	  python3 tools/newpkg/tests/test_newpkg.py

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
	$(Q)printf 'set timeout=0\nset default=0\nserial --unit=0 --speed=115200 --stop=1\nterminal_input serial\nterminal_output serial\nmenuentry "NEWOS" {\n  multiboot2 /boot/newos.elf\n  boot\n}\nmenuentry "NEWOS (test mode)" {\n  multiboot2 /boot/newos.elf test_mode=1\n  boot\n}\n' \
	  > $(BUILD_DIR)/iso/boot/grub/grub.cfg
	$(Q)grub-mkrescue -o $(ISO_IMG) $(BUILD_DIR)/iso 2>/dev/null || \
	  xorriso -as mkisofs -b boot/grub/eltorito.img -no-emul-boot \
	  -boot-load-size 4 -boot-info-table --grub2-boot-info \
	  --grub2-mbr /usr/lib/grub/i386-pc/boot.img \
	  -o $(ISO_IMG) $(BUILD_DIR)/iso
	$(E) "ISO created: $(ISO_IMG)"

# Limine bootloader binaries (bios + uefi CD boot images), cached under
# $(BUILD_DIR)/limine-bins. The release tag can be overridden.
LIMINE_RELEASE  ?= v12.9.0

$(LIMINE_ROOT)/limine.conf: FORCE | dirs
	$(Q)mkdir -p $(LIMINE_ROOT)
	$(Q)printf 'timeout=0\nserial: yes\n\n/NEWOS\n    protocol: limine\n    path: boot():/boot/newos-limine.elf\n' > $@
	$(Q)if [ -n "$(LIMINE_CMDLINE)" ]; then \
	  printf '    cmdline: $(LIMINE_CMDLINE)\n' >> $@; fi

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

# Cross toolchain (opt-in): builds binutils+GCC for x86_64-elf under
# build/toolchain/. Use with `make CROSS_PREFIX=x86_64-elf- ...`
# after exporting toolchain/out/bin on PATH.
toolchain:
	$(Q)bash toolchain/build.sh

qemu-limine: $(LIMINE_ISO)
	$(E) "Booting NEWOS (Limine) in QEMU ..."
	$(Q)$(QEMU) -cdrom $(LIMINE_ISO) -boot order=d -serial stdio -no-reboot -m 128M

# Target-specific variable: LIMINE_CMDLINE reaches the ISO via the FORCE-prereq
# limine.conf, adding a `cmdline:` option to the boot entry (test_mode).
qemu-limine-test: LIMINE_CMDLINE := test_mode=1
qemu-limine-test: $(LIMINE_ISO)
	$(E) "Running automated test suite through Limine in QEMU ..."
	$(Q)$(QEMU) -cdrom $(LIMINE_ISO) -boot order=d -serial stdio -no-reboot -m 128M \
	  -device isa-debug-exit,iobase=0xf4 -device pcnet; \
	  echo "QEMU exit status: $$?"

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
	  grep -q '=== 8 tests, 0 failures ===' $(VBOX_SERIAL) 2>/dev/null && break; \
	  sleep 1; \
	done; \
	VBoxManage controlvm $(VBOX_VM) poweroff >/dev/null 2>&1; \
	if grep -q '=== 8 tests, 0 failures ===' $(VBOX_SERIAL); then \
	  echo "VBox: boot complete, 8/8 kernel tests PASS"; \
	else \
	  echo "VBox: FAILED - see $(VBOX_SERIAL)"; exit 1; \
	fi

qemu: all
	$(E) "Booting NEWOS in QEMU (serial console) ..."
	$(Q)$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 128M

qemu-debug: all
	$(E) "Booting NEWOS in QEMU with GDB server on port 1234 ..."
	$(Q)$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 128M \
	  -s -S -d guest_errors

# `all` deliberately produces only the ISO, so the PVH suite must ask for
# its own kernel: without this dependency QEMU ran over a missing (or
# stale) ELF and the `exit 1 = PASS` convention reported a false green.
qemu-test: $(KERNEL_BIN)
	$(E) "Running automated test suite in QEMU ..."
	$(Q)$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 128M \
	  -device isa-debug-exit,iobase=0xf4 -device pcnet -append "test_mode=1"; \
	  rc=$$?; echo "QEMU exit status: $$rc"; \
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
	$(Q)timeout $(or $(RC_TIMEOUT),60) $(QEMU) -kernel $(KERNEL_BIN) \
	  -m 512M -display none -no-reboot \
	  -device isa-debug-exit,iobase=0xf4 \
	  -serial file:$(BUILD_DIR)/nsh.log -append "test_mode=1" \
	    >/dev/null 2>&1 || true
	$(Q)cat $(BUILD_DIR)/nsh.log

qemu-network: all
	$(E) "Booting NEWOS with user-mode networking ..."
	$(Q)$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 128M \
	  -netdev user,id=net0 -device virtio-net-pci,netdev=net0

qemu-disk: $(DISK_IMG)
	$(E) "Booting NEWOS from disk image ..."
	$(Q)$(QEMU) -drive file=$(DISK_IMG),format=raw,if=ide -serial stdio -no-reboot -m 128M

$(DISK_IMG): all
	$(Q)mkdir -p $(dir $@)
	$(E) "Creating disk image ..."
	$(Q)dd if=/dev/zero of=$(DISK_IMG) bs=1M count=64 2>/dev/null

clean:
	$(Q)rm -rf $(BUILD_DIR)
	$(E) "Cleaned."

-include $(OBJS:.o=.d)
