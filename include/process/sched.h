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
/* TLS FS bases the same epilogue applies on every return to ring 3:
 * g_user_fsbase for the thread currently being serviced,
 * g_sched_next_fsbase for the handoff target. */
extern u64 g_user_fsbase;
extern u64 g_sched_next_fsbase;

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

/* Cooperative yield for blocking syscalls (WAITPID/SLEEP/READ): if another
 * process is runnable, hand it the CPU when this call returns through the
 * interrupt epilogue. The caller's user frame `f` is parked (rewound past
 * `int $0x80`) as its resume point, so coming back re-executes the same
 * syscall instruction with the same arguments (retry, not arbitrary user
 * code): only idempotent wait loops may call this.
 *
 * Returns 1 when a handoff was published: the caller must return to the
 * epilogue at once (its frame is parked; resume re-traps). Continuing to
 * spin after a publish would run with a stale `current`, double-rewind
 * `rip` and park into the wrong thread's slot. Returns 0 when nobody else
 * is runnable (the frame was left untouched; the caller keeps spinning). */
int sched_yield(struct x64_iframe *f);

/* Used by process_exit() to (a) find a successor and (b) perform the actual
 * handoff when the caller returns through the interrupt epilogue. */
struct process *sched_pick_next_runnable(void);
void sched_handoff(struct process *next);
void sched_remove_runnable(struct process *p);

/* Introspection for SYS_PS / SYS_KILL (runs with interrupts disabled;
 * the callback must not block or allocate). */
void sched_visit(void (*fn)(struct process *p, void *arg), void *arg);
struct process *sched_find_process(pid_t pid);   /* runnable or zombie */

#endif