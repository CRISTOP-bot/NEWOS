#include <x86_irq.h>
#include <core/core_types.h>
#include <core/core_panic.h>
#include <x86_pic.h>

/* Generic 8259 IRQ dispatch.
 *
 * Vectors X86_IRQ_BASE .. X86_IRQ_BASE+15 carry the legacy PIC interrupts
 * (remapped in x64_pic_init). Drivers register a callback per IRQ here; a
 * vector with no handler still gets an EOI so an unexpected line can never
 * wedge the PIC. Single CPU, single pair of picos: no locking required.
 */

static struct {
    x86_irq_handler_t fn;
    void *arg;
} g_irqs[X86_IRQ_COUNT];

void x86_irq_register(int irq, x86_irq_handler_t fn, void *arg)
{
    if (irq < 0 || irq >= X86_IRQ_COUNT || !fn) {
        panic("x86_irq_register: bad irq");
        return;
    }
    g_irqs[irq].fn = fn;
    g_irqs[irq].arg = arg;
}

void x86_irq_dispatch(int vec)
{
    int irq = vec - X86_IRQ_BASE;

    if (irq < 0 || irq >= X86_IRQ_COUNT)
        return;

    if (g_irqs[irq].fn)
        g_irqs[irq].fn(g_irqs[irq].arg);

    x64_pic_send_eoi(irq);
}