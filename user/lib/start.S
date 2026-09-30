; nsh userland entry: the kernel drops to ring 3 with SysV rdi/rsi
; already holding (argc, argv), so _start just forwards into main and
; exits with its return code.

global _start
extern main
extern sys_exit

section .text
_start:
    and rsp, -16
    call main
    mov rdi, rax
    call sys_exit
.hang:
    hlt
    jmp .hang
