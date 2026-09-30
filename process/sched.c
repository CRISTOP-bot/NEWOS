#include <process/sched.h>
#include <process/proc_process.h>
#include <process/proc_thread.h>
#include <mm/mm_vmm.h>
#include <mm/mm_usercopy.h>
#include <core/core_printk.h>
#include <core/core_panic.h>
#include <x86_cpu.h>
#include <x86_fpu.h>
#include <x86_gdt.h>
#include <x86_frame.h>

/* Minimal tick scheduler.
 *
 * Run queue: a fixed-size list of runnable processes. Every preemption runs
 * in interrupt context (IF clear); the few mutators reachable from syscall
 * context (spawn / zombify / reclaim) are wrapped in cli/st so they can never
 * tear a queue read done by sched_on_tick().
 *
 * Thread state lives entirely in each thread's own saved interrupt frame on
 * its kernel stack: sched_on_tick() records current->thread.entry_sp and
 * writes the next thread's entry_sp into g_sched_next_sp, which the isr
 * common epilogue uses to relocate RSP and resume the target.
 */

static struct process *s_run[PROC_MAX_RUNNABLE];
static int s_run_count;
static int s_scan;   /* next slot to start the round-robin scan from */

static struct process *s_zombie[PROC_MAX_ZOMBIE];
static int s_zombie_count;
static u64 s_zombie_clock = 1;   /* own tick counter, advanced each tick */

#define ZOMBIE_MAX_AGE (2000)    /* ticks before a zombie is reaped */

void sched_init(void)
{
    s_run_count = 0;
    s_zombie_count = 0;
    s_scan = 0;
}

void sched_add_process(struct process *p)
{
    cpu_cli();
    if (s_run_count < PROC_MAX_RUNNABLE) {
        s_run[s_run_count++] = p;
        p->state = PROCESS_STATE_RUNNING;
    } else {
        printk("sched: run queue full (%d); dropping pid %u\n",
               PROC_MAX_RUNNABLE, (unsigned)p->pid);
        process_free(p);
    }
    cpu_sti();
}

/* ---- zombies ------------------------------------------------------------ */

int sched_zombify(struct process *p)
{
    int rc = 0;
    cpu_cli();
    if (s_zombie_count < PROC_MAX_ZOMBIE) {
        s_zombie[s_zombie_count++] = p;
    } else {
        rc = -1;
    }
    cpu_sti();
    return rc;
}

struct process *sched_zombie_find(pid_t pid)
{
    for (int i = 0; i < s_zombie_count; i++) {
        if (s_zombie[i]->pid == pid)
            return s_zombie[i];
    }
    return NULL;
}

void sched_zombie_reclaim(struct process *z)
{
    cpu_cli();
    for (int i = 0; i < s_zombie_count; i++) {
        if (s_zombie[i] == z) {
            s_zombie[i] = s_zombie[s_zombie_count - 1];
            s_zombie_count--;
            break;
        }
    }
    cpu_sti();
    process_free(z);
}

static void sched_reap_stale(void)
{
    if (!s_zombie_count)
        return;
    if (++s_zombie_clock <= ZOMBIE_MAX_AGE)
        return;

    /* Nobody waited on these in time: reclaim them all. */
    for (int i = 0; i < s_zombie_count; i++)
        process_free(s_zombie[i]);
    s_zombie_count = 0;
    s_zombie_clock = 1;
}

/* ---- preemption --------------------------------------------------------- */

void sched_handoff(struct process *next)
{
    /* DIAG-TEMP: validate the resume slot before publishing it. A
     * corrupted slot here means something overwrote the reserved area
     * (C call chains reaching past the live frames); dump the pattern
     * to identify the writer. */
    {
        u64 *slot = (u64 *)(next->thread.entry_sp - 17 * 8);
        u64 rip = slot[17]; /* r15..rax(15), vec, err, rip, cs, ... */
        u64 cs = slot[18];
        if (cs != 0x08 && cs != 0x1b) {
            printk("diag: CORRUPT slot for %s/%u entry_sp=%p cs=%llx rip=%p\n",
                   next->name, (unsigned)next->pid,
                   (void *)next->thread.entry_sp, cs, (void *)rip);
            for (int i = 0; i < 22; i++)
                printk("diag: slot[%d]=%p\n", i, (void *)slot[i]);
            for (;;)
                cpu_hlt();
        }
        /* The slot must still hold exactly what was last parked. A
         * mismatch means something overwrote the reserved slot between
         * park and handoff (resume would jump to garbage). */
        if (rip != next->thread.last_park_rip) {
            printk("diag: SLOT-MISMATCH for %s/%u: slot rip=%p last_park=%p cs=%llx\n",
                   next->name, (unsigned)next->pid,
                   (void *)rip, (void *)next->thread.last_park_rip, cs);
            for (int i = 0; i < 22; i++)
                printk("diag: slot[%d]=%p\n", i, (void *)slot[i]);
            for (;;)
                cpu_hlt();
        }
    }
    /* These fields must become visible atomically: with IF set in the
     * syscall body a tick can land between these writes, and a half
     * published handoff (e.g. next_sp of the target with the old CR3) makes
     * the epilogue resume a Frankenstein context. */
    cpu_cli();
    process_current_set(next);
    vmm_switch_to(next->space);
    x64_tss_set_rsp0(next->thread.kernel_stack_top);
    g_sched_next_fsbase = next->thread.fsbase;
    g_user_fsbase = next->thread.fsbase;
    /* Deliberately NOT g_user_fpu_area: the register bank still holds the
     * outgoing thread's state, and the epilogue saves it there before
     * restoring this image. */
    g_sched_next_fpu_area = (u64)(uintptr_t)&next->thread.fpu[0];
    g_sched_next_sp = next->thread.entry_sp;
    cpu_sti();
}

int sched_yield(struct x64_iframe *f)
{
    struct process *cur = process_current();
    if (!cur)
        return 0;
    /* A published-but-unconsumed handoff means this trap already parked
     * its frame: parking again would rewind `rip` a second time (landing
     * mid-instruction) and `cur` is already the handoff target, so the
     * slot would belong to the wrong thread. The caller must return to
     * the epilogue; resume re-traps from the parked frame. */
    if (g_sched_next_sp)
        return 1;
    struct process *next = sched_pick_next_runnable();
    if (next && next != cur) {
        /* Only rewind genuine post-`int $0x80` trap frames. Anything
         * else here means a stale frame reached the scheduler, which
         * would park a bogus RIP. */
        u8 insn[2] = { 0, 0 };
        if (copy_from_user(insn, (const void *)(f->rip - 2), 2) != 0 ||
            insn[0] != 0xcd || insn[1] != 0x80) {
            printk("diag: YIELD-REWIND refused for %s/%u: rip=%p rax=%llx vec=%llu bytes=%02x%02x\n",
                   cur->name, (unsigned)cur->pid,
                   (void *)f->rip, f->rax, f->vec, insn[0], insn[1]);
            x64_dump_iframe(f);
            for (;;)
                cpu_hlt();
        }
        /* Rewind past the 2-byte `int $0x80` BEFORE parking: resume
         * must re-enter the syscall from scratch. Parking the live
         * frame as-is would resume after the trap with rax still
         * holding the syscall number, so every blocking syscall that
         * handed off (waitpid, read, sleep) would "return" its own
         * number instead of the real result. */
        f->rip -= 2;
        thread_save_entry(&cur->thread, f);
        sched_handoff(next);
        return 1;
    }
    return 0;
}

struct process *sched_pick_next_runnable(void)
{
    struct process *cur = process_current();

    /* Round-robin scan: start one slot past the last winner so every
     * runnable process gets a tick before the cycle repeats. Without the
     * rotating start, the picker below would always hand the CPU to the
     * first non-current entry and starve every later process on the queue. */
    for (int k = 0; k < s_run_count; k++) {
        int i = (s_scan + k) % s_run_count;
        struct process *c = s_run[i];
        if (c == cur || c->state != PROCESS_STATE_RUNNING)
            continue;
        s_scan = (i + 1) % s_run_count;
        return c;
    }
    return NULL;
}

void sched_remove_runnable(struct process *p)
{
    cpu_cli();
    for (int i = 0; i < s_run_count; i++) {
        if (s_run[i] == p) {
            s_run[i] = s_run[s_run_count - 1];
            s_run_count--;
            break;
        }
    }
    cpu_sti();
}

void sched_visit(void (*fn)(struct process *p, void *arg), void *arg)
{
    if (!fn)
        return;
    cpu_cli();
    for (int i = 0; i < s_run_count; i++)
        fn(s_run[i], arg);
    for (int i = 0; i < s_zombie_count; i++)
        fn(s_zombie[i], arg);
    cpu_sti();
}

struct process *sched_find_process(pid_t pid)
{
    struct process *found = NULL;
    cpu_cli();
    for (int i = 0; i < s_run_count && !found; i++) {
        if (s_run[i]->pid == pid)
            found = s_run[i];
    }
    for (int i = 0; i < s_zombie_count && !found; i++) {
        if (s_zombie[i]->pid == pid)
            found = s_zombie[i];
    }
    cpu_sti();
    return found;
}

void sched_on_tick(struct x64_iframe *f)
{
    struct process *cur = process_current();

    sched_reap_stale();

    if (!cur || cur->state != PROCESS_STATE_RUNNING)
        return;

    /* Preemption switches between user contexts only. A tick taken while
     * the CPU is already in the kernel (inside a syscall) carries no
     * user ss/rsp to resume and its frame lives on the thread's live
     * kernel stack: saving it as entry_sp would both clobber the real
     * user resume point and make the epilogue iretq pop garbage as
     * RSP/SS. Kernel paths simply run until they return to ring 3. */
    if ((f->cs & 3) != 3)
        return;

    /* Record where the current thread was interrupted. The live frame is
     * parked into the thread's reserved slot: pointing entry_sp at the
     * live stack would let the thread's own next syscall overwrite its
     * resume point before the switch-back. */
    thread_save_entry(&cur->thread, f);

    struct process *next = sched_pick_next_runnable();
    if (next && next != cur)
        sched_handoff(next);
}