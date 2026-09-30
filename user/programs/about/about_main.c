#include "../../lib/nshlib.h"

/* about: NEWOS system information. Every line comes from a live source:
 * uname/sysinfo syscalls plus /etc/version and /etc/hostname. */

static void read_first_line(const char *path, char *out, u64 cap)
{
    long fd = sys_open(path, O_READ);
    long n, i = 0;

    out[0] = '\0';
    if (fd < 0)
        return;
    while (i + 1 < (long)cap) {
        char c;
        n = sys_read((int)fd, &c, 1);
        if (n != 1 || c == '\n')
            break;
        out[i++] = c;
    }
    out[i] = '\0';
    sys_close((int)fd);
}

int main(int argc, char **argv)
{
    struct nsh_uname u;
    struct nsh_sysinfo si;
    char version[64], host[64];
    u64 total_mib, free_mib;

    (void)argc;
    (void)argv;
    if (sys_uname(&u) != 0)
        return fail("about", "uname failed");
    if (sys_sysinfo(&si) != 0)
        return fail("about", "sysinfo failed");
    read_first_line("/etc/version", version, sizeof(version));
    read_first_line("/etc/hostname", host, sizeof(host));

    total_mib = si.total_frames * 4096u / (1024u * 1024u);
    free_mib = si.free_frames * 4096u / (1024u * 1024u);

    nputs("NEWOS - a from-scratch x86_64 operating system\n");
    putf("  version:  %s\n", version[0] ? version : u.release);
    putf("  kernel:   %s %s (%s)\n", u.sysname, u.release, u.machine);
    putf("  host:     %s\n", host[0] ? host : "newos");
    putf("  memory:   %u MiB total, %u MiB free\n", total_mib, free_mib);
    putf("  uptime:   %u s\n", si.uptime_sec);
    nputs("  license:  MIT (see LICENSE)\n");
    return 0;
}
