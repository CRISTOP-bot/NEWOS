#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <core/core_types.h>

void panic(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)))
    __attribute__((noreturn));

_Noreturn void halt(void);

#endif