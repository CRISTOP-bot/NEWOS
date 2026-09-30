#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    static struct nsh_time t;

    (void)argc;
    (void)argv;
    if (sys_gettime(&t) != 0)
        return fail("date", "clock unavailable");
    putu(t.year);
    nputc('-');
    put2(t.mon);
    nputc('-');
    put2(t.day);
    nputc(' ');
    put2(t.hour);
    nputc(':');
    put2(t.min);
    nputc(':');
    put2(t.sec);
    nputc('\n');
    return 0;
}
