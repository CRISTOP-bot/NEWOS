#ifndef DRIVERS_PS2_H
#define DRIVERS_PS2_H

#include <core/core_types.h>

/* i8042 PS/2 controller: keyboard (port 1, IRQ1) + auxiliary mouse
 * (port 2, IRQ12). QEMU provides both by default; real hardware too. */

void ps2_keyboard_init(void);
void ps2_mouse_init(void);

/* Pop the oldest pending pixel event (positions in device pixels,
 * wheel = signed delta carried by that packet). Returns 0 when the
 * ring is empty. Producer: IRQ12; consumer: /dev/mouse and SYS_MOUSE_GET
 * readers (single consumer). */
int ps2_mouse_pop_event(int *x, int *y, int *buttons, int *wheel);

/* Latest pixel state + packet sequence number (non-destructive). */
void ps2_mouse_get(u64 *seq, int *x, int *y, int *buttons, int *wheel);

#endif
