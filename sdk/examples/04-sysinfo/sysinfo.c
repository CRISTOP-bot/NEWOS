/* 04-sysinfo: NEWOS-specific system calls.
 *
 * <newos/nsh.h> exposes the thin libnsh.a wrappers and the ABI records from
 * <newos/syscall_abi.h>. No libc is required for the syscalls themselves;
 * stdio is used here only to print the results.
 */
#include <stdio.h>
#include <newos/nsh.h>

int main(void)
{
    struct nsh_uname u;
    struct nsh_sysinfo si;
    struct nsh_time t;

    if (sys_uname(&u) == 0)
        printf("uname: %s %s %s (%s)\n", u.sysname, u.release, u.version,
               u.machine);
    else
        puts("uname: unavailable");

    if (sys_sysinfo(&si) == 0)
        printf("memory: %llu frames total, %llu free; uptime %llus @ %llu Hz\n",
               si.total_frames, si.free_frames, si.uptime_sec, si.hz);

    if (sys_gettime(&t) == 0)
        printf("RTC: %04u-%02u-%02u %02u:%02u:%02u\n",
               (unsigned)t.year, t.mon, t.day, t.hour, t.min, t.sec);

    printf("pid: %ld\n", sys_getpid());
    return 0;
}
