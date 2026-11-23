/* 01-hello: the smallest useful NEWOS application.
 *
 * Build:  make -C sdk
 * Run:    copy sdk/build/01-hello.elf to /bin/hello-sdk and run `hello-sdk`.
 *
 * Shows: standard main(), the freestanding libc stdio facade, and a clean
 * return (crt0 turns main's return value into SYS_EXIT).
 */
#include <stdio.h>

int main(void)
{
    puts("Hello from a NEWOS SDK application!");
    printf("libc printf: %d + %d = %d, hex %#x\n", 2, 3, 2 + 3, 0x2a);
    return 0;
}
