#include <core/core_time.h>
#include <drivers/pit_timer.h>
#include <drivers/cmos_rtc.h>

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

/* Wall clock through the CMOS RTC (lets the syscall layer stay clear
 * of device drivers per the layering contract). */
int core_time_wallclock(struct nsh_time *t)
{
    u16 year;
    u8 mon, day, hour, min, sec;

    if (!t)
        return -1;
    if (cmos_rtc_get(&year, &mon, &day, &hour, &min, &sec) != 0)
        return -1;
    t->year = (nsh_u16)year;
    t->mon = mon;
    t->day = day;
    t->hour = hour;
    t->min = min;
    t->sec = sec;
    t->_pad = 0;
    return 0;
}