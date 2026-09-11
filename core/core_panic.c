#include <core/core_panic.h>
#include <core/core_printk.h>
#include <x86_cpu.h>

void __noreturn panic(const char *fmt, ...)
{
    cpu_cli();
    printk("\n*** KERNEL PANIC ***\n");

    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vprintk(fmt, args);
    __builtin_va_end(args);

    printk("\n*** System halted.\n");

    for (;;)
        cpu_hlt();
}

void __noreturn halt(void)
{
    cpu_cli();
    for (;;)
        cpu_hlt();
}