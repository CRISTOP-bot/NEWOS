#include <core/core_time.h>
#include <drivers/pit_timer.h>

/* Thin wrappers over the 8254 PIT driver. */

u64 core_time_ticks(void)
{
    return pit_ticks();
}

u64 core_time_hz(void)
{
    return pit_hz();
}

void core_time_delay_us(u64 us)
{
    time_delay_us(us);
}