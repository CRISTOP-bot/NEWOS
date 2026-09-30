#include <syscall/syscall.h>
#include <process/proc_process.h>
#include <process/proc_thread.h>
#include <process/sched.h>
#include <ipc/ipc.h>
#include <mm/mm_usercopy.h>
#include <mm/mm_vmm.h>
#include <mm/mm_pmm.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <core/core_printk.h>
#include <core/core_console.h>
#include <core/core_time.h>
#include <abi/syscall_abi.h>
#include <iru_string.h>
#include <x86_cpu.h>

/* Minimal socket address structure (IPv4 only) */
struct socket_addr {
    int family;
    char data[16];
};

/* Socket states */
#define SOCKET_CLOSED    0
#define SOCKET_BOUND     1
#define SOCKET_LISTEN    2
#define SOCKET_CONNECTED 3
#define SOCKET_ESTABLISHED 4

/* Address families */
#define AF_INET          2

/* Socket types */
#define SOCK_STREAM      1
#define SOCK_DGRAM       2

/* POSIX types for syscall ABI */
typedef int socklen_t;

/* Resolve a user path against the caller's cwd into `out` (512 bytes).
 * Absolute paths pass through; relative ones join cwd + '/' + path. */
static int make_abs(struct process *p, const char *in, char *out)
{
    if (!p || !in || !*in)
        return -1;
    if (in[0] == '/') {
        if (strlen(in) >= 512)
            return -1;
        strcpy(out, in);
        return 0;
    }
    size_t cl = strlen(p->cwd);
    size_t il = strlen(in);
    if (cl + 1 + il >= 512)
        return -1;
    memcpy(out, p->cwd, cl);
    size_t n = cl;
    if (n == 0 || out[n - 1] != '/')
        out[n++] = '/';
    memcpy(out + n, in, il + 1);
    return 0;
}

/* Copy a NUL-terminated user string, bounded by `cap`. Returns 0 with
 * `out` filled, -1 on any fault or missing terminator.
 *
 * Copies byte-by-byte so validation never runs past the string: the argv
 * block ends up to 15 padding bytes below USER_STACK_TOP, so a fixed
 * 16-byte chunk would over-read a short string (e.g. "/dev", 5 bytes)
 * into unmapped memory above the stack and fail validation even though
 * the string itself is fully mapped. */
static int copy_user_string(char *out, const void *user_ptr, size_t cap)
{
    size_t done = 0;

    if (cap == 0)
        return -1;
    while (done + 1 < cap) {
        u8 c;
        if (copy_from_user(&c, (const u8 *)user_ptr + done, 1) != 0)
            return -1;
        out[done] = (char)c;
        if (c == '\0')
            return 0;
        done++;
    }
    out[cap - 1] = '\0';
    return -1;
}

/* THE int $0x80 dispatcher.
 *
 * Register conventions (see uapi/syscall.h):
 *   rax = number, rdi/rsi/rdx/r10/r8/r9 = args
 *   return value -> rax via the shared isr frame. */

/* A pipe fd stores a refcounted struct ipc_pipe_fd in its data slot; the
 * magic field distinguishes it from sockets (whose data starts with a
 * small fd number) and plain files. */
static struct ipc_pipe_fd *pipe_of(struct process *p, int fd)
{
    struct ipc_pipe_fd *pf;

    if (fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file)
        return NULL;
    pf = (struct ipc_pipe_fd *)p->fds[fd].data;
    if (!pf || pf->magic != IPC_PIPE_FD_MAGIC)
        return NULL;
    return pf;
}

/* Clone one parent fd into a child slot (fresh file so offsets stay
 * independent; pipe ends share the pipe and bump its refcount). */
static int fd_inherit(struct process *parent, int pfd,
                      struct process *child, int cfd)
{
    struct fd_entry *se;
    struct vfs_file *nf;
    struct ipc_pipe_fd *spf, *cpf;

    if (pfd < 0 || pfd >= PROCESS_MAX_FDS ||
        cfd < 0 || cfd >= PROCESS_MAX_FDS)
        return -1;
    se = &parent->fds[pfd];
    if (!se->file)
        return -1;

    if (child->fds[cfd].data)
        ipc_pipe_fd_close((struct ipc_pipe_fd *)child->fds[cfd].data);
    if (child->fds[cfd].file)
        vfs_close(child->fds[cfd].file);
    child->fds[cfd].file = NULL;
    child->fds[cfd].data = NULL;

    nf = kmalloc(sizeof(*nf));
    if (!nf)
        return -1;
    memcpy(nf, se->file, sizeof(*nf));
    spf = pipe_of(parent, pfd);
    if (spf) {
        cpf = ipc_pipe_fd_new(spf->pipe, spf->is_reader);
        if (!cpf) {
            kfree(nf);
            return -1;
        }
        child->fds[cfd].data = cpf;
    }
    child->fds[cfd].file = nf;
    return 0;
}

/* SYS_PS collection state (single CPU: no races with the tick reader). */
static struct nsh_proc *s_ps_out;
static size_t s_ps_cap;
static size_t s_ps_count;

static void ps_fill(struct process *q, void *arg)
{
    (void)arg;
    if (s_ps_count >= s_ps_cap)
        return;
    struct nsh_proc *e = &s_ps_out[s_ps_count++];
    e->pid = (nsh_s64)q->pid;
    e->ppid = (nsh_s64)q->ppid;
    e->state = (nsh_s64)q->state;
    memset(e->name, 0, sizeof(e->name));
    strncpy(e->name, q->name, sizeof(e->name) - 1);
}

/* Terminate `want` with SIGINT semantics (exit code 128+9). Shared by
 * SYS_KILL and the Ctrl+C path of SYS_WAITPID. Returns 0 on success. */
static int proc_kill(pid_t want, struct process *self)
{
    if (want <= 1)
        return -1;                         /* pid 0/1: init is sacred */
    struct process *t = sched_find_process(want);
    if (!t || t->state == PROCESS_STATE_EXITED)
        return -1;
    if (t == self)
        process_exit(self, PROCESS_EXIT_SIGNAL_BASE + 9);  /* noreturn */
    sched_remove_runnable(t);
    t->exit_code = PROCESS_EXIT_SIGNAL_BASE + 9;
    t->state = PROCESS_STATE_EXITED;
    if (sched_zombify(t) != 0)
        process_free(t);                   /* not running: safe */
    return 0;
}

long syscall_dispatch(struct x64_iframe *f)
{
    u64 nr = f->rax;
    struct process *p = process_current();
    long ret = SYSCALL_RET_ERROR;

    switch (nr) {
    case SYS_WRITE: {
        int fd = (int)f->rdi;
        u64 len = f->rdx;

        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file)
            break;

        /* Bounded static bounce buffer: the whole write payload is copied
         * through this single scratch page. */
        static u8 kbuf[512];
        if (len > sizeof(kbuf))
            len = sizeof(kbuf);
        if (copy_from_user(kbuf, (const void *)(uintptr_t)f->rsi, len) != 0)
            break;

        {
            struct ipc_pipe_fd *pf = pipe_of(p, fd);
            if (pf) {
                for (;;) {
                    if (pf->pipe->reader_count == 0)
                        break;      /* no readers: error */
                    /* All-or-nothing: a re-trapped write must never
                     * duplicate bytes, so only commit when the whole
                     * payload fits. */
                    if (pf->pipe->len + len <= IPC_PIPE_CAPACITY) {
                        ret = (long)ipc_pipe_write(pf->pipe, kbuf,
                                                   (size_t)len);
                        break;
                    }
                    if (sched_yield(f))
                        return 0;
                    core_time_delay_us(250);
                }
                break;
            }
        }

        if (p->fds[fd].file->uflags & NSH_O_APPEND) {
            if (p->fds[fd].file->inode)
                p->fds[fd].file->offset = p->fds[fd].file->inode->size;
        }
        int n = vfs_write(p->fds[fd].file, kbuf, len);
        if (n < 0)
            break;
        ret = n;
        break;
    }

    case SYS_GETPID:        if (p)
            ret = (long)p->pid;
        break;

    case SYS_READ: {
        int fd = (int)f->rdi;
        u64 len = f->rdx;

        if (!p || len == 0)
            break;
        if (fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file)
            break;

        {
            struct ipc_pipe_fd *pf = pipe_of(p, fd);
            if (pf) {
                static u8 pbuf[512];
                if (len > sizeof(pbuf))
                    len = sizeof(pbuf);
                for (;;) {
                    size_t n = ipc_pipe_read(pf->pipe, pbuf, (size_t)len);
                    if (n > 0) {
                        if (copy_to_user(
                                (void *)(uintptr_t)f->rsi, pbuf, n) != 0)
                            break;
                        ret = (long)n;
                        break;
                    }
                    if (pf->pipe->writer_count == 0) {
                        ret = 0;          /* all writers gone: EOF */
                        break;
                    }
                    if (sched_yield(f))
                        return 0;
                    core_time_delay_us(250);
                }
                break;
            }
        }

        /* /dev/serial on stdin keeps the blocking console semantics the
         * shell relies on (a byte is always delivered); a redirected
         * stdin is an ordinary file and reads through VFS like any fd. */
        if (fd == STDIN_FILENO && devfs_is_serial(p->fds[fd].file)) {
            static u8 kbuf[512];
            if (len > sizeof(kbuf))
                len = sizeof(kbuf);

            while (core_console_rx_available() == 0) {
                /* Idle in favor of runnable peers while waiting for input.
                 * A published handoff returns straight to the epilogue:
                 * resume re-traps from the parked frame, so spinning on
                 * here with a stale `current` would corrupt the peer's
                 * slot (double-rewind). */
                if (sched_yield(f))
                    return 0;
                core_time_delay_us(250);
            }

            int n = 0;
            while (n < (int)len && core_console_rx_available() > 0)
                kbuf[n++] = (u8)core_console_rx_pop();

            if (copy_to_user((void *)(uintptr_t)f->rsi, kbuf, (size_t)n) != 0)
                break;
            ret = n;
            break;
        }

        static u8 kbuf[512];
        if (len > sizeof(kbuf))
            len = sizeof(kbuf);
        int n = vfs_read(p->fds[fd].file, kbuf, (size_t)len);
        if (n < 0)
            break;
        if (n > 0 && copy_to_user((void *)(uintptr_t)f->rsi, kbuf,
                                  (size_t)n) != 0)
            break;
        ret = n;
        break;
    }

    case SYS_OPEN: {
        if (!p)
            break;

        /* Userland (and unmodified Linux binaries) pass the Linux x86_64
         * O_* flags; translate them to the VFS read/write/create model. */
        int lflags = (int)f->rsi;
        int flags = 0;
        int acc = lflags & NSH_O_ACCMODE;

        if (acc == NSH_O_RDONLY)
            flags = VFS_O_READ;
        else if (acc == NSH_O_WRONLY)
            flags = VFS_O_WRITE;
        else if (acc == NSH_O_RDWR)
            flags = VFS_O_READ | VFS_O_WRITE;
        else
            break;      /* accmode 3 is reserved by the Linux ABI */
        if (lflags & NSH_O_CREAT)
            flags |= VFS_O_CREATE;

        static char path[512];
        if (copy_user_string(path, (const void *)(uintptr_t)f->rdi,
                             sizeof(path)) != 0)
            break;
        /* The user buffer must hold a NUL-terminated path. */
        if (path[0] == '\0')
            break;

        /* Resolve against the caller's cwd before touching the VFS. */
        static char abs[512];
        if (make_abs(p, path, abs) != 0)
            break;

        int fd = -1;
        for (int i = 0; i < PROCESS_MAX_FDS; i++) {
            if (!p->fds[i].file) {
                fd = i;
                break;
            }
        }
        if (fd < 0)
            break;

        struct vfs_file *file = vfs_open(abs, flags);
        if (!file)
            break;
        file->uflags = (u32)lflags;
        if ((lflags & NSH_O_TRUNC) && file->inode && file->inode->ops &&
            file->inode->ops->truncate)
            file->inode->ops->truncate(file->inode, 0);
        if ((lflags & NSH_O_APPEND) && file->inode)
            file->offset = file->inode->size;
        p->fds[fd].file = file;
        ret = fd;
        break;
    }

    case SYS_CLOSE: {
        int fd = (int)f->rdi;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file)
            break;
        if (p->fds[fd].data)
            ipc_pipe_fd_close((struct ipc_pipe_fd *)p->fds[fd].data);
        p->fds[fd].data = NULL;
        vfs_close(p->fds[fd].file);
        p->fds[fd].file = NULL;
        ret = 0;
        break;
    }

    case SYS_PIPE: {
        /* rdi = int fds[2]: [0] read end, [1] write end. */
        if (!p)
            break;
        int out[2] = { -1, -1 };
        struct ipc_pipe *pipe = ipc_pipe_new();
        struct ipc_pipe_fd *rf = NULL, *wf = NULL;
        struct vfs_file *rfile, *wfile;

        if (!pipe)
            break;
        rf = ipc_pipe_fd_new(pipe, 1);
        wf = ipc_pipe_fd_new(pipe, 0);
        rfile = kzalloc(sizeof(*rfile));
        wfile = kzalloc(sizeof(*wfile));
        if (!rf || !wf || !rfile || !wfile)
            goto pipe_fail;
        for (int i = 0; i < PROCESS_MAX_FDS; i++) {
            if (!p->fds[i].file) {
                if (out[0] < 0)
                    out[0] = i;
                else {
                    out[1] = i;
                    break;
                }
            }
        }
        if (out[1] < 0)
            goto pipe_fail;
        rfile->flags = VFS_O_READ;
        wfile->flags = VFS_O_WRITE;
        p->fds[out[0]].file = rfile;
        p->fds[out[0]].data = rf;
        p->fds[out[1]].file = wfile;
        p->fds[out[1]].data = wf;
        if (copy_to_user((void *)(uintptr_t)f->rdi, out,
                         sizeof(out)) != 0) {
            p->fds[out[0]].file = NULL;
            p->fds[out[0]].data = NULL;
            p->fds[out[1]].file = NULL;
            p->fds[out[1]].data = NULL;
            vfs_close(rfile);
            vfs_close(wfile);
            goto pipe_fail;
        }
        ret = 0;
        break;
    pipe_fail:
        if (rf)
            ipc_pipe_fd_close(rf);
        if (wf)
            ipc_pipe_fd_close(wf);
        if (rfile)
            kfree(rfile);
        if (wfile)
            kfree(wfile);
        /* pipe itself: both counts already dropped by fd_close */
        break;
    }

    case SYS_SLEEP: {
        /* Linux nanosleep: rdi = struct nsh_timespec *, rsi = rem. */
        u64 ms = 0;
        struct nsh_timespec ts;
        if (!p)
            break;
        if (f->rdi) {
            if (copy_from_user(&ts, (const void *)(uintptr_t)f->rdi,
                               sizeof(ts)) != 0)
                break;
            if (ts.tv_sec < 0 || ts.tv_nsec < 0)
                break;
            ms = (u64)ts.tv_sec * 1000 + (u64)ts.tv_nsec / 1000000;
        }
        /* The deadline must survive re-traps: every resume restarts this
         * syscall from scratch, so recomputing `due` per invocation would
         * push it forward each time and sleep forever while a peer stays
         * runnable. */
        if (!p->thread.sleeping) {
            p->thread.sleep_due = core_time_ticks() +
                                  (ms * core_time_hz()) / (u64)1000;
            p->thread.sleeping = 1;
        }
        while ((s64)(core_time_ticks() - p->thread.sleep_due) < 0) {
            /* Don't spin hot while others are runnable; re-check on resume. */
            if (sched_yield(f))
                return 0;
            core_time_delay_us(250);
        }
        p->thread.sleeping = 0;
        ret = 0;
        break;
    }

    case SYS_SPAWN: {
        if (!p)
            break;

        static char path[64];
        memset(path, 0, sizeof(path));
        if (copy_from_user(path, (const void *)(uintptr_t)f->rdi,
                           sizeof(path) - 1) != 0)
            break;

        static char name[16];
        memset(name, 0, sizeof(name));
        if (f->rsi &&
            copy_from_user(name, (const void *)(uintptr_t)f->rsi,
                           sizeof(name) - 1) != 0)
            break;

        struct process *child =
            process_create_from_vfs(path, f->rsi ? name : path);
        if (!child)
            break;
        child->ppid = p->pid;
        memcpy(child->cwd, p->cwd, sizeof(child->cwd));
        sched_add_process(child);
        ret = (long)child->pid;
        break;
    }

    case SYS_SPAWN2:
    case SYS_SPAWN3: {
        /* Spawn with real argv: rdi = path, rsi = char **argv,
         * rdx = argc. The child enters with SysV rdi/rsi.
         * SPAWN3 additionally wires r10 = parent fd into the child's
         * stdin and r8 = parent fd into its stdout (-1 keeps the
         * default serial console). */
        if (!p)
            break;

        static char path2[512];
        if (copy_user_string(path2, (const void *)(uintptr_t)f->rdi,
                             sizeof(path2)) != 0)
            break;
        if (path2[0] == '\0')
            break;

        u64 argc64 = f->rdx;
        if (argc64 > NSH_MAX_ARGS)
            break;
        int argc = (int)argc64;

        static nsh_u64 uptrs[NSH_MAX_ARGS];
        if (argc > 0 &&
            copy_from_user(uptrs, (const void *)(uintptr_t)f->rsi,
                           (size_t)argc * sizeof(uptrs[0])) != 0)
            break;

        static char args[NSH_MAX_ARGS][NSH_MAX_ARGLEN];
        int bad = 0;
        for (int i = 0; i < argc && !bad; i++) {
            if (copy_user_string(args[i],
                                 (const void *)(uintptr_t)uptrs[i],
                                 sizeof(args[i])) != 0)
                bad = 1;
        }
        if (bad)
            break;

        static char abs2[512];
        if (make_abs(p, path2, abs2) != 0)
            break;

        const char *base = strrchr(abs2, '/');
        base = (base && base[1]) ? base + 1 : abs2;

        struct process *child =
            process_create_args(abs2, base, argc, args);
        if (!child)
            break;
        child->ppid = p->pid;
        memcpy(child->cwd, p->cwd, sizeof(child->cwd));
        if (nr == SYS_SPAWN3) {
            fd_inherit(p, (int)f->r10, child, STDIN_FILENO);
            fd_inherit(p, (int)f->r8, child, STDOUT_FILENO);
        }
        sched_add_process(child);
        ret = (long)child->pid;
        break;
    }

    case SYS_WAITPID: {
        if (!p)
            break;
        pid_t want = (pid_t)f->rdi;

        while (1) {
            struct process *z = sched_zombie_find(want);
            if (z) {
                int code = z->exit_code;
                pid_t zpid = z->pid;
                sched_zombie_reclaim(z);
                if (f->rsi) {
                    /* Linux wait4 semantics: return the pid and write a
                     * status word encodable with WIFEXITED/WEXITSTATUS. */
                    int status;
                    if (code >= PROCESS_EXIT_SIGNAL_BASE)
                        status = code - PROCESS_EXIT_SIGNAL_BASE;
                    else
                        status = (code & 0xff) << 8;
                    if (copy_to_user((void *)(uintptr_t)f->rsi, &status,
                                     sizeof(status)) != 0)
                        break;
                    ret = (long)zpid;
                } else {
                    ret = (long)code;    /* legacy: bare exit code */
                }
                break;
            }
            /* Unknown or already-reaped pid: fail instead of hanging
             * forever (previously a nonexistent pid wedged the shell). */
            if (!sched_find_process(want))
                break;
            /* Ctrl+C while waiting kills the child (128+9), then we keep
             * waiting until it lands as a zombie so the caller sees the
             * signal exit code. The byte is consumed here so the shell's
             * next read_line does not also receive it. */
            if (core_console_rx_peek() == 0x03) {
                (void)core_console_rx_pop();
                proc_kill(want, p);
                continue;
            }
            /* Let a runnable child use the CPU; re-poll on resume. A
             * published handoff returns straight to the epilogue (see
             * SYS_READ): spinning on here would double-rewind `rip`. */
            if (sched_yield(f))
                return 0;
            core_time_delay_us(250);
        }
        break;
    }

    case SYS_MKDIR: {
        if (!p)
            break;
        static char path[512];
        if (copy_user_string(path, (const void *)(uintptr_t)f->rdi,
                             sizeof(path)) != 0)
            break;
        if (path[0] == '\0')
            break;
        static char abs[512];
        if (make_abs(p, path, abs) != 0)
            break;
        ret = (vfs_mkdir(abs) == 0) ? 0 : SYSCALL_RET_ERROR;
        break;
    }

    case SYS_UNLINK: {
        if (!p)
            break;
        static char path[512];
        if (copy_user_string(path, (const void *)(uintptr_t)f->rdi,
                             sizeof(path)) != 0)
            break;
        if (path[0] == '\0')
            break;
        static char abs[512];
        if (make_abs(p, path, abs) != 0)
            break;
        ret = (vfs_unlink(abs) == 0) ? 0 : SYSCALL_RET_ERROR;
        break;
    }

    case SYS_RENAME: {
        /* rdi = oldpath, rsi = newpath. Replaces an existing target. */
        if (!p)
            break;
        static char oname[512], nname[512];
        if (copy_user_string(oname, (const void *)(uintptr_t)f->rdi,
                             sizeof(oname)) != 0)
            break;
        if (copy_user_string(nname, (const void *)(uintptr_t)f->rsi,
                             sizeof(nname)) != 0)
            break;
        if (oname[0] == '\0' || nname[0] == '\0')
            break;
        static char oabs[512], nabs[512];
        if (make_abs(p, oname, oabs) != 0)
            break;
        if (make_abs(p, nname, nabs) != 0)
            break;
        ret = (vfs_rename(oabs, nabs) == 0) ? 0 : SYSCALL_RET_ERROR;
        break;
    }

    case SYS_READDIR: {
        /* rdi = path, rsi = struct nsh_dirent *buf, rdx = buf bytes.
         * Returns the child count (capped by the buffer). */
        if (!p)
            break;
        static char path[512];
        if (copy_user_string(path, (const void *)(uintptr_t)f->rdi,
                             sizeof(path)) != 0)
            break;
        static char abs[512];
        if (make_abs(p, (*path ? path : "/"), abs) != 0)
            break;
        struct vfs_inode *dir = vfs_lookup(abs);
        if (!dir || !(dir->mode & VFS_MODE_DIR))
            break;
        u64 cap = f->rdx / sizeof(struct nsh_dirent);
        static struct nsh_dirent dents[64];
        size_t count = 0;
        struct list_node *node;
        LIST_FOR_EACH(node, &dir->children) {
            if ((u64)count >= cap || count >= 64)
                break;
            struct vfs_inode *in =
                LIST_NODE_ENTRY(node, struct vfs_inode, chain);
            memset(&dents[count], 0, sizeof(dents[count]));
            strncpy(dents[count].name, in->name,
                    sizeof(dents[count].name) - 1);
            dents[count].mode = in->mode;
            dents[count].size = in->size;
            count++;
        }
        if (count &&
            copy_to_user((void *)(uintptr_t)f->rsi, dents,
                         count * sizeof(dents[0])) != 0)
            break;
        ret = (long)count;
        break;
    }

    case SYS_CHDIR: {
        if (!p)
            break;
        static char path[512];
        if (copy_user_string(path, (const void *)(uintptr_t)f->rdi,
                             sizeof(path)) != 0)
            break;
        if (path[0] == '\0')
            break;
        static char abs[512];
        if (make_abs(p, path, abs) != 0)
            break;
        struct vfs_inode *dir = vfs_lookup(abs);
        if (!dir || !(dir->mode & VFS_MODE_DIR))
            break;
        if (strlen(abs) >= sizeof(p->cwd))
            break;
        strcpy(p->cwd, abs);
        ret = 0;
        break;
    }

    case SYS_GETCWD: {
        if (!p)
            break;
        size_t len = strlen(p->cwd) + 1;
        if (len > f->rsi)
            break;
        if (copy_to_user((void *)(uintptr_t)f->rdi, p->cwd, len) != 0)
            break;
        ret = (long)len;
        break;
    }

    case SYS_PS: {
        /* rsi = struct nsh_proc *buf, rdx = buffer bytes. */
        if (!p)
            break;
        u64 cap = f->rdx / sizeof(struct nsh_proc);
        static struct nsh_proc procs[32];
        s_ps_count = 0;
        s_ps_out = procs;
        s_ps_cap = (cap > 32) ? 32 : (size_t)cap;
        sched_visit(ps_fill, NULL);
        if (s_ps_count &&
            copy_to_user((void *)(uintptr_t)f->rsi, procs,
                         s_ps_count * sizeof(procs[0])) != 0)
            break;
        ret = (long)s_ps_count;
        break;
    }

    case SYS_KILL: {
        if (!p)
            break;
        if (proc_kill((pid_t)f->rdi, p) == 0)
            ret = 0;
        break;
    }

    case SYS_UNAME: {
        static struct nsh_uname u;
        memset(&u, 0, sizeof(u));
        strcpy(u.sysname, "NEWOS");
        strcpy(u.nodename, "newos");
        strcpy(u.release, "0.2.0-pre-alpha");
        strcpy(u.version, "Limine " __DATE__);
        strcpy(u.machine, "x86_64");
        strcpy(u.domainname, "(none)");
        if (copy_to_user((void *)(uintptr_t)f->rdi, &u, sizeof(u)) != 0)
            break;
        ret = 0;
        break;
    }

    case SYS_GETTIME: {
        struct nsh_time t;
        if (core_time_wallclock(&t) != 0)
            break;
        if (copy_to_user((void *)(uintptr_t)f->rdi, &t, sizeof(t)) != 0)
            break;
        ret = 0;
        break;
    }

    case SYS_SYSINFO: {
        struct nsh_sysinfo si;
        si.total_frames = pmm_total_frames();
        si.free_frames = pmm_free_frames();
        si.hz = core_time_hz();
        si.uptime_sec = si.hz ? core_time_ticks() / si.hz : 0;
        if (copy_to_user((void *)(uintptr_t)f->rdi, &si, sizeof(si)) != 0)
            break;
        ret = 0;
        break;
    }

    case SYS_FBINFO: {
        struct nsh_fbinfo fi;
        u64 w, h, pitch;
        u32 bpp;
        memset(&fi, 0, sizeof(fi));
        if (fb_present()) {
            fb_geometry(&w, &h, &pitch, &bpp);
            fi.present = 1;
            fi.width = w;
            fi.height = h;
            fi.pitch = pitch;
            fi.bpp = bpp;
        }
        if (copy_to_user((void *)(uintptr_t)f->rdi, &fi, sizeof(fi)) != 0)
            break;
        ret = 0;
        break;
    }

    case SYS_FBWRITE: {
        /* Blit caller-supplied XRGB8888 pixels. The request header is
         * fetched whole first (fixed size, single validation), then each
         * row is copied separately so a short/malicious pixlen fails
         * cleanly instead of over-reading user memory. */
        struct nsh_fbwrite rq;
        if (!p || !fb_present())
            break;
        if (copy_from_user(&rq, (const void *)(uintptr_t)f->rdi,
                           sizeof(rq)) != 0)
            break;
        if (!rq.w || !rq.h)
            break;
        {
            u64 w, h, pitch;
            u32 bpp;
            fb_geometry(&w, &h, &pitch, &bpp);
            if (rq.x >= w || rq.y >= h)
                break;
            if (rq.w > w - rq.x || rq.h > h - rq.y)
                break;
            if (rq.w > 4096 || rq.h > 4096)
                break;
            if (rq.pixlen / 4 < rq.w * rq.h)
                break;
        }
        {
            /* Row-sized scratch: one 2048-pixel row (8 KiB) per pass. */
            static u32 rowbuf[2048];
            u64 y;
            for (y = 0; y < rq.h; y++) {
                u64 x0 = 0;
                while (x0 < rq.w) {
                    u64 n = rq.w - x0;
                    u64 bytes;
                    if (n > 2048)
                        n = 2048;
                    bytes = n * 4;
                    if (copy_from_user(rowbuf,
                                       (const void *)(uintptr_t)(rq.pixels +
                                                                (y * rq.w +
                                                                 x0) * 4),
                                       (size_t)bytes) != 0)
                        goto fbwrite_fail;
                    if (fb_blit_rows(rq.x + x0, rq.y + y, n, 1,
                                     rowbuf) < 0)
                        goto fbwrite_fail;
                    x0 += n;
                }
            }
        }
        ret = 0;
        break;
    fbwrite_fail:
        break;
    }

    case SYS_LSEEK: {
        int fd = (int)f->rdi;
        s64 off = (s64)f->rsi;
        int whence = (int)f->rdx;
        struct vfs_file *file;
        s64 base, pos;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS)
            break;
        file = p->fds[fd].file;
        if (!file)
            break;
        if (whence == SEEK_END)
            base = (s64)file->inode->size;
        else if (whence == SEEK_CUR)
            base = (s64)file->offset;
        else if (whence == SEEK_SET)
            base = 0;
        else
            break;
        pos = base + off;
        if (pos < 0)
            break;
        file->offset = (u64)pos;
        ret = (long)pos;
        break;
    }

    case SYS_SOCKET: {
        int domain = (int)f->rdi;
        int type = (int)f->rsi;
        int protocol = (int)f->rdx;
        if (!p) break;
        /* Allocate a free fd slot */
        for (int i = 0; i < PROCESS_MAX_FDS; i++) {
            if (!p->fds[i].file) {
                p->fds[i].file = kzalloc(sizeof(struct vfs_file));
                if (!p->fds[i].file) { ret = -1; break; }
                /* Initialize socket state */
                p->fds[i].data = kzalloc(sizeof(struct socket_state));
                if (!p->fds[i].data) { ret = -1; break; }
                struct socket_state *s = (struct socket_state *)p->fds[i].data;
                s->fd = i;
                s->domain = domain;
                s->type = type;
                s->protocol = protocol;
                s->state = SOCKET_CLOSED;
                /* Set protocol based on domain/type */
                if (domain == 2) { /* AF_INET */
                    if (type == 1) { /* SOCK_STREAM */
                        s->proto = 6; /* TCP */
                    } else if (type == 2) { /* SOCK_DGRAM */
                        s->proto = 17; /* UDP */
                    } else {
                        s->proto = 1; /* ICMP */
                    }
                }
                ret = i;
                break;
            }
        }
        ret = -1; /* no free fds */
        break;
    }

    case SYS_BIND: {
        int fd = (int)f->rdi;
        struct sockaddr *addr = (struct sockaddr *)f->rsi;
        socklen_t addrlen = (socklen_t)f->rdx;
        (void)addr;
        (void)addrlen;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s || s->state != SOCKET_CLOSED) break;
        /* Simple bind: just mark as bound, real impl would copy addr */
        s->state = SOCKET_BOUND;
        s->port = 80; /* default */
        ret = 0;
        break;
    }

    case SYS_CONNECT: {
        int fd = (int)f->rdi;
        struct sockaddr *addr = (struct sockaddr *)f->rsi;
        socklen_t addrlen = (socklen_t)f->rdx;
        (void)addr;
        (void)addrlen;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s || s->state != SOCKET_BOUND) break;
        /* Mark as connected */
        s->state = SOCKET_CONNECTED;
        /* For simplicity, accept any connection */
        ret = 0;
        break;
    }

    case SYS_ACCEPT: {
        int fd = (int)f->rdi;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s || s->state != SOCKET_LISTEN) break;
        /* Return a new fd for the accepted connection */
        for (int i = 0; i < PROCESS_MAX_FDS; i++) {
            if (!p->fds[i].file) {
                p->fds[i].file = kzalloc(sizeof(struct vfs_file));
                p->fds[i].data = kzalloc(sizeof(struct socket_state));
                struct socket_state *ns = (struct socket_state *)p->fds[i].data;
                ns->fd = i;
                ns->proto = s->proto;
                ns->state = SOCKET_ESTABLISHED;
                ret = i;
                break;
            }
        }
        ret = -1;
        break;
    }

    case SYS_LISTEN: {
        int fd = (int)f->rdi;
        int backlog = (int)f->rsi;
        (void)backlog;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s) break;
        s->state = SOCKET_LISTEN;
        ret = 0;
        break;
    }

    case SYS_SENDTO: {
        int fd = (int)f->rdi;
        const void *buf = (const void *)f->rsi;
        size_t len = (size_t)f->rdx;
        int flags = (int)f->r10;
        struct sockaddr *dest_addr = (struct sockaddr *)f->r8;
        int dest_len = (int)f->r9;
        (void)buf;
        (void)flags;
        (void)dest_addr;
        (void)dest_len;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s || s->state != SOCKET_CONNECTED) break;
        /* Minimal send: copy to bounce buffer and transmit */
        static u8 kbuf[1500];
        if (len > sizeof(kbuf)) len = sizeof(kbuf);
        if (copy_from_user(kbuf, buf, len) != 0) break;
        /* TODO: actual network transmit */
        ret = (long)len;
        break;
    }

    case SYS_SEND: {
        int fd = (int)f->rdi;
        const void *buf = (const void *)f->rsi;
        size_t len = (size_t)f->rdx;
        int flags = (int)f->r10;
        (void)flags;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s || s->state != SOCKET_CONNECTED) break;
        /* Minimal send: copy to bounce buffer and transmit */
        static u8 kbuf[1500];
        if (len > sizeof(kbuf)) len = sizeof(kbuf);
        if (copy_from_user(kbuf, buf, len) != 0) break;
        /* TODO: actual network transmit */
        ret = (long)len;
        break;
    }
    case SYS_RECV: {
        int fd = (int)f->rdi;
        void *buf = (void *)f->rsi;
        size_t len = (size_t)f->rdx;
        int flags = (int)f->r10;
        (void)flags;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s || s->state != SOCKET_CONNECTED) break;
        /* Minimal recv: return dummy data */
        static u8 kbuf[1500];
        memset(kbuf, 0, sizeof(kbuf));
        size_t rcv = (len < 100) ? len : 100;
        if (copy_to_user(buf, kbuf, rcv) != 0) break;
        ret = (long)rcv;
        break;
    }

    case SYS_RECVFROM: {
        int fd = (int)f->rdi;
        void *buf = (void *)f->rsi;
        size_t len = (size_t)f->rdx;
        int flags = (int)f->r10;
        struct socket_addr *from_addr = (struct socket_addr *)f->r8;
        int *from_len = (int *)f->r9;
        (void)flags;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s || s->state != SOCKET_CONNECTED) break;
        /* Minimal recv: return dummy data */
        static u8 kbuf[1500];
        memset(kbuf, 0, sizeof(kbuf));
        size_t rcv = (len < 100) ? len : 100;
        if (copy_to_user(buf, kbuf, rcv) != 0) break;
        if (from_addr) {
            from_addr->family = AF_INET;
            from_addr->data[0] = 0;
            from_addr->data[1] = 0;
            from_addr->data[2] = 0;
            from_addr->data[3] = 0;
        }
        if (from_len) *from_len = 16;
        ret = (long)rcv;
        break;
    }

    case SYS_GETSOCKNAME: {
        int fd = (int)f->rdi;
        struct socket_addr *addr = (struct socket_addr *)f->rsi;
        int *addrlen = (int *)f->rdx;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s) break;
        /* Return dummy address */
        if (addr && addrlen) {
            addr->family = AF_INET;
            addr->data[0] = 0;
            addr->data[1] = 0;
            *addrlen = 16;
        }
        ret = 0;
        break;
    }

    case SYS_GETPEERNAME: {
        int fd = (int)f->rdi;
        struct socket_addr *addr = (struct socket_addr *)f->rsi;
        int *addrlen = (int *)f->rdx;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s) break;
        /* Return dummy peer address */
        if (addr && addrlen) {
            addr->family = AF_INET;
            addr->data[0] = 0;
            addr->data[1] = 0;
            *addrlen = 16;
        }
        ret = 0;
        break;
    }

    case SYS_SHUTDOWN: {
        int fd = (int)f->rdi;
        int how = (int)f->rsi;
        (void)how;
        if (!p || fd < 0 || fd >= PROCESS_MAX_FDS || !p->fds[fd].file) break;
        struct socket_state *s = (struct socket_state *)p->fds[fd].data;
        if (!s) break;
        s->state = SOCKET_CLOSED;
        ret = 0;
        break;
    }

    case SYS_MOUSE_GET: {
        struct nsh_mouse m;
        int x, y, b, w;
        u64 seq;

        ps2_mouse_get(&seq, &x, &y, &b, &w);
        memset(&m, 0, sizeof(m));
        m.x = x;
        m.y = y;
        m.seq = seq;
        m.buttons = (nsh_u32)b;
        m.wheel = (nsh_s32)w;
        if (copy_to_user((void *)(uintptr_t)f->rdi, &m, sizeof(m)) != 0)
            break;
        ret = 0;
        break;
    }

    case SYS_EXIT:
    case SYS_EXIT_GROUP:
        if (!p) {
            printk("syscall: SYS_EXIT with no current process\n");
            break;
        }
        process_exit(p, (int)f->rdi);   /* noreturn */
        break;

    default:
        ret = syscall_posix(f, nr);
        break;
    }

    return ret;
}