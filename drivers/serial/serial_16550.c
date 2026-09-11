#include <drivers/serial_16550.h>
#include <x86_io.h>
#include <x86_irq.h>
#include <x86_pic.h>

static int serial_initialized = 0;

/* Interrupt-driven receive for COM1: bytes pushed by the RX IRQ handler
 * land here and are consumed by the shell/console (SYS_READ). Single
 * consumer, IRQ producer; 512-byte ring is far beyond any paste burst. */
#define SERIAL_RX_RING_SIZE 512
static volatile u8 g_rx_ring[SERIAL_RX_RING_SIZE];
static volatile u32 g_rx_head;
static volatile u32 g_rx_tail;

void serial_init(u16 port)
{
    outb(port + UART_REG_IER, 0x00);              /* disable interrupts     */
    outb(port + UART_REG_LCR, 0x80);              /* DLAB = 1               */
    outb(port + UART_REG_DLL, 0x01);              /* divisor low  = 1       */
    outb(port + UART_REG_DLM, 0x00);              /* divisor high = 0  -> 115200 baud */
    outb(port + UART_REG_LCR, 0x03);              /* 8N1                    */
    outb(port + UART_REG_FCR, 0xC7);              /* enable + clear FIFOs   */
    outb(port + UART_REG_MCR, 0x0B);              /* DTR | RTS | OUT2       */
    serial_initialized = 1;
}

static void serial_rx_irq_handler(void *arg)
{
    u16 port = (u16)(uintptr_t)arg;

    while (serial_rx_ready(port)) {
        int c = serial_getc(port);
        if (c < 0)
            break;
        u32 next = (g_rx_head + 1) & (SERIAL_RX_RING_SIZE - 1);
        if (next != g_rx_tail) {
            g_rx_ring[g_rx_head] = (u8)c;
            g_rx_head = next;
        }
    }
}

void serial_rx_irq_enable(u16 port)
{
    g_rx_head = g_rx_tail = 0;
    x86_irq_register(X86_IRQ_SERIAL1, serial_rx_irq_handler,
                     (void *)(uintptr_t)port);
    outb(port + UART_REG_IER, 0x01);              /* enable RX data ready   */
    x64_pic_set_mask(X86_IRQ_SERIAL1, 0);
}

int serial_rx_available(u16 port)
{
    (void)port;
    return (int)((g_rx_head - g_rx_tail) & (SERIAL_RX_RING_SIZE - 1));
}

int serial_rx_pop(u16 port)
{
    (void)port;
    if (g_rx_tail == g_rx_head)
        return -1;
    u8 c = g_rx_ring[g_rx_tail];
    g_rx_tail = (g_rx_tail + 1) & (SERIAL_RX_RING_SIZE - 1);
    return c;
}

void serial_putc(u16 port, char c)
{
    if (!serial_initialized)
        return;

    if (c == '\n') {
        /* wait for THR empty, then send CR and LF */
        while (!(inb(port + UART_REG_LSR) & UART_LSR_THRE))
            ;
        outb(port + UART_REG_THR, '\r');
    }

    while (!(inb(port + UART_REG_LSR) & UART_LSR_THRE))
        ;
    outb(port + UART_REG_THR, c);
}

static int serial_readable(u16 port)
{
    return (inb(port + UART_REG_LSR) & UART_LSR_DR) != 0;
}

int serial_getc(u16 port)
{
    return serial_readable(port) ? inb(port + UART_REG_RBR) : -1;
}

int serial_rx_ready(u16 port)
{
    return serial_readable(port);
}

void serial_write(u16 port, const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        serial_putc(port, buf[i]);
}

void early_console_init(void)
{
    serial_init(SERIAL_COM1);
}

void early_console_putc(char c)
{
    serial_putc(SERIAL_COM1, c);
}