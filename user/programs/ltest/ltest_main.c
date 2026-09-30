#include "../../../libc/include/stdio.h"
#include "../../../libc/include/string.h"
#include "../../../libc/include/stdlib.h"

/* ltest: libc self-test. Exercises string/memory/stdio/heap and reports
 * PASS/FAIL per area; exit code 0 iff everything passes. */

static int fails;

#define CHECK(cond, name) do { \
    if (cond) { printf("  [PASS] %s\n", name); } \
    else { printf("  [FAIL] %s\n", name); fails++; } \
} while (0)

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("libc self-test\n");

    CHECK(strlen("hello") == 5, "strlen");
    CHECK(strcmp("abc", "abd") < 0 && strcmp("x", "x") == 0, "strcmp");
    CHECK(strncmp("abcd", "abce", 3) == 0, "strncmp");
    CHECK(atoi("-42") == -42 && atoi("17") == 17, "atoi");
    {
        char b[16];
        strcpy(b, "hi");
        strcat(b, "-there");
        CHECK(strcmp(b, "hi-there") == 0, "strcpy/strcat");
        CHECK(strchr(b, '-') == b + 2, "strchr");
    }
    {
        char a[8], c[8];
        memset(a, 0x5A, sizeof(a));
        memcpy(c, a, sizeof(c));
        CHECK(memcmp(a, c, sizeof(a)) == 0, "memset/memcpy/memcmp");
        c[3] = 0;
        memmove(c + 2, c, 4);
        CHECK(c[2] == 0x5A && c[5] == 0, "memmove-overlap");
    }
    {
        int *p = (int *)malloc(64);
        int ok = p != NULL;
        if (ok) {
            int i;
            for (i = 0; i < 16; i++)
                p[i] = i * 3;
            for (i = 0; i < 16; i++)
                if (p[i] != i * 3)
                    ok = 0;
            free(p);
        }
        CHECK(ok, "malloc/free");
    }
    {
        int *z = (int *)calloc(8, sizeof(int));
        int ok = z != NULL, i;
        for (i = 0; ok && i < 8; i++)
            if (z[i] != 0)
                ok = 0;
        CHECK(ok, "calloc-zero");
        z = (int *)realloc(z, 16 * sizeof(int));
        ok = z != NULL;
        if (ok) {
            for (i = 0; i < 8; i++)
                if (z[i] != 0)
                    ok = 0;
            z[15] = 0x12345678;
            ok = ok && z[15] == 0x12345678;
            free(z);
        }
        CHECK(ok, "realloc-keep");
    }
    {
        srand(1234);
        int a = rand(), b = rand();
        CHECK(a != b && a >= 0 && b >= 0, "rand");
    }
    printf("ltest: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
