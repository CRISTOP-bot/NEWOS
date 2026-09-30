#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <core/core_types.h>
#include <process/proc_thread.h>
#include <mm/mm_vmm.h>
#include <mm/mm_heap.h>
#include <fs/vfs.h>

struct x64_iframe;

#define PROCESS_STATE_NEW      0
#define PROCESS_STATE_RUNNING  1
#define PROCESS_STATE_EXITED   2

#define PROCESS_MAX_FDS        16
#define PROCESS_CWD_SIZE       256
/* Fixed base where anonymous SYS_MMAP regions start being handed out. */
#define PROCESS_MMAP_BASE      0x10000000ull

/* Socket states */
#define SOCKET_CLOSED    0
#define SOCKET_BOUND     1
#define SOCKET_LISTEN    2
#define SOCKET_CONNECTED 3
#define SOCKET_ESTABLISHED 4

/* Socket types */
#define SOCK_STREAM      1
#define SOCK_DGRAM       2
#define SOCK_DGRAM       2

/* Protocols */
#define PROTO_TCP        6
#define PROTO_UDP        17
#define PROTO_ICMP       1

/* Socket state per-FD */
struct socket_state {
    int fd;
    int domain;
    int type;
    int protocol;
    int proto;        /* internal: TCP/UDP/RAM */
    int state;
    u16 port;
};

#define PROCESS_EXIT_SIGNAL_BASE 128   /* Unix-style: 128 + signal */

struct fd_entry {
    struct vfs_file *file;
    void *data;
};

struct process {
    pid_t pid;
    pid_t ppid;
    char name[32];
    struct vmm_address_space *space;
    struct thread thread;
    struct fd_entry fds[PROCESS_MAX_FDS];
    char cwd[PROCESS_CWD_SIZE];   /* per-process working directory */
    uintptr_t user_stack_start;
    u64 user_stack_size;
    uintptr_t entry;
    /* Dynamic user memory (SYS_MMAP / SYS_BRK). mmap regions are handed
     * out from a bump cursor; the brk heap grows from the image end. */
    uintptr_t mmap_next;
    uintptr_t brk_cur;
    uintptr_t image_end;      /* first VA past the ELF segments */
    int state;
    int exit_code;
    int is_init;        /* the boot-launched process (kernel boots into it) */
};

/* Load an ELF64 file from the VFS into a new process. Returns NULL on any
 * error (bad image, ENOMEM, unusable entry, ...). */
struct process *process_create_from_vfs(const char *path, const char *name);

/* Same, but lays out argc/argv on the fresh user stack (SysV: rdi = argc,
 * rsi = argv) so spawned programs receive real command-line arguments.
 * argv holds up to 16 NUL-terminated strings (< 128 chars each). */
struct process *process_create_args(const char *path, const char *name,
                                    int argc, char argv[][128]);

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