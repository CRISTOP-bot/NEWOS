#include <kernel/qemu_debug.h>
#include <arch/x86_64/io.h>

/* QEMU's isa-debug-exit device: writing a byte to port 0xf4 makes QEMU
 * terminate with exit status (code << 1) | 1. The automated test harness
 * (make qemu-test / CI) uses this to observe a machine-checkable result:
 * code 0 -> QEMU exits 1 (tests passed), code != 0 -> QEMU exits >= 3. */

#define QEMU_DEBUG_EXIT_PORT 0xf4

void qemu_debug_exit(int code)
{
    outb(QEMU_DEBUG_EXIT_PORT, (u8)code);
    for (;;)
        __asm__ volatile("hlt");
}