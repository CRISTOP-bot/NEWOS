#ifndef DRIVERS_VGA_CONSOLE_H
#define DRIVERS_VGA_CONSOLE_H

#include <drivers/tty_console.h>

/* VGA 80x25 text-mode console (BIOS/legacy path only; unused and harmless
 * on EFI framebuffers). */

void vga_console_init(void);

/* PS/2 mouse cursor overlay: a highlighted cell tracking the pointer.
 * Coordinates are text cells (0..79, 0..24); buttons select the highlight
 * color as click feedback. Safe to call from IRQ context. */
void vga_mouse_place(int x, int y, int buttons);
void vga_mouse_hide(void);

extern struct console_device g_vga_console;

#endif