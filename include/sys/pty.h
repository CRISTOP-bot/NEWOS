#ifndef PTY_H
#define PTY_H

#include <core/core_types.h>
#include <lib/kernel/iru_lock.h>

#define PTY_MAX_COUNT 64
#define PTY_QUEUE_SIZE 4096
#define PTY_ID_BASE 1024

struct winsize {
    u16 ws_row;
    u16 ws_col;
    u16 ws_xpixel;
    u16 ws_ypixel;
};

struct pty_pair {
    int id;
    bool used;
    u8 master_to_slave[PTY_QUEUE_SIZE];
    u8 slave_to_master[PTY_QUEUE_SIZE];
    u32 m2s_head, m2s_tail, s2m_head, s2m_tail;
    int fg_pid;
    struct winsize ws;
    struct spinlock lock;
};

void pty_init(void);
int pty_create(void);
int pty_destroy(int pty_id);
struct pty_pair *pty_get(int pty_id);
int pty_write_output(int pty_id, const char *data, u32 len);
int pty_read_output(int pty_id, char *buf, u32 len);
int pty_write_input(int pty_id, const char *buf, u32 len);
int pty_read_input(int pty_id, char *buf, u32 len);
int pty_set_foreground(int pty_id, int pid);
int pty_get_foreground(int pty_id);

#endif
