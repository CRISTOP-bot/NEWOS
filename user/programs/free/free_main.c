#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    static struct nsh_sysinfo si;
    u64 total_kb, free_kb, used_kb;

    (void)argc;
    (void)argv;
    if (sys_sysinfo(&si) != 0)
        return fail("free", "cannot read system info");
    total_kb = si.total_frames * 4;
    free_kb = si.free_frames * 4;
    used_kb = total_kb - free_kb;
    nputs("             total       used       free\n");
    putf("Mem:     %10uK %10uK %10uK\n", total_kb, used_kb, free_kb);
    return 0;
}
