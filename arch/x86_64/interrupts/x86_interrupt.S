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
    mov rsp, rbp

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