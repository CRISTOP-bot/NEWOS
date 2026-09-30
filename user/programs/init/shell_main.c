/* nsh: the NEWOS shell (/init, /bin/sh).
 *
 * A real interactive shell, not a demo: UTF-8 line editing (arrows,
 * Home/End, Delete, history, Ctrl+A/C/D/E/K/L/U/V/W/Y), quoting, builtins (cd, exit,
 * help, history, clear) and external /bin programs spawned with real
 * argc/argv. Input arrives from serial and the PS/2 keyboard alike;
 * output goes to the multiplexed console (serial + VGA text).
 */

#include "../../lib/nshlib.h"

#define LINE_MAX 256
#define HIST_MAX 16
#define MAX_ARGS 16
#define MAX_STAGES 8
#define O_TRUNC NSH_O_TRUNC

/* --- history ----------------------------------------------------------- */

static char hist[HIST_MAX][LINE_MAX];
static int hcount;      /* stored entries */
static int hnav;        /* -1 = current draft, else index into hist */
static char draft[LINE_MAX];

/* --- line editor ------------------------------------------------------- */

static char line[LINE_MAX];
static int len;         /* bytes used */
static int pos;         /* cursor (byte index) */
static int maxdraw;     /* widest redraw so far (for stale-char cleanup) */
static char prompt[300];

static void redraw(void);

/* Kill-ring clipboard for Ctrl+K/U/W (cut) and Ctrl+Y/V (paste). */
static char clip[LINE_MAX];
static int cliplen;

/* Delete line[from..to), stashing the removed bytes in the clipboard. */
static void kill_range(int from, int to)
{
    int n;
    if (from < 0)
        from = 0;
    if (to > len)
        to = len;
    if (from >= to)
        return;
    n = to - from;
    if (n >= (int)sizeof(clip))
        n = (int)sizeof(clip) - 1;
    for (int i = 0; i < n; i++)
        clip[i] = line[from + i];
    clip[n] = '\0';
    cliplen = n;
    n = len - to;
    for (int i = 0; i < n; i++)
        line[from + i] = line[to + i];
    len -= to - from;
    pos = from;
    hnav = -1;
    redraw();
}

/* Display width: continuation bytes (10xxxxxx) don't advance the cell. */
static int dwidth(const char *s, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (((u8)s[i] & 0xC0) != 0x80)
            w++;
    }
    return w;
}

static void redraw(void)
{
    int w = dwidth(prompt, (int)nstrlen(prompt)) + dwidth(line, len);
    nputc('\r');
    nputs(prompt);
    nputsn(line, (u64)len);
    for (int i = w; i < maxdraw; i++)
        nputc(' ');
    if (maxdraw < w)
        maxdraw = w;
    nputc('\r');
    nputs(prompt);
    /* Move from line start to the cursor cell. */
    int target = dwidth(prompt, (int)nstrlen(prompt)) + dwidth(line, pos);
    for (int i = 0; i < target; i++)
        nputs("\x1B[C");
}

static void set_prompt(void)
{
    static char cwd[256];
    nstrcpy(prompt, "root@newos:");
    if (sys_getcwd(cwd, sizeof(cwd)) > 0)
        nstrcat(prompt, cwd);
    else
        nstrcat(prompt, "?");
    nstrcat(prompt, "# ");
}

/* Step one char back over UTF-8 continuation bytes. */
static int prev_char(int p)
{
    if (p <= 0)
        return 0;
    p--;
    while (p > 0 && (((u8)line[p] & 0xC0) == 0x80))
        p--;
    return p;
}

static int next_char(int p)
{
    if (p >= len)
        return len;
    p++;
    while (p < len && (((u8)line[p] & 0xC0) == 0x80))
        p++;
    return p;
}

static void hist_show(int idx)
{
    nstrcpy(line, hist[idx]);
    len = (int)nstrlen(line);
    pos = len;
}

/* Returns 1 when the line is submitted, 0 to keep editing, -1 to exit. */
static int edit_key(char c)
{
    /* Pending ANSI escape state lives in the reader below; here we get
     * fully decoded actions through codes >= 0x80. */
    if (c == '\r' || c == '\n') {
        nputc('\n');
        maxdraw = 0;
        return 1;
    }
    if (c == 0x03) {                    /* Ctrl+C: cancel line */
        nputs("^C\n");
        len = pos = 0;
        hnav = -1;
        maxdraw = 0;
        return 0;
    }
    if (c == 0x04) {                    /* Ctrl+D: exit on empty line */
        if (len == 0) {
            nputs("exit\n");
            return -1;
        }
        return 0;
    }
    if (c == 0x0C) {                    /* Ctrl+L: clear screen */
        for (int i = 0; i < 25; i++)
            nputc('\n');
        nputs("\x1B[2J\x1B[H");
        maxdraw = 0;
        redraw();
        return 0;
    }
    if (c == 0x01) {                    /* Ctrl+A: start of line */
        pos = 0;
        redraw();
        return 0;
    }
    if (c == 0x05) {                    /* Ctrl+E: end of line */
        pos = len;
        redraw();
        return 0;
    }
    if (c == 0x15) {                    /* Ctrl+U: cut whole line */
        kill_range(0, len);
        return 0;
    }
    if (c == 0x0B) {                    /* Ctrl+K: cut to end of line */
        kill_range(pos, len);
        return 0;
    }
    if (c == 0x17) {                    /* Ctrl+W: cut word behind cursor */
        int p = pos;
        while (p > 0 && (line[p - 1] == ' ' || line[p - 1] == '\t'))
            p = prev_char(p);
        while (p > 0 && line[p - 1] != ' ' && line[p - 1] != '\t')
            p = prev_char(p);
        kill_range(p, pos);
        return 0;
    }
    if (c == 0x19 || c == 0x16) {        /* Ctrl+Y / Ctrl+V: paste */
        if (cliplen > 0 && len < (int)sizeof(line) - 1) {
            int n = cliplen;
            if (n > (int)sizeof(line) - 1 - len)
                n = (int)sizeof(line) - 1 - len;
            for (int i = len; i > pos; i--)
                line[i + n - 1] = line[i - 1];
            for (int i = 0; i < n; i++)
                line[pos + i] = clip[i];
            len += n;
            pos += n;
            hnav = -1;
            redraw();
        }
        return 0;
    }
    if (c == 0x7F || c == 0x08) {        /* backspace: erase one char */
        if (pos > 0) {
            int p = prev_char(pos);
            int n = len - pos;
            for (int i = 0; i < n; i++)
                line[p + i] = line[pos + i];
            len -= pos - p;
            pos = p;
            hnav = -1;
            redraw();
        }
        return 0;
    }
    if (c == (char)0x80) {              /* left */
        pos = prev_char(pos);
        redraw();
        return 0;
    }
    if (c == (char)0x81) {              /* right */
        pos = next_char(pos);
        redraw();
        return 0;
    }
    if (c == (char)0x82) {              /* up: older history */
        if (hcount == 0)
            return 0;
        if (hnav == -1) {
            nstrcpy(draft, line);
            hnav = hcount - 1;
        } else if (hnav > 0) {
            hnav--;
        } else {
            return 0;
        }
        hist_show(hnav);
        redraw();
        return 0;
    }
    if (c == (char)0x83) {              /* down: newer history */
        if (hnav == -1)
            return 0;
        if (hnav < hcount - 1) {
            hnav++;
            hist_show(hnav);
        } else {
            hnav = -1;
            nstrcpy(line, draft);
            len = (int)nstrlen(line);
            pos = len;
        }
        redraw();
        return 0;
    }
    if (c == (char)0x84) {              /* home */
        pos = 0;
        redraw();
        return 0;
    }
    if (c == (char)0x85) {              /* end */
        pos = len;
        redraw();
        return 0;
    }
    if (c == (char)0x86) {              /* delete at cursor */
        if (pos < len) {
            int np = next_char(pos);
            int n = len - np;
            for (int i = 0; i < n; i++)
                line[pos + i] = line[np + i];
            len -= np - pos;
            hnav = -1;
            redraw();
        }
        return 0;
    }
    /* Printable byte (ASCII or UTF-8 piece): insert at the cursor. */
    if ((u8)c >= 0x20 && len < (int)sizeof(line) - 1) {
        for (int i = len; i > pos; i--)
            line[i] = line[i - 1];
        line[pos++] = c;
        len++;
        hnav = -1;
        redraw();
        return 0;
    }
    return 0;
}

/* Read one edited line. Returns 0 normally, -1 on Ctrl+D / EOF. */
static int read_line(void)
{
    int esc = 0, csi = 0, csi_num = 0;

    len = pos = 0;
    maxdraw = 0;
    hnav = -1;
    line[0] = '\0';
    set_prompt();
    redraw();

    for (;;) {
        char c;
        if (sys_read(0, &c, 1) != 1)
            return -1;

        if (esc) {
            if (!csi) {
                if (c == '[') {
                    csi = 1;
                    csi_num = 0;
                } else {
                    esc = 0;   /* bare ESC: ignore */
                }
                continue;
            }
            if (c >= '0' && c <= '9') {
                /* CSI input is untrusted; keep the parameter bounded. */
                if (csi_num < 1000)
                    csi_num = csi_num * 10 + (c - '0');
                continue;
            }
            esc = csi = 0;
            if (c == 'A')
                c = (char)0x82;   /* up */
            else if (c == 'B')
                c = (char)0x83;   /* down */
            else if (c == 'C')
                c = (char)0x81;   /* right */
            else if (c == 'D')
                c = (char)0x80;   /* left */
            else if (c == 'H')
                c = (char)0x84;   /* home */
            else if (c == 'F')
                c = (char)0x85;   /* end */
            else if (c == '~') {
                if (csi_num == 3)
                    c = (char)0x86;   /* delete */
                else
                    continue;         /* ins/pgup/pgdn: ignore */
            } else {
                continue;
            }
        } else if (c == 0x1B) {
            esc = 1;
            continue;
        }

        int r = edit_key(c);
        if (r != 0)
            return r;
    }
}

/* --- parsing ----------------------------------------------------------- */

/* Operator characters split into their own tokens when unquoted. */
static int is_op(char c)
{
    return c == '|' || c == '<' || c == '>';
}

/* tok_op[i] = 1 when args[i] came from an unquoted operator, so a quoted
 * "|" stays a plain word. Filled by tokenize(). */
static u8 tok_op[MAX_ARGS + 2];

/* Kind of an operator token: 1 "|", 2 "<", 3 ">", 4 ">>". */
static int op_kind(const char *s)
{
    if (s[0] == '|' && !s[1])
        return 1;
    if (s[0] == '<' && !s[1])
        return 2;
    if (s[0] == '>' && !s[1])
        return 3;
    if (s[0] == '>' && s[1] == '>' && !s[2])
        return 4;
    return 0;
}

static int tokenize(char *ln, char *argv[], int max)
{
    int n = 0;

    while (*ln && n < max) {
        while (*ln == ' ' || *ln == '\t')
            ln++;
        if (!*ln)
            break;
        if (n >= max)
            return -1;
        if (is_op(*ln)) {
            /* Operator tokens live in opsym: `>>` may be glued to its
             * path, and we must never overwrite the byte after them. */
            static char opsym[MAX_ARGS + 2][3];
            char *op = opsym[n];
            op[0] = *ln++;
            op[2] = '\0';
            if (op[0] == '>' && *ln == '>') {
                op[1] = '>';
                ln++;
            } else
                op[1] = '\0';
            tok_op[n] = 1;
            argv[n++] = op;
            continue;
        }
        char *word = ln;
        char *dst = ln, *src = ln;
        char quote = 0;
        while (*src) {
            if (quote) {
                if (*src == quote) {
                    quote = 0;
                    src++;
                } else {
                    *dst++ = *src++;
                }
            } else if (*src == '\'' || *src == '"') {
                quote = *src++;
            } else if (*src == '\\' && src[1]) {
                src++;
                *dst++ = *src++;
            } else if (*src == ' ' || *src == '\t') {
                break;
            } else if (is_op(*src)) {
                break;             /* word stops at an operator token */
            } else {
                *dst++ = *src++;
            }
        }
        if (quote)
            return -1;             /* unmatched single/double quote */
        if (dst < src)
            *dst = '\0';
        else if (*src == ' ' || *src == '\t')
            *src++ = '\0';
        /* Leave an operator in place for the next token iteration. */
        tok_op[n] = 0;
        argv[n++] = word;
        ln = src;
    }
    return n;
}

/* --- builtins ---------------------------------------------------------- */

static int builtin_cd(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/";
    if (argc > 2) {
        nputs("cd: too many arguments\n");
        return 1;
    }
    if (sys_chdir(dir) != 0) {
        putf("cd: %s: no such directory\n", dir);
        return 1;
    }
    return 0;
}

static int builtin_history(void)
{
    for (int i = 0; i < hcount; i++)
        putf("%d  %s\n", (u64)(i + 1), hist[i]);
    return 0;
}

static int builtin_help(void)
{
    nputs("nsh - NEWOS 0.3.0-gui shell\n");
    nputs("builtins: cd, exit, help, history, clear\n");
    nputs("/bin: ls cat echo mkdir rmdir rm mv touch pwd clear uname\n");
    nputs("       hostname whoami id date uptime ps kill sleep free\n");
    nputs("       df wc head grep find sort hello img vid desktop\n");
    nputs("       about newpkg newfetch kilo nano ltest\n");
    nputs("GNU tools install under /usr/bin; use /usr/bin/<tool>\n");
    nputs("Images: img /etc/splash.bmp  |  img /etc/test.ppm\n");
    nputs("nano/kilo: terminal text editor (Ctrl+S saves, Ctrl+Q quits)\n");
    nputs("pipes: cmd1 | cmd2    redirect: cmd > f, cmd >> f, cmd < f\n");
    nputs("boot: init=<path> gui=0/1 headless=1 test_mode=1\n");
    nputs("keys: Ctrl+A/E line ends, Ctrl+U/W/K cut, Ctrl+Y/V paste,\n");
    nputs("      Ctrl+C cancel, Ctrl+D exit, Ctrl+L clear\n");
    return 0;
}

static int builtin_clear(void)
{
    for (int i = 0; i < 25; i++)
        nputc('\n');
    nputs("\x1B[2J\x1B[H");
    return 0;
}

/* --- main loop --------------------------------------------------------- */

static void hist_push(const char *ln)
{
    if (!*ln)
        return;
    if (hcount > 0 && nstrcmp(hist[hcount - 1], ln) == 0)
        return;   /* skip repeats */
    if (hcount < HIST_MAX) {
        nstrcpy(hist[hcount++], ln);
    } else {
        for (int i = 1; i < HIST_MAX; i++)
            nstrcpy(hist[i - 1], hist[i]);
        nstrcpy(hist[HIST_MAX - 1], ln);
    }
}

static void show_motd(void)
{
    long fd = sys_open("/etc/motd", O_READ);
    if (fd < 0)
        return;
    char buf[256];
    long n;
    while ((n = sys_read((int)fd, buf, sizeof(buf))) > 0)
        sys_write(1, buf, (u64)n);
    sys_close((int)fd);
}

static int is_builtin_name(const char *s)
{
    return nstrcmp(s, "cd") == 0 || nstrcmp(s, "exit") == 0 ||
           nstrcmp(s, "help") == 0 || nstrcmp(s, "history") == 0 ||
           nstrcmp(s, "clear") == 0;
}

/* Spawn an executable path while passing the original command argv. */
static long spawn_path(const char *path, char **argv, int argc,
                       int fdin, int fdout)
{
    if (fdin < 0 && fdout < 0)
        return sys_spawn2(path, argv, argc);
    return sys_spawn3(path, argv, argc, fdin, fdout);
}

/* Bare commands search native tools first, then installed GNU tools. */
static long spawn_bin(char **argv, int argc, int fdin, int fdout)
{
    char path[256];
    const char *name = argv[0];
    long pid;

    for (const char *p = name; *p; p++) {
        if (*p == '/')
            return spawn_path(name, argv, argc, fdin, fdout);
    }
    if (nstrlen(name) > sizeof(path) - 10) {
        putf("nsh: command too long\n");
        return -1;
    }
    nstrcpy(path, "/bin/");
    nstrcat(path, name);
    pid = spawn_path(path, argv, argc, fdin, fdout);
    if (pid >= 0)
        return pid;
    nstrcpy(path, "/usr/bin/");
    nstrcat(path, name);
    pid = spawn_path(path, argv, argc, fdin, fdout);
    if (pid < 0)
        putf("nsh: %s: command not found\n", name);
    return pid;
}

static int wait_rc(long pid)
{
    long rc = sys_waitpid((pid_t)pid);

    if (rc == 128 + 9)
        putf("^C\n");                       /* killed via the waitpid Ctrl+C path */
    else if (rc != 0)
        putf("[exit %d]\n", rc);
    return (int)rc;
}

static int run_external(char **argv, int argc)
{
    long pid = spawn_bin(argv, argc, -1, -1);

    if (pid < 0)
        return 127;
    return wait_rc(pid);
}

/* Run `args` when it contains operators: parse stages + redirections,
 * wire pipes, spawn every stage, then reap. */
static void run_pipeline(char *args[], int n)
{
    static char *st_av[MAX_STAGES][MAX_ARGS + 1];
    int st_n[MAX_STAGES];
    long pid[MAX_STAGES];
    int pfd[MAX_STAGES][2];
    const char *in_path = 0, *out_path = 0;
    int append = 0, in_stage = -1, out_stage = -1;
    int ns = 0, i, nstages, in_fd = -1, out_fd = -1;

    for (i = 0; i < MAX_STAGES; i++)
        pfd[i][0] = pfd[i][1] = -1;
    st_n[0] = 0;
    for (i = 0; i < n; i++) {
        int k = tok_op[i] ? op_kind(args[i]) : 0;
        if (!k) {
            if (st_n[ns] == MAX_ARGS) {
                putf("nsh: too many arguments (maximum %d)\n", MAX_ARGS);
                return;
            }
            st_av[ns][st_n[ns]++] = args[i];
            continue;
        }
        if (k == 1) {
            if (st_n[ns] == 0 || ns == MAX_STAGES - 1) {
                putf("nsh: syntax error near '|'\n");
                return;
            }
            ns++;
            st_n[ns] = 0;
            continue;
        }
        if (i + 1 >= n || tok_op[i + 1]) {
            putf("nsh: %s: missing file operand\n", args[i]);
            return;
        }
        if (k == 2) {
            if (in_path) {
                putf("nsh: only one '<' allowed\n");
                return;
            }
            in_path = args[++i];
            in_stage = ns;
        } else {
            out_path = args[++i];
            append = (k == 4);
            out_stage = ns;
        }
    }
    if (st_n[ns] == 0) {
        putf("nsh: syntax error: dangling operator\n");
        return;
    }
    nstages = ns + 1;
    if (in_stage != -1 && in_stage != 0) {
        putf("nsh: '<' must precede the first command\n");
        return;
    }
    if (out_stage != -1 && out_stage != ns) {
        putf("nsh: '>' must follow the last command\n");
        return;
    }
    for (i = 0; i < nstages; i++) {
        st_av[i][st_n[i]] = 0;
        if (nstages > 1 && is_builtin_name(st_av[i][0])) {
            putf("nsh: %s: builtin cannot run in a pipeline\n",
                 st_av[i][0]);
            return;
        }
    }

    if (in_path) {
        in_fd = (int)sys_open(in_path, O_READ);
        if (in_fd < 0) {
            putf("nsh: %s: cannot open\n", in_path);
            return;
        }
    }
    if (out_path) {
        out_fd = (int)sys_open(out_path, O_WRITE | O_CREATE | O_TRUNC);
        if (out_fd < 0) {
            putf("nsh: %s: cannot open for writing\n", out_path);
            if (in_fd >= 0)
                sys_close(in_fd);
            return;
        }
        if (append)
            sys_lseek(out_fd, 0, SEEK_END);
    }
    for (i = 1; i < nstages; i++) {
        if (sys_pipe(pfd[i - 1]) < 0) {
            putf("nsh: out of pipes\n");
            goto fail;
        }
    }
    for (i = 0; i < nstages; i++) {
        int fdin = (i == 0) ? in_fd : pfd[i - 1][0];
        int fdout = (i == nstages - 1) ? out_fd : pfd[i][1];

        pid[i] = spawn_bin(st_av[i], st_n[i], fdin, fdout);
        if (pid[i] < 0) {
            /* Release the parent's pipe references before waiting. A child
             * may be blocked writing to a pipe whose reader was never
             * spawned; waiting first would deadlock the shell. */
            for (int j = 1; j < nstages; j++) {
                if (pfd[j - 1][0] >= 0) {
                    sys_close(pfd[j - 1][0]);
                    pfd[j - 1][0] = -1;
                }
                if (pfd[j - 1][1] >= 0) {
                    sys_close(pfd[j - 1][1]);
                    pfd[j - 1][1] = -1;
                }
            }
            if (in_fd >= 0) {
                sys_close(in_fd);
                in_fd = -1;
            }
            if (out_fd >= 0) {
                sys_close(out_fd);
                out_fd = -1;
            }
            while (--i >= 0)
                sys_waitpid((pid_t)pid[i]);
            goto fail;
        }
    }
    /* Our copies of the pipe ends must go: a held write end would stop
     * downstream EOF forever. */
    for (i = 1; i < nstages; i++) {
        sys_close(pfd[i - 1][0]);
        sys_close(pfd[i - 1][1]);
    }
    if (in_fd >= 0)
        sys_close(in_fd);
    if (out_fd >= 0)
        sys_close(out_fd);
    for (i = 0; i < nstages; i++)
        wait_rc(pid[i]);
    return;

fail:
    if (in_fd >= 0)
        sys_close(in_fd);
    if (out_fd >= 0)
        sys_close(out_fd);
    for (i = 1; i < nstages; i++) {
        if (pfd[i - 1][0] >= 0)
            sys_close(pfd[i - 1][0]);
        if (pfd[i - 1][1] >= 0)
            sys_close(pfd[i - 1][1]);
    }
}

/* One command line: builtins run here, everything else is spawned. Shared
 * by the interactive loop and the boot script (see run_script). */
static void exec_line(char *ln)
{
    static char *args[MAX_ARGS + 1];
    int n = tokenize(ln, args, MAX_ARGS);

    if (n < 0) {
        nputs("nsh: syntax error: unmatched quote or too many tokens\n");
        return;
    }
    if (n == 0)
        return;
    args[n] = 0;

    int ops = 0;
    for (int i = 0; i < n; i++)
        if (tok_op[i])
            ops = 1;
    if (ops) {
        run_pipeline(args, n);
    } else if (nstrcmp(args[0], "exit") == 0) {
        nputs("Bye.\n");
        sys_exit(0);
    } else if (nstrcmp(args[0], "cd") == 0) {
        builtin_cd(n, args);
    } else if (nstrcmp(args[0], "help") == 0) {
        builtin_help();
    } else if (nstrcmp(args[0], "history") == 0) {
        builtin_history();
    } else if (nstrcmp(args[0], "clear") == 0) {
        builtin_clear();
    } else {
        run_external(args, n);
    }
}

/* Execute a file of commands. Returns 0 normally, -1 when `path` is absent
 * (not an error: an image without a script just boots interactively),
 * -2 on a syntax-free read failure. */
static int run_script(const char *path)
{
    static char buf[512];
    static char ln[256];
    long fd = sys_open(path, O_READ);
    long n;
    size_t pos = 0, have = 0, used = 0;
    int status = 0;
    int overflow = 0;

    if (fd < 0)
        return -1;

    for (;;) {
        if (used == have) {
            n = sys_read((int)fd, buf, sizeof(buf));
            if (n < 0) {
                status = -2;
                break;
            }
            if (n == 0)
                break;
            have = (size_t)n;
            used = 0;
        }
        while (used < have) {
            char c = buf[used++];
            if (c == '\n') {
                if (overflow) {
                    nputs("nsh: script line too long; skipped\n");
                } else {
                    ln[pos] = '\0';
                    if (pos && ln[0] != '#')
                        exec_line(ln);
                }
                pos = 0;
                overflow = 0;
                continue;
            }
            if (c == '\r')
                continue;
            if (pos < sizeof(ln) - 1)
                ln[pos++] = c;
            else
                overflow = 1;
        }
    }
    if (overflow) {
        nputs("nsh: script line too long; skipped\n");
    } else if (pos) {
        ln[pos] = '\0';
        if (ln[0] != '#')
            exec_line(ln);
    }
    sys_close((int)fd);
    return status;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* A boot script configures the session and stays out of the way: end it
     * with `exit` to hand control back to the kernel (what a non-interactive
     * CI run wants), or let it fall through to the prompt. */
    if (run_script("/nsh.rc") < 0)
        run_script("/etc/nsh.rc");

    show_motd();
    nputs("Type 'help' for commands.\n");

    for (;;) {
        int r = read_line();
        if (r < 0)
            sys_exit(0);
        line[len] = '\0';
        hist_push(line);
        exec_line(line);
    }
}
