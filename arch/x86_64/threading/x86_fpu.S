; ============================================================================
; NEWOS x86_64 x87/SSE state primitives
; ----------------------------------------------------------------------------
; FXSAVE/FXRSTOR move the whole 512-byte state image between the register bank
; and memory (layout in x86_fpu.h). The memory operand MUST be 16-byte
; aligned: the CPU raises #GP otherwise. Every area is struct thread's
; aligned(16) `fpu` member, checked at thread setup, so these helpers are only
; ever handed a valid address.
; ============================================================================

section .text

; void x64_fpu_save(void *area) - park the live register bank.
[GLOBAL x64_fpu_save]
x64_fpu_save:
    fxsave [rdi]
    ret

; void x64_fpu_load(const void *area) - make an image the live state.
[GLOBAL x64_fpu_load]
x64_fpu_load:
    fxrstor [rdi]
    ret
