/* NEWOS application template.
 *
 * Copy this whole directory, rename main.c, set NAME in the Makefile, and
 * run `make`. The SDK supplies: headers, libc.a, libnsh.a, a crt0 and the
 * static linker script. No dynamic loader, no shared objects, no host libc.
 */
#include <stdio.h>
#include <newos/nsh.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("myapp running on %s (pid %ld)\n", "NEWOS", sys_getpid());
    return 0;
}
