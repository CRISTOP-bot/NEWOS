#include "../../lib/nshlib.h"

static const char *state_name(long s)
{
    if (s == 1)
        return "RUN";
    if (s == 2)
        return "ZOMB";
    return "NEW";
}

int main(int argc, char **argv)
{
    static struct nsh_proc ps[32];
    long n;

    (void)argc;
    (void)argv;
    n = sys_ps(ps, sizeof(ps));
    if (n < 0)
        return fail("ps", "cannot list processes");
    nputs("PID   PPID  STATE NAME\n");
    for (long i = 0; i < n; i++)
        putf("%u     %u     %s    %s\n", (u64)ps[i].pid, (u64)ps[i].ppid,
             state_name((long)ps[i].state), ps[i].name);
    return 0;
}
