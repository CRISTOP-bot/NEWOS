#include <sys/work_queue.h>
#include <lib/kernel/iru_lock.h>
#include <core/core_printk.h>

#define WORK_QUEUE_SIZE 64

static work_item_t work_queue[WORK_QUEUE_SIZE];
static volatile int wq_head = 0;
static volatile int wq_tail = 0;
static struct spinlock wq_lock;

void work_queue_init(void) {
    spinlock_init(&wq_lock, "workq");
}

void work_queue_submit(work_fn_t fn, void *arg) {
    if (!fn) return;
    u64 flags = spinlock_acquire_irqsave(&wq_lock);
    int next_tail = (wq_tail + 1) % WORK_QUEUE_SIZE;
    if (next_tail == wq_head) {
        spinlock_release_irqrestore(&wq_lock, flags);
        return;
    }
    work_queue[wq_tail].fn = fn;
    work_queue[wq_tail].arg = arg;
    wq_tail = next_tail;
    spinlock_release_irqrestore(&wq_lock, flags);
}

bool work_queue_drain_one(void) {
    u64 flags = spinlock_acquire_irqsave(&wq_lock);
    if (wq_head == wq_tail) {
        spinlock_release_irqrestore(&wq_lock, flags);
        return false;
    }
    work_item_t item = work_queue[wq_head];
    wq_head = (wq_head + 1) % WORK_QUEUE_SIZE;
    spinlock_release_irqrestore(&wq_lock, flags);
    if (item.fn) {
        item.fn(item.arg);
    }
    return true;
}

void work_queue_drain_loop(void) {
    while (1) {
        while (work_queue_drain_one()) {
        }
        __asm__ volatile("sti; hlt; cli");
    }
}
