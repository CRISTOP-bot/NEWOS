/* 02-args: command-line arguments.
 *
 * crt0 forwards the kernel's (argc, argv) straight into main. NEWOS gives
 * each program its own argv area; there is no environment block, so getenv()
 * starts empty (use setenv() if you need one).
 *
 * Run:  args alpha 42
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    printf("%s: %d argument(s)\n", argv[0], argc);
    for (int i = 1; i < argc; ++i) {
        long n = strtol(argv[i], (char **)0, 10);
        printf("  [%d] '%s'  (as number: %ld)\n", i, argv[i], n);
    }
    return 0;
}
