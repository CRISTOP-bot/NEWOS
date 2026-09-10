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
KERNEL_BIN      := $(BUILD_DIR)/newos-$(ARCH).elf
ISO_IMG         := $(BUILD_DIR)/newos-$(ARCH).iso
DISK_IMG        := $(BUILD_DIR)/newos-$(ARCH).img

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
# ---------------------------------------------------------------------------
KERNEL_SRCS_C   := $(wildcard kernel/*.c kernel/core/*.c kernel/init/*.c \
                   kernel/oops/*.c kernel/process/*.c kernel/thread/*.c \
                   kernel/syscall/*.c kernel/elf/*.c) \
                   $(wildcard mm/pmm/*.c mm/vmm/*.c mm/heap/*.c mm/*.c \
                   mm/kmap/*.c mm/user/*.c mm/memory_debug/*.c) \
                   $(wildcard lib/*.c lib/libk/*.c lib/libstring/*.c \
                   lib/libmath/*.c) \
                   $(wildcard drivers/core/*.c drivers/serial/*.c \
                   drivers/tty/*.c drivers/console/*.c \
                   drivers/framebuffer/*.c drivers/pci/*.c \
                   drivers/qemu/*.c) \
                   $(wildcard fs/vfs/*.c fs/*.c fs/devfs/*.c fs/tmpfs/*.c \
                   fs/initramfs/*.c) \
                   $(wildcard ipc/*.c)

ARCH_SRCS_C     := $(wildcard arch/$(ARCH)/*.c arch/$(ARCH)/cpu/*.c \
                    arch/$(ARCH)/gdt/*.c arch/$(ARCH)/idt/*.c \
                    arch/$(ARCH)/pic/*.c arch/$(ARCH)/mm/*.c \
                    arch/$(ARCH)/thread/*.c arch/$(ARCH)/boot/*.c)
ARCH_SRCS_ASM   := $(wildcard arch/$(ARCH)/*.S arch/$(ARCH)/boot/*.S \
                   arch/$(ARCH)/cpu/*.S arch/$(ARCH)/interrupts/*.S \
                   arch/$(ARCH)/thread/*.S)

# ---------------------------------------------------------------------------
# Userland paths (definitions precede the object list, which uses them)
# ---------------------------------------------------------------------------
HELLO_DIR       := userland/programs/hello
HELLO_OBJ       := $(BUILD_DIR)/userland/hello.o
HELLO_BIN       := $(BUILD_DIR)/userland/hello.elf
HELLO_EMBED     := $(BUILD_DIR)/obj/userland/hello.bin.o

OBJS_C          := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(KERNEL_SRCS_C) $(ARCH_SRCS_C))
OBJS_ASM        := $(patsubst %.S,$(BUILD_DIR)/obj/%.o,$(ARCH_SRCS_ASM))
OBJS            := $(OBJS_C) $(OBJS_ASM) $(HELLO_EMBED)

INCLUDE         := -Iinclude -Iinclude/kernel -Ilib -Ilib/libk -I. -Ifs -Imm \
                   -Idrivers -Ikernel -Iarch

# ---------------------------------------------------------------------------
# Userland build + embed
# ---------------------------------------------------------------------------

# Userland is freestanding but has access to the full instruction set
# (no -mno-red-zone, the program runs with a normal SysV stack).
USER_CFLAGS     := -c -O2 -ffreestanding -nostdlib -fno-pic -fno-pie \
                   -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables \
                   -fno-exceptions -mgeneral-regs-only -Wall -Wextra -Werror

$(HELLO_OBJ): $(HELLO_DIR)/hello.c
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

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------
.PHONY: all clean qemu qemu-debug qemu-test qemu-network qemu-disk iso \
        check-config dirs

.DEFAULT_GOAL := all

all: $(KERNEL_BIN)

dirs:
	@mkdir -p $(BUILD_DIR)/obj

check-config:
	@test -f $(CONFIG) && echo "Using config: $(CONFIG)" || { \
	  echo "ERROR: config $(CONFIG) not found"; exit 1; }

$(BUILD_DIR)/obj/%.o: %.c | dirs
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) $(INCLUDE) -o $@ $<

$(BUILD_DIR)/obj/%.o: %.S | dirs
	@mkdir -p $(dir $@)
	@echo "  AS  $<"
	@$(NASM) $(ASFLAGS) -o $@ $<

$(KERNEL_BIN): $(OBJS) | check-config dirs
	@mkdir -p $(dir $@)
	@echo "  LD  $(KERNEL_BIN)"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "  SIZE"
	@size $@
	@echo "=== Build complete: $(KERNEL_BIN) ==="

iso: $(KERNEL_BIN)
	@mkdir -p $(BUILD_DIR)/iso/boot/grub
	@cp $(KERNEL_BIN) $(BUILD_DIR)/iso/boot/newos.elf
	@printf 'set timeout=0\nset default=0\nserial --unit=0 --speed=115200 --stop=1\nterminal_input serial\nterminal_output serial\nmenuentry "NEWOS" {\n  multiboot2 /boot/newos.elf\n  boot\n}\nmenuentry "NEWOS (test mode)" {\n  multiboot2 /boot/newos.elf test_mode=1\n  boot\n}\n' \
	  > $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@grub-mkrescue -o $(ISO_IMG) $(BUILD_DIR)/iso 2>/dev/null || \
	  xorriso -as mkisofs -b boot/grub/eltorito.img -no-emul-boot \
	  -boot-load-size 4 -boot-info-table --grub2-boot-info \
	  --grub2-mbr /usr/lib/grub/i386-pc/boot.img \
	  -o $(ISO_IMG) $(BUILD_DIR)/iso
	@echo "ISO created: $(ISO_IMG)"

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
	  -device isa-debug-exit,iobase=0xf4 -append "test_mode=1"; \
	  echo "QEMU exit status: $$?"

qemu-network: all
	@echo "Booting NEWOS with user-mode networking ..."
	@$(QEMU) -kernel $(KERNEL_BIN) -serial stdio -no-reboot -m 128M \
	  -netdev user,id=net0 -device virtio-net-pci,netdev=net0

qemu-disk: $(DISK_IMG)
	@echo "Booting NEWOS from disk image ..."
	@$(QEMU) -drive file=$(DISK_IMG),format=raw,if=ide -serial stdio -no-reboot -m 128M

$(DISK_IMG): all
	@echo "Creating disk image ..."
	@dd if=/dev/zero of=$(DISK_IMG) bs=1M count=64 2>/dev/null

clean:
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned."

-include $(OBJS:.o=.d)
