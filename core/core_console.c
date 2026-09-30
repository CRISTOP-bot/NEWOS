#include <core/core_console.h>
#include <core/core_types.h>
#include <drivers/serial_16550.h>
#include <x86_cpu.h>

/* Console input is the COM1 receiver: bytes arrive through the
 * interrupt-driven ring, but the read path also probes the UART directly.
 * The 16550 only asserts an RX interrupt once data has actually seated in
 * the FIFO, and some emulations gate backend injection on the guest reading
 * RBR; polling RBR here guarantees input flows even when the ring is idle.
 * The ring and the direct probe share a single consumer (sys_read); the
 * interrupt handler owns the ring write side, so the direct read is kept
 * atomic with it by disabling interrupts. */

/* Second producer: the PS/2 keyboard. Keystrokes typed in the graphical
 * (VGA) window land here; the consumer below merges both queues so the
 * shell cannot tell serial pastes apart from local typing. */
#define KBD_RING_SIZE 256
static volatile u8 g_kbd_ring[KBD_RING_SIZE];
static volatile u32 g_kbd_head;
static volatile u32 g_kbd_tail;

void core_console_kbd_push(u8 c)
{
    u32 next = (g_kbd_head + 1) & (KBD_RING_SIZE - 1);
    if (next != g_kbd_tail) {
        g_kbd_ring[g_kbd_head] = c;
        g_kbd_head = next;
    }
}

static int kbd_available(void)
{
    return (int)((g_kbd_head - g_kbd_tail) & (KBD_RING_SIZE - 1));
}

static int kbd_pop(void)
{
    if (g_kbd_tail == g_kbd_head)
        return -1;
    u8 c = g_kbd_ring[g_kbd_tail];
    g_kbd_tail = (g_kbd_tail + 1) & (KBD_RING_SIZE - 1);
    return c;
}

static int kbd_peek(void)
{
    if (g_kbd_tail == g_kbd_head)
        return -1;
    return g_kbd_ring[g_kbd_tail];
}

int core_console_rx_available(void)
{
    int n;
    cpu_cli();
    n = (serial_rx_available(SERIAL_COM1) > 0 || kbd_available() > 0) ? 1 : 0;
    if (!n)
        n = serial_rx_ready(SERIAL_COM1) ? 1 : 0;
    cpu_sti();
    return n;
}

int core_console_rx_pop(void)
{
    int c;

    cpu_cli();
    c = serial_rx_pop(SERIAL_COM1);
    if (c < 0)
        c = serial_getc(SERIAL_COM1);   /* nothing queued: probe the UART */
    if (c < 0)
        c = kbd_pop();                  /* fall back to local keystrokes */
    cpu_sti();
    return c;
}

int core_console_rx_peek(void)
{
    int c;

    cpu_cli();
    c = serial_rx_peek(SERIAL_COM1);
    if (c < 0)
        c = kbd_peek();
    cpu_sti();
    return c;
}
