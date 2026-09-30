#include <syscall/syscall.h>
#include <process/proc_process.h>
#include <mm/mm_usercopy.h>
#include <mm/mm_vmm.h>
#include <mm/mm_pmm.h>
#include <mm/mm_heap.h>
#include <fs/vfs.h>
#include <ipc/ipc.h>
#include <core/core.h>
#include <core/core_time.h>
#include <core/core_printk.h>
#include <abi/syscall_abi.h>
#include <iru_string.h>
#include <iru_list.h>

/* POSIX extension set for the int $0x80 dispatcher.
 *
 * syscall_dispatch() delegates every number it does not implement itself
 * here: the Linux x86_64 calls an unmodified musl static binary needs at
 * startup and for stdio (memory mapping, stat family, fd management,
 * terminal ioctls, identity getters, clocks, and no-op signal stubs).
 *
 * Return convention matches the rest of the ABI: >= 0 success,
 * SYSCALL_RET_ERROR (-1) failure; no errno objects are exposed. */

/* ---- small shared helpers --------------------------------------------- */

static s64 days_from_civil(s64 y, u32 m, u32 d)
{
    y -= m <= 2;
    s64 era = (y >= 0 ? y : y - 399) / 400;
    u32 yoe = (u32)(y - era * 400);
    u32 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    u32 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (s64)doe - 719468;
}

/* Seconds since the Unix epoch from the CMOS wall clock (best effort:
 * second granularity, no TZ). */
static s64 epoch_now(void)
{
    struct nsh_time t;
    if (core_time_wallclock(&t) != 0)
        return 0;
    s64 days = days_from_civil((s64)t.year, t.mon, t.day);
    return days * 86400 + (s64)t.hour * 3600 + (s64)t.min * 60 + t.sec;
}

static u64 epoch_frac_ns(void)
{
    u64 hz = core_time_hz();
    if (!hz)
        return 0;
    return (core_time_ticks() % hz) * 1000000000ull / hz;
}

/* Resolve a user path against the caller's cwd (same rules as the core
 * dispatcher's make_abs). */
static int posix_abs(struct process *p, const char *in, char *out)
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

static int posix_user_string(char *out, const void *user_ptr, size_t cap)
{
    size_t done = 0;

    if (cap == 0 || !user_ptr)
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

static struct vfs_file *posix_file(struct process *p, int fd)
{
    if (!p || fd < 0 || fd >= PROCESS_MAX_FDS)
        return NULL;
    return p->fds[fd].file;
}

/* ---- stat family -------------------------------------------------------- */

static void fill_stat(struct vfs_inode *in, struct nsh_stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_ino = in->ino;
    st->st_nlink = 1;
    st->st_mode = in->mode;
    st->st_blksize = 4096;
    st->st_size = (s64)in->size;
    if (in->mode & VFS_MODE_DIR)
        st->st_size = 4096;
    st->st_blocks = ((s64)in->size + 511) / 512;
    st->st_atime = st->st_mtime = st->st_ctime = (nsh_s64)epoch_now();
}

static long posix_stat_at(struct process *p, u64 upath, u64 ubuf)
{
    static char path[512];
    static char abs[512];
    struct nsh_stat st;

    if (posix_user_string(path, (const void *)(uintptr_t)upath,
                          sizeof(path)) != 0)
        return SYSCALL_RET_ERROR;
    if (posix_abs(p, path, abs) != 0)
        return SYSCALL_RET_ERROR;
    struct vfs_inode *in = vfs_lookup(abs);
    if (!in)
        return SYSCALL_RET_ERROR;
    fill_stat(in, &st);
    if (copy_to_user((void *)(uintptr_t)ubuf, &st, sizeof(st)) != 0)
        return SYSCALL_RET_ERROR;
    return 0;
}

static long posix_fstat(struct x64_iframe *f, struct process *p)
{
    struct vfs_file *file = posix_file(p, (int)f->rdi);
    struct nsh_stat st;

    if (!file || !file->inode)
        return SYSCALL_RET_ERROR;
    fill_stat(file->inode, &st);
    if (copy_to_user((void *)(uintptr_t)f->rsi, &st, sizeof(st)) != 0)
        return SYSCALL_RET_ERROR;
    return 0;
}

/* ---- getdents64 --------------------------------------------------------- */

static u8 dt_type(u32 mode)
{
    if ((mode & VFS_MODE_TYPE_MASK) == VFS_MODE_DIR)
        return NSH_DT_DIR;
    if ((mode & VFS_MODE_TYPE_MASK) == VFS_MODE_CHAR)
        return NSH_DT_CHR;
    return NSH_DT_REG;
}

static long posix_getdents64(struct x64_iframe *f, struct process *p)
{
    struct vfs_file *file = posix_file(p, (int)f->rdi);
    u64 cap = f->rdx;
    u64 written = 0;
    size_t want = (size_t)file->offset;
    size_t seen = 0;

    if (!file || !file->inode || !(file->inode->mode & VFS_MODE_DIR))
        return SYSCALL_RET_ERROR;

    struct list_node *node;
    LIST_FOR_EACH(node, &file->inode->children) {
        struct vfs_inode *in = LIST_NODE_ENTRY(node, struct vfs_inode, chain);
        if (seen++ < want)
            continue;
        size_t namelen = strlen(in->name);
        if (namelen >= 256)
            continue;
        size_t reclen = (19 + namelen + 1 + 7) & ~(size_t)7;
        if (written + reclen > cap)
            break;
        u8 rec[32 + 256];
        memset(rec, 0, reclen);
        *(u64 *)(rec + 0) = in->ino;                 /* d_ino */
        *(u64 *)(rec + 8) = (u64)(want + 1);         /* d_off (next index) */
        *(u16 *)(rec + 16) = (u16)reclen;            /* d_reclen */
        rec[18] = dt_type(in->mode);                 /* d_type */
        memcpy(rec + 19, in->name, namelen + 1);
        if (copy_to_user((u8 *)(uintptr_t)f->rsi + written, rec,
                         reclen) != 0)
            break;
        written += reclen;
        want++;
    }
    file->offset = want;
    return (long)written;
}

/* ---- fd management: dup / dup2 / fcntl ---------------------------------- */

/* Clone one fd of `p` onto another slot of the same process (pipe ends
 * share the pipe with a fresh refcounted handle, files get a private copy
 * so offsets stay independent, matching Linux dup). */
static int posix_fd_dup(struct process *p, int from, int to)
{
    struct fd_entry *se = &p->fds[from];
    if (!se->file)
        return -1;
    if (p->fds[to].data)
        ipc_pipe_fd_close((struct ipc_pipe_fd *)p->fds[to].data);
    if (p->fds[to].file)
        vfs_close(p->fds[to].file);
    p->fds[to].file = NULL;
    p->fds[to].data = NULL;

    struct vfs_file *nf = kmalloc(sizeof(*nf));
    if (!nf)
        return -1;
    memcpy(nf, se->file, sizeof(*nf));
    struct ipc_pipe_fd *spf = (struct ipc_pipe_fd *)se->data;
    if (spf && spf->magic == IPC_PIPE_FD_MAGIC) {
        struct ipc_pipe_fd *cpf = ipc_pipe_fd_new(spf->pipe, spf->is_reader);
        if (!cpf) {
            kfree(nf);
            return -1;
        }
        p->fds[to].data = cpf;
    }
    p->fds[to].file = nf;
    return to;
}

static int posix_first_free_fd(struct process *p, int start)
{
    for (int i = start; i < PROCESS_MAX_FDS; i++)
        if (!p->fds[i].file)
            return i;
    return -1;
}

static long posix_fcntl(struct x64_iframe *f, struct process *p)
{
    int fd = (int)f->rdi;
    int cmd = (int)f->rsi;
    long arg = (long)f->rdx;
    struct vfs_file *file = posix_file(p, fd);

    if (!file)
        return SYSCALL_RET_ERROR;
    switch (cmd) {
    case NSH_F_DUPFD: {
        if (arg < 0)
            return SYSCALL_RET_ERROR;
        int to = posix_first_free_fd(p, (int)arg);
        if (to < 0 || posix_fd_dup(p, fd, to) < 0)
            return SYSCALL_RET_ERROR;
        return to;
    }
    case NSH_F_GETFD:
        return 0;
    case NSH_F_SETFD:
        return 0;
    case NSH_F_GETFL:
        return (long)file->uflags;
    case NSH_F_SETFL:
        file->uflags = (file->uflags & ~(NSH_O_NONBLOCK | NSH_O_APPEND)) |
                       ((u32)arg & (NSH_O_NONBLOCK | NSH_O_APPEND));
        return 0;
    default:
        return SYSCALL_RET_ERROR;
    }
}

/* ---- ioctl: terminal model ---------------------------------------------- */

/* One flat console behind /dev/serial: termios values are accepted and
 * remembered nowhere; reads still go through the kbd ring, writes through
 * the active console. isatty() succeeds on any console fd, which is the
 * only answer programs actually inspect. */
static const struct nsh_termios posix_termios = {
    .c_iflag = 0000001 | 00000004000 | 00000020,   /* BRKINT|ICRNL|IXON */
    .c_oflag = 0000001 | 0000004,                  /* OPOST|ONLCR */
    .c_cflag = 0010000 | 0000060 | 0000200,        /* B38400|CS8|CREAD */
    .c_lflag = 0000002 | 0000010 | 0000004 |
               0000020 | 0000100,                  /* ISIG|ICANON|ECHO|
                                                    * ECHOE|ECHOK */
    .c_cc = {
        3,   /* VINTR  ^C */
        28,  /* VQUIT  ^\ */
        127, /* VERASE ^? */
        21,  /* VKILL  ^U */
        4,   /* VEOF   ^D */
        0,   /* VTIME */
        1,   /* VMIN */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    },
    .c_ispeed = 14,
    .c_ospeed = 14,
};

static void posix_winsize(struct nsh_winsize *ws)
{
    u64 w = 640, h = 400, pitch;
    u32 bpp;
    memset(ws, 0, sizeof(*ws));
    if (fb_present())
        fb_geometry(&w, &h, &pitch, &bpp);
    ws->ws_col = (nsh_u16)(w / 8);
    ws->ws_row = (nsh_u16)(h / 16);
    if (!ws->ws_col)
        ws->ws_col = 80;
    if (!ws->ws_row)
        ws->ws_row = 25;
    ws->ws_xpixel = (nsh_u16)w;
    ws->ws_ypixel = (nsh_u16)h;
}

static long posix_ioctl(struct x64_iframe *f, struct process *p)
{
    int fd = (int)f->rdi;
    u64 req = f->rsi;
    u64 uptr = f->rdx;
    struct vfs_file *file = posix_file(p, fd);

    if (!file)
        return SYSCALL_RET_ERROR;
    switch (req) {
    case NSH_TCGETS: {
        struct nsh_termios t = posix_termios;
        return copy_to_user((void *)(uintptr_t)uptr, &t, sizeof(t)) == 0
                   ? 0 : SYSCALL_RET_ERROR;
    }
    case NSH_TCSETS:
    case NSH_TCSETSW:
    case NSH_TCSETSF:
        return 0;
    case NSH_TIOCGWINSZ: {
        struct nsh_winsize ws;
        posix_winsize(&ws);
        return copy_to_user((void *)(uintptr_t)uptr, &ws, sizeof(ws)) == 0
                   ? 0 : SYSCALL_RET_ERROR;
    }
    case NSH_TIOCSWINSZ:
        return 0;
    case NSH_FIONREAD: {
        int zero = 0;
        return copy_to_user((void *)(uintptr_t)uptr, &zero,
                            sizeof(zero)) == 0 ? 0 : SYSCALL_RET_ERROR;
    }
    default:
        return SYSCALL_RET_ERROR;
    }
}

/* ---- memory: mmap / munmap / mprotect / brk ----------------------------- */

static u32 posix_prot_flags(u64 prot)
{
    u32 vf = VMM_PRESENT | VMM_USER | VMM_NX;
    if (prot & NSH_PROT_WRITE)
        vf |= VMM_WRITE;
    if (prot & NSH_PROT_EXEC)
        vf &= ~VMM_NX;
    return vf;
}

static long posix_mmap(struct x64_iframe *f, struct process *p)
{
    uintptr_t hint = (uintptr_t)f->rdi;
    size_t len = (size_t)f->rsi;
    u64 prot = f->rdx;
    u64 flags = f->r10;
    uintptr_t stack_floor = p->user_stack_start;
    uintptr_t va;

    if (!p)
        return SYSCALL_RET_ERROR;
    if (!(flags & NSH_MAP_ANONYMOUS))
        return SYSCALL_RET_ERROR;         /* file-backed maps unsupported */
    len = ALIGN_UP(len, PAGE_SIZE);
    if (len == 0 || len > (1ull << 34))
        return SYSCALL_RET_ERROR;
    if (flags & NSH_MAP_FIXED) {
        if (hint & (PAGE_SIZE - 1))
            return SYSCALL_RET_ERROR;
        va = hint;
        if (va < USER_SPACE_BASE || va >= stack_floor || va + len < va)
            return SYSCALL_RET_ERROR;
    } else {
        va = p->mmap_next;
        if (hint && !(hint & (PAGE_SIZE - 1)) && hint >= va &&
            hint + len <= stack_floor)
            va = hint;
        if (va + len > stack_floor || va + len < va)
            return SYSCALL_RET_ERROR;
    }

    u32 vf = posix_prot_flags(prot);
    u64 done = 0;
    for (uintptr_t a = va; a < va + len; a += PAGE_SIZE) {
        uintptr_t phys;
        if (vmm_page_lookup(p->space, a, &phys, NULL)) {
            if (!(flags & NSH_MAP_FIXED))
                continue;                 /* bump region: cannot happen */
            vmm_unmap_pages(p->space, a, 1);
            pmm_frame_free(phys >> PAGE_SHIFT);
        }
        if (vmm_alloc_page(p->space, a, vf) != 0) {
            /* Roll back: unmapped+freed frames stay, mapped ones too;
             * address space destroy is the actual reclaimer at exit. */
            if (flags & NSH_MAP_FIXED)
                return SYSCALL_RET_ERROR;
            break;                        /* partial map still usable */
        }
        done += PAGE_SIZE;
    }
    if (!(flags & NSH_MAP_FIXED)) {
        if (done == 0)
            return SYSCALL_RET_ERROR;
        p->mmap_next = ALIGN_UP(va + done, PAGE_SIZE);
    } else if (done != len) {
        return SYSCALL_RET_ERROR;
    }
    return (long)va;
}

static long posix_munmap(struct x64_iframe *f, struct process *p)
{
    uintptr_t va = (uintptr_t)f->rdi;
    size_t len = (size_t)f->rsi;

    if (!p)
        return SYSCALL_RET_ERROR;
    if (va & (PAGE_SIZE - 1))
        return SYSCALL_RET_ERROR;
    len = ALIGN_UP(len, PAGE_SIZE);
    for (uintptr_t a = va; a < va + len; a += PAGE_SIZE) {
        uintptr_t phys;
        if (!vmm_page_lookup(p->space, a, &phys, NULL))
            continue;
        vmm_unmap_pages(p->space, a, 1);
        pmm_frame_free(phys >> PAGE_SHIFT);
    }
    return 0;
}

static long posix_mprotect(struct x64_iframe *f, struct process *p)
{
    uintptr_t va = (uintptr_t)f->rdi;
    size_t len = (size_t)f->rsi;
    u64 prot = f->rdx;

    if (!p)
        return SYSCALL_RET_ERROR;
    if (va & (PAGE_SIZE - 1))
        return SYSCALL_RET_ERROR;
    len = ALIGN_UP(len, PAGE_SIZE);
    u32 vf = posix_prot_flags(prot);
    for (uintptr_t a = va; a < va + len; a += PAGE_SIZE) {
        uintptr_t phys;
        if (!vmm_page_lookup(p->space, a, &phys, NULL))
            return SYSCALL_RET_ERROR;
        vmm_unmap_pages(p->space, a, 1);
        if (vmm_map_pages(p->space, a, phys, 1, vf) != 0)
            return SYSCALL_RET_ERROR;
    }
    return 0;
}

static long posix_brk(struct x64_iframe *f, struct process *p)
{
    uintptr_t want = (uintptr_t)f->rdi;
    uintptr_t start = p->image_end;
    uintptr_t limit = PROCESS_MMAP_BASE;

    if (!p)
        return SYSCALL_RET_ERROR;
    if (want == 0)
        return (long)p->brk_cur;
    if (want < start)
        return (long)p->brk_cur;
    if (want > limit)
        return (long)p->brk_cur;

    uintptr_t old_top = ALIGN_UP(p->brk_cur, PAGE_SIZE);
    uintptr_t new_top = ALIGN_UP(want, PAGE_SIZE);
    for (uintptr_t a = old_top; a < new_top; a += PAGE_SIZE) {
        if (!vmm_page_lookup(p->space, a, NULL, NULL) &&
            vmm_alloc_page(p->space, a,
                           VMM_USER | VMM_WRITE | VMM_NX) != 0)
            break;
    }
    p->brk_cur = want;
    return (long)p->brk_cur;
}

/* ---- identity / process group -------------------------------------------- */

static long posix_getppid(struct process *p)
{
    return p ? (long)p->ppid : SYSCALL_RET_ERROR;
}

/* ---- clocks --------------------------------------------------------------- */

static long posix_gettimeofday(struct x64_iframe *f)
{
    struct nsh_timeval tv;
    if (!f->rdi)
        return 0;
    tv.tv_sec = (nsh_s64)epoch_now();
    tv.tv_usec = (nsh_s64)(epoch_frac_ns() / 1000);
    return copy_to_user((void *)(uintptr_t)f->rdi, &tv, sizeof(tv)) == 0
               ? 0 : SYSCALL_RET_ERROR;
}

static long posix_clock_gettime(struct x64_iframe *f)
{
    struct nsh_timespec ts;
    u64 id = f->rdi;

    if (id == 0) {
        ts.tv_sec = (nsh_s64)epoch_now();
        ts.tv_nsec = (nsh_s64)epoch_frac_ns();
    } else {
        u64 hz = core_time_hz();
        u64 t = core_time_ticks();
        ts.tv_sec = hz ? (nsh_s64)(t / hz) : 0;
        ts.tv_nsec = hz ? (nsh_s64)((t % hz) * 1000000000ull / hz) : 0;
    }
    return copy_to_user((void *)(uintptr_t)f->rsi, &ts, sizeof(ts)) == 0
               ? 0 : SYSCALL_RET_ERROR;
}

static long posix_times(struct x64_iframe *f, struct process *p)
{
    struct nsh_tms tms;
    memset(&tms, 0, sizeof(tms));
    tms.tms_utime = core_time_ticks();
    if (p)
        tms.tms_cutime = tms.tms_utime / 2;
    if (f->rdi &&
        copy_to_user((void *)(uintptr_t)f->rdi, &tms, sizeof(tms)) != 0)
        return SYSCALL_RET_ERROR;
    return (long)tms.tms_utime;
}

/* ---- TLS (arch_prctl) ----------------------------------------------------- */

static long posix_arch_prctl(struct x64_iframe *f, struct process *p)
{
    u64 code = f->rdi;
    u64 arg = f->rsi;
    u64 *slot = p ? &p->thread.fsbase : NULL;

    if (!p)
        return SYSCALL_RET_ERROR;
    switch (code) {
    case NSH_ARCH_SET_FS:
        *slot = arg;
        g_user_fsbase = arg;
        arch_set_fsbase(arg);
        return 0;
    case NSH_ARCH_GET_FS:
        return copy_to_user((void *)(uintptr_t)arg, slot,
                            sizeof(u64)) == 0 ? 0 : SYSCALL_RET_ERROR;
    case NSH_ARCH_SET_GS:
    case NSH_ARCH_GET_GS:
        /* GS is unused here; accept symmetrically so startup code that
         * probes both bases does not fail. */
        if (code == NSH_ARCH_GET_GS) {
            u64 zero = 0;
            if (copy_to_user((void *)(uintptr_t)arg, &zero,
                             sizeof(zero)) != 0)
                return SYSCALL_RET_ERROR;
        }
        return 0;
    default:
        return SYSCALL_RET_ERROR;
    }
}

/* ---- truncate family ------------------------------------------------------- */

static int posix_trunc_inode(struct vfs_inode *in, u64 size)
{
    if (!in || !in->ops || !in->ops->truncate)
        return -1;
    return in->ops->truncate(in, (size_t)size) == 0 ? 0 : -1;
}

static long posix_ftruncate(struct x64_iframe *f, struct process *p)
{
    struct vfs_file *file = posix_file(p, (int)f->rdi);
    if (!file)
        return SYSCALL_RET_ERROR;
    return posix_trunc_inode(file->inode, f->rsi) == 0
               ? 0 : SYSCALL_RET_ERROR;
}

/* ---- misc ------------------------------------------------------------------ */

static u64 s_rng_state;

static long posix_getrandom(struct x64_iframe *f, struct process *p)
{
    u8 buf[256];
    u64 want = f->rsi;
    u64 done = 0;

    if (!p || !f->rdi || want == 0)
        return want == 0 ? 0 : SYSCALL_RET_ERROR;
    if (!s_rng_state)
        s_rng_state = core_time_ticks() * 2862933555777941757ull + 3037000499;
    while (done < want) {
        size_t chunk = want - done > sizeof(buf) ? sizeof(buf)
                                                 : (size_t)(want - done);
        for (size_t i = 0; i < chunk; i++) {
            s_rng_state ^= s_rng_state << 13;
            s_rng_state ^= s_rng_state >> 7;
            s_rng_state ^= s_rng_state << 17;
            buf[i] = (u8)(s_rng_state >> 33);
        }
        if (copy_to_user((u8 *)(uintptr_t)f->rdi + done, buf, chunk) != 0)
            return done ? (long)done : SYSCALL_RET_ERROR;
        done += chunk;
    }
    return (long)done;
}

long syscall_posix(struct x64_iframe *f, u64 nr)
{
    struct process *p = process_current();

    switch (nr) {
    /* Memory. */
    case SYS_MMAP:      return posix_mmap(f, p);
    case SYS_MUNMAP:    return posix_munmap(f, p);
    case SYS_MPROTECT:  return posix_mprotect(f, p);
    case SYS_BRK:       return posix_brk(f, p);
    case SYS_MADVISE:   return 0;

    /* fd management. */
    case SYS_DUP: {
        if (!p)
            return SYSCALL_RET_ERROR;
        int to = posix_first_free_fd(p, 0);
        if (to < 0 || posix_fd_dup(p, (int)f->rdi, to) < 0)
            return SYSCALL_RET_ERROR;
        return to;
    }
    case SYS_DUP2: {
        int from = (int)f->rdi, to = (int)f->rsi;
        if (!p || from < 0 || from >= PROCESS_MAX_FDS ||
            to < 0 || to >= PROCESS_MAX_FDS || !p->fds[from].file)
            return SYSCALL_RET_ERROR;
        if (from == to)
            return to;
        if (posix_fd_dup(p, from, to) < 0)
            return SYSCALL_RET_ERROR;
        return to;
    }
    case SYS_FCNTL:     return posix_fcntl(f, p);
    case SYS_IOCTL:     return posix_ioctl(f, p);

    /* Filesystem. */
    case SYS_STAT:
    case SYS_LSTAT:     return posix_stat_at(p, f->rdi, f->rsi);
    case SYS_NEWFSTATAT: return posix_stat_at(p, f->rsi, f->rdx);
    case SYS_FSTAT:     return posix_fstat(f, p);
    case SYS_GETDENTS64: return posix_getdents64(f, p);
    case SYS_ACCESS: {
        static char path[512];
        static char abs[512];
        if (posix_user_string(path, (const void *)(uintptr_t)f->rdi,
                              sizeof(path)) != 0)
            return SYSCALL_RET_ERROR;
        if (posix_abs(p, path, abs) != 0)
            return SYSCALL_RET_ERROR;
        return vfs_lookup(abs) ? 0 : SYSCALL_RET_ERROR;
    }
    case SYS_TRUNCATE: {
        static char path[512];
        static char abs[512];
        if (posix_user_string(path, (const void *)(uintptr_t)f->rdi,
                              sizeof(path)) != 0)
            return SYSCALL_RET_ERROR;
        if (posix_abs(p, path, abs) != 0)
            return SYSCALL_RET_ERROR;
        return posix_trunc_inode(vfs_lookup(abs), f->rsi) == 0
                   ? 0 : SYSCALL_RET_ERROR;
    }
    case SYS_FTRUNCATE: return posix_ftruncate(f, p);

    /* Identity / process groups. */
    case SYS_GETUID:
    case SYS_GETGID:
    case SYS_GETEUID:
    case SYS_GETEGID:   return 0;
    case SYS_GETPPID:   return posix_getppid(p);
    case SYS_GETPGRP:   return p ? (long)p->pid : SYSCALL_RET_ERROR;
    case SYS_SETSID:    return p ? (long)p->pid : SYSCALL_RET_ERROR;
    case SYS_UMASK:     return 0000022;

    /* Clocks. */
    case SYS_GETTIMEOFDAY: return posix_gettimeofday(f);
    case SYS_CLOCK_GETTIME: return posix_clock_gettime(f);
    case SYS_TIMES:     return posix_times(f, p);

    /* Signals: the cooperative kernel delivers none, so installation
     * succeeds against void handlers (SIG_DFL semantics everywhere). */
    case SYS_RT_SIGACTION:
    case SYS_RT_SIGPROCMASK:
    case SYS_SIGALTSTACK:
        return 0;
    case SYS_SET_TID_ADDRESS:
        return (long)(p ? p->pid : 0);

    /* TLS base management. */
    case SYS_ARCH_PRCTL: return posix_arch_prctl(f, p);

    /* Random. */
    case SYS_GETRANDOM: return posix_getrandom(f, p);

    /* poll with "nothing to report" semantics (no event loops yet). */
    case SYS_POLL:      return 0;

    /* Not implemented yet: process plumbing (fork/execve/readlink). */
    case SYS_FORK:
    case SYS_EXECVE:
    case SYS_READLINK:
        printk("syscall_posix: pid %u: unsupported syscall %llu\n",
               p ? (unsigned)p->pid : 0, nr);
        return SYSCALL_RET_ERROR;

    default:
        printk("syscall_posix: pid %u: unknown syscall %llu\n",
               p ? (unsigned)p->pid : 0, nr);
        return SYSCALL_RET_ERROR;
    }
}
