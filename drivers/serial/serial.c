#include <drivers/serial/serial.h>
#include <arch/x86_64/io.h>

static int serial_initialized = 0;

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