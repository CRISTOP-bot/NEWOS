#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/stdlib.h"
#include "../include/errno.h"
#include "../include/unistd.h"
#include "../include/fcntl.h"
#include "syscall.h"
#include "../../abi/syscall_abi.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

/* The header's va_list is the same builtin type, so these mix freely. */

long libc_write(int fd, const void *buf, unsigned long len);

static void outc(char c)
{
    libc_write(1, &c, 1);
}

static void outs(const char *s)
{
    size_t n = 0;
    if (!s)
        s = "(null)";
    while (s[n])
        n++;
    if (n)
        libc_write(1, s, n);
}

static void outu(unsigned long v, int base, int upper)
{
    char tmp[24];
    int i = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) {
        outc('0');
        return;
    }
    while (v && i < (int)sizeof(tmp)) {
        tmp[i++] = dig[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (i--)
        outc(tmp[i]);
}

int putchar(int c)
{
    char ch = (char)c;
    libc_write(1, &ch, 1);
    return c;
}
int puts(const char *s)
{
    outs(s);
    outc('\n');
    return 0;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        long prec;
        if (*fmt != '%') {
            outc(*fmt++);
            continue;
        }
        fmt++;
        /* Field width is accepted and ignored; precision (".N") caps
         * %s output. Anything else keeps arg consumption aligned. */
        while (*fmt >= '0' && *fmt <= '9')
            fmt++;
        prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt - '0');
                fmt++;
            }
        }
        switch (*fmt) {
        case 's': {
            const char *v = va_arg(ap, const char *);
            long i = 0;
            if (!v)
                v = "(null)";
            while (*v && (prec < 0 || i < prec)) {
                outc(*v++);
                i++;
            }
            break;
        }
        case 'd': {
            int iv = va_arg(ap, int);
            if (iv < 0) {
                outc('-');
                outu((unsigned long)(-(long)iv), 10, 0);
            } else {
                outu((unsigned long)iv, 10, 0);
            }
            break;
        }
        case 'u':
            outu(va_arg(ap, unsigned int), 10, 0);
            break;
        case 'x':
            outu(va_arg(ap, unsigned int), 16, 0);
            break;
        case 'X':
            outu(va_arg(ap, unsigned int), 16, 1);
            break;
        case 'p':
            outs("0x");
            outu(va_arg(ap, unsigned long), 16, 0);
            break;
        case 'c':
            outc((char)va_arg(ap, int));
            break;
        case '%':
            outc('%');
            break;
        default:
            outc('%');
            if (*fmt)
                outc(*fmt);
            break;
        }
        if (*fmt)
            fmt++;
    }
    va_end(ap);
    return 0;
}

/* --- FILE streams (thin fd wrappers, unbuffered) ----------------------- */

static FILE file_stdin = { 0, 0, 0 };
static FILE file_stdout = { 1, 0, 0 };
static FILE file_stderr = { 2, 0, 0 };

FILE *stdin = &file_stdin;
FILE *stdout = &file_stdout;
FILE *stderr = &file_stderr;

FILE *fopen(const char *path, const char *mode)
{
    int flags = O_RDONLY;
    int plus = 0;
    int fd;
    FILE *f;
    const char *m;

    if (!path || !mode || !mode[0])
        return NULL;
    for (m = mode + 1; *m; m++) {
        if (*m == '+')
            plus = 1;
        else if (*m != 'b')
            return NULL;
    }
    if (mode[0] == 'r')
        flags = plus ? O_RDWR : O_RDONLY;
    else if (mode[0] == 'w')
        flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a')
        flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
    else
        return NULL;

    fd = open(path, flags);
    if (fd < 0) {
        errno = ENOENT;
        return NULL;
    }
    f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        libc_sys1(SYS_CLOSE, fd);
        errno = ENOMEM;
        return NULL;
    }
    f->fd = fd;
    f->err = 0;
    f->eof = 0;
    return f;
}

int fclose(FILE *f)
{
    int r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (f == stdin || f == stdout || f == stderr)
        return 0;
    r = (int)libc_sys1(SYS_CLOSE, f->fd);
    free(f);
    return r;
}

size_t fread(void *p, size_t s, size_t n, FILE *f)
{
    size_t want, got = 0;
    if (!f || !p || !s) {
        if (f)
            f->err = 1;
        return 0;
    }
    want = s * n;
    while (got < want) {
        long r = libc_sys3(SYS_READ, f->fd, (long)((char *)p + got),
                           (long)(want - got));
        if (r < 0) {
            f->err = 1;
            break;
        }
        if (r == 0) {
            f->eof = 1;
            break;
        }
        got += (size_t)r;
    }
    return s ? got / s : 0;
}

size_t fwrite(const void *p, size_t s, size_t n, FILE *f)
{
    size_t want, got = 0;
    if (!f || !p || !s) {
        if (f)
            f->err = 1;
        return 0;
    }
    want = s * n;
    while (got < want) {
        long r = libc_sys3(SYS_READ, f->fd, (long)((const char *)p + got),
                           (long)(want - got));
        /* SYS_WRITE */
        if (r <= 0) {
            f->err = 1;
            break;
        }
        got += (size_t)r;
    }
    return s ? got / s : 0;
}

int fseek(FILE *f, long off, int whence)
{
    long r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    r = libc_sys3(SYS_LSEEK, f->fd, off, whence);
    if (r < 0) {
        f->err = 1;
        return -1;
    }
    f->eof = 0;
    return 0;
}

long ftell(FILE *f)
{
    long r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    r = libc_sys3(SYS_LSEEK, f->fd, 0, SEEK_CUR);
    if (r < 0)
        f->err = 1;
    return r;
}

void rewind(FILE *f)
{
    if (!f)
        return;
    if (libc_sys3(SYS_LSEEK, f->fd, 0, SEEK_SET) < 0)
        f->err = 1;
    else
        f->eof = 0;
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0;
    if (!s || n <= 0 || !f) {
        if (f)
            f->err = 1;
        return NULL;
    }
    while (i + 1 < n) {
        long r = libc_sys3(SYS_READ, f->fd, (long)(s + i), 1);
        if (r < 0) {
            f->err = 1;
            return i ? s : NULL;
        }
        if (r == 0) {
            f->eof = 1;
            return i ? s : NULL;
        }
        if (s[i++] == '\n')
            break;
    }
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *f)
{
    char ch = (char)c;
    long r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    r = libc_sys3(SYS_WRITE, f->fd, (long)&ch, 1);
    if (r != 1) {
        f->err = 1;
        return -1;
    }
    return c;
}

int fputs(const char *s, FILE *f)
{
    size_t n = 0;
    if (!s || !f) {
        if (f)
            f->err = 1;
        return -1;
    }
    while (s[n])
        n++;
    if (n && (size_t)fwrite(s, 1, n, f) != n)
        return -1;
    return 0;
}

int getc(FILE *f)
{
    unsigned char ch;
    long r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    r = libc_sys3(SYS_READ, f->fd, (long)&ch, 1);
    if (r < 0) {
        f->err = 1;
        return -1;
    }
    if (r == 0) {
        f->eof = 1;
        return -1;
    }
    return (int)ch;
}

int putc(int c, FILE *f)
{
    return fputc(c, f);
}

int getchar(void)
{
    return getc(stdin);
}

int fflush(FILE *f)
{
    (void)f;
    return 0;   /* unbuffered: nothing to flush */
}

int ferror(FILE *f)
{
    return f ? f->err : 1;
}

int feof(FILE *f)
{
    return f ? f->eof : 1;
}

void clearerr(FILE *f)
{
    if (f) {
        f->err = 0;
        f->eof = 0;
    }
}

void perror(const char *s)
{
    if (s && *s) {
        libc_sys3(SYS_WRITE, 2, (long)s, (long)strlen(s));
        libc_sys3(SYS_WRITE, 2, (long)": ", 2);
    }
    {
        const char *e = strerror(errno);
        size_t n = strlen(e);
        libc_sys3(SYS_WRITE, 2, (long)e, (long)n);
        libc_sys3(SYS_WRITE, 2, (long)"\n", 1);
    }
}

ssize_t getline(char **lp, size_t *cap, FILE *f)
{
    size_t len = 0;
    if (!lp || !cap || !f) {
        if (f)
            f->err = 1;
        return -1;
    }
    if (!*lp) {
        *cap = 128;
        *lp = (char *)malloc(*cap);
        if (!*lp)
            return -1;
    }
    for (;;) {
        long r;
        if (len + 1 >= *cap) {
            size_t ncap = *cap * 2;
            char *np = (char *)realloc(*lp, ncap);
            if (!np)
                return -1;
            *lp = np;
            *cap = ncap;
        }
        r = libc_sys3(SYS_READ, f->fd, (long)(*lp + len), 1);
        if (r < 0) {
            f->err = 1;
            return -1;
        }
        if (r == 0) {
            f->eof = 1;
            return len ? (ssize_t)len : -1;
        }
        if ((*lp)[len++] == '\n')
            return (ssize_t)len;
    }
}

/* --- bounded formatting ------------------------------------------------ */

struct snk {
    char *dst;
    size_t cap;     /* usable bytes excluding the NUL */
    size_t len;
};

static void sn_emit(struct snk *s, char c)
{
    if (s->len < s->cap)
        s->dst[s->len] = c;
    s->len++;
}

static void sn_str(struct snk *s, const char *v)
{
    if (!v)
        v = "(null)";
    while (*v)
        sn_emit(s, *v++);
}

static void sn_num(struct snk *s, unsigned long v, int base, int upper)
{
    char tmp[24];
    int i = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (!v) {
        sn_emit(s, '0');
        return;
    }
    while (v && i < (int)sizeof(tmp)) {
        tmp[i++] = dig[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (i--)
        sn_emit(s, tmp[i]);
}

static int vsnprintf_inner(char *dst, size_t n, const char *fmt, va_list ap)
{
    struct snk s = { dst, n ? n - 1 : 0, 0 };
    if (!dst)
        return -1;
    while (*fmt) {
        long prec;
        if (*fmt != '%') {
            sn_emit(&s, *fmt++);
            continue;
        }
        fmt++;
        while (*fmt >= '0' && *fmt <= '9')
            fmt++;
        prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt - '0');
                fmt++;
            }
        }
        switch (*fmt) {
        case 's': {
            const char *v = va_arg(ap, const char *);
            long i = 0;
            if (!v)
                v = "(null)";
            while (*v && (prec < 0 || i < prec)) {
                sn_emit(&s, *v++);
                i++;
            }
            break;
        }
        case 'd': {
            int iv = va_arg(ap, int);
            if (iv < 0) {
                sn_emit(&s, '-');
                sn_num(&s, (unsigned long)(-(long)iv), 10, 0);
            } else {
                sn_num(&s, (unsigned long)iv, 10, 0);
            }
            break;
        }
        case 'u':
            sn_num(&s, va_arg(ap, unsigned int), 10, 0);
            break;
        case 'x':
            sn_num(&s, va_arg(ap, unsigned int), 16, 0);
            break;
        case 'X':
            sn_num(&s, va_arg(ap, unsigned int), 16, 1);
            break;
        case 'p':
            sn_str(&s, "0x");
            sn_num(&s, va_arg(ap, unsigned long), 16, 0);
            break;
        case 'c':
            sn_emit(&s, (char)va_arg(ap, int));
            break;
        case '%':
            sn_emit(&s, '%');
            break;
        default:
            sn_emit(&s, '%');
            if (*fmt)
                sn_emit(&s, *fmt);
            break;
        }
        if (*fmt)
            fmt++;
    }
    if (n)
        dst[s.len < s.cap ? s.len : s.cap] = '\0';
    return (int)s.len;
}

int vsnprintf(char *dst, size_t n, const char *fmt, va_list ap)
{
    return vsnprintf_inner(dst, n, fmt, ap);
}

int snprintf(char *dst, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf_inner(dst, n, fmt, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    /* Bounded single pass: 4 KiB staging, then one write. Long lines
     * truncate (editors format short status lines; kilo's longest is
     * well under a hundred bytes). */
    static char stage[4096];
    int n;
    size_t w;
    if (!f || !fmt)
        return -1;
    n = vsnprintf_inner(stage, sizeof(stage), fmt, ap);
    if (n < 0)
        return -1;
    w = (size_t)n < sizeof(stage) ? (size_t)n : sizeof(stage) - 1;
    if (w && fwrite(stage, 1, w, f) != w)
        return -1;
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
