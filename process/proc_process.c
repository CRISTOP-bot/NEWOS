#include <process/proc_process.h>
#include <process/proc_thread.h>
#include <process/proc_elf.h>
#include <process/sched.h>
#include <mm/mm_heap.h>
#include <mm/mm_pmm.h>
#include <core/core_printk.h>
#include <core/core_panic.h>
#include <mm/mm_vmm.h>
#include <fs/vfs.h>
#include <ipc/ipc.h>
#include <core/core_time.h>
#include <x86_cpu.h>
#include <x86_fpu.h>
#include <x86_gdt.h>
#include <x86_frame.h>
#include <iru_string.h>

/* Process implementation: ELF loading into a private address space, fd
 * table, and termination (SYS_EXIT / user fault) handling. */

static struct process *s_current;
static u64 s_next_pid = 1;

static pid_t alloc_pid(void)
{
    return (pid_t)s_next_pid++;
}

struct process *process_current(void)
{
    return s_current;
}

void process_current_set(struct process *p)
{
    s_current = p;
}

/* Release every resource a process owns. Must run on a kernel stack other
 * than the process's own (killing the stack mid-use corrupts memory). */
void process_free(struct process *p)
{
    /* If this process owned the register bank, its FXSAVE image is about to
     * disappear: the epilogue must not park the live registers into freed
     * memory on the next handoff. */
    if (g_user_fpu_area == (u64)(uintptr_t)&p->thread.fpu[0])
        g_user_fpu_area = 0;

    for (int i = 0; i < PROCESS_MAX_FDS; i++) {
        if (p->fds[i].data)
            ipc_pipe_fd_close((struct ipc_pipe_fd *)p->fds[i].data);
        if (p->fds[i].file) {
            vfs_close(p->fds[i].file);
            p->fds[i].file = NULL;
        }
        p->fds[i].data = NULL;
    }

    if (p->thread.kernel_stack_top) {
        uintptr_t base_va =
            p->thread.kernel_stack_top - THREAD_KERNEL_STACK_SIZE;
        uintptr_t base_phys = virt_to_phys(base_va);
        if (base_phys != ~0ull) {
            phys_free_block(base_phys >> PAGE_SHIFT,
                            THREAD_KERNEL_STACK_ORDER);
            p->thread.kernel_stack_top = 0;
        }
    }

    if (p->space) {
        vmm_address_space_destroy(p->space);
        p->space = NULL;
    }

    kfree(p);
}

static void proc_abort(struct process *p)
{
    process_free(p);
}

/* Write into a not-yet-running process's address space through the direct
 * map (page-table walk, no CR3 switch). Only used for the initial stack
 * setup above; the child is not runnable yet so nothing can race it. */
static int write_child_mem(struct vmm_address_space *space, u64 uaddr,
                           const void *src, size_t len)
{
    const u8 *s = (const u8 *)src;
    while (len > 0) {
        uintptr_t page = (uintptr_t)uaddr & ~(PAGE_SIZE - 1);
        size_t off = (size_t)((uintptr_t)uaddr - page);
        size_t n = PAGE_SIZE - off;
        uintptr_t phys = 0;
        if (n > len)
            n = len;
        if (!vmm_page_lookup(space, page, &phys, NULL))
            return -1;
        memcpy((u8 *)phys_to_virt(phys) + off, s, n);
        uaddr += n;
        s += n;
        len -= n;
    }
    return 0;
}

static int write_child_u64(struct vmm_address_space *space, u64 uaddr,
                           u64 val)
{
    return write_child_mem(space, uaddr, &val, sizeof(val));
}

/* Auxiliary-vector tags written on the initial stack (values from the
 * Linux psABI; PROC may not import the ABI header, so they are mirrored
 * here deliberately). */
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_FLAGS  8
#define AT_ENTRY  9
#define AT_UID    11
#define AT_GID    12
#define AT_EUID   13
#define AT_EGID   14
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_NULL   0

/* Load `path`, a built-in ELF64, into a fully set up process. */
struct process *process_create_args(const char *path, const char *name,
                                    int argc, char argv[][128])
{
    if (argc < 0 || argc > 16 || (argc > 0 && !argv))
        return NULL;
    struct vfs_file *f = vfs_open(path, VFS_O_READ);
    if (!f) {
        printk("proc: cannot open '%s'\n", path);
        return NULL;
    }

    size_t cap = 4096, len = 0;
    u8 *buf = kmalloc(cap);
    if (!buf) {
        vfs_close(f);
        return NULL;
    }
    for (;;) {
        if (len == cap) {
            cap *= 2;
            u8 *nb = kmalloc(cap);
            if (!nb) {
                kfree(buf);
                vfs_close(f);
                return NULL;
            }
            memcpy(nb, buf, len);
            kfree(buf);
            buf = nb;
        }
        int n = vfs_read(f, buf + len, cap - len);
        if (n <= 0)
            break;
        len += (size_t)n;
    }
    vfs_close(f);

    if (len == 0) {
        kfree(buf);
        return NULL;
    }

    struct elf_image img = { .data = buf, .size = len };
    if (elf_validate(&img) != 0) {
        printk("proc: '%s' is not a loadable ELF64 executable\n", path);
        kfree(buf);
        return NULL;
    }

    struct process *p = kzalloc(sizeof(*p));
    if (!p) {
        kfree(buf);
        return NULL;
    }
    p->pid = alloc_pid();
    p->ppid = 0;
    strncpy(p->name, name, sizeof(p->name) - 1);
    p->state = PROCESS_STATE_NEW;
    p->exit_code = -1;
    p->cwd[0] = '/';
    p->cwd[1] = '\0';

    /* Standard fds -> serial console. */
    for (int i = 0; i < 3; i++)
        p->fds[i].file = vfs_open("/dev/serial", VFS_O_READ | VFS_O_WRITE);

    if (vmm_address_space_create(name, &p->space) != 0)
        goto fail;
    if (elf_load_segments(&img, p->space) != 0)
        goto fail;
    if (elf_entry_valid(&img, p->space) != 0)
        goto fail;
    p->entry = img.entry;

    /* User stack (one contiguous region below USER_STACK_TOP). */
    uintptr_t stack_bottom = (uintptr_t)USER_STACK_TOP - (uintptr_t)USER_STACK_SIZE;
    for (uintptr_t va = stack_bottom; va < (uintptr_t)USER_STACK_TOP;
         va += PAGE_SIZE) {
        if (vmm_alloc_page(p->space, va,
                           VMM_USER | VMM_WRITE | VMM_NX) != 0)
            goto fail;
    }
    p->user_stack_start = stack_bottom;
    p->user_stack_size = USER_STACK_SIZE;

    /* Dynamic-memory anchors: the brk heap starts right after the image,
     * anonymous mmap bumps upward from a fixed base in the low half. */
    p->image_end = img.load_end;
    p->brk_cur = (img.load_end + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
    p->mmap_next = PROCESS_MMAP_BASE;

    /* Argument block on the fresh user stack, System V / Linux layout:
     *   [argc][argv..][NULL][envp NULL][auxv pairs][AT_NULL][strings][rnd]
     * Real static binaries (musl) read argv/envp/auxv from the stack; the
     * legacy NEWOS crt1 keeps taking (rdi, rsi), which thread_set_user_args
     * still pokes below. Written through the direct map by physical
     * address: no CR3 switching, no TLB games, parent mappings untouched.
     */
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)buf;
    const struct elf64_phdr *ph =
        (const struct elf64_phdr *)(buf + eh->e_phoff);
    u64 at_phdr = 0;
    for (u16 i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_offset <= eh->e_phoff &&
            (u64)eh->e_phnum * eh->e_phentsize <=
                ph[i].p_filesz - (eh->e_phoff - ph[i].p_offset)) {
            at_phdr = ph[i].p_vaddr + (eh->e_phoff - ph[i].p_offset);
            break;
        }
    }

    u64 aux[] = {
        AT_PHDR,   at_phdr,
        AT_PHENT,  eh->e_phentsize,
        AT_PHNUM,  eh->e_phnum,
        AT_PAGESZ, PAGE_SIZE,
        AT_BASE,   0,
        AT_FLAGS,  0,
        AT_ENTRY,  eh->e_entry,
        AT_UID,    0,
        AT_GID,    0,
        AT_EUID,   0,
        AT_EGID,   0,
        AT_SECURE, 0,
        AT_RANDOM, 0,          /* patched with the blob VA below */
        AT_NULL,   0,
    };
    size_t auxn = sizeof(aux) / sizeof(aux[0]);

    size_t head_words = 1 + (argc > 0 ? (size_t)argc + 1 : 1) + 1 + auxn;
    size_t strs = 16;               /* AT_RANDOM blob last */
    for (int i = 0; i < argc; i++)
        strs += strlen(argv[i]) + 1;

    size_t total = head_words * 8 + strs;
    total = (total + 15) & ~(size_t)15;
    uintptr_t user_rsp = (uintptr_t)USER_STACK_TOP - total;
    if (user_rsp < stack_bottom) {
        printk("proc: argument block too large (%zu)\n", total);
        goto fail;
    }

    u64 wp = user_rsp;
    u64 str_uaddr = user_rsp + head_words * 8;
    u64 rnd_uaddr = str_uaddr;
    for (int i = 0; i < argc; i++)
        rnd_uaddr += strlen(argv[i]) + 1;
    u64 rnd_state = core_time_ticks() * 6364136223846793005ull +
                    (u64)p->pid;

    if (write_child_u64(p->space, wp, (u64)argc) != 0)
        goto fail;
    wp += 8;
    if (argc > 0) {
        for (int i = 0; i < argc; i++) {
            size_t l = strlen(argv[i]) + 1;
            if (write_child_u64(p->space, wp, str_uaddr) != 0)
                goto fail;
            if (write_child_mem(p->space, str_uaddr, argv[i], l) != 0)
                goto fail;
            wp += 8;
            str_uaddr += l;
        }
    }
    if (write_child_u64(p->space, wp, 0) != 0)      /* argv terminator */
        goto fail;
    wp += 8;
    if (write_child_u64(p->space, wp, 0) != 0)      /* empty envp */
        goto fail;
    wp += 8;
    for (size_t i = 0; i < auxn; i += 2) {
        u64 val = aux[i + 1];
        if (aux[i] == AT_RANDOM)
            val = rnd_uaddr;
        if (write_child_u64(p->space, wp, aux[i]) != 0 ||
            write_child_u64(p->space, wp + 8, val) != 0)
            goto fail;
        wp += 16;
    }
    {
        /* 16 bytes of "random" for the libc stack protector seed. */
        for (int i = 0; i < 16; i++) {
            rnd_state ^= rnd_state << 13;
            rnd_state ^= rnd_state >> 7;
            rnd_state ^= rnd_state << 17;
            u8 b = (u8)(rnd_state >> 32);
            if (write_child_mem(p->space, rnd_uaddr + (u64)i, &b, 1) != 0)
                goto fail;
        }
    }

    if (thread_setup_user(&p->thread, p->entry,
                          user_rsp, p->space->cr3) != 0)
        goto fail;
    if (argc > 0)
        thread_set_user_args(&p->thread, (u64)argc, user_rsp + 8);
    p->thread.process = p;

    kfree(buf);
    printk("proc: %s pid=%u entry=%p segments=%d stack [%llx, %llx)\n",
           p->name, (unsigned)p->pid, p->entry, img.nsegments,
           p->user_stack_start, p->user_stack_start + p->user_stack_size);
    return p;

fail:
    proc_abort(p);
    kfree(buf);
    return NULL;
}

struct process *process_create_from_vfs(const char *path, const char *name)
{
    return process_create_args(path, name, 0, NULL);
}

void process_exit(struct process *p, int code)
{
    p->exit_code = code;
    p->state = PROCESS_STATE_EXITED;
    process_current_set(NULL);

    /* The init process ends the session: resume the kernel boot flow that
     * teleported into it (the only user of the captured resume context). */
    if (p->is_init) {
        vmm_switch_to(vmm_kernel_space());
        x64_tss_set_rsp0(thread_init_stack());
        u64 *from = &p->thread.sp;
        thread_resume_to(from);
        __builtin_unreachable();
    }

    /* Any other process becomes a zombie; the CPU returns to the interrupt
     * epilogue, which hands it to the next runnable process. */
    sched_remove_runnable(p);
    if (sched_zombify(p) != 0) {
        printk("proc: zombie list full; leaking pid %u\n", (unsigned)p->pid);
        process_free(p);
    }

    struct process *next = sched_pick_next_runnable();
    if (!next) {
        printk("proc: no runnable processes left; halting.\n");
        for (;;)
            cpu_hlt();
    }
    sched_handoff(next);
}

void process_page_fault(struct x64_iframe *f, uintptr_t addr, u64 err)
{
    struct process *p = process_current();
    /* DIAG-TEMP: pin down scheduler/stack confusion (stale CR3/RSP0). */
    printk("diag: fault cr3=%p cur=%s/%u space_cr3=%p entry_sp=%p kstack_top=%p f_rsp=%p\n",
           (void *)read_cr3(), p ? p->name : "(null)",
           (unsigned)(p ? p->pid : 0),
           (p && p->space) ? (void *)p->space->cr3 : (void *)0,
           p ? (void *)p->thread.entry_sp : (void *)0,
           p ? (void *)p->thread.kernel_stack_top : (void *)0,
           (void *)f->rsp);
    printk("\nUSER PAGE FAULT at %p error=%llx (%s)\n", (void *)addr, err,
           (err & 1) ? "protection" : "not-present");
    x64_dump_iframe(f);
    if (!p)
        panic("page fault outside a process");

    printk("proc: killing %s (pid %u)\n", p->name, (unsigned)p->pid);
    process_exit(p, PROCESS_EXIT_SIGNAL_BASE + 11);
}

void process_exception(struct x64_iframe *f, int vec, u64 err)
{
    struct process *p = process_current();
    printk("\nUSER EXCEPTION: raw vector %d, error holder %llx\n", vec, err);
    x64_dump_iframe(f);
    if (!p)
        panic("exception outside a process");

    printk("proc: killing %s (pid %u)\n", p->name, (unsigned)p->pid);
    process_exit(p, PROCESS_EXIT_SIGNAL_BASE + (vec & 0x1f));
}