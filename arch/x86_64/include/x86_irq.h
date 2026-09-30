#ifndef ARCH_X86_64_IRQ_H
#define ARCH_X86_64_IRQ_H

#include <core/core_types.h>

#define X86_IRQ_BASE 0x20 /* 8259 remap base */
#define X86_IRQ_COUNT 16
#define X86_IRQ_PIT 0
#define X86_IRQ_KEYBOARD 1
#define X86_IRQ_SERIAL2 3
#define X86_IRQ_SERIAL1 4
#define X86_IRQ_MOUSE 12
#define X86_IRQ_NET 11

typedef void (*x86_irq_handler_t)(void *arg);

void x86_irq_register(int irq, x86_irq_handler_t fn, void *arg);
void x86_irq_dispatch(int vec);

#endif