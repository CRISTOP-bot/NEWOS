#ifndef ARCH_X86_64_IFRAME_H
#define ARCH_X86_64_IFRAME_H

#include <core/core_types.h>

/* Layout of a saved interrupt frame. isr_common pushes GPRs in the order
 * rax .. r15, so rax ends up at the HIGHEST address (pushed first, stack
 * grows down); this struct is declared in reverse push order: r15 at offset
 * 0 .. rax at offset 112, followed by vec, err and the CPU-saved frame:
 *   r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx,
 *   rax, vec, err, rip, cs, rflags, [rsp, ss]
 * rsp/ss are only meaningful for a user -> kernel transition. */
struct x64_iframe {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx,
        rbx, rax;
    u64 vec;
    u64 err;
    u64 rip, cs, rflags;
    u64 rsp, ss;
};

void x64_dump_iframe(struct x64_iframe *f);

#endif