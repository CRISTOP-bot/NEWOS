/* 03-files: stdio file I/O and metadata.
 *
 * NEWOS libc FILE streams are thin fd wrappers with no user-space buffering:
 * fread/fwrite map directly onto SYS_READ/SYS_WRITE. Remember that the RAM
 * filesystem has no symlinks and no permission enforcement.
 */
#include <stdio.h>
#include <sys/stat.h>

#define PATH "/tmp/newos-sdk-demo.txt"

int main(void)
{
    FILE *f = fopen(PATH, "w");
    if (!f) {
        perror("fopen(write)");
        return 1;
    }
    fputs("written by the NEWOS SDK\n", f);
    fprintf(f, "line two: %d\n", 42);
    if (fclose(f) != 0) {
        perror("fclose");
        return 1;
    }

    struct stat st;
    if (stat(PATH, &st) == 0)
        printf("%s: %ld bytes, mode %#o\n", PATH, (long)st.st_size,
               (unsigned)st.st_mode);
    else
        perror("stat");

    f = fopen(PATH, "r");
    if (!f) {
        perror("fopen(read)");
        return 1;
    }
    char line[128];
    while (fgets(line, sizeof line, f))
        printf("read: %s", line);
    fclose(f);

    if (remove(PATH) != 0)
        perror("remove");
    return 0;
}
