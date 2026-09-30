#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    int secs;

    if (argc != 2)
        return fail("sleep", "usage: sleep <seconds>");
    secs = natoi(argv[1]);
    if (secs < 0)
        return fail("sleep", "invalid interval");
    sys_sleep((u64)secs * 1000);
    return 0;
}
