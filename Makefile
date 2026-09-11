# NEWOS Build System
# ============================================================================
# A complete, layered build system for the NEWOS operating system.
#
# Primary targets:
#   make all          - build the kernel
#   make qemu         - boot the kernel in QEMU
#   make qemu-debug   - boot in QEMU with GDB server on port 1234
#   make qemu-test    - run the automated test suite inside QEMU
#   make qemu-network - boot with user-mode networking
#   make qemu-disk    - boot from a created disk image
#   make clean        - remove all build artifacts

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
ARCH            ?= x86_64
CONFIG          ?= configs/$(ARCH)/debug.config
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

CC              := gcc
LD              := ld
NASM            := nasm
QEMU            := qemu-system-x86_64

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
                   arch/$(ARCH)/cpu/x86_vendor.c arch/$(ARCH)/cpu/x86_gdt.c \
                   arch/$(ARCH)/interrupts/x86_idt.c \
                   arch/$(ARCH)/interrupts/x86_pic.c arch/$(ARCH)/interrupts/x86_irq.c \
                   arch/$(ARCH)/memory/x86_paging.c
ARCH_SRCS_ASM   := arch/$(ARCH)/boot/x86_entry.S arch/$(ARCH)/boot/limine_entry.S \
                   arch/$(ARCH)/interrupts/x86_interrupt.S \
                   arch/$(ARCH)/threading/x86_context.S

# core: kernel core (log, oops, init) and privileged subsystems
CORE_SRCS       := core/core_panic.c core/core_printk.c process/elf_loader.c \
                   core/core_cmdline.c core/core_init.c \
                   core/core_selftest.c core/core_oops.c \
                   core/core_console.c core/core_time.c \
                   process/proc_process.c syscall/syscall_dispatch.c \
                   process/proc_thread.c process/sched.c

# mm: memory-management domain
MM_SRCS         := mm/mm_heap.c mm/mm_kmalloc.c mm/mm_debug.c \
                   mm/mm_pmm_alloc.c mm/mm_pmm_bitmap.c mm/mm_pmm_buddy.c \
                   mm/mm_frame.c mm/mm_usercopy.c mm/mm_address_space.c \
                   mm/mm_vmm.c

# storage: VFS and backends
FS_SRCS         := fs/devfs/devfs.c fs/initramfs/initramfs.c fs/tmpfs/tmpfs.c \
                   fs/vfs/vfs_inode.c fs/vfs/vfs_core.c

# drivers: hardware-facing services
DRV_SRCS        := drivers/console/tty_console.c drivers/console/vga_text_console.c \
                   drivers/core/device_core.c \
                   drivers/core/driver_core.c drivers/qemu/qemu_debug.c \
                   drivers/pci/pci_bus.c drivers/pci/pci_chipset.c \
                   drivers/pci/pci_ids.c \
                   drivers/net/pcnet.c \
                   drivers/serial/serial_16550.c drivers/timer/pit_timer.c

# ipc, lib/kernel: reusable kernel-side utilities
IPC_SRCS        := ipc/ipc_pipe.c
LIB_SRCS        := lib/kernel/iru_bitmap.c lib/kernel/iru_format.c \
                   lib/kernel/iru_lock.c lib/kernel/iru_memory.c \
                   lib/kernel/iru_ringbuf.c lib/kernel/iru_string.c

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

OBJS_C          := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(KERNEL_SRCS_C) $(ARCH_SRCS_C))
OBJS_ASM        := $(patsubst %.S,$(BUILD_DIR)/obj/%.o,$(ARCH_SRCS_ASM))
OBJS            := $(OBJS_C) $(OBJS_ASM) $(HELLO_EMBED) $(INIT_EMBED)

INCLUDE         := -Iinclude -I. -Ilib/kernel -Iarch/$(ARCH)/include

# ---------------------------------------------------------------------------
# Userland build + embed
# ---------------------------------------------------------------------------

# Userland is freestanding but has access to the full instruction set
# (no -mno-red-zone, the program runs with a normal SysV stack).
USER_CFLAGS     := -c -O2 -ffreestanding -nostdlib -fno-pic -fno-pie \
                   -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables \
                   -fno-exceptions -mgeneral-regs-only -Wall -Wextra -Werror

$(HELLO_OBJ): $(HELLO_DIR)/hello_main.c
	@mkdir -p $(dir $@)
	@echo "  UCC $<"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(HELLO_BIN): $(HELLO_OBJ) $(HELLO_DIR)/linker.ld
	@mkdir -p $(dir $@)
	@echo "  ULD $@"
	@$(LD) -n -static -nostdlib -T $(HELLO_DIR)/linker.ld -o $@ $(HELLO_OBJ)

# Embed hello.elf as an opaque blob. The linker derives the exported symbols
# from the input file name, so the link runs from $(BUILD_DIR) with the file
# named exactly `hello.elf` -> _binary_hello_elf_{start,end,size}.
$(HELLO_EMBED): $(HELLO_BIN)
	@mkdir -p $(dir $@)
	@cp $(HELLO_BIN) $(BUILD_DIR)/hello.elf
	@echo "  EMB $<"
	@cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/hello.bin.o hello.elf

$(INIT_OBJ): $(INIT_DIR)/shell_main.c
	@mkdir -p $(dir $@)
	@echo "  UCC $<"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(INIT_BIN): $(INIT_OBJ) $(INIT_DIR)/linker.ld
	@mkdir -p $(dir $@)
	@echo "  ULD $@"
	@$(LD) -n -static -nostdlib -T $(INIT_DIR)/linker.ld -o $@ $(INIT_OBJ)

# Roaming init shell, embedded as `init.elf` -> _binary_init_elf_{start,end,size}.
$(INIT_EMBED): $(INIT_BIN)
	@mkdir -p $(dir $@)
	@cp $(INIT_BIN) $(BUILD_DIR)/init.elf
	@echo "  EMB $<"
	@cd $(BUILD_DIR) && $(LD) -r -b binary -o obj/userland/init.bin.o init.elf

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------
.PHONY: all clean qemu qemu-debug qemu-test qemu-network qemu-disk iso \
        qemu-limine qemu-limine-test limine check-config dirs lint-layers FORCE

.DEFAULT_GOAL := all

all: $(KERNEL_BIN)

dirs:
	@mkdir -p $(BUILD_DIR)/obj

check-config:
	@test -f $(CONFIG) && echo "Using config: $(CONFIG)" || { \
	  echo "ERROR: config $(CONFIG) not found"; exit 1; }

lint-layers:
	@scripts/ci/lint_layering.sh

$(BUILD_DIR)/obj/%.o: %.c | dirs
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) $(INCLUDE) -o $@ $<

$(BUILD_DIR)/obj/%.o: %.S | dirs
	@mkdir -p $(dir $@)
	@echo "  AS  $<"
	@$(NASM) $(ASFLAGS) -o $@ $<

$(KERNEL_BIN): $(OBJS) lint-layers | check-config dirs
	@mkdir -p $(dir $@)
	@echo "  LD  $(KERNEL_BIN)"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "  SIZE"
	@size $@
	@echo "=== Build complete: $(KERNEL_BIN) ==="

iso: $(KERNEL_BIN)
	@mkdir -p $(dir $(ISO_IMG)) $(BUILD_DIR)/iso/boot/grub
	@cp $(KERNEL_BIN) $(BUILD_DIR)/iso/boot/newos.elf
	@printf 'set timeout=0\nset default=0\nserial --unit=0 --speed=115200 --stop=1\nterminal_input serial\nterminal_output serial\nmenuentry "NEWOS" {\n  multiboot2 /boot/newos.elf\n  boot\n}\nmenuentry "NEWOS (test mode)" {\n  multiboot2 /boot/newos.elf test_mode=1\n  boot\n}\n' \
	  > $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@grub-mkrescue -o $(ISO_IMG) $(BUILD_DIR)/iso 2>/dev/null || \
	  xorriso -as mkisofs -b boot/grub/eltorito.img -no-emul-boot \
	  -boot-load-size 4 -boot-info-table --grub2-boot-info \
	  --grub2-mbr /usr/lib/grub/i386-pc/boot.img \
	  -o $(ISO_IMG) $(BUILD_DIR)/iso
	@echo "ISO created: $(ISO_IMG)"

# Limine bootloader binaries (bios + uefi CD boot images), cached under
# $(BUILD_DIR)/limine-bins. The release tag can be overridden.
LIMINE_RELEASE  ?= v12.9.0

$(LIMINE_ROOT)/limine.conf: FORCE | dirs
	@mkdir -p $(LIMINE_ROOT)
	@printf 'timeout=0\nserial: yes\n\n/NEWOS\n    protocol: limine\n    path: boot():/boot/newos-limine.elf\n' > $@
	@if [ -n "$(LIMINE_CMDLINE)" ]; then \
	  printf '    cmdline: $(LIMINE_CMDLINE)\n' >> $@; fi

$(LIMINE_ROOT)/boot/newos-limine.elf: $(LIMINE_BIN) | dirs
	@mkdir -p $(dir $@)
	@cp $(LIMINE_BIN) $@

$(LIMINE_BINS)/limine-bios-cd.bin: $(LIMINE_BINS)/limine
	@echo "  LIMINE binaries present in $(LIMINE_BINS)"

$(LIMINE_BINS)/limine:
	@bash scripts/fetch-limine.sh $(LIMINE_RELEASE) $(LIMINE_BINS)

$(LIMINE_BIN): $(OBJS) lint-layers | check-config dirs
	@mkdir -p $(dir $@)
	@echo "  LD  $(LIMINE_BIN)"
	@$(LD) -n --gc-sections -T scripts/limine.ld -o $@ $(OBJS)
	@echo "  SIZE"
	@size $@

$(LIMINE_ISO): $(LIMINE_BIN) $(LIMINE_ROOT)/limine.conf \
               $(LIMINE_ROOT)/boot/newos-limine.elf \
               $(LIMINE_BINS)/limine-bios-cd.bin
	@mkdir -p $(dir $(LIMINE_ISO)) $(LIMINE_ROOT)/boot $(LIMINE_ROOT)/EFI/BOOT
	@cp $(LIMINE_BINS)/limine-bios-cd.bin $(LIMINE_ROOT)/boot/
	@cp $(LIMINE_BINS)/limine-uefi-cd.bin $(LIMINE_ROOT)/boot/
	@cp $(LIMINE_BINS)/limine-bios.sys $(LIMINE_ROOT)/
	@cp $(LIMINE_BINS)/BOOTX64.EFI $(LIMINE_ROOT)/EFI/BOOT/
	@rm -f $(LIMINE_ISO)
	@xorriso -as mkisofs -R -r -J \
	  -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
	  -boot-info-table -hfsplus -apm-block-size 2048 \
	  --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
	  --protective-msdos-label -volid NEWOS \
	  -o $(LIMINE_ISO) $(LIMINE_ROOT)
	@$(LIMINE_BINS)/limine bios-install $(LIMINE_ISO)
	@echo "Limine ISO created: $(LIMINE_ISO)"

limine: $(LIMINE_BIN)

qemu-limine: $(LIMINE_ISO)
	@echo "Booting NEWOS (Limine) in QEMU ..."
	@$(QEMU) -cdrom $(LIMINE_ISO) -boot order=d -serial stdio -no-reboot -m 128M

# Target-specific variable: LIMINE_CMDLINE reaches the ISO via the FORCE-prereq
# limine.conf, adding a `cmdline:` option to the boot entry (test_mode).
qemu-limine-test: LIMINE_CMDLINE := test_mode=1
qemu-limine-test: $(LIMINE_ISO)
	@echo "Running automated test suite through Limine in QEMU ..."
	@$(QEMU) -cdrom $(LIMINE_ISO) -boot order=d -serial stdio -no-reboot -m 128M \
	  -device isa-debug-exit,iobase=0xf4 -device pcnet; \
	  echo "QEMU exit status: $$?"

qemu: all
	@echo "Booting NEWOS in QEMU (serial console) ..."
	@$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 128M

qemu-debug: all
	@echo "Booting NEWOS in QEMU with GDB server on port 1234 ..."
	@$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 128M \
	  -s -S -d guest_errors

qemu-test: all
	@echo "Running automated test suite in QEMU ..."
	@$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 128M \
	  -device isa-debug-exit,iobase=0xf4 -device pcnet -append "test_mode=1"; \
	  echo "QEMU exit status: $$?"

qemu-network: all
	@echo "Booting NEWOS with user-mode networking ..."
	@$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 128M \
	  -netdev user,id=net0 -device virtio-net-pci,netdev=net0

qemu-disk: $(DISK_IMG)
	@echo "Booting NEWOS from disk image ..."
	@$(QEMU) -drive file=$(DISK_IMG),format=raw,if=ide -serial stdio -no-reboot -m 128M

$(DISK_IMG): all
	@mkdir -p $(dir $@)
	@echo "Creating disk image ..."
	@dd if=/dev/zero of=$(DISK_IMG) bs=1M count=64 2>/dev/null

clean:
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned."

-include $(OBJS:.o=.d)
