#ifndef DRIVERS_CMOS_RTC_H
#define DRIVERS_CMOS_RTC_H

#include <core/core_types.h>

/* PC CMOS real-time clock (ports 0x70/0x71): the only wall-clock source
 * on stock QEMU/PC hardware. Year is returned full (20xx assumed: the
 * RTC stores 2 digits and QEMU follows the PC convention). */

void cmos_rtc_init(void);
int cmos_rtc_get(u16 *year, u8 *mon, u8 *day,
                 u8 *hour, u8 *min, u8 *sec);   /* 0 on success */

#endif
