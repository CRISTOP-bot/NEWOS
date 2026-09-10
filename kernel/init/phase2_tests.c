#include <kernel/printk.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/elf.h>
#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/usercopy.h>
#include <kernel/kmalloc.h>
#include <uapi/syscall.h>
#include <arch/x86_64/iframe.h>
#include <libk/string.h>

/* Phase 2 unit tests: PMM phys wrappers, address-space lifecycle + CR3
 * switching, user-range validation/usercopy, ELF loading, and syscall
 * dispatch. Each returns 0 on success. */

/* ---------------- data helpers ---------------- */

static int load_file(const char *path, u8 **out, size_t *out_len)
{
    struct vfs_file *f = vfs_open(path, VFS_O_READ);
    if (!f)
        return -1;

    size_t cap = 4096, len = 0;
    u8 *buf = kmalloc(cap);
    if (!buf) {
        vfs_close(f);
        return -1;
    }
    for (;;) {
        if (len == cap) {
            cap *= 2;
            u8 *nb = kmalloc(cap);
            if (!nb) {
                kfree(buf);
                vfs_close(f);
                return -1;
            }
            memcpy(nb, buf, len);
            kfree(buf);
            buf = nb;
        }
        int n = vfs_read(f, buf + len, cap - len);
        if (n <= 0)
            break;
        len += (size_t)n;
    }
    vfs_close(f);

    *out = buf;
    *out_len = len;
    return 0;
}

/* ---------------- tests ---------------- */

static int ktest2_phys(void)
{
    if (!pmm_is_initialized())
        return -1;

    /* Alloc marks a frame used; freeing returns it to the free pool. */
    u64 f = PMM_INVALID_FRAME;
    if (phys_alloc_frame(&f) != 0 || f == PMM_INVALID_FRAME)
        return -1;
    if (phys_is_reserved(f) == 0) {
        phys_free_frame(f);
        return -1;
    }
    phys_free_frame(f);
    if (phys_is_reserved(f) != 0)
        return -1;

    /* Reserve/release a low frame (part of the kernel image reservation:
     * the first tracked frame 0x100000 is reserved at boot). */
    u64 base = 0x100000;
    if (phys_is_reserved(base >> PAGE_SHIFT) == 0)
        return -1;
    phys_release_range(base, base + 0x1000);
    if (phys_is_reserved(base >> PAGE_SHIFT) != 0)
        return -1;
    phys_reserve_range(base, base + 0x1000);
    if (phys_is_reserved(base >> PAGE_SHIFT) == 0)
        return -1;

    /* Block alloc/free round trip. */
    u64 b = PMM_INVALID_FRAME;
    if (phys_alloc_block(2, &b) != 0)
        return -1;
    if (phys_is_reserved(b) == 0) {
        phys_free_block(b, 2);
        return -1;
    }
    phys_free_block(b, 2);
    return 0;
}

static int ktest2_address_space(void)
{
    struct vmm_address_space *as;
    if (vmm_address_space_create("test-as", &as) != 0)
        return -1;

    /* Kernel half must be inherited. */
    u32 kflags = 0;
    if (!vmm_page_lookup(as, KERNEL_BASE_VA, NULL, &kflags))
        return -1;
    if (!(kflags & VMM_PRESENT))
        return -1;

    uintptr_t va = (uintptr_t)USER_SPACE_BASE;
    if (vmm_alloc_page(as, va, VMM_USER | VMM_WRITE | VMM_NX) != 0)
        goto fail;

    u32 flags = 0;
    if (!vmm_page_lookup(as, va, NULL, &flags))
        goto fail;
    if ((flags & (VMM_PRESENT | VMM_USER)) != (VMM_PRESENT | VMM_USER))
        goto fail;

    if (vmm_validate_user_range(as, va, PAGE_SIZE) != 0)
        goto fail;
    if (vmm_validate_user_range(as, va - 1, PAGE_SIZE) == 0)
        goto fail;
    if (vmm_validate_user_range(as, (uintptr_t)USER_SPACE_END - 512, 1024) == 0)
        goto fail;

    if (vmm_unmap_pages(as, va, 1) != 0)
        goto fail;

    /* Kernel-half mappings must be rejected. */
    if (vmm_alloc_page(as, KERNEL_BASE_VA, VMM_PRESENT) == 0)
        goto fail;

    vmm_address_space_destroy(as);
    return 0;

fail:
    vmm_address_space_destroy(as);
    return -1;
}

static int ktest2_usercopy(void)
{
    struct vmm_address_space *as;
    if (vmm_address_space_create("copy-as", &as) != 0)
        return -1;

    uintptr_t va = (uintptr_t)USER_SPACE_BASE;
    if (vmm_alloc_page(as, va, VMM_USER | VMM_WRITE | VMM_NX) != 0)
        goto fail;

    if (vmm_switch_to(as) != 0)
        goto fail;

    static const char msg[] = "usercopy-data";
    char out[32] = { 0 };

    /* Write the payload via kernel map, then read it back as a guest. */
    u32 f = 0;
    uintptr_t phys = 0;
    if (!vmm_page_lookup(as, va, &phys, &f))
        goto fail_back;
    memcpy((void *)phys_to_virt(phys), msg, sizeof(msg));
    if (copy_from_user(out, (const void *)va, sizeof(msg)) != 0)
        goto fail_back;
    if (memcmp(out, msg, sizeof(msg)) != 0)
        goto fail_back;
    if (copy_to_user((void *)va, msg, sizeof(msg)) != 0)
        goto fail_back;

    /* Invalid ranges must be refused, not copied. */
    if (copy_from_user(out, (const void *)(USER_SPACE_END - 64), 128) == 0)
        goto fail_back;

    if (vmm_switch_to(vmm_kernel_space()) != 0)
        goto fail;

    vmm_address_space_destroy(as);
    return 0;

fail_back:
    vmm_switch_to(vmm_kernel_space());
fail:
    vmm_address_space_destroy(as);
    return -1;
}

static int ktest2_elf(void)
{
    u8 *buf;
    size_t len;
    if (load_file("/bin/hello", &buf, &len) != 0)
        return -1;

    struct elf_image img = { .data = buf, .size = len };
    if (elf_validate(&img) != 0)
        goto fail;

    struct vmm_address_space *as;
    if (vmm_address_space_create("elf-as", &as) != 0)
        goto fail;

    if (elf_load_segments(&img, as) != 0)
        goto fail_as;
    if (elf_entry_valid(&img, as) != 0)
        goto fail_as;

    /* Entry page must be present, user, executable (no NX). */
    u32 flags = 0;
    if (!vmm_page_lookup(as, img.entry, NULL, &flags))
        goto fail_as;
    if (!(flags & VMM_PRESENT) || !(flags & VMM_USER))
        goto fail_as;
    if (flags & VMM_NX)
        goto fail_as;

    /* A garbage image must be rejected. */
    {
        u8 bad[64];
        memset(bad, 0x41, sizeof(bad));
        struct elf_image bimg = { .data = bad, .size = sizeof(bad) };
        if (elf_validate(&bimg) == 0)
            goto fail_as;
    }

    vmm_address_space_destroy(as);
    kfree(buf);
    return 0;

fail_as:
    vmm_address_space_destroy(as);
fail:
    kfree(buf);
    return -1;
}

static int ktest2_syscall(void)
{
    /* GETPID with no current process must fail cleanly. */
    if (process_current() != NULL)
        return -1;

    struct x64_iframe f;
    memset(&f, 0, sizeof(f));
    f.rax = SYS_GETPID;
    if (syscall_dispatch(&f) != SYSCALL_RET_ERROR)
        return -1;

    /* A real process: GETPID returns its pid, a bad fd is refused, and a
     * write through the serial console rounds-trips. */
    struct process *p = process_create_from_vfs("/bin/hello", "self-test");
    if (!p)
        return -1;

    process_current_set(p);
    f.rax = SYS_GETPID;
    f.rdi = f.rsi = f.rdx = 0;
    if (syscall_dispatch(&f) != (long)p->pid)
        goto fail;

    f.rax = SYS_WRITE;
    f.rdi = PROCESS_MAX_FDS + 1;        /* bogus fd */
    f.rsi = (u64)USER_SPACE_BASE;
    f.rdx = 4;
    if (syscall_dispatch(&f) != SYSCALL_RET_ERROR)
        goto fail;

    /* Valid write: map a page in the process's own address space with a
     * message and push it out via SYS_WRITE. */
    uintptr_t va = (uintptr_t)USER_SPACE_BASE + (uintptr_t)USER_SPACE_END / 2;
    va &= ~(uintptr_t)(PAGE_SIZE - 1);
    if (vmm_alloc_page(p->space, va, VMM_USER | VMM_WRITE | VMM_NX) != 0)
        goto fail;
    uintptr_t phys = 0;
    if (!vmm_page_lookup(p->space, va, &phys, NULL))
        goto fail;
    memcpy((void *)phys_to_virt(phys), "syscall-test\n", 13);

    if (vmm_switch_to(p->space) != 0)
        goto fail;
    f.rax = SYS_WRITE;
    f.rdi = STDOUT_FILENO;
    f.rsi = va;
    f.rdx = 13;
    if (syscall_dispatch(&f) != 13)
        goto fail_back;
    if (vmm_switch_to(vmm_kernel_space()) != 0)
        goto fail;

    process_current_set(NULL);
    process_free(p);
    return 0;

fail_back:
    vmm_switch_to(vmm_kernel_space());
fail:
    process_current_set(NULL);
    process_free(p);
    return -1;
}

/* ---------------- driver ---------------- */

struct ktest2 {
    const char *name;
    int (*fn)(void);
};

static const struct ktest2 ktests2[] = {
    { "pmm-phys",    ktest2_phys        },
    { "address-spaces", ktest2_address_space },
    { "usercopy",    ktest2_usercopy    },
    { "elf-loader",  ktest2_elf         },
    { "syscalls",    ktest2_syscall     },
};

int run_phase2_tests(void)
{
    int failures = 0;

    printk("=== NEWOS PHASE 2 TESTS ===\n");
    for (size_t i = 0; i < sizeof(ktests2) / sizeof(ktests2[0]); i++) {
        int rc = ktests2[i].fn();
        printk("  [%s] %s%s\n", rc == 0 ? "PASS" : "FAIL",
               ktests2[i].name, rc ? "  <-- failure" : "");
        if (rc)
            failures++;
    }
    printk("=== %zu tests, %d failures ===\n",
           sizeof(ktests2) / sizeof(ktests2[0]), failures);
    return failures;
}