#include <core/core_printk.h>
#include <core/core_types.h>
#include <iru_format.h>
#include <drivers/tty_console.h>
#include <drivers/serial_16550.h>

#define KLOG_RING_SIZE 65536

static u8 klog_ring_data[KLOG_RING_SIZE];
static size_t klog_head = 0;
static int klog_ready = 0;

void printk_init(void)
{
    klog_ready = 1;
    klog_head = 0;
    early_console_init();
    console_init();
    console_register(&g_serial_console);
}

static void printk_emit(char c, void *opaque)
{
    (void)opaque;

    console_write_char(c);

    if (klog_ready) {
        klog_ring_data[klog_head] = (u8)c;
        klog_head = (klog_head + 1) % KLOG_RING_SIZE;
    }
}

void vprintk(const char *fmt, __builtin_va_list args)
{
    format_fmt(printk_emit, NULL, fmt, args);
}

void printk_lvl(int level, const char *fmt, ...)
{
    if (level == KLOG_DEBUG)
        return;
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vprintk(fmt, args);
    __builtin_va_end(args);
}

void printk(const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vprintk(fmt, args);
    __builtin_va_end(args);
}