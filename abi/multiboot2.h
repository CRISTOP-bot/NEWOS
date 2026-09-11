#ifndef KERNEL_MULTIBOOT2_H
#define KERNEL_MULTIBOOT2_H

#include <stdint.h>

#define MULTIBOOT2_MAGIC_CHECK  0x36d76289ul
#define MULTIBOOT2_HEADER_MAGIC 0xE85250D6ul

struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

/* Tag types */
#define MULTIBOOT2_TAG_END            0
#define MULTIBOOT2_TAG_CMDLINE        1
#define MULTIBOOT2_TAG_BOOTLOADER     2
#define MULTIBOOT2_TAG_MODULE         3
#define MULTIBOOT2_TAG_BASIC_MEMINFO  4
#define MULTIBOOT2_TAG_BOOTDEV        5
#define MULTIBOOT2_TAG_MMAP           6
#define MULTIBOOT2_TAG_VBE            7
#define MULTIBOOT2_TAG_FRAMEBUFFER    8
#define MULTIBOOT2_TAG_ELF_SECTIONS   9
#define MULTIBOOT2_TAG_APM            10
#define MULTIBOOT2_TAG_EFI32          11
#define MULTIBOOT2_TAG_EFI64          12
#define MULTIBOOT2_TAG_SMBIOS         13
#define MULTIBOOT2_TAG_ACPI_NEW       14
#define MULTIBOOT2_TAG_ACPI_OLD       15
#define MULTIBOOT2_TAG_NETWORK        16
#define MULTIBOOT2_TAG_EFI_MMAP       17
#define MULTIBOOT2_TAG_EFI_BS         18
#define MULTIBOOT2_TAG_EFI32_IH       19
#define MULTIBOOT2_TAG_EFI64_IH       20
#define MULTIBOOT2_TAG_LOAD_BASE_ADDR 21

struct multiboot2_mmap {
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed));

struct multiboot2_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

#define MULTIBOOT2_MMAP_RAM     1
#define MULTIBOOT2_MMAP_RESERVED 2
#define MULTIBOOT2_MMAP_ACPI    3
#define MULTIBOOT2_MMAP_NVS     4

#endif