#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < 25; i++)
        nputc('\n');
    nputs("\x1B[2J\x1B[H");
    return 0;
}
