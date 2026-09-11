#include <mm/mm_usercopy.h>
#include <mm/mm_vmm.h>
#include <core/core.h>
#include <iru_string.h>

/* User pointer validation and copying.
 *
 * All user pointers travel through these helpers; the kernel never
 * dereferences a user-controlled address directly. Ranges must be fully
 * mapped, present + USER and inside the active user region (single-CPU,
 * single-threaded, so no unmapping can race a validated copy). */

int user_range_valid(const void *user_ptr, size_t len)
{
    return vmm_validate_user_range(vmm_current(), (uintptr_t)user_ptr, len);
}

int copy_from_user(void *dst, const void *user_ptr, size_t len)
{
    if (len && user_range_valid(user_ptr, len) != 0)
        return -1;
    if (len)
        memcpy(dst, user_ptr, len);
    return 0;
}

int copy_to_user(void *user_ptr, const void *src, size_t len)
{
    if (len && user_range_valid(user_ptr, len) != 0)
        return -1;
    if (len)
        memcpy(user_ptr, src, len);
    return 0;
}