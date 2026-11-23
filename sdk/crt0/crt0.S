/* NEWOS static application entry point (GAS syntax).
 *
 * Functionally identical to user/lib/start.S (the nasm version shipped in the
 * kernel image): the kernel enters ring 3 with rdi/rsi holding argc/argv, so
 * _start aligns the stack, forwards both registers to main(), and exits with
 * main's return code through sys_exit() from libnsh.a.
 *
 * This copy exists so the host SDK can build applications without nasm.
 * If you change the ABI entry, update BOTH files.
 */
    .global _start
    .extern main
    .extern sys_exit
    .section .text
_start:
    andq $-16, %rsp
    call main
    movq %rax, %rdi
    call sys_exit
1:
    hlt
    jmp 1b

    .section .note.GNU-stack,"",@progbits
