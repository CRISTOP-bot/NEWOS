#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H

#include <core/core_types.h>

struct process;
struct x64_iframe;

#define PROC_MAX_RUNNABLE 16
#define PROC_MAX_ZOMBIE   16

/* Handoff slot owned by the arch interrupt epilogue. The scheduler writes
 * the target thread's entry_sp here to preempt / hand the CPU over. */
extern u64 g_sched_next_sp;

/* FIFO for creation of runnable processes. */
void sched_init(void);
void sched_add_process(struct process *p);

/* Process became a zombie: drop it from the run queue, keep it alive until
 * waitpid (or the lazy reaper) frees it. Returns 0 unless queue overflow. */
int sched_zombify(struct process *p);
struct process *sched_zombie_find(pid_t pid);
void sched_zombie_reclaim(struct process *z);

/* Timer-tick preemption hook (runs inside an ISR, interrupts disabled).
 * Saves the interrupted frame into the current process and, if another
 * process is runnable, hands the CPU to it. */
void sched_on_tick(struct x64_iframe *f);

/* Used by process_exit() to (a) find a successor and (b) perform the actual
 * handoff when the caller returns through the interrupt epilogue. */
struct process *sched_pick_next_runnable(void);
void sched_handoff(struct process *next);
void sched_remove_runnable(struct process *p);

#endif