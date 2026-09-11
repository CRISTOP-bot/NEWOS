#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <core/core_types.h>
#include <mm/mm_vmm.h>

/* Minimal ELF64 loading. The loader validates a full in-memory image and
 * maps its PT_LOAD segments into a (not-yet-active) user address space. */

#define ELF_MAGIC      0x464C457Fu       /* "\177ELF" little-endian */
#define ELFCLASS64     2
#define ELFDATA2LSB    1
#define ET_EXEC        2
#define EM_X86_64      62
#define EV_CURRENT     1

#define PT_LOAD        1

#define PF_X           0x1
#define PF_W           0x2
#define PF_R           0x4

struct elf64_ehdr {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
};

struct elf64_phdr {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
};

struct elf_image {
    const u8 *data;
    size_t size;
    uintptr_t entry;
    uintptr_t load_start;   /* lowest  mapped user VA (page aligned) */
    uintptr_t load_end;     /* highest mapped user VA + 1 (page aligned) */
    int nsegments;
};

/* Returns 0 if `img` is a loadable x86_64 ET_EXEC. */
int elf_validate(struct elf_image *img);

/* Map every PT_LOAD segment into `as`. Returns 0 on success; a partially
 * loaded address space can be reclaimed with vmm_address_space_destroy(). */
int elf_load_segments(struct elf_image *img, struct vmm_address_space *as);

/* Check that e_entry is mapped, user-accessible and executable. */
int elf_entry_valid(struct elf_image *img, struct vmm_address_space *as);

/* Translate PT_LOAD p_flags into VMM mapping flags. */
u32 elf_flags_for(u32 p_flags);

#endif