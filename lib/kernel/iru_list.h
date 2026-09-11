#ifndef LIBK_LIST_H
#define LIBK_LIST_H

#include <core/core_types.h>

struct list_node {
    struct list_node *next;
    struct list_node *prev;
};

#define LIST_INIT(name) { &(name), &(name) }

static inline void list_init(struct list_node *head)
{
    head->next = head;
    head->prev = head;
}

static inline void list_insert_after(struct list_node *pos,
                                     struct list_node *node)
{
    node->next = pos->next;
    node->prev = pos;
    pos->next->prev = node;
    pos->next = node;
}

static inline void list_insert_before(struct list_node *pos,
                                      struct list_node *node)
{
    node->prev = pos->prev;
    node->next = pos;
    pos->prev->next = node;
    pos->prev = node;
}

static inline void list_push_back(struct list_node *head,
                                  struct list_node *node)
{
    list_insert_before(head, node);
}

static inline void list_push_front(struct list_node *head,
                                   struct list_node *node)
{
    list_insert_after(head, node);
}

static inline void list_remove(struct list_node *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

static inline int list_is_empty(const struct list_node *head)
{
    return head->next == head;
}

static inline struct list_node *list_pop_front(struct list_node *head)
{
    if (list_is_empty(head))
        return NULL;
    struct list_node *node = head->next;
    list_remove(node);
    return node;
}

struct list_node *list_last(struct list_node *head);

#define LIST_FOR_EACH(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

#define LIST_FOR_EACH_SAFE(pos, tmp, head) \
    for ((pos) = (head)->next, (tmp) = (pos)->next; (pos) != (head); \
         (pos) = (tmp), (tmp) = (pos)->next)

#define LIST_NODE_ENTRY(ptr, type, member) \
    ((type *)((char *)(ptr) - (unsigned long)&((type *)0)->member))

#endif