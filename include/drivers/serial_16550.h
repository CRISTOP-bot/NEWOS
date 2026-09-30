#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <core/core_types.h>

#define SERIAL_COM1 0x3F8
#define SERIAL_COM2 0x2F8
#define SERIAL_COM3 0x3E8
#define SERIAL_COM4 0x2E8

/* Standard 16550 UART registers (offsets from base) */
#define UART_REG_RBR      0  /* receiver buffer  (read)       */
#define UART_REG_THR      0  /* transmitter hold (write)      */
#define UART_REG_IER      1  /* interrupt enable             */
#define UART_REG_FCR      2  /* FIFO control                 */
#define UART_REG_LCR      3  /* line control                 */
#define UART_REG_MCR      4  /* modem control                */
#define UART_REG_LSR      5  /* line status                  */
#define UART_REG_MSR      6  /* modem status                 */
#define UART_REG_DLL      0  /* divisor latch low  (DLAB=1)  */
#define UART_REG_DLM      1  /* divisor latch high (DLAB=1)  */

#define UART_LSR_DR       0x01 /* data ready       */
#define UART_LSR_THRE     0x20 /* THR empty        */

void serial_init(u16 port);
void serial_putc(u16 port, char c);
int  serial_getc(u16 port);
void serial_write(u16 port, const char *buf, size_t len);
int  serial_rx_ready(u16 port);

/* Interrupt-driven receive path (COM1). Only enabled once the IDT/IRQ
 * machinery is up; bytes are buffered in an internal ring. */
void serial_rx_irq_enable(u16 port);
int  serial_rx_available(u16 port);
int  serial_rx_pop(u16 port);
int  serial_rx_peek(u16 port);          /* -1 when ring empty */

/* Default console port used by printk. */
void early_console_init(void);
void early_console_putc(char c);

#endif