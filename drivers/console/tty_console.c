#include <drivers/tty_console.h>
#include <drivers/serial_16550.h>

#define MAX_CONSOLE_DEVICES 8

static struct console_device *g_consoles[MAX_CONSOLE_DEVICES];
static int g_console_count = 0;

void console_init(void)
{
    g_console_count = 0;
}

void console_register(struct console_device *dev)
{
    if (g_console_count < MAX_CONSOLE_DEVICES)
        g_consoles[g_console_count++] = dev;
}

void console_write_char(char c)
{
    for (int i = 0; i < g_console_count; i++)
        g_consoles[i]->write(g_consoles[i], c);
}

void console_write(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        console_write_char(buf[i]);
}

void console_write_str(const char *s)
{
    while (*s)
        console_write_char(*s++);
}

/* Built-in serial console backend. */
static void serial_console_write(struct console_device *dev, char c)
{
    (void)dev;
    serial_putc(SERIAL_COM1, c);
}

struct console_device g_serial_console = {
    .name = "serial0",
    .write = serial_console_write,
};