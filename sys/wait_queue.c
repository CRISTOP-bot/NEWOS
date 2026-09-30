#include <sys/wait_queue.h>
#include <core/core_printk.h>
#include <process/proc_process.h>
#include <process/sched.h>

void wait_queue_init(struct wait_queue_head *h) {
    h->head = NULL;
    spinlock_init(&h->lock, "waitq");
}

void wait_queue_add(struct wait_queue_head *h, struct wait_queue_entry *entry) {
    u64 flags = spinlock_acquire_irqsave(&h->lock);
    entry->next = h->head;
    h->head = entry;
    spinlock_release_irqrestore(&h->lock, flags);
}

void wait_queue_remove(struct wait_queue_head *h, struct wait_queue_entry *entry) {
    u64 flags = spinlock_acquire_irqsave(&h->lock);
    struct wait_queue_entry *prev = NULL;
    struct wait_queue_entry *curr = h->head;
    while (curr) {
        if (curr == entry) {
            if (prev) prev->next = curr->next;
            else h->head = curr->next;
            curr->next = NULL;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    spinlock_release_irqrestore(&h->lock, flags);
}

void wait_queue_wake_all(struct wait_queue_head *h) {
    u64 flags = spinlock_acquire_irqsave(&h->lock);
    struct wait_queue_entry *curr = h->head;
    while (curr) {
        if (curr->proc) {
            curr->proc->state = PROCESS_STATE_RUNNING;
        }
        curr = curr->next;
    }
    spinlock_release_irqrestore(&h->lock, flags);
}
