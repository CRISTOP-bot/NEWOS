; ============================================================================
; NEWOS x86_64 boot trampoline
;
; Boot flow:
;   1. bootloader (QEMU multiboot2 / GRUB) loads kernel at PHYS_BASE and
;      enters here in 32-bit protected mode with:
;          EAX = MULTIBOOT2_MAGIC_CHECK (0x36d76289)
;          EBX = physical address of multiboot2 info structure
;   2. We build early four-level page tables (identity map of the low 1 GiB
;      plus the high-half kernel map), enable PAE + long mode + paging.
;   3. A far jump selects a 64-bit code segment and enters long mode.
;   4. We relocate to the high kernel mapping and call the C entry point
;      arch_main(magic, mbi_phys).
;
; All code in .early_text/.early_data runs at physical (identity) addresses.
; ============================================================================

[BITS 32]

section .multiboot
align 8
mb_header_start:
    dd 0xE85250D6                ; magic
    dd 0                         ; architecture (0 = i386)
    dd mb_header_end - mb_header_start  ; header length
    dd 0x100000000 - (0xE85250D6 + 0 + (mb_header_end - mb_header_start)) ; checksum
    ; terminal tags
    dw 0                         ; type: end
    dw 0                         ; flags
    dd 8                         ; size
mb_header_end:

; PVH ELF note for direct QEMU -kernel boot (QEMU's x86_load_pvh). This
; coexists with the multiboot2 header: multiboot2-capable bootloaders ignore
; it, and QEMU's PVH loader ignores everything but this note. QEMU enters us
; in 32-bit protected mode (paging disabled) with:
;     EAX = PVH_MAGIC (0x336ec578)
;     EBX = physical address of struct hvm_start_info
section .note.Xen alloc note
align 4
    dd 3                       ; namesz ("Xen")
    dd 4                       ; descsz (32-bit physical entry point)
    dd 18                      ; type = XEN_ELFNOTE_PHYS32_ENTRY
    db "Xen", 0
align 4
    dd _start                  ; physical entry point (links to ~PHYS_BASE)

[GLOBAL _start]
[GLOBAL x64_pml4]
[GLOBAL x64_pdpt_low]
[GLOBAL x64_pdpt_high]
[GLOBAL x64_pd]
[GLOBAL x64_pd_kernel]
[GLOBAL x64_pt_kernel]
[GLOBAL x64_pdpt_direct]
[GLOBAL x64_pd_direct]
[EXTERN arch_main]
section .early_text

_start:
    cli
    cld

    ; Preserve the multiboot2 magic and info pointer.
    mov [mbi_magic], eax
    mov [mbi_phys], ebx

    ; Build the early page tables.
    call x64_page_tables_setup

    ; Enable PAE.
    mov eax, cr4
    or eax, (1 << 5)             ; CR4.PAE
    mov cr4, eax

    ; Load PML4 into CR3.
    mov eax, x64_pml4            ; physical (identity) address
    mov cr3, eax

    ; EFER.LME = 1.
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)             ; EFER.LME
    wrmsr

    ; Enable paging -> enters long mode (compatibility).
    mov eax, cr0
    or eax, (1 << 31)            ; CR0.PG
    mov cr0, eax

    ; Load our 64-bit GDT and far-jump into long mode.
    lgdt [gdt_ptr]
    jmp dword 0x08:long_mode_entry

; ============================================================================
; 64-bit long mode entry. Still running at identity-mapped low addresses.
; ============================================================================

[BITS 64]
long_mode_entry:
    ; Reload segment registers with the flat 64-bit data selector.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Set up an early stack (identity mapped, low physical).
    mov rax, early_stack_top
    mov rsp, rax
    mov rbp, 0

    ; Hand physical pointers to the C entry point. The identity map is still
    ; active, so the mbi structure is reachable; the C code converts to the
    ; high-half view via phys_to_virt().
    mov edi, [mbi_magic]
    mov esi, [mbi_phys]

    ; Relocate to the high-half mapping and enter the kernel.
    mov rax, arch_main
    call rax

    ; arch_main must not return.
    cli
.hang:
    hlt
    jmp .hang

; ============================================================================
; Early page table construction
; ============================================================================
; Layout:
;   x64_pml4[0]           -> x64_pdpt_low     (identity, low 1 GiB)
;   x64_pml4[508]         -> x64_pdpt_direct  (direct phys map, 2 MiB pages)
;   x64_pdpt_direct[i]    -> x64_pd_direct[i] (1 GiB covered per PD)
;   x64_pml4[511]         -> x64_pdpt_high    (high-half kernel map)
;   x64_pdpt_low[0]       -> x64_pd           (identity 2 MiB huge pages)
;   x64_pdpt_high[510]    -> x64_pd_kernel    (VA 0xffffffff80000000)
;   x64_pd_kernel[i]      = phys 0x100000 + i * 2 MiB  (2 MiB huge pages)
;   x64_pd[i]             = i * 2 MiB          (2 MiB huge pages, R/W)
;   x64_pdpt_direct[i]    = i * 1 GiB          (via x64_pd_direct[i])
; ============================================================================

; Early high-half window: KERNEL_MAP_PTS page tables of 4 KiB pages, so the
; whole kernel image plus .bss is addressable before x64_paging_init rebuilds
; the tables. 4 KiB rather than 2 MiB huge pages because PHYS_BASE is 1 MiB:
; a huge page ignores its low 21 bits, so aliasing an unaligned base shifts
; the whole window by 1 MiB and every fetch faults.
%define KERNEL_MAP_PTS 32          ; 32 * 2 MiB = 64 MiB of window

[BITS 32]
x64_page_tables_setup:
    pushad

    ; Zero the tables (early bss is zeroed by nasm, but be explicit).
    cld
    mov edi, x64_pml4
    mov ecx, (4096 * 11) / 4
    xor eax, eax
    rep stosd
    mov ecx, (4096 * KERNEL_MAP_PTS) / 4
    mov edi, x64_pt_kernel
    xor eax, eax
    rep stosd

    ; PML4 wiring.
    mov eax, x64_pdpt_low
    or  eax, 0x3                   ; present | writable
    mov [x64_pml4 + 0],        eax ; identity
    mov eax, x64_pdpt_direct
    or  eax, 0x3
    mov [x64_pml4 + 508 * 8],  eax ; direct physical map (VA DIRECT_PHYS_BASE)
    mov eax, x64_pdpt_high
    or  eax, 0x3
    mov [x64_pml4 + 511 * 8],  eax ; high half

    ; PDPT wiring.
    mov eax, x64_pd
    or  eax, 0x3
    mov [x64_pdpt_low + 0],        eax  ; index 0   -> identity (0 .. 1 GiB)
    mov eax, x64_pd_kernel
    or  eax, 0x3
    mov [x64_pdpt_high + 510 * 8], eax  ; index 510 -> VA 0xffffffff80000000

    ; High-half page directory: KERNEL_MAP_PTS page tables aliasing the
    ; kernel's physical base (PHYS_BASE). The kernel's text/data/bss LMAs lie
    ; at PHYS_BASE + off, so this early view maps them 1:1 until
    ; x64_paging_init installs the real tables. The image is not small: the
    ; embedded /bin toolbox and any staged `.new` archive live in .rodata, and
    ; a three-table (6 MiB) window silently triple-faults the boot as soon as
    ; .bss grows past it.
    mov edi, x64_pd_kernel
    mov eax, x64_pt_kernel
    or  eax, 0x3
    mov ecx, KERNEL_MAP_PTS
.fill_kernel_pd:
    mov [edi], eax
    add edi, 8
    add eax, 0x1000               ; next page table is contiguous
    loop .fill_kernel_pd

    mov edi, x64_pt_kernel
    mov ebx, 0x100000             ; PHYS_BASE
    mov ecx, 512 * KERNEL_MAP_PTS
.fill_kernel_pt:
    mov eax, ebx
    or  eax, 0x3                  ; present | writable
    mov [edi], eax
    add edi, 8
    add ebx, 0x1000               ; next 4 KiB page
    loop .fill_kernel_pt

    ; Identity page directory: 512 x 2 MiB huge pages covering 0 .. 1 GiB.
    mov edi, x64_pd
    mov ecx, 512
    xor eax, eax
.fill_pd:
    ; entry = (i << 21) | present | writable | huge
    mov ebx, eax
    shl ebx, 21
    or  ebx, 0x83
    mov [edi], ebx
    add edi, 8
    inc eax
    loop .fill_pd

    ; Direct-map PDPT: 4 entries -> 4 page directories (1 GiB each).
    ; Filled with 2 MiB pages, NOT 1 GiB pages: some hypervisors
    ; (VirtualBox) hide PDPE1GB from CPUID, turning every 1 GiB page
    ; into a reserved-bit #PF on first touch.
    mov edi, x64_pdpt_direct
    mov esi, x64_pd_direct
    mov ecx, 4
.fill_direct_pdpt:
    mov eax, esi
    or  eax, 0x3                  ; present | writable
    mov [edi], eax
    add edi, 8
    add esi, 4096
    loop .fill_direct_pdpt

    ; Fill the 4 direct-map PDs: 2048 x 2 MiB pages covering 0 .. 4 GiB.
    mov edi, x64_pd_direct
    mov ecx, 2048
    xor eax, eax                  ; 2 MiB page index
.fill_direct_pd:
    ; entry = (i << 21) | present | writable | huge
    mov ebx, eax
    shl ebx, 21
    or  ebx, 0x83
    mov [edi], ebx
    add edi, 8
    inc eax
    loop .fill_direct_pd

    popad
    ret

; ============================================================================
; Data
; ============================================================================

section .early_bss nobits alloc noexec write
align 4096
x64_pml4:
    resb 4096
x64_pdpt_low:
    resb 4096
x64_pdpt_high:
    resb 4096
x64_pd:
    resb 4096
x64_pd_kernel:
    resb 4096
x64_pdpt_direct:
    resb 4096
x64_pd_direct:
    resb 16384

; Kernel-image page tables, shared by both boot paths. The Limine path cannot
; use huge pages either: Limine loads the image 4 KiB-aligned, not 2 MiB, and
; a huge page with an unaligned base faults on its reserved bits.
align 4096
x64_pt_kernel:
    resb 4096 * KERNEL_MAP_PTS

align 16
mbi_magic:
    resd 1
mbi_phys:
    resd 1
early_stack_bottom:
    resb 16384
early_stack_top:
    resd 0

section .early_data
align 8
gdt_start:
    dq 0x0000000000000000       ; null
    dq 0x00AF9A0000000000       ; 64-bit kernel code / 0x08
    dq 0x00CF920000000000       ; kernel data          / 0x10
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start