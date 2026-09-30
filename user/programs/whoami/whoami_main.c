#include "../../lib/nshlib.h"

/* Prints the session owner from /etc/passwd (first entry). The system
 * boots single-user, so in practice this is root. */

int main(int argc, char **argv)
{
    char buf[128];
    long n, fd = sys_open("/etc/passwd", O_READ);
    long i = 0;

    (void)argc;
    (void)argv;
    if (fd < 0)
        return fail("whoami", "cannot read /etc/passwd");
    n = sys_read((int)fd, buf, sizeof(buf) - 1);
    sys_close((int)fd);
    if (n <= 0)
        return fail("whoami", "cannot read /etc/passwd");
    buf[n] = '\0';
    while (i < n && buf[i] != ':' && buf[i] != '\n') {
        nputc(buf[i]);
        i++;
    }
    if (i == 0)
        return fail("whoami", "empty passwd entry");
    nputc('\n');
    return 0;
}
