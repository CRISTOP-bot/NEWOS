#include "../include/time.h"
#include "../../abi/syscall_abi.h"
#include "syscall.h"

struct sysinfo_min {
    unsigned long total_frames;
    unsigned long free_frames;
    unsigned long uptime_sec;
    unsigned long hz;
};

time_t time(time_t *t)
{
    struct sysinfo_min si;
    if (libc_sys1(SYS_SYSINFO, (long)&si) != 0)
        return (time_t)-1;
    if (t)
        *t = (time_t)si.uptime_sec;
    return (time_t)si.uptime_sec;
}
