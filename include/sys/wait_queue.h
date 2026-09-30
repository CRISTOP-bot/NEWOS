#ifndef WAIT_QUEUE_H
#define WAIT_QUEUE_H

#include <core/core_types.h>
#include <lib/kernel/iru_lock.h>

struct process;

struct wait_queue_entry {
    struct process *proc;
    struct wait_queue_entry *next;
};

struct wait_queue_head {
    struct wait_queue_entry *head;
    struct spinlock lock;
};

void wait_queue_init(struct wait_queue_head *h);
void wait_queue_add(struct wait_queue_head *h, struct wait_queue_entry *entry);
void wait_queue_remove(struct wait_queue_head *h, struct wait_queue_entry *entry);
void wait_queue_wake_all(struct wait_queue_head *h);

#endif
