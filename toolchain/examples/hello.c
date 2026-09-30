#include <stdio.h>

int main(int argc, char **argv)
{
    printf("Hello from NEWOS GCC (argc=%d)\n", argc);
    if (argc > 1)
        printf("first argument: %s\n", argv[1]);
    return 0;
}
