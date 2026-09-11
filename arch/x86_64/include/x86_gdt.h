#ifndef ARCH_X86_64_GDT_H
#define ARCH_X86_64_GDT_H

#include <core/core_types.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

void x64_gdt_init(void);
void x64_gdt_reload(void);

/* Returns the GDT index in the GDT where a TSS slot was reserved. */
int x64_tss_install(u64 rsp0);

/* Update RSP0 on the fly (no ltr needed); used before entering user mode
 * so the first interrupt/exception has a valid kernel stack. */
void x64_tss_set_rsp0(u64 rsp0);

/* Current privileged stack top (mainly for diagnostics). */
u64 x64_current_kernel_stack(void);

#endif