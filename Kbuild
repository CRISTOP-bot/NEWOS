# Kbuild: NEWOS kernel build description
#
# This file drives the per-directory object lists consumed by the top-level
# build. Each subsystem lists the object files it contributes. Keep one file
# per responsibility (see docs/architecture/coding_style).

# --- arch/x86_64 -----------------------------------------------------------
obj-$(CONFIG_ARCH_X86_64) += arch/x86_64/boot/entry.o
obj-$(CONFIG_ARCH_X86_64) += arch/x86_64/boot/init.o
obj-$(CONFIG_ARCH_X86_64) += arch/x86_64/cpu/gdt.o
obj-$(CONFIG_ARCH_X86_64) += arch/x86_64/cpu/cpuid.o
obj-$(CONFIG_ARCH_X86_64) += arch/x86_64/gdt/gdt.c
obj-$(CONFIG_ARCH_X86_64) += arch/x86_64/idt/idt.c
obj-$(CONFIG_ARCH_X86_64) += arch/x86_64/mm/paging.c
obj-$(CONFIG_ARCH_X86_64) += arch/x86_64/io/port.c

# --- kernel core -----------------------------------------------------------
obj-y += kernel/core/printk.o
obj-y += kernel/core/panic.o
obj-y += kernel/init/init.o
obj-y += kernel/oops/oops.o

# --- memory ----------------------------------------------------------------
obj-$(CONFIG_PMM_BITMAP) += mm/pmm/bitmap.o
obj-$(CONFIG_PMM_BUDDY)  += mm/pmm/buddy.o
obj-$(CONFIG_PMM_BUDDY)  += mm/pmm/allocator.o
obj-$(CONFIG_VMM)        += mm/vmm/address_space.o
obj-$(CONFIG_KMALLOC)    += mm/heap/kmalloc.o
obj-y += mm/heap/init.o
obj-$(CONFIG_MEMORY_DEBUG) += mm/memory_debug/debug.o

# --- filesystems -----------------------------------------------------------
obj-$(CONFIG_VFS)       += fs/vfs/vfs.o
obj-$(CONFIG_VFS)       += fs/vfs/inode.o
obj-$(CONFIG_TMPFS)     += fs/tmpfs/tmpfs.o
obj-$(CONFIG_DEVFS)     += fs/devfs/devfs.o

# --- drivers ---------------------------------------------------------------
obj-$(CONFIG_SERIAL_CONSOLE) += drivers/serial/serial.o
obj-$(CONFIG_DRIVER_MODEL)   += drivers/core/driver.o
obj-$(CONFIG_DRIVER_MODEL)   += drivers/core/device.o

# --- kernel libraries ------------------------------------------------------
obj-y += lib/libk/string.o
obj-y += lib/libk/memory.o
obj-y += lib/libk/bitmap.o
obj-y += lib/libk/ringbuf.o
obj-y += lib/libk/list.o
obj-y += lib/libk/lock.o
obj-y += lib/libk/format.o