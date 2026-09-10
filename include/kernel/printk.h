#ifndef KERNEL_PRINTK_H
#define KERNEL_PRINTK_H

#include <kernel/types.h>

/* Kernel log levels; bit 0..3 severity, used by the klog ring buffer. */
#define KLOG_EMERG   0
#define KLOG_ALERT   1
#define KLOG_CRIT    2
#define KLOG_ERR     3
#define KLOG_WARNING 4
#define KLOG_NOTICE  5
#define KLOG_INFO    6
#define KLOG_DEBUG   7

void printk(const char *fmt, ...);

void printk_lvl(int level, const char *fmt, ...);

void vprintk(const char *fmt, __builtin_va_list args);

/* Called very early by arch_main to wire serial + console backends. */
void printk_init(void);

#define pr_emerg(...)   printk_lvl(KLOG_EMERG,   __VA_ARGS__)
#define pr_alert(...)   printk_lvl(KLOG_ALERT,   __VA_ARGS__)
#define pr_crit(...)    printk_lvl(KLOG_CRIT,    __VA_ARGS__)
#define pr_err(...)     printk_lvl(KLOG_ERR,     __VA_ARGS__)
#define pr_warn(...)    printk_lvl(KLOG_WARNING, __VA_ARGS__)
#define pr_notice(...)  printk_lvl(KLOG_NOTICE,  __VA_ARGS__)
#define pr_info(...)    printk_lvl(KLOG_INFO,    __VA_ARGS__)
#define pr_debug(...)   printk_lvl(KLOG_DEBUG,   __VA_ARGS__)

#endif