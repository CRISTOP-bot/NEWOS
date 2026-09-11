#ifndef KERNEL_USERCOPY_H
#define KERNEL_USERCOPY_H

#include <core/core_types.h>

/* User <-> kernel memory transfer.
 *
 * Every user pointer is validated against the *current* address space
 * before use: the whole range must be mapped present + USER and reside
 * inside the user region. The kernel then copies directly (single-CPU,
 * single-threaded in this phase, so a validated range cannot disappear
 * under us). Returns 0 on success, -1 on any invalid range. */

int user_range_valid(const void *user_ptr, size_t len);
int copy_from_user(void *dst, const void *user_ptr, size_t len);
int copy_to_user(void *user_ptr, const void *src, size_t len);

#endif