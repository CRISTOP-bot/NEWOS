#include <kernel/elf.h>
#include <kernel/kernel.h>
#include <kernel/pmm.h>
#include <kernel/printk.h>
#include <libk/string.h>

/* ELF64 loader.
 *
 * Static ET_EXEC images only (no shared objects, no dynamic relocations).
 * The kernel scans the program headers, loads each PT_LOAD segment into
 * freshly allocated frames, zero-fills the BSS tail (memsz > filesz) and
 * maps the pages with the permissions derived from p_flags. */

int elf_validate(struct elf_image *img)
{
    if (!img || !img->data || img->size < sizeof(struct elf64_ehdr))
        return -1;

    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)img->data;

    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return -1;
    if (eh->e_ident[4] != ELFCLASS64)
        return -1;
    if (eh->e_ident[5] != ELFDATA2LSB)
        return -1;
    if (eh->e_type != ET_EXEC)
        return -1;
    if (eh->e_machine != EM_X86_64)
        return -1;
    if (eh->e_version != EV_CURRENT)
        return -1;
    if (eh->e_phentsize != sizeof(struct elf64_phdr) || eh->e_phnum == 0)
        return -1;
    if (eh->e_phoff == 0 || eh->e_phoff + (u64)eh->e_phnum * eh->e_phentsize >
        img->size)
        return -1;

    img->entry = eh->e_entry;
    img->load_start = ~0ull;
    img->load_end = 0;
    img->nsegments = 0;
    return 0;
}

u32 elf_flags_for(u32 p_flags)
{
    u32 flags = VMM_PRESENT | VMM_USER;
    if (p_flags & PF_W)
        flags |= VMM_WRITE;
    if (!(p_flags & PF_X))
        flags |= VMM_NX;
    return flags;
}

static int elf_map_segment(struct elf_image *img,
                           const struct elf64_phdr *ph,
                           struct vmm_address_space *as)
{
    uintptr_t p_vaddr = ph->p_vaddr;
    uintptr_t p_filesz = ph->p_filesz;
    uintptr_t p_memsz = ph->p_memsz;

    if (p_filesz > p_memsz || p_memsz > (uintptr_t)USER_SPACE_END)
        return -1;
    if (ph->p_offset + p_filesz > img->size)
        return -1;
    uintptr_t vstart = ALIGN_DOWN(p_vaddr, PAGE_SIZE);
    uintptr_t vend = ALIGN_UP(p_vaddr + p_memsz, PAGE_SIZE);
    if (vend < p_vaddr)                 /* overflow */
        return -1;
    if (vstart < (uintptr_t)USER_SPACE_BASE || vend > (uintptr_t)USER_SPACE_END)
        return -1;

    u32 flags = elf_flags_for(ph->p_flags);

    for (uintptr_t va = vstart; va < vend; va += PAGE_SIZE) {
        u64 frame;
        if (phys_alloc_frame(&frame) != 0)
            return -1;

        u8 *page = (u8 *)phys_to_virt(frame << PAGE_SHIFT);
        memset(page, 0, PAGE_SIZE);

        /* Copy file bytes overlapping this page; the rest stays zero
         * (that is the BSS tail of the segment). */
        uintptr_t src_begin = MAX(va, p_vaddr);
        uintptr_t src_end = MIN(va + PAGE_SIZE, p_vaddr + p_filesz);
        if (src_end > src_begin) {
            uintptr_t in_page = src_begin - va;
            uintptr_t in_file = src_begin - p_vaddr;
            memcpy(page + in_page, img->data + ph->p_offset + in_file,
                   src_end - src_begin);
        }

        if (vmm_map_pages(as, va, frame << PAGE_SHIFT, 1, flags) != 0) {
            phys_free_frame(frame);
            return -1;
        }
        img->nsegments++;
    }

    if (vstart < img->load_start)
        img->load_start = vstart;
    if (vend > img->load_end)
        img->load_end = vend;
    return 0;
}

int elf_load_segments(struct elf_image *img, struct vmm_address_space *as)
{
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)img->data;

    for (u16 i = 0; i < eh->e_phnum; i++) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)
            (img->data + eh->e_phoff + (size_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;
        if (elf_map_segment(img, ph, as) != 0)
            return -1;
    }
    return 0;
}

int elf_entry_valid(struct elf_image *img, struct vmm_address_space *as)
{
    if (img->entry >= (uintptr_t)USER_SPACE_END)
        return -1;

    u32 flags = 0;
    if (!vmm_page_lookup(as, img->entry, NULL, &flags))
        return -1;
    if (!(flags & VMM_PRESENT) || !(flags & VMM_USER))
        return -1;
    if (flags & VMM_NX)
        return -1;                     /* entry must be executable */
    return 0;
}