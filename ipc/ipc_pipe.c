#include <ipc/ipc.h>
#include <mm/mm_heap.h>
#include <iru_string.h>

void ipc_pipe_init(struct ipc_pipe *pipe)
{
    pipe->head = 0;
    pipe->tail = 0;
    pipe->len = 0;
    /* A bare pipe has no open ends: each ipc_pipe_fd_new bumps one. */
    pipe->reader_count = 0;
    pipe->writer_count = 0;
}

struct ipc_pipe *ipc_pipe_new(void)
{
    struct ipc_pipe *pipe = kzalloc(sizeof(*pipe));
    if (pipe)
        ipc_pipe_init(pipe);
    return pipe;
}

struct ipc_pipe_fd *ipc_pipe_fd_new(struct ipc_pipe *pipe, int is_reader)
{
    struct ipc_pipe_fd *pf;

    if (!pipe)
        return NULL;
    pf = kzalloc(sizeof(*pf));
    if (!pf)
        return NULL;
    pf->magic = IPC_PIPE_FD_MAGIC;
    pf->pipe = pipe;
    pf->is_reader = is_reader;
    if (is_reader)
        pipe->reader_count++;
    else
        pipe->writer_count++;
    return pf;
}

void ipc_pipe_fd_close(struct ipc_pipe_fd *pf)
{
    struct ipc_pipe *pipe;

    if (!pf || pf->magic != IPC_PIPE_FD_MAGIC)
        return;
    pf->magic = 0;
    pipe = pf->pipe;
    if (pf->is_reader) {
        if (pipe->reader_count > 0)
            pipe->reader_count--;
    } else if (pipe->writer_count > 0) {
        pipe->writer_count--;
    }
    kfree(pf);
    if (!pipe->reader_count && !pipe->writer_count)
        kfree(pipe);
}

size_t ipc_pipe_write(struct ipc_pipe *pipe, const void *data, size_t n)
{
    if (!pipe || !data)
        return 0;
    if (pipe->reader_count == 0)
        return 0;
    if (pipe->len > IPC_PIPE_CAPACITY)
        pipe->len = IPC_PIPE_CAPACITY;   /* clamp corrupted state */

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
    if (!pipe || !data)
        return 0;
    if (pipe->len > IPC_PIPE_CAPACITY)
        pipe->len = IPC_PIPE_CAPACITY;
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
    if (pipe->reader_count > 0)
        pipe->reader_count--;
    return pipe->reader_count;
}

int ipc_pipe_close_writer(struct ipc_pipe *pipe)
{
    if (pipe->writer_count > 0)
        pipe->writer_count--;
    return pipe->writer_count;
}