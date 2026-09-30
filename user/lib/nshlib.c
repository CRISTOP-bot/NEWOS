/* nshlib implementation: syscall stubs + string/output helpers. */

#include "nshlib.h"

#define SYSC0(nr) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr) : "memory"); r; })
#define SYSC1(nr, a) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)) : "memory"); r; })
#define SYSC2(nr, a, b) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)) \
    : "memory"); r; })
#define SYSC3(nr, a, b, c) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)), \
    "d"((long)(c)) : "memory"); r; })
#define SYSC4(nr, a, b, c, d) ({ long r; \
    register long _x10 __asm__("r10") = (long)(d); \
    __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)), \
    "d"((long)(c)), "r"(_x10) : "memory"); r; })
#define SYSC5(nr, a, b, c, d, e) ({ long r; \
    register long _x10 __asm__("r10") = (long)(d); \
    register long _x8 __asm__("r8") = (long)(e); \
    __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)), \
    "d"((long)(c)), "r"(_x10), "r"(_x8) : "memory"); r; })

long sys_read(int fd, void *buf, u64 len)
{
    return SYSC3(SYS_READ, fd, buf, len);
}

long sys_write(int fd, const void *buf, u64 len)
{
    return SYSC3(SYS_WRITE, fd, buf, len);
}

long sys_open(const char *path, int flags)
{
    return SYSC2(SYS_OPEN, path, flags);
}

long sys_close(int fd)
{
    return SYSC1(SYS_CLOSE, fd);
}

long sys_mkdir(const char *path)
{
    return SYSC1(SYS_MKDIR, path);
}

long sys_unlink(const char *path)
{
    return SYSC1(SYS_UNLINK, path);
}

long sys_rename(const char *oldpath, const char *newpath)
{
    return SYSC2(SYS_RENAME, oldpath, newpath);
}

long sys_readdir(const char *path, struct nsh_dirent *buf, u64 bytes)
{
    return SYSC3(SYS_READDIR, path, buf, bytes);
}

long sys_chdir(const char *path)
{
    return SYSC1(SYS_CHDIR, path);
}

long sys_getcwd(char *buf, u64 size)
{
    return SYSC2(SYS_GETCWD, buf, size);
}

long sys_spawn2(const char *path, char **argv, int argc)
{
    return SYSC3(SYS_SPAWN2, path, argv, argc);
}

long sys_waitpid(pid_t pid)
{
    return SYSC2(SYS_WAITPID, pid, 0);
}

long sys_getpid(void)
{
    return SYSC0(SYS_GETPID);
}

long sys_sleep(u64 ms)
{
    /* SYS_SLEEP is Linux nanosleep: the kernel reads a timespec. */
    struct nsh_timespec ts = { .tv_sec = (long)(ms / 1000),
                               .tv_nsec = (long)(ms % 1000) * 1000000 };
    return SYSC1(SYS_SLEEP, &ts);
}

long sys_ps(struct nsh_proc *buf, u64 bytes)
{
    return SYSC2(SYS_PS, buf, bytes);
}

long sys_kill(pid_t pid)
{
    return SYSC1(SYS_KILL, pid);
}

long sys_uname(struct nsh_uname *u)
{
    return SYSC1(SYS_UNAME, u);
}

long sys_gettime(struct nsh_time *t)
{
    return SYSC1(SYS_GETTIME, t);
}

long sys_sysinfo(struct nsh_sysinfo *si)
{
    return SYSC1(SYS_SYSINFO, si);
}

long sys_fbinfo(struct nsh_fbinfo *fi)
{
    return SYSC1(SYS_FBINFO, fi);
}

long sys_fbwrite(const struct nsh_fbwrite *rq)
{
    return SYSC1(SYS_FBWRITE, rq);
}

long sys_lseek(int fd, long off, int whence)
{
    return SYSC3(SYS_LSEEK, fd, off, whence);
}

long sys_mouse_get(struct nsh_mouse *m)
{
    return SYSC1(SYS_MOUSE_GET, m);
}

long sys_pipe(int fds[2])
{
    return SYSC1(SYS_PIPE, fds);
}

long sys_spawn3(const char *path, char **argv, int argc, int fdin, int fdout)
{
    return SYSC5(SYS_SPAWN3, path, argv, argc, fdin, fdout);
}

void sys_exit(int code)
{
    SYSC1(SYS_EXIT, code);
    for (;;)
        ;
}

/* --- strings ----------------------------------------------------------- */

u64 nstrlen(const char *s)
{
    u64 n = 0;
    while (s[n])
        n++;
    return n;
}

int nstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int nstrncmp(const char *a, const char *b, u64 n)
{
    for (u64 i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i])
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
    }
    return 0;
}

char *nstrcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

char *nstrcat(char *d, const char *s)
{
    char *r = d;
    while (*d)
        d++;
    while ((*d++ = *s++))
        ;
    return r;
}

char *nstrchr(const char *s, char c)
{
    while (*s) {
        if (*s == c)
            return (char *)s;
        s++;
    }
    return c ? 0 : (char *)s;
}

void *nmemcpy(void *d, const void *s, u64 n)
{
    u8 *dd = d;
    const u8 *ss = s;
    for (u64 i = 0; i < n; i++)
        dd[i] = ss[i];
    return d;
}

void *nmemset(void *d, int c, u64 n)
{
    u8 *dd = d;
    for (u64 i = 0; i < n; i++)
        dd[i] = (u8)c;
    return d;
}

int natoi(const char *s)
{
    int neg = 0, v = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

u64 natou(const char *s)
{
    u64 v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (u64)(*s++ - '0');
    return v;
}

/* --- output ------------------------------------------------------------ */

void nputc(char c)
{
    sys_write(1, &c, 1);
}

void nputs(const char *s)
{
    u64 n = nstrlen(s);
    if (n)
        sys_write(1, s, n);
}

void nputsn(const char *s, u64 n)
{
    if (n)
        sys_write(1, s, n);
}

void putdec(long v)
{
    char buf[24];
    int i = sizeof(buf);
    unsigned long u;
    if (v < 0) {
        nputc('-');
        u = (unsigned long)(-(v + 1)) + 1;
    } else {
        u = (unsigned long)v;
    }
    if (u == 0) {
        nputc('0');
        return;
    }
    while (u > 0) {
        buf[--i] = (char)('0' + (u % 10));
        u /= 10;
    }
    nputsn(&buf[i], sizeof(buf) - (u64)i);
}

void putu(u64 v)
{
    char buf[24];
    int i = sizeof(buf);
    if (v == 0) {
        nputc('0');
        return;
    }
    while (v > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    nputsn(&buf[i], sizeof(buf) - (u64)i);
}

void puthex(u64 v)
{
    char buf[16];
    int i = sizeof(buf);
    if (v == 0) {
        nputs("0x0");
        return;
    }
    while (v > 0) {
        u64 d = v & 0xF;
        buf[--i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v >>= 4;
    }
    nputs("0x");
    nputsn(&buf[i], sizeof(buf) - (u64)i);
}

void put2(u64 v)
{
    nputc((char)('0' + ((v / 10) % 10)));
    nputc((char)('0' + (v % 10)));
}

static void vemits(int fd, const char *fmt, __builtin_va_list ap)
{
    char tmp[32];
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            sys_write(fd, fmt, 1);
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            u64 n = nstrlen(s);
            if (n)
                sys_write(fd, s, n);
            break;
        }
        case 'd': {
            long v = __builtin_va_arg(ap, long);
            int neg = v < 0;
            unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1
                                  : (unsigned long)v;
            int i = sizeof(tmp);
            if (u == 0)
                tmp[--i] = '0';
            while (u > 0) {
                tmp[--i] = (char)('0' + (u % 10));
                u /= 10;
            }
            if (neg)
                tmp[--i] = '-';
            sys_write(fd, &tmp[i], sizeof(tmp) - (u64)i);
            break;
        }
        case 'u': {
            unsigned long u = __builtin_va_arg(ap, unsigned long);
            int i = sizeof(tmp);
            if (u == 0)
                tmp[--i] = '0';
            while (u > 0) {
                tmp[--i] = (char)('0' + (u % 10));
                u /= 10;
            }
            sys_write(fd, &tmp[i], sizeof(tmp) - (u64)i);
            break;
        }
        case 'x': {
            unsigned long u = __builtin_va_arg(ap, unsigned long);
            int i = sizeof(tmp);
            if (u == 0)
                tmp[--i] = '0';
            while (u > 0) {
                unsigned long d = u & 0xF;
                tmp[--i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
                u >>= 4;
            }
            sys_write(fd, &tmp[i], sizeof(tmp) - (u64)i);
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            sys_write(fd, &c, 1);
            break;
        }
        default:
            sys_write(fd, fmt, 1);
            break;
        }
    }
}

void putf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vemits(1, fmt, ap);
    __builtin_va_end(ap);
}

void fputf(int fd, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vemits(fd, fmt, ap);
    __builtin_va_end(ap);
}

int fail(const char *prog, const char *msg)
{
    fputf(1, "%s: %s\n", prog, msg);
    return 1;
}
