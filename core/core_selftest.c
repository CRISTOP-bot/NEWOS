#include <core/core_printk.h>
#include <drivers/pit_timer.h>
#include <process/sched.h>
#include <mm/mm_pmm.h>
#include <mm/mm_vmm.h>
#include <process/proc_elf.h>
#include <process/proc_process.h>
#include <syscall/syscall.h>
#include <fs/vfs.h>
#include <mm/mm_usercopy.h>
#include <mm/mm_heap.h>
#include <abi/syscall_abi.h>
#include <x86_frame.h>
#include <x86_cpu.h>
#include <x86_fpu.h>
#include <iru_string.h>

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

/* ---------------- x87/SSE state ---------------- */

/* Three images plus the one the registers held on entry. All 16-byte aligned:
 * FXSAVE/FXRSTOR fault with #GP on a misaligned operand. */
static u8 fpu_live[X64_FPU_AREA_SIZE] __attribute__((aligned(X64_FPU_AREA_ALIGN)));
static u8 fpu_a[X64_FPU_AREA_SIZE] __attribute__((aligned(X64_FPU_AREA_ALIGN)));
static u8 fpu_b[X64_FPU_AREA_SIZE] __attribute__((aligned(X64_FPU_AREA_ALIGN)));

static u16 fpu_get_u16(const u8 *a, u64 off)
{
    u16 v;
    memcpy(&v, a + off, sizeof(v));
    return v;
}

static u32 fpu_get_u32(const u8 *a, u64 off)
{
    u32 v;
    memcpy(&v, a + off, sizeof(v));
    return v;
}

static void fpu_put_u16(u8 *a, u64 off, u16 v) { memcpy(a + off, &v, sizeof(v)); }
static void fpu_put_u32(u8 *a, u64 off, u32 v) { memcpy(a + off, &v, sizeof(v)); }
static void fpu_put_u64(u8 *a, u64 off, u64 v) { memcpy(a + off, &v, sizeof(v)); }

/* The fields the register bank must carry through a save/restore pair.
 * FSW and the FOP/FIP/FDP pointer fields are deliberately left out: the CPU
 * does not restore them as written, so comparing them would be flaky. */
static int fpu_cmp(const u8 *x, const u8 *y)
{
    if (fpu_get_u16(x, X64_FPU_FCW_OFF) != fpu_get_u16(y, X64_FPU_FCW_OFF))
        return -1;
    if (x[X64_FPU_FTW_OFF] != y[X64_FPU_FTW_OFF])
        return -1;
    if (fpu_get_u32(x, X64_FPU_MXCSR_OFF) != fpu_get_u32(y, X64_FPU_MXCSR_OFF))
        return -1;
    for (int i = 0; i < 16; i++)
        if (memcmp(x + X64_FPU_XMM_OFF + (u64)i * 16,
                   y + X64_FPU_XMM_OFF + (u64)i * 16, 16) != 0)
            return -1;
    for (int i = 0; i < 8; i++)
        if (memcmp(x + X64_FPU_ST_OFF + (u64)i * 16,
                   y + X64_FPU_ST_OFF + (u64)i * 16, 10) != 0)
            return -1;
    return 0;
}

static int ktest2_fpu(void)
{
    if (!x64_fpu_available()) {
        printk("fpu: CPU has no FXSR - state handling not testable here\n");
        return 0;
    }
    /* Every gcc x86-64 build emits SSE2 for float/double: without it no real
     * user binary can run, so a missing SSE2 is a failure of this milestone. */
    if (!x64_fpu_sse2())
        return -1;

    /* x64_fpu_enable() must have left the state accessible (TS clear is what
     * makes FP/SSE instructions execute instead of trapping #NM). */
    u64 cr0 = read_cr0();
    if ((cr0 & (X64_CR0_TS | X64_CR0_EM)) != 0 || !(cr0 & X64_CR0_MP))
        return -1;
    u64 want = X64_CR4_OSFXSR | X64_CR4_OSMMX;
    if ((read_cr4() & want) != want)
        return -1;

    /* The test rewrites the whole register bank; park it first. Phase 2 runs
     * before any user context exists, so nothing real is being clobbered. */
    x64_fpu_save(fpu_live);

    /* 1. The image every new thread starts from. */
    x64_fpu_area_init(fpu_a);
    if (fpu_get_u16(fpu_a, X64_FPU_FCW_OFF) != X64_FPU_FCW_INIT ||
        fpu_get_u32(fpu_a, X64_FPU_MXCSR_OFF) != X64_FPU_MXCSR_INIT ||
        fpu_a[X64_FPU_FTW_OFF] != X64_FPU_FTW_EMPTY)
        goto fail;
    for (int i = 0; i < 16; i++)
        for (int b = 0; b < 16; b++)
            if (fpu_a[X64_FPU_XMM_OFF + i * 16 + b] != 0)
                goto fail;

    /* 2. It must actually be loadable and stable (a bad image #GP's here). */
    x64_fpu_load(fpu_a);
    x64_fpu_save(fpu_b);
    if (fpu_cmp(fpu_a, fpu_b) != 0)
        goto fail;

    /* 3. A distinctive state, the case that matters for the context switch:
     * every XMM register and every x87 slot carrying its own bit pattern, a
     * non-default MXCSR and all tags valid (so the x87 slots really move). */
    x64_fpu_area_init(fpu_a);
    fpu_put_u32(fpu_a, X64_FPU_MXCSR_OFF, 0x1FA1);  /* DE+IE flags set */
    fpu_a[X64_FPU_FTW_OFF] = 0x00;                  /* all 8 tags valid */
    for (int i = 0; i < 16; i++) {
        u64 base = 0xF000000000000000ull + ((u64)i << 32);
        fpu_put_u64(fpu_a, X64_FPU_XMM_OFF + (u64)i * 16, base | 0x01234567ull);
        fpu_put_u64(fpu_a, X64_FPU_XMM_OFF + (u64)i * 16 + 8,
                    base + 0x89ABCDEFull);
    }
    for (int i = 0; i < 8; i++) {
        /* Valid normalized 80-bit number: integer bit set, bias 0x3FFF. */
        fpu_put_u64(fpu_a, X64_FPU_ST_OFF + (u64)i * 16,
                    0x8000000000000000ull | (u64)(i + 1));
        fpu_put_u16(fpu_a, X64_FPU_ST_OFF + (u64)i * 16 + 8, 0x3FFF);
    }
    x64_fpu_load(fpu_a);
    x64_fpu_save(fpu_b);
    if (fpu_cmp(fpu_a, fpu_b) != 0)
        goto fail;

    /* 4. Switching back and forth must not leak one context's numbers into
     * the other (this is the exact pair the epilogue runs per handoff). */
    x64_fpu_load(fpu_live);
    x64_fpu_load(fpu_a);
    x64_fpu_load(fpu_live);
    x64_fpu_save(fpu_b);
    if (fpu_cmp(fpu_live, fpu_b) != 0)
        goto fail;

    x64_fpu_load(fpu_live);
    return 0;

fail:
    x64_fpu_load(fpu_live);
    return -1;
}

/* ---------------- driver ---------------- */

static int ktest2_timer(void)
{
    u64 t0 = pit_ticks();
    time_delay_us(10000);
    u64 t1 = pit_ticks();

    if (t1 - t0 < 10)
        return -1;
    return 0;
}

/* SYS_OPEN / SYS_CLOSE / SYS_READ-through-VFS, driven through the real
 * dispatcher with a real user address space active (usercopy validated). */
static int ktest2_open_close(void)
{
    struct process *p = process_create_from_vfs("/bin/hello", "opentest");
    if (!p)
        return -1;

    process_current_set(p);

    /* One scratch user page: path strings at +0, data buffer at +256. */
    uintptr_t va = (uintptr_t)USER_SPACE_BASE + (uintptr_t)USER_SPACE_END / 2;
    va &= ~(uintptr_t)(PAGE_SIZE - 1);
    if (vmm_alloc_page(p->space, va, VMM_USER | VMM_WRITE | VMM_NX) != 0)
        goto fail;
    uintptr_t phys = 0;
    if (!vmm_page_lookup(p->space, va, &phys, NULL))
        goto fail;
    u8 *up = (u8 *)phys_to_virt(phys);
    const uintptr_t va_data = va + 256;

    if (vmm_switch_to(p->space) != 0)
        goto fail;

    struct x64_iframe f;
    memset(&f, 0, sizeof(f));

    /* Open an existing file and read it through the new SYS_READ path.
     * SYS_OPEN speaks the Linux O_* flags (the ABI userland and real
     * binaries use), so the test must not pass internal VFS_O_* here. */
    memcpy(up, "/etc/version", 13);
    f.rax = SYS_OPEN;
    f.rdi = va;
    f.rsi = NSH_O_RDONLY;
    long fd = syscall_dispatch(&f);
    if (fd < 3 || fd >= PROCESS_MAX_FDS)
        goto fail_back;

    memset(&f, 0, sizeof(f));
    f.rax = SYS_READ;
    f.rdi = (u64)fd;
    f.rsi = va_data;
    f.rdx = 32;
    long n = syscall_dispatch(&f);
    if (n <= 0)
        goto fail_back;
    {
        char out[40];
        memset(out, 0, sizeof(out));
        if (copy_from_user(out, (const void *)va_data, (size_t)n) != 0)
            goto fail_back;
        if (n < 5 || memcmp(out, "NEWOS", 5) != 0)
            goto fail_back;
    }

    /* Close works once; a second close of the same fd is refused. */
    memset(&f, 0, sizeof(f));
    f.rax = SYS_CLOSE;
    f.rdi = (u64)fd;
    if (syscall_dispatch(&f) != 0)
        goto fail_back;
    memset(&f, 0, sizeof(f));
    f.rax = SYS_CLOSE;
    f.rdi = (u64)fd;
    if (syscall_dispatch(&f) != SYSCALL_RET_ERROR)
        goto fail_back;

    /* O_CREATE round trip: write through SYS_WRITE, read back. */
    memcpy(up, "/tmp/open-test", 15);
    memset(&f, 0, sizeof(f));
    f.rax = SYS_OPEN;
    f.rdi = va;
    f.rsi = NSH_O_RDWR | NSH_O_CREAT;
    fd = syscall_dispatch(&f);
    if (fd < 0)
        goto fail_back;
    memcpy(up + 256, "hello-open", 10);
    memset(&f, 0, sizeof(f));
    f.rax = SYS_WRITE;
    f.rdi = (u64)fd;
    f.rsi = va_data;
    f.rdx = 10;
    if (syscall_dispatch(&f) != 10)
        goto fail_back;
    memset(&f, 0, sizeof(f));
    f.rax = SYS_CLOSE;
    f.rdi = (u64)fd;
    if (syscall_dispatch(&f) != 0)
        goto fail_back;

    memset(&f, 0, sizeof(f));
    f.rax = SYS_OPEN;
    f.rdi = va;                 /* still "/tmp/open-test" */
    f.rsi = NSH_O_RDONLY;
    fd = syscall_dispatch(&f);
    if (fd < 0)
        goto fail_back;
    memset(&f, 0, sizeof(f));
    f.rax = SYS_READ;
    f.rdi = (u64)fd;
    f.rsi = va_data;
    f.rdx = 32;
    n = syscall_dispatch(&f);
    if (n != 10)
        goto fail_back;
    {
        char out[16];
        memset(out, 0, sizeof(out));
        if (copy_from_user(out, (const void *)va_data, 10) != 0)
            goto fail_back;
        if (memcmp(out, "hello-open", 10) != 0)
            goto fail_back;
    }
    memset(&f, 0, sizeof(f));
    f.rax = SYS_CLOSE;
    f.rdi = (u64)fd;
    if (syscall_dispatch(&f) != 0)
        goto fail_back;

    /* Error cases: missing file without O_CREATE, bogus flags. */
    memcpy(up, "/no/such/file", 14);
    memset(&f, 0, sizeof(f));
    f.rax = SYS_OPEN;
    f.rdi = va;
    f.rsi = NSH_O_RDONLY;
    if (syscall_dispatch(&f) != SYSCALL_RET_ERROR)
        goto fail_back;
    memcpy(up, "/etc/version", 13);
    memset(&f, 0, sizeof(f));
    f.rax = SYS_OPEN;
    f.rdi = va;
    f.rsi = NSH_O_RDWR | NSH_O_WRONLY;     /* 2|1: accmode 3, reserved */
    if (syscall_dispatch(&f) != SYSCALL_RET_ERROR)
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

/* Process lifetime without real CPU handoff (no timer context yet whose
 * preemption could run the child): create -> schedule -> zombie -> reclaim. */static int ktest2_proc_life(void)
{
    struct process *c =
        process_create_from_vfs("/bin/hello", "lifetest");
    if (!c)
        return -1;

    pid_t pid = c->pid;
    c->ppid = 999;
    sched_add_process(c);

    if (c->state != PROCESS_STATE_RUNNING)
        return -1;

    c->exit_code = 42;
    c->state = PROCESS_STATE_EXITED;
    sched_remove_runnable(c);
    if (sched_zombify(c) != 0)
        return -1;

    struct process *z = sched_zombie_find(pid);
    if (!z || z->exit_code != 42)
        return -1;

    sched_zombie_reclaim(z);
    return 0;
}

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
    { "open-close",  ktest2_open_close  },
    { "timer-pit",   ktest2_timer       },
    { "proc-life",   ktest2_proc_life   },
    { "fpu-state",   ktest2_fpu         },
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