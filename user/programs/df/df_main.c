#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    static struct nsh_sysinfo si;
    u64 total_b, free_b, used_b, pct;

    (void)argc;
    (void)argv;
    if (sys_sysinfo(&si) != 0)
        return fail("df", "cannot read system info");
    total_b = si.total_frames * 4;   /* 1K blocks */
    free_b = si.free_frames * 4;
    used_b = total_b - free_b;
    pct = total_b ? (used_b * 100) / total_b : 0;
    nputs("Filesystem  1K-blocks  Used  Available  Use%  Mounted on\n");
    putf("memfs       %8u  %8u  %9u  %3u%%  /\n", total_b, used_b, free_b,
         pct);
    return 0;
}
