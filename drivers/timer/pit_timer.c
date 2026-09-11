#include <drivers/pit_timer.h>
#include <x86_irq.h>
#include <x86_pic.h>
#include <x86_io.h>

/* 8254 PIT driver (channel 0, IRQ0).
 *
 * Programs channel 0 in mode 3 (square wave) at the requested rate and
 * services the tick IRQ. The tick count is the kernel-wide time base used
 * by delays, ARP timeouts and (later) the scheduler's time slice.
 */

#define PIT_BASE 0x40
#define PIT_CMD  0x43
#define PIT_FREQ 1193182UL

#define PIT_SEL0        0x00
#define PIT_ACCESS_LH   0x30
#define PIT_MODE3       0x06
#define PIT_BCD_OFF     0x00

static volatile u64 g_pit_ticks;
static u32 g_pit_hz;

static void pit_irq_handler(void *arg)
{
    (void)arg;
    g_pit_ticks++;
}

static void pit_set_rate(u32 hz)
{
    u32 div;
    u8 cmd;

    if (hz == 0)
        hz = PIT_DEFAULT_HZ;
    if (hz > 100000)
        hz = 100000;
    g_pit_hz = hz;

    div = PIT_FREQ / hz;
    if (div == 0)
        div = 1;

    cmd = (u8)(PIT_SEL0 | PIT_ACCESS_LH | PIT_MODE3 | PIT_BCD_OFF);
    outb(PIT_CMD, cmd);
    outb(PIT_BASE, (u8)(div & 0xFF));
    outb(PIT_BASE, (u8)((div >> 8) & 0xFF));
}

void pit_init(u32 hz)
{
    pit_set_rate(hz);
    g_pit_ticks = 0;
    x86_irq_register(X86_IRQ_PIT, pit_irq_handler, 0);
    x64_pic_set_mask(X86_IRQ_PIT, 0);
}

u64 pit_ticks(void)
{
    return g_pit_ticks;
}

u64 pit_hz(void)
{
    return g_pit_hz;
}

void time_delay_us(u64 us)
{
    u64 start = pit_ticks();
    u64 need = (us * g_pit_hz) / 1000000UL;

    while (pit_ticks() - start < need)
        __asm__ volatile("pause" ::: "memory");
}