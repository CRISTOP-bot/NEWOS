; ============================================================================
; NEWOS x86_64 interrupt stubs
;
; Each CPU exception / IRQ vector gets a tiny stub that pushes an error
; code (zero for vectors without one) and the vector number, then falls into
; the common handler which saves all GPRs and calls isr_handler() in C.
;
; Stack layout at isr_handler (lowest address first):
;   u64 rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13,
;   r14, r15, vec, err, rip, cs, rflags, [rsp, ss]
; ============================================================================

[EXTERN isr_handler]

section .text

%macro ISR_COMMON 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rbp, rsp               ; save frame base (callee-saved)
    mov rdi, rsp               ; arg0: pointer to iframe
    call isr_handler
    ; The syscall body runs with IF set (see isr_handler), so a tick can
    ; land in the pops / thread-switch below and push its own frame over
    ; the half-popped target state, corrupting the resume. Close the
    ; window before touching the stacks; iretq restores the frame's own
    ; IF (set for user frames) at the end.
    cli
    mov rsp, rbp

    ; TLS: install the FS base of the thread we are about to resume, but
    ; only when the target frame lives in ring 3 (kernel frames keep the
    ; kernel's own FS base of 0). GPRs are still on the stack here, so
    ; rax/rcx/rdx are free scratch.
    cmp   qword [g_sched_next_sp], 0
    jne   .fs_from_next
    mov   rax, [g_user_fsbase]
    jmp   .fs_check_ring
.fs_from_next:
    mov   rax, [g_sched_next_fsbase]
.fs_check_ring:
    test qword [rbp + 18*8], 3         ; iframe.cs offset: ring bits
    jz    .fs_skip
    push rcx
    push rdx
    mov   rcx, 0C0000100h              ; IA32_FS_BASE
    mov   rdx, rax
    shr   rdx, 32
    wrmsr                              ; rdx:rax <- MSR_FS_BASE
    pop   rdx
    pop   rcx
.fs_skip:

    ; x87/SSE: the register bank belongs to the thread being left behind.
    ; When a handoff is published the whole 512-byte image must move, or the
    ; target resumes on the outgoing thread's xmm/MXCSR and the outgoing
    ; thread later comes back to someone else's numbers. g_user_fpu_area is
    ; the image that currently owns the registers (0 = no user context owns
    ; them: kernel boot, or a process that was freed while it owned them, in
    ; which case there is nothing worth saving). Kernel code is built
    ; -mno-sse -mgeneral-regs-only, so the registers still hold exactly the
    ; outgoing thread's state at this point.
    cmp   qword [g_sched_next_sp], 0
    je    .fpu_skip
    mov   rax, [g_user_fpu_area]
    test  rax, rax
    jz    .fpu_load
    fxsave [rax]
.fpu_load:
    mov   rax, [g_sched_next_fpu_area]
    test  rax, rax
    jz    .fpu_skip
    fxrstor [rax]
    mov   [g_user_fpu_area], rax
.fpu_skip:

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Optional scheduler handoff. When g_sched_next_sp is non-zero the CPU
    ; must resume ANOTHER thread (timer preemption, or a process exit):
    ; relocate RSP onto that thread's saved interrupt frame and pop its
    ; GPRs from its own frame -- the interrupted frame stays untouched and
    ; valid for the next time the current thread is switched back to.
    cmp   qword [g_sched_next_sp], 0
    jz    .no_sched_switch
    mov   rbx, [g_sched_next_sp]   ; rbx = &target_frame.rip
    mov   qword [g_sched_next_sp], 0
    lea   rsp, [rbx - 0x88]        ; point at the target GPR block (r15)
    pop   r15
    pop   r14
    pop   r13
    pop   r12
    pop   r11
    pop   r10
    pop   r9
    pop   r8
    pop   rbp
    pop   rdi
    pop   rsi
    pop   rdx
    pop   rcx
    pop   rbx
    pop   rax
.no_sched_switch:
    add rsp, 16                ; skip vec + err
    iretq
%endmacro

%macro ISR_DEF 1-2 0
[GLOBAL isr%1]
isr%1:
%if %2 == 0
    push qword 0               ; dummy error code
%endif
    push qword %1              ; vector number
    jmp isr_common
%endmacro

; --- Vectors 0 .. 31 ------------------------------------------------------
ISR_DEF 0
ISR_DEF 1
ISR_DEF 2
ISR_DEF 3
ISR_DEF 4
ISR_DEF 5
ISR_DEF 6
ISR_DEF 7
ISR_DEF 8, 1                  ; double fault (error code)
ISR_DEF 9
ISR_DEF 10, 1                 ; invalid TSS
ISR_DEF 11, 1                 ; segment not present
ISR_DEF 12, 1                 ; stack fault
ISR_DEF 13, 1                 ; general protection
ISR_DEF 14, 1                 ; page fault
ISR_DEF 15
ISR_DEF 16
ISR_DEF 17, 1                 ; alignment check
ISR_DEF 18
ISR_DEF 19
ISR_DEF 20
ISR_DEF 21, 1                 ; control protection
ISR_DEF 22
ISR_DEF 23
ISR_DEF 24
ISR_DEF 25
ISR_DEF 26
ISR_DEF 27
ISR_DEF 28
ISR_DEF 29, 1                 ; #VC (AMD SVM)
ISR_DEF 30, 1                 ; security exception
ISR_DEF 31

; --- IRQs and software vectors 32 .. 255 ----------------------------------
%assign i 32
%rep 224
ISR_DEF i
%assign i i+1
%endrep

isr_common:
    ISR_COMMON

; --- Per-vector stub address table (0 .. 255) -------------------------------
; The C IDT installer gates every vector to its own stub, so a vector's
; number in the pushed frame always matches the gate that delivered it.
; Every IDT entry is routed to the callback whose stub encodes that vector.

section .data
[GLOBAL isr_stubs_all]
isr_stubs_all:
%assign i 0
%rep 256
    dq isr%+i
%assign i i+1
%endrep

; Handoff slot consumed by isr_common's epilogue (defined here so the asm
; path owns the storage; the scheduler just writes the next thread's
; entry_sp value into it).
[GLOBAL g_sched_next_sp]
g_sched_next_sp:
    dq 0

; TLS FS bases consumed by the epilogue: g_user_fsbase is the base of the
; thread the kernel is currently servicing (kept in sync by
; SYS_ARCH_PRCTL and the scheduler), g_sched_next_fsbase the base of the
; handoff target.
[GLOBAL g_user_fsbase]
g_user_fsbase:
    dq 0
[GLOBAL g_sched_next_fsbase]
g_sched_next_fsbase:
    dq 0

; x87/SSE images used by the handoff above: g_user_fpu_area is the image the
; register bank currently belongs to, g_sched_next_fpu_area the image of the
; pending handoff target (both are addresses of struct thread::fpu, so they
; live in the direct map and stay reachable under every CR3).
[GLOBAL g_user_fpu_area]
g_user_fpu_area:
    dq 0
[GLOBAL g_sched_next_fpu_area]
g_sched_next_fpu_area:
    dq 0

section .text
; void arch_set_fsbase(u64 base) - immediate MSR write for the running
; thread (the epilogue applies the stored value on every ring-3 return).
[GLOBAL arch_set_fsbase]
arch_set_fsbase:
    push rcx
    mov  rcx, 0C0000100h
    mov  rax, rdi
    mov  rdx, rax
    shr  rdx, 32
    wrmsr
    pop  rcx
    ret