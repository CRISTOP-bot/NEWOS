#include <sys/tty.h>
#include <lib/kernel/iru_lock.h>
#include <core/core_printk.h>

static tty_t g_ttys[TTY_COUNT];

void tty_init(void) {
    for (int i = 0; i < TTY_COUNT; i++) {
        g_ttys[i].id = i;
        g_ttys[i].used = false;
        g_ttys[i].fg_pid = -1;
        g_ttys[i].ws.ws_row = 25;
        g_ttys[i].ws.ws_col = 80;
        g_ttys[i].in_queue.head = g_ttys[i].in_queue.tail = 0;
        g_ttys[i].out_queue.head = g_ttys[i].out_queue.tail = 0;
    }
    for (int i = 0; i < TTY_COUNT; i++) {
        spinlock_init(&g_ttys[i].lock, "tty");
    }
}

int tty_create(void) {
    for (int i = 0; i < TTY_COUNT; i++) {
        if (!g_ttys[i].used) {
            g_ttys[i].used = true;
            g_ttys[i].fg_pid = -1;
            return i;
        }
    }
    return -1;
}

tty_t *tty_get(int id) {
    if (id < 0 || id >= TTY_COUNT) return NULL;
    return &g_ttys[id];
}

void tty_write(int id, const char *data, u32 len) {
    tty_t *t = tty_get(id);
    if (!t) return;
    u64 flags = spinlock_acquire_irqsave(&t->lock);
    for (u32 i = 0; i < len; i++) {
        u32 next = (t->out_queue.head + 1) % TTY_OUT_QUEUE_SIZE;
        if (next != t->out_queue.tail) {
            t->out_queue.buffer[t->out_queue.head] = data[i];
            t->out_queue.head = next;
        }
    }
    spinlock_release_irqrestore(&t->lock, flags);
}

int tty_read(int id, char *buf, u32 len) {
    tty_t *t = tty_get(id);
    if (!t) return 0;
    u64 flags = spinlock_acquire_irqsave(&t->lock);
    u32 count = 0;
    while (t->in_queue.head != t->in_queue.tail && count < len) {
        buf[count++] = t->in_queue.buffer[t->in_queue.tail];
        t->in_queue.tail = (t->in_queue.tail + 1) % TTY_IN_QUEUE_SIZE;
    }
    spinlock_release_irqrestore(&t->lock, flags);
    return count;
}

void tty_push_key(int id, u8 scancode) {
    tty_t *t = tty_get(id);
    if (!t) return;
    u64 flags = spinlock_acquire_irqsave(&t->lock);
    u32 next = (t->in_queue.head + 1) % TTY_IN_QUEUE_SIZE;
    if (next != t->in_queue.tail) {
        t->in_queue.buffer[t->in_queue.head] = scancode;
        t->in_queue.head = next;
    }
    spinlock_release_irqrestore(&t->lock, flags);
}

void tty_push_char(int id, u8 c) {
    tty_t *t = tty_get(id);
    if (!t) return;
    u64 flags = spinlock_acquire_irqsave(&t->lock);
    u32 next = (t->in_queue.head + 1) % TTY_IN_QUEUE_SIZE;
    if (next != t->in_queue.tail) {
        t->in_queue.buffer[t->in_queue.head] = c;
        t->in_queue.head = next;
    }
    spinlock_release_irqrestore(&t->lock, flags);
}

int tty_read_key(int id, u8 *buf, u32 len) {
    return tty_read(id, (char*)buf, len);
}

int tty_set_foreground(int id, int pid) {
    tty_t *t = tty_get(id);
    if (!t) return -1;
    t->fg_pid = pid;
    return 0;
}

int tty_get_foreground(int id) {
    tty_t *t = tty_get(id);
    if (!t) return -1;
    return t->fg_pid;
}
