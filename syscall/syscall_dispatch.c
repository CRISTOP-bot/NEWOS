#include <syscall/syscall.h>
#include <process/proc_process.h>
#include <process/proc_thread.h>
#include <process/sched.h>
#include <mm/mm_usercopy.h>
#include <mm/mm_vmm.h>
#include <fs/vfs.h>
#include <core/core_printk.h>
#include <core/core_console.h>
#include <core/core_time.h>
#include <abi/syscall_abi.h>
#include <iru_string.h>
#include <x86_cpu.h>

/* THE int $0x80 dispatcher.
 *
 * Register conventions (see uapi/syscall.h):
 *   rax = number, rdi/rsi/rdx/r10/r8/r9 = args
 *   return value -> rax via the shared isr frame. */

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

        int n = vfs_write(p->fds[fd].file, kbuf, len);
        if (n < 0)
            break;
        ret = n;
        break;
    }

    case SYS_GETPID:
        if (p)
            ret = (long)p->pid;
        break;

    case SYS_READ: {
        int fd = (int)f->rdi;
        u64 len = f->rdx;

        if (!p || len == 0)
            break;
        if (fd != STDIN_FILENO) {
            printk("syscall: pid %u: read not supported on fd %d\n",
                   (unsigned)p->pid, fd);
            break;
        }

        /* Blocking console read. The wait loop yields to the timer, so
         * other processes keep running; keyboard bytes arrive via the
         * serial RX interrupt. */
        static u8 kbuf[512];
        if (len > sizeof(kbuf))
            len = sizeof(kbuf);

        while (core_console_rx_available() == 0)
            core_time_delay_us(250);

        int n = 0;
        while (n < (int)len && core_console_rx_available() > 0)
            kbuf[n++] = (u8)core_console_rx_pop();

        if (copy_to_user((void *)(uintptr_t)f->rsi, kbuf, (size_t)n) != 0)
            break;
        ret = n;
        break;
    }

    case SYS_SLEEP: {
        u64 ms = f->rdi;
        u64 due = core_time_ticks() +
                  (ms * core_time_hz()) / (u64)1000;
        while ((s64)(core_time_ticks() - due) < 0)
            core_time_delay_us(250);
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
        sched_add_process(child);
        ret = (long)child->pid;
        break;
    }

    case SYS_WAITPID: {
        pid_t want = (pid_t)f->rdi;

        while (1) {
            struct process *z = sched_zombie_find(want);
            if (z) {
                int code = z->exit_code;
                sched_zombie_reclaim(z);
                ret = (long)code;
                break;
            }
            core_time_delay_us(250);
        }
        break;
    }

    case SYS_EXIT:
        if (!p) {
            printk("syscall: SYS_EXIT with no current process\n");
            break;
        }
        process_exit(p, (int)f->rdi);   /* noreturn */
        break;

    default:
        printk("syscall: pid %u: unknown syscall %llu\n",
               p ? (unsigned)p->pid : 0, nr);
        break;
    }

    return ret;
}