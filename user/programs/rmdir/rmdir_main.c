#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    int rc = 0;

    if (argc < 2)
        return fail("rmdir", "missing operand");
    for (int i = 1; i < argc; i++) {
        if (sys_unlink(argv[i]) != 0) {
            fputf(1, "rmdir: failed to remove '%s'\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
