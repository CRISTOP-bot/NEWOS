#ifndef CORE_CONSOLE_H
#define CORE_CONSOLE_H

/* Kernel console services. The console is fed by the serial RX interrupt;
 * higher layers (SYS_READ, the shell) consume single bytes without pulling
 * in a device driver. */

int core_console_rx_available(void);
int core_console_rx_pop(void);          /* -1 when empty */

#endif