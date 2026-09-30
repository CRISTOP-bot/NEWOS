#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <core/core_types.h>
#include <x86_frame.h>

/* System call dispatcher. Runs on the user interrupt frame and returns the
 * value that ends up in RAX for the guest (SYS_EXIT never returns). */
long syscall_dispatch(struct x64_iframe *f);

/* POSIX extension set (mmap/brk/stat/ioctl/dup/fcntl/ids/time/aux stubs),
 * consulted by syscall_dispatch for numbers it does not handle itself.
 * Returns SYSCALL_RET_ERROR for unknown numbers. */
long syscall_posix(struct x64_iframe *f, u64 nr);

/* Per-thread TLS base (x86_64 FS base). arch_set_fsbase() writes the
 * MSR immediately; g_user_fsbase is the value applied by the interrupt
 * epilogue on every return to ring 3, g_sched_next_fsbase the value for
 * a scheduler handoff frame. All defined in x86_interrupt.S. */
void arch_set_fsbase(u64 base);
extern u64 g_user_fsbase;
extern u64 g_sched_next_fsbase;

/* Framebuffer backend (implemented in drivers/graphics/fb.c). Declared
 * here instead of <drivers/fb.h> because the syscall layer may not
 * import driver headers (see scripts/ci/lint_layering.sh). */
int fb_present(void);
void fb_geometry(u64 *w, u64 *h, u64 *pitch, u32 *bpp);
long fb_blit_rows(u64 x, u64 y, u64 w, u64 h, const u32 *rows);

/* PS/2 mouse backend (drivers/input/ps2_mouse.c), same layering reason. */
void ps2_mouse_get(u64 *seq, int *x, int *y, int *buttons, int *wheel);

#endif