#ifndef DRIVERS_VGA_CONSOLE_H
#define DRIVERS_VGA_CONSOLE_H

#include <drivers/tty_console.h>

/* VGA 80x25 text-mode console (BIOS/legacy path only; unused and harmless
 * on EFI framebuffers). */

void vga_console_init(void);

extern struct console_device g_vga_console;

#endif