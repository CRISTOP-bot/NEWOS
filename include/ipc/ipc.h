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
    int   reader_active;
    int   writer_active;
};

void ipc_pipe_init(struct ipc_pipe *pipe);
size_t ipc_pipe_write(struct ipc_pipe *pipe, const void *data, size_t n);
size_t ipc_pipe_read(struct ipc_pipe *pipe, void *data, size_t n);
size_t ipc_pipe_avail(const struct ipc_pipe *pipe);
int    ipc_pipe_close_reader(struct ipc_pipe *pipe);
int    ipc_pipe_close_writer(struct ipc_pipe *pipe);

#endif