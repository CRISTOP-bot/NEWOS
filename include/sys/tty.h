#ifndef TTY_H
#define TTY_H

#include <core/core_types.h>
#include <lib/kernel/iru_lock.h>

#define TTY_COUNT 4
#define TTY_IN_QUEUE_SIZE 1024
#define TTY_OUT_QUEUE_SIZE 2048

struct winsize {
    u16 ws_row;
    u16 ws_col;
    u16 ws_xpixel;
    u16 ws_ypixel;
};

typedef struct {
    u8 buffer[TTY_IN_QUEUE_SIZE];
    u32 head;
    u32 tail;
    struct spinlock lock;
} tty_queue_t;

typedef struct {
    int id;
    bool used;
    bool is_serial;
    u16 serial_port;
    struct winsize ws;
    tty_queue_t in_queue;
    tty_queue_t out_queue;
    int fg_pid;
    struct spinlock lock;
} tty_t;

void tty_init(void);
int tty_create(void);
tty_t *tty_get(int id);
void tty_write(int id, const char *data, u32 len);
int tty_read(int id, char *buf, u32 len);
void tty_push_key(int id, u8 scancode);
void tty_push_char(int id, u8 c);
int tty_read_key(int id, u8 *buf, u32 len);
int tty_set_foreground(int id, int pid);
int tty_get_foreground(int id);

#endif
