#include <sys/pty.h>
#include <lib/kernel/iru_lock.h>
#include <core/core_printk.h>

static struct pty_pair g_ptys[PTY_MAX_COUNT];
static struct spinlock g_pty_global_lock;

static void pty_queue_push(u8 *buf, u32 *head, u32 *tail, u8 val, u32 size) {
    u32 next = (*head + 1) % size;
    if (next != *tail) {
        buf[*head] = val;
        *head = next;
    }
}

static int pty_queue_pop(u8 *buf, u32 *head, u32 *tail, u8 *out, u32 len, u32 size) {
    u32 count = 0;
    while (*head != *tail && count < len) {
        out[count++] = buf[*tail];
        *tail = (*tail + 1) % size;
    }
    return count;
}

void pty_init(void) {
    for (int i = 0; i < PTY_MAX_COUNT; i++) {
        g_ptys[i].id = i;
        g_ptys[i].used = false;
        g_ptys[i].fg_pid = -1;
        g_ptys[i].ws.ws_row = 25;
        g_ptys[i].ws.ws_col = 80;
        g_ptys[i].m2s_head = g_ptys[i].m2s_tail = 0;
        g_ptys[i].s2m_head = g_ptys[i].s2m_tail = 0;
    }
    spinlock_init(&g_pty_global_lock, "pty_global");
}

struct pty_pair *pty_get(int pty_id) {
    if (pty_id < PTY_ID_BASE) return NULL;
    int idx = pty_id - PTY_ID_BASE;
    if (idx < 0 || idx >= PTY_MAX_COUNT) return NULL;
    return &g_ptys[idx];
}

int pty_create(void) {
    u64 flags = spinlock_acquire_irqsave(&g_pty_global_lock);
    for (int i = 0; i < PTY_MAX_COUNT; i++) {
        if (!g_ptys[i].used) {
            g_ptys[i].used = true;
            g_ptys[i].fg_pid = -1;
            spinlock_init(&g_ptys[i].lock, "pty");
            spinlock_release_irqrestore(&g_pty_global_lock, flags);
            return PTY_ID_BASE + i;
        }
    }
    spinlock_release_irqrestore(&g_pty_global_lock, flags);
    return -1;
}

int pty_destroy(int pty_id) {
    struct pty_pair *p = pty_get(pty_id);
    if (!p) return -1;
    u64 flags = spinlock_acquire_irqsave(&p->lock);
    p->used = false;
    p->fg_pid = -1;
    spinlock_release_irqrestore(&p->lock, flags);
    return 0;
}

int pty_write_output(int pty_id, const char *data, u32 len) {
    struct pty_pair *p = pty_get(pty_id);
    if (!p || !p->used) return 0;
    u64 flags = spinlock_acquire_irqsave(&p->lock);
    for (u32 i = 0; i < len; i++) {
        pty_queue_push(p->slave_to_master, &p->s2m_head, &p->s2m_tail, (u8)data[i], PTY_QUEUE_SIZE);
    }
    spinlock_release_irqrestore(&p->lock, flags);
    return len;
}

int pty_read_output(int pty_id, char *buf, u32 len) {
    struct pty_pair *p = pty_get(pty_id);
    if (!p || !p->used) return 0;
    u64 flags = spinlock_acquire_irqsave(&p->lock);
    int ret = pty_queue_pop(p->slave_to_master, &p->s2m_head, &p->s2m_tail, (u8*)buf, len, PTY_QUEUE_SIZE);
    spinlock_release_irqrestore(&p->lock, flags);
    return ret;
}

int pty_write_input(int pty_id, const char *buf, u32 len) {
    struct pty_pair *p = pty_get(pty_id);
    if (!p || !p->used) return 0;
    u64 flags = spinlock_acquire_irqsave(&p->lock);
    for (u32 i = 0; i < len; i++) {
        pty_queue_push(p->master_to_slave, &p->m2s_head, &p->m2s_tail, (u8)buf[i], PTY_QUEUE_SIZE);
    }
    spinlock_release_irqrestore(&p->lock, flags);
    return len;
}

int pty_read_input(int pty_id, char *buf, u32 len) {
    struct pty_pair *p = pty_get(pty_id);
    if (!p || !p->used) return 0;
    u64 flags = spinlock_acquire_irqsave(&p->lock);
    int ret = pty_queue_pop(p->master_to_slave, &p->m2s_head, &p->m2s_tail, (u8*)buf, len, PTY_QUEUE_SIZE);
    spinlock_release_irqrestore(&p->lock, flags);
    return ret;
}

int pty_set_foreground(int pty_id, int pid) {
    struct pty_pair *p = pty_get(pty_id);
    if (!p) return -1;
    p->fg_pid = pid;
    return 0;
}

int pty_get_foreground(int pty_id) {
    struct pty_pair *p = pty_get(pty_id);
    if (!p) return -1;
    return p->fg_pid;
}
