#ifndef DRIVERS_CONSOLE_H
#define DRIVERS_CONSOLE_H

#include <kernel/types.h>

/* Console output abstraction. The kernel console multiplexes to any
 * registered backends (serial, VGA text, framebuffer). */

struct console_device {
    const char *name;
    void (*write)(struct console_device *dev, char c);
    /* optional full-buffer writer */
    size_t (*write_block)(struct console_device *dev, const char *buf,
                          size_t len);
};

void console_init(void);
void console_register(struct console_device *dev);
void console_write_char(char c);
void console_write(const char *buf, size_t len);
void console_write_str(const char *s);

/* Backend registered by printk_init(); drives COM1. */
extern struct console_device g_serial_console;

#endif