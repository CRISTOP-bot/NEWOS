#ifndef CORE_CONSOLE_H
#define CORE_CONSOLE_H

#include <core/core_types.h>

/* Kernel console services.
 *
 * Input has two producers: the serial RX interrupt (COM1) and the PS/2
 * keyboard interrupt (anything typed in the QEMU/VGA window). Both feed
 * byte queues; higher layers (SYS_READ, the shell) consume single merged
 * bytes without pulling in a device driver. Serial is drained first so a
 * pasted serial burst keeps its ordering ahead of interleaved keystrokes.
 */

int core_console_rx_available(void);
int core_console_rx_pop(void);          /* -1 when empty */
int core_console_rx_peek(void);         /* oldest queued byte, -1 if none */

/* PS/2 keyboard pushes decoded bytes here (IRQ context). */
void core_console_kbd_push(u8 c);

#endif
