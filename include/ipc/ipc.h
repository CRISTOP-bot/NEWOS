#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <core/core_types.h>

/* Inter-Process Communication primitives.
 *
 * The pipe is a byte-oriented, bounded, in-memory channel with blocking
 * semantics added once the scheduler exists. In the early phase the channel
 * is a single-consumer/single-producer FIFO over a kernel buffer. */

#define IPC_PIPE_CAPACITY 4096

struct ipc_pipe {
    u8  buffer[IPC_PIPE_CAPACITY];
    size_t head;
    size_t tail;
    size_t len;
    /* Open-end reference counts: EOF when writers hit 0, EPIPE when
     * readers hit 0. One increment per fd that names this pipe end. */
    int   reader_count;
    int   writer_count;
};

void ipc_pipe_init(struct ipc_pipe *pipe);
size_t ipc_pipe_write(struct ipc_pipe *pipe, const void *data, size_t n);
size_t ipc_pipe_read(struct ipc_pipe *pipe, void *data, size_t n);
size_t ipc_pipe_avail(const struct ipc_pipe *pipe);
int    ipc_pipe_close_reader(struct ipc_pipe *pipe);
int    ipc_pipe_close_writer(struct ipc_pipe *pipe);

/* Per-fd pipe handle. Stored in process fd slots; the magic field lets
 * the syscall layer tell a pipe fd from a plain file or socket. */
#define IPC_PIPE_FD_MAGIC 0x50495046u   /* "PIPF" */

struct ipc_pipe_fd {
    u32 magic;
    struct ipc_pipe *pipe;
    int is_reader;
};

/* Allocate + init a fresh pipe. */
struct ipc_pipe *ipc_pipe_new(void);

/* Allocate a handle for one end; bumps the matching refcount. */
struct ipc_pipe_fd *ipc_pipe_fd_new(struct ipc_pipe *pipe, int is_reader);

/* Release one fd's reference (bumps counts down, frees the pipe when
 * both ends reach 0). Safe to call with NULL or non-pipe pointers. */
void ipc_pipe_fd_close(struct ipc_pipe_fd *pf);

#endif