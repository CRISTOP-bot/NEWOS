#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    int nl = 1, i = 1, first = 1;

    if (argc > 1 && nstrcmp(argv[1], "-n") == 0) {
        nl = 0;
        i = 2;
    }
    for (; i < argc; i++) {
        if (!first)
            nputc(' ');
        first = 0;
        nputs(argv[i]);
    }
    if (nl)
        nputc('\n');
    return 0;
}
