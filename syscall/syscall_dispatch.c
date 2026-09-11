#include <syscall/syscall.h>
#include <process/proc_process.h>
#include <process/proc_thread.h>
#include <mm/mm_usercopy.h>
#include <mm/mm_vmm.h>
#include <fs/vfs.h>
#include <core/core_printk.h>
#include <abi/syscall_abi.h>
#include <iru_string.h>

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