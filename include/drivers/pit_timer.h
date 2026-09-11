#ifndef DRIVERS_PIT_TIMER_H
#define DRIVERS_PIT_TIMER_H

#include <core/core_types.h>

#define PIT_DEFAULT_HZ 1000

void pit_init(u32 hz);
u64  pit_ticks(void);
u64  pit_hz(void);
void time_delay_us(u64 us);

#endif