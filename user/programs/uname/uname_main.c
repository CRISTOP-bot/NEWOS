#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    static struct nsh_uname u;
    int s = 0, r = 0, m = 0;

    if (sys_uname(&u) != 0)
        return fail("uname", "cannot read system name");
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1])
            return fail("uname", "extra operand");
        for (int k = 1; argv[i][k]; k++) {
            if (argv[i][k] == 's')
                s = 1;
            else if (argv[i][k] == 'r')
                r = 1;
            else if (argv[i][k] == 'm')
                m = 1;
            else if (argv[i][k] == 'a')
                s = r = m = 1;
            else
                return fail("uname", "unknown option");
        }
    }
    if (!s && !r && !m)
        s = 1;
    if (s)
        putf("%s", u.sysname);
    if (r)
        putf("%s%s", s ? " " : "", u.release);
    if (m)
        putf("%s%s", (s || r) ? " " : "", u.machine);
    nputc('\n');
    return 0;
}
