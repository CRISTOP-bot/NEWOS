#ifndef CORE_TIME_H
#define CORE_TIME_H

#include <core/core_types.h>

/* Kernel time services. Wrapped in core so higher layers (syscall, ...) can
 * use the PIT tick without depending on a device driver. */

u64 core_time_ticks(void);
u64 core_time_hz(void);
void core_time_delay_us(u64 us);

#endif