#ifndef KERNEL_LIMINE_H
#define KERNEL_LIMINE_H

#include <stdint.h>

/* Limine boot protocol (boot ABI) definitions, matching the Limine boot
 * protocol as specified at
 * https://github.com/Limine-Bootloader/limine-protocol.
 *
 * Kernel-side requests live between the start/end markers and are found by
 * the bootloader by scanning the loaded executable image for their 4-qword
 * IDs. Base revision 3 is requested via the LIMINE_BASE_REVISION tag so the
 * bootloader honors the request delimiters and provides base-revision 3
 * guarantees (e.g. 4KiB-aligned usable memory map entries).
 */

#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88ull, 0x0a82e883a194f07bull

#define LIMINE_REQUESTS_START_MARKER \
    uint64_t limine_requests_start_marker[4] = { 0xf6b8f4b39de7d1ae, \
        0xfab91a6940fcb9cf, 0x785c6ed015d3e316, 0x181e920a7852b9d9 }
#define LIMINE_REQUESTS_END_MARKER \
    uint64_t limine_requests_end_marker[2] = { 0xadc0e0531bb10d03, \
        0x9572709f31764c62 }

#define LIMINE_BASE_REVISION(N) \
    uint64_t limine_base_revision[3] = { 0xf9562b2d5c95a6c8, \
        0x6a7b384944536bdc, (N) }

#define LIMINE_BOOTLOADER_INFO_REQUEST \
    { LIMINE_COMMON_MAGIC, 0xf55038d8e2a1202f, 0x279426fcf5f59740 }
#define LIMINE_FIRMWARE_TYPE_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x8c2f75d90bef28a8, 0x7045a4688eac00c3 }
#define LIMINE_HHDM_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852, 0x63984e959a98244b }
#define LIMINE_MEMMAP_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }
#define LIMINE_EXECUTABLE_ADDRESS_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x71ba76863cc55f63, 0xb2644a48c516a487 }
#define LIMINE_ENTRY_POINT_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x13d86c035a1cd3e1, 0x2b0caa89d8f3026a }
#define LIMINE_EXECUTABLE_CMDLINE_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x4b161536e598651e, 0xb390ad4a2f1f303a }

/* Boot loader info */
struct limine_bootloader_info_response {
    uint64_t revision;
    char *name;
    char *version;
};

struct limine_bootloader_info_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_bootloader_info_response *response;
};

/* Firmware type */
#define LIMINE_FIRMWARE_TYPE_X86BIOS 0
#define LIMINE_FIRMWARE_TYPE_UEFI32  1
#define LIMINE_FIRMWARE_TYPE_UEFI64  2
#define LIMINE_FIRMWARE_TYPE_SBI     3

struct limine_firmware_type_response {
    uint64_t revision;
    uint64_t firmware_type;
};

struct limine_firmware_type_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_firmware_type_response *response;
};

/* Higher half direct map (HHDM) */
struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

/* Memory map */
#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6
#define LIMINE_MEMMAP_FRAMEBUFFER            7

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

/* Executable address (uniform virtual<->physical offset) */
struct limine_executable_address_response {
    uint64_t revision;
    uint64_t physical_base;
    uint64_t virtual_base;
};

struct limine_executable_address_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_executable_address_response *response;
};

/* Entry point override (Limine enters the ELF entry unless overridden) */
typedef void (*limine_entry_point_fn)(void);

struct limine_entry_point_response {
    uint64_t revision;
};

struct limine_entry_point_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_entry_point_response *response;
    limine_entry_point_fn entry;
};

/* Executable command line (kernel command line for this boot) */
struct limine_executable_cmdline_response {
    uint64_t revision;
    char *cmdline;
};

struct limine_executable_cmdline_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_executable_cmdline_response *response;
};

/* Framebuffer (values match the Limine boot protocol specification,
 * https://github.com/Limine-Bootloader/limine-protocol).
 *
 * The bootloader switches video to a graphics mode and reports every
 * available framebuffer; the kernel uses the first one. Only
 * memory_model == LIMINE_FRAMEBUFFER_RGB is handled. */
#define LIMINE_FRAMEBUFFER_REQUEST \
    { LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75, 0xa3148604f6fab11b }

#define LIMINE_FRAMEBUFFER_RGB 1

struct limine_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
};

#endif