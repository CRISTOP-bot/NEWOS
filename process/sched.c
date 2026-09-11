#include <process/sched.h>
#include <process/proc_process.h>
#include <process/proc_thread.h>
#include <mm/mm_vmm.h>
#include <core/core_printk.h>
#include <core/core_panic.h>
#include <x86_cpu.h>
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
    process_current_set(next);
    vmm_switch_to(next->space);
    x64_tss_set_rsp0(next->thread.kernel_stack_top);
    g_sched_next_sp = next->thread.entry_sp;
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

void sched_on_tick(struct x64_iframe *f)
{
    struct process *cur = process_current();

    sched_reap_stale();

    if (!cur || cur->state != PROCESS_STATE_RUNNING)
        return;

    /* Record where the current thread was interrupted. entry_sp keeps
     * pointing at this frame until the thread next preempts/resumes, so
     * only the pointer needs updating here. */
    cur->thread.entry_sp = (uintptr_t)&f->rip;

    struct process *next = sched_pick_next_runnable();
    if (next && next != cur)
        sched_handoff(next);
}