#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    int rc = 0;

    if (argc < 2)
        return fail("touch", "missing operand");
    for (int i = 1; i < argc; i++) {
        long fd = sys_open(argv[i], O_WRITE | O_CREATE);
        if (fd < 0) {
            fputf(1, "touch: cannot create '%s'\n", argv[i]);
            rc = 1;
            continue;
        }
        sys_close((int)fd);
    }
    return rc;
}
