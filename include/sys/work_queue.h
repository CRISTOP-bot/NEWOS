#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include <core/core_types.h>

typedef void (*work_fn_t)(void *arg);

typedef struct {
    work_fn_t fn;
    void *arg;
} work_item_t;

void work_queue_submit(work_fn_t fn, void *arg);
bool work_queue_drain_one(void);
void work_queue_drain_loop(void);

#endif
