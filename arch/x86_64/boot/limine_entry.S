; ============================================================================
; NEWOS x86_64 Limine boot entry
;
; Limine enters the executable in 64-bit long mode with paging already
; enabled and the kernel image mapped at its (high-half) virtual addresses.
; Unlike the multiboot2/PVH path there is no trampoline to run: we only need
; a stack before handing control to the C entry point.
;
; The C code (limine.c) reads the Limine protocol responses while Limine's
; page tables are active, installs the kernel's own page tables (identity +
; direct physical map + high-half kernel) into CR3, and then boots the rest
; of the kernel exactly like the native path.
; ============================================================================

[BITS 64]

section .limine_entry

[GLOBAL _limine_entry]
[EXTERN limine_arch_main]

_limine_entry:
    cli
    cld

    ; Limine provides a flat 64-bit environment; set up our own stack from
    ; the kernel .bss before calling C.
    lea rsp, [rel limine_stack_top]
    mov rbp, 0

    call limine_arch_main

    ; limine_arch_main must not return.
    cli
.hang:
    hlt
    jmp .hang

section .bss nobits
align 16
limine_stack_bottom:
    resb 16384
limine_stack_top: