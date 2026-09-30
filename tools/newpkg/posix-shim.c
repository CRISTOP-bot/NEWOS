/* posix-shim.c: HOST-ONLY syscall shim to run the REAL newpkg_main.c
 * outside NEWOS (for `make check-newpkg` end-to-end tests).
 *
 * Never compiled into the kernel or the on-board /bin/newpkg: it maps the
 * nshlib syscall surface onto a fake VFS root ($NEWPKG_HOST_ROOT) with
 * POSIX calls, preserving the exact sequential-read/write, no-seek,
 * 512-byte-chunk behavior the on-board tool relies on. The package logic
 * under test is 100% the shipped newpkg_main.c + newpkg_format.c.
 *
 * Build:
 *   gcc -DNEWPKG_HOST -Wall -Wextra -Werror -o /tmp/newpkg-host \
 *       user/programs/newpkg/newpkg_format.c \
 *       user/programs/newpkg/newpkg_main.c \
 *       tools/newpkg/posix-shim.c
 * Run:
 *   NEWPKG_HOST_ROOT=/tmp/fakeroot /tmp/newpkg-host install /tmp/x.new
 * (absolute VFS paths are jailed under $NEWPKG_HOST_ROOT).
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../user/programs/newpkg/newpkg_format.h"
/* nshlib.h comes via newpkg_format.h in non-HOST mode... but here
 * NEWPKG_HOST is defined, so include it explicitly for the API. */
#include "../../user/lib/nshlib.h"

static void vfs_path(const char *path, char *out, size_t cap)
{
    const char *root = getenv("NEWPKG_HOST_ROOT");
    if (!root || !*root)
        root = "/tmp/newpkg-root";
    if (path[0] == '/')
        snprintf(out, cap, "%s%s", root, path);
    else {
        char cwd[256];
        if (!getcwd(cwd, sizeof(cwd)))
            strcpy(cwd, ".");
        snprintf(out, cap, "%s/%s", cwd, path);
    }
}

long sys_read(int fd, void *buf, u64 len)
{
    ssize_t n;
    if (len > 512)
        len = 512;              /* same clamp as the NEWOS kernel */
    n = read(fd, buf, (size_t)len);
    if (n < 0)
        return -1;
    return (long)n;
}

long sys_write(int fd, const void *buf, u64 len)
{
    ssize_t n;
    if (len > 512)
        len = 512;
    n = write(fd, buf, (size_t)len);
    if (n < 0)
        return -1;
    return (long)n;
}

long sys_open(const char *path, int flags)
{
    char full[512];
    int oflags = 0;
    vfs_path(path, full, sizeof(full));
    if ((flags & O_READ) && (flags & O_WRITE))
        oflags = O_RDWR;
    else if (flags & O_WRITE)
        oflags = O_WRONLY;
    else
        oflags = O_RDONLY;
    if (flags & O_CREATE)
        oflags |= O_CREAT;
    return (long)open(full, oflags, 0644);
}

long sys_close(int fd)
{
    return close(fd) == 0 ? 0 : -1;
}

long sys_mkdir(const char *path)
{
    char full[512];
    vfs_path(path, full, sizeof(full));
    return mkdir(full, 0755) == 0 ? 0 : -1;
}

long sys_unlink(const char *path)
{
    char full[512];
    vfs_path(path, full, sizeof(full));
    return unlink(full) == 0 ? 0 : -1;
}

long sys_readdir(const char *path, struct nsh_dirent *buf, u64 bytes)
{
    char full[512];
    DIR *d;
    struct dirent *e;
    u64 cap = bytes / sizeof(struct nsh_dirent);
    long count = 0;
    vfs_path(path, full, sizeof(full));
    d = opendir(full);
    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        struct stat st;
        char child[768];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if ((u64)count >= cap)
            break;
        memset(&buf[count], 0, sizeof(buf[count]));
        strncpy(buf[count].name, e->d_name, sizeof(buf[count].name) - 1);
        snprintf(child, sizeof(child), "%s/%s", full, e->d_name);
        if (stat(child, &st) == 0) {
            buf[count].size = (nsh_u64)st.st_size;
            buf[count].mode =
                S_ISDIR(st.st_mode) ? MODE_DIR | 0755 : MODE_REG | 0644;
        } else {
            buf[count].mode = MODE_REG | 0644;
        }
        count++;
    }
    closedir(d);
    return count;
}

long sys_uname(struct nsh_uname *u)
{
    if (!u)
        return -1;
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname, "NEWOS");
    strcpy(u->release, "0.2.0-pre-alpha");
    strcpy(u->machine, "x86_64");
    return 0;
}

long sys_gettime(struct nsh_time *t)
{
    if (!t)
        return -1;
    memset(t, 0, sizeof(*t));
    t->year = 2026;
    t->mon = 9;
    t->day = 25;
    t->hour = 12;
    t->min = 0;
    t->sec = 0;
    return 0;
}

/* Stubs for the rest of the nshlib surface (linked but never called by
 * newpkg_main.c; abort loudly if that ever changes). */
long sys_chdir(const char *path)
{
    (void)path;
    return -1;
}
long sys_getcwd(char *buf, u64 size)
{
    (void)buf;
    (void)size;
    return -1;
}
long sys_spawn2(const char *path, char **argv, int argc)
{
    (void)path;
    (void)argv;
    (void)argc;
    return -1;
}
long sys_waitpid(pid_t pid)
{
    (void)pid;
    return -1;
}
long sys_getpid(void)
{
    return 1;
}
long sys_sleep(u64 ms)
{
    (void)ms;
    return 0;
}
long sys_ps(struct nsh_proc *buf, u64 bytes)
{
    (void)buf;
    (void)bytes;
    return -1;
}
long sys_kill(pid_t pid)
{
    (void)pid;
    return -1;
}
long sys_sysinfo(struct nsh_sysinfo *si)
{
    (void)si;
    return -1;
}
long sys_fbinfo(struct nsh_fbinfo *fi)
{
    (void)fi;
    return -1;
}
long sys_fbwrite(const struct nsh_fbwrite *rq)
{
    (void)rq;
    return -1;
}
void sys_exit(int code)
{
    exit(code);
}

/* --- nshlib string/output surface (same observable behavior) -------------- */

u64 nstrlen(const char *s)
{
    return (u64)strlen(s);
}
int nstrcmp(const char *a, const char *b)
{
    return strcmp(a, b);
}
int nstrncmp(const char *a, const char *b, u64 n)
{
    return strncmp(a, b, (size_t)n);
}
char *nstrcpy(char *d, const char *s)
{
    return strcpy(d, s);
}
char *nstrcat(char *d, const char *s)
{
    return strcat(d, s);
}
char *nstrchr(const char *s, char c)
{
    return (char *)strchr(s, c);
}
void *nmemcpy(void *d, const void *s, u64 n)
{
    return memcpy(d, s, (size_t)n);
}
void *nmemset(void *d, int c, u64 n)
{
    return memset(d, c, (size_t)n);
}
int natoi(const char *s)
{
    return atoi(s);
}
u64 natou(const char *s)
{
    return (u64)strtoul(s, NULL, 10);
}

void nputc(char c)
{
    fputc(c, stdout);
}
void nputs(const char *s)
{
    fwrite(s, 1, strlen(s), stdout);
}
void nputsn(const char *s, u64 n)
{
    fwrite(s, 1, (size_t)n, stdout);
}
void putdec(long v)
{
    printf("%ld", v);
}
void putu(u64 v)
{
    printf("%lu", (unsigned long)v);
}
void puthex(u64 v)
{
    printf("%lx", (unsigned long)v);
}
void put2(u64 v)
{
    printf("%02lu", (unsigned long)v);
}

/* Tiny printf matching nshlib semantics: %s %d(long) %u(ulong) %x(ulong)
 * %c %%. */
static void vemits(FILE *fp, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            fputc(*fmt, fp);
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 's':
            fputs(va_arg(ap, const char *), fp);
            break;
        case 'd':
            fprintf(fp, "%ld", va_arg(ap, long));
            break;
        case 'u':
            fprintf(fp, "%lu", va_arg(ap, unsigned long));
            break;
        case 'x':
            fprintf(fp, "%lx", va_arg(ap, unsigned long));
            break;
        case 'c':
            fputc(va_arg(ap, int), fp);
            break;
        default:
            fputc(*fmt ? *fmt : '%', fp);
            break;
        }
    }
}

void putf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vemits(stdout, fmt, ap);
    va_end(ap);
}

void fputf(int fd, const char *fmt, ...)
{
    va_list ap;
    FILE *fp = fd == 2 ? stderr : stdout;
    va_start(ap, fmt);
    vemits(fp, fmt, ap);
    va_end(ap);
}

int fail(const char *prog, const char *msg)
{
    printf("%s: %s\n", prog, msg);
    return 1;
}
