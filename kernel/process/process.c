#include <kernel/process.h>
#include <kernel/thread.h>
#include <kernel/elf.h>
#include <kernel/kmalloc.h>
#include <kernel/pmm.h>
#include <kernel/printk.h>
#include <kernel/panic.h>
#include <kernel/vmm.h>
#include <kernel/vfs.h>
#include <arch/x86_64/gdt/gdt.h>
#include <arch/x86_64/iframe.h>
#include <arch/x86_64/io.h>
#include <libk/string.h>

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
    printk("PF p=%p\n", (void *)p);
    for (int i = 0; i < PROCESS_MAX_FDS; i++) {
        if (p->fds[i].file) {
            vfs_close(p->fds[i].file);
            p->fds[i].file = NULL;
        }
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

/* Load `path`, a built-in ELF64, into a fully set up process. */
struct process *process_create_from_vfs(const char *path, const char *name)
{
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

    if (thread_setup_user(&p->thread, p->entry,
                          (uintptr_t)USER_STACK_TOP, p->space->cr3) != 0)
        goto fail;
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

void process_exit(struct process *p, int code)
{
    unsigned char dbg = (unsigned char)code + '0';
    outb(0x3f8, dbg);
    p->exit_code = code;
    p->state = PROCESS_STATE_EXITED;
    process_current_set(NULL);

    /* Leave the process address space before touching anything else. The
     * kernel half is shared, so we keep executing despite the CR3 write. */
    vmm_switch_to(vmm_kernel_space());
    x64_tss_set_rsp0(thread_init_stack());

    u64 *from = &p->thread.sp;
    thread_resume_to(from);
    __builtin_unreachable();
}

void process_page_fault(struct x64_iframe *f, uintptr_t addr, u64 err)
{
    struct process *p = process_current();
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