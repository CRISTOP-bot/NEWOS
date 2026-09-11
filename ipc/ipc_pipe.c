#include <ipc/ipc.h>
#include <iru_string.h>

void ipc_pipe_init(struct ipc_pipe *pipe)
{
    pipe->head = 0;
    pipe->tail = 0;
    pipe->len = 0;
    pipe->reader_active = 1;
    pipe->writer_active = 1;
}

size_t ipc_pipe_write(struct ipc_pipe *pipe, const void *data, size_t n)
{
    if (!pipe->reader_active)
        return 0;

    const u8 *src = (const u8 *)data;
    size_t space = IPC_PIPE_CAPACITY - pipe->len;
    size_t m = (n < space) ? n : space;

    for (size_t i = 0; i < m; i++) {
        pipe->buffer[pipe->head] = src[i];
        pipe->head = (pipe->head + 1) % IPC_PIPE_CAPACITY;
    }
    pipe->len += m;
    return m;
}

size_t ipc_pipe_read(struct ipc_pipe *pipe, void *data, size_t n)
{
    u8 *dst = (u8 *)data;
    size_t m = (n < pipe->len) ? n : pipe->len;

    for (size_t i = 0; i < m; i++) {
        dst[i] = pipe->buffer[pipe->tail];
        pipe->tail = (pipe->tail + 1) % IPC_PIPE_CAPACITY;
    }
    pipe->len -= m;
    return m;
}

size_t ipc_pipe_avail(const struct ipc_pipe *pipe)
{
    return pipe->len;
}

int ipc_pipe_close_reader(struct ipc_pipe *pipe)
{
    pipe->reader_active = 0;
    return 0;
}

int ipc_pipe_close_writer(struct ipc_pipe *pipe)
{
    pipe->writer_active = 0;
    return 0;
}