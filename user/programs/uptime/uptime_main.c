#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    static struct nsh_sysinfo si;
    u64 s, m, h, d;

    (void)argc;
    (void)argv;
    if (sys_sysinfo(&si) != 0)
        return fail("uptime", "cannot read system info");
    s = si.uptime_sec;
    d = s / 86400;
    s %= 86400;
    h = s / 3600;
    s %= 3600;
    m = s / 60;
    s %= 60;
    nputs("up ");
    if (d) {
        putu(d);
        nputs(d == 1 ? " day, " : " days, ");
    }
    if (h || d) {
        putu(h);
        nputs(h == 1 ? " hour, " : " hours, ");
    }
    putu(m);
    nputs(m == 1 ? " min" : " mins");
    nputc('\n');
    return 0;
}
