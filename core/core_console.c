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

int core_console_rx_available(void)
{
    if (serial_rx_available(SERIAL_COM1) > 0)
        return 1;
    return serial_rx_ready(SERIAL_COM1) ? 1 : 0;
}

int core_console_rx_pop(void)
{
    int c;

    cpu_cli();
    c = serial_rx_pop(SERIAL_COM1);
    if (c < 0)
        c = serial_getc(SERIAL_COM1);   /* nothing queued: probe the UART */
    cpu_sti();
    return c;
}