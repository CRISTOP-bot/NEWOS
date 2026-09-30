#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    int rc = 0, i = 1;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == '9' &&
        argv[1][2] == '\0')
        i = 2;   /* -9 accepted (every kill here is SIGKILL-strength) */
    if (i >= argc)
        return fail("kill", "missing pid");
    for (; i < argc; i++) {
        int pid = natoi(argv[i]);
        if (pid <= 0 || sys_kill(pid) != 0) {
            fputf(1, "kill: (%s) no such process\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
