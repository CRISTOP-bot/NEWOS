#ifndef NEWOS_LIBC_TIME_H
#define NEWOS_LIBC_TIME_H

/* Wall/monotonic time via the SYSINFO uptime counter (seconds since
 * boot). Sufficient for relative timeouts; there is no epoch clock
 * in userland yet (CMOS lives behind SYS_GETTIME, different shape). */

typedef long time_t;

/* Seconds since boot, or (time_t)-1 on failure. */
time_t time(time_t *t);

#endif
