#include "../../lib/nshlib.h"

/* newfetch: fastfetch-style system overview. The logo is static art;
 * every datum beside it is read live (uname, sysinfo, fbinfo, /bin
 * listing, /etc files). */

/* Flat reduction of brand/newos.svg: 14x14, because fbcon cells are 8x8
 * square, so equal column and row counts are what keep the plate square.
 * ASCII only, because fbcon maps every byte outside 0x20..0x7E to '?'. */
static const char *logo[] = {
    "+------------+",
    "|            |",
    "|            |",
    "|            |",
    "|   \\        |",
    "|    \\       |",
    "|     \\      |",
    "|     /      |",
    "|    /       |",
    "|   /   ___  |",
    "|            |",
    "|           /",
    "|          /",
    "+-----------",
};

#define LOGO_ROWS (sizeof(logo) / sizeof(logo[0]))

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

static u64 bin_count(void)
{
    static struct nsh_dirent dents[64];
    long n = sys_readdir("/bin", dents, sizeof(dents));
    return n < 0 ? 0 : (u64)n;
}

int main(int argc, char **argv)
{
    struct nsh_uname u;
    struct nsh_sysinfo si;
    struct nsh_fbinfo fi;
    char host[64];
    u64 total_mib, used_mib, h, m, s;
    int i;

    (void)argc;
    (void)argv;
    if (sys_uname(&u) != 0)
        return fail("newfetch", "uname failed");
    if (sys_sysinfo(&si) != 0)
        return fail("newfetch", "sysinfo failed");
    read_first_line("/etc/hostname", host, sizeof(host));

    total_mib = si.total_frames * 4096u / (1024u * 1024u);
    used_mib = total_mib - si.free_frames * 4096u / (1024u * 1024u);
    h = si.uptime_sec / 3600u;
    m = (si.uptime_sec / 60u) % 60u;
    s = si.uptime_sec % 60u;

    for (i = 0; i < (int)LOGO_ROWS; i++) {
        nputs(logo[i]);
        nputs("  ");
        switch (i) {
        case 3:
            putf("%s@%s\n", "root", host[0] ? host : "newos");
            break;
        case 4:
            putf("OS: %s %s\n", u.sysname, u.release);
            break;
        case 5:
            putf("Kernel: %s (%s)\n", u.release, u.machine);
            break;
        case 6:
            putf("Uptime: %uh %um %us\n", h, m, s);
            break;
        case 7:
            putf("Memory: %u / %u MiB\n", used_mib, total_mib);
            break;
        case 8:
            nputs("Shell: nsh (/init)\n");
            break;
        case 9:
            if (sys_fbinfo(&fi) == 0 && fi.present)
                putf("Display: %ux%u x%u\n", fi.width, fi.height,
                     (u64)fi.bpp);
            else
                nputs("Display: text 80x25\n");
            break;
        case 10:
            putf("Packages: %u (/bin)\n", bin_count());
            break;
        default:
            nputs("\n");
            break;
        }
    }
    return 0;
}
