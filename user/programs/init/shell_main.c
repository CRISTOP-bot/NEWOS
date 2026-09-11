/* NEWOS initial shell (/init).
 *
 * A minimal ring-3 console shell: reads a line from stdin (the serial
 * console, fed by the COM1 RX interrupt), splits it on whitespace and
 * either runs a builtin or spawns /bin/<command> and waits for it.
 */

typedef unsigned long   u64;
typedef unsigned int    u32;
typedef long            s64;
typedef unsigned long   size_t;
typedef int             pid_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int sys_read(int fd, void *buf, u64 len)
{
    long ret;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "a"(0), "D"((long)fd), "S"(buf), "d"(len)
                         : "memory");
    return (int)ret;
}

static int sys_write(int fd, const void *buf, u64 len)
{
    long ret;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "a"(1), "D"((long)fd), "S"(buf), "d"(len)
                         : "memory");
    return (int)ret;
}

static void sys_exit(int code)
{
    __asm__ __volatile__("int $0x80" : : "a"(2), "D"(code));
    for (;;)
        ;
}

static pid_t sys_spawn(const char *path, const char *name)
{
    long ret;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "a"(5), "D"((long)path), "S"((long)name)
                         : "rcx", "r11", "memory");
    return (pid_t)ret;
}

static int sys_waitpid(pid_t pid)
{
    long ret;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "a"(6), "D"((long)pid)
                         : "rcx", "r11", "memory");
    return (int)ret;
}

static void out(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    if (n)
        sys_write(STDOUT_FILENO, s, n);
}

static void putc(char c)
{
    sys_write(STDOUT_FILENO, &c, 1);
}

static void putdec(int v)
{
    if (v < 0) {
        putc('-');
        v = -v;
    }
    char buf[16];
    int i = sizeof(buf);
    if (v == 0) {
        putc('0');
        return;
    }
    while (v > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    sys_write(1, &buf[i], sizeof(buf) - (size_t)i);
}

/* A byte is always a "word" in this shell's vocabulary: tokens are split on
 * whitespace, everything else is one character. */
static int tokenize(char *line, char *argv[], int max)
{
    int n = 0;
    char *p = line;
    int in = 0;
    for (; *p; p++) {
        int ws = (*p == ' ' || *p == '\t');
        if (!ws && !in) {
            if (n < max)
                argv[n++] = p;
            in = 1;
        } else if (ws && in) {
            *p = '\0';
            in = 0;
        }
    }
    return n;
}

static void run_line(char *line)
{
    char *argv[8];
    int argc = tokenize(line, argv, 8);

    if (argc == 0)
        return;

    const char *cmd = argv[0];

    if (str_eq(cmd, "exit")) {
        out("newos: shutting down.\n");
        sys_exit(0);
    } else if (str_eq(cmd, "help")) {
        out("commands: help, echo <text>, exit, hello, spawn /bin/<cmd>\n");
    } else if (str_eq(cmd, "echo")) {
        for (int i = 1; i < argc; i++) {
            out(argv[i]);
            if (i + 1 < argc)
                putc(' ');
        }
        putc('\n');
    } else {
        /* Build /bin/<command> and run it as a child. */
        char path[128];
        pid_t pid = -1;
        path[0] = '/';
        path[1] = 'b';
        path[2] = 'i';
        path[3] = 'n';
        path[4] = '/';
        size_t l = 0;
        while (cmd[l] && l < 122) {
            path[5 + l] = cmd[l];
            l++;
        }
        path[5 + l] = '\0';

        out("newos: spawning ");
        out(cmd);
        out(" ...\n");
        pid = sys_spawn(path, cmd);
        if (pid < 0) {
            out("newos: unknown command or spawn failed.\n");
            return;
        }
        int rc = sys_waitpid(pid);
        out("newos: pid ");
        putdec((int)pid);
        out(" exited with status ");
        putdec(rc);
        out("\n");
    }
}

static void shell_main(void)
{
    char line[256];
    size_t n = 0;

    out("\nNEWOS interactive shell\n");
    for (;;) {
        out("newos> ");
        n = 0;
        for (;;) {
            char c;
            if (sys_read(STDIN_FILENO, &c, 1) != 1)
                break;

            if (c == '\r' || c == '\n') {
                out("\n");
                line[n] = '\0';
                break;
            } else if (c == 0x7f || c == 0x08) {
                if (n > 0) {
                    n--;
                    out("\b \b");
                }
                continue;
            } else if (c >= ' ' && n < sizeof(line) - 1) {
                line[n++] = c;
                putc(c);
            }
            /* ignore control chars */
        }
        run_line(line);
    }
}

void _start(void)
{
    shell_main();
    sys_exit(0);
}