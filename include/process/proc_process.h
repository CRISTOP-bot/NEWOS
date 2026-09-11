#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <core/core_types.h>
#include <process/proc_thread.h>
#include <mm/mm_vmm.h>
#include <fs/vfs.h>

struct x64_iframe;

#define PROCESS_STATE_NEW      0
#define PROCESS_STATE_RUNNING  1
#define PROCESS_STATE_EXITED   2

#define PROCESS_MAX_FDS        16

#define PROCESS_EXIT_SIGNAL_BASE 128   /* Unix-style: 128 + signal */

struct fd_entry {
    struct vfs_file *file;
};

struct process {
    pid_t pid;
    pid_t ppid;
    char name[32];
    struct vmm_address_space *space;
    struct thread thread;
    struct fd_entry fds[PROCESS_MAX_FDS];
    uintptr_t user_stack_start;
    u64 user_stack_size;
    uintptr_t entry;
    int state;
    int exit_code;
    int is_init;        /* the boot-launched process (kernel boots into it) */
};

/* Load an ELF64 file from the VFS into a new process. Returns NULL on any
 * error (bad image, ENOMEM, unusable entry, ...). */
struct process *process_create_from_vfs(const char *path, const char *name);

/* Free all resources a process owns (must not run on the process's own
 * kernel stack). */
void process_free(struct process *p);

/* Terminate the current process with `code`. The init process (pid 1)
 * resumes the kernel boot flow; any other process becomes a zombie and the
 * next runnable process takes over. */
void process_exit(struct process *p, int code);

struct process *process_current(void);
void process_current_set(struct process *p);

/* User-mode fault/exception termination (called by the arch handler). */
void process_page_fault(struct x64_iframe *f, uintptr_t addr, u64 err);
void process_exception(struct x64_iframe *f, int vec, u64 err);

#endif