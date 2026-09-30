#include <core/core_printk.h>
#include <core/core_panic.h>
#include <mm/mm_pmm.h>
#include <mm/mm_heap.h>
#include <fs/vfs.h>
#include <ipc/ipc.h>
#include <core/core_bootinfo.h>
#include <mm/mm_debug.h>
#include <core/core.h>
#include <drivers/qemu_debug.h>
#include <drivers/pcnet.h>
#include <mm/mm_vmm.h>
#include <process/proc_process.h>
#include <process/proc_thread.h>
#include <process/sched.h>
#include <x86_cpu.h>
#include <x86_fpu.h>
#include <x86_gdt.h>
#include <iru_string.h>
#include <iru_bitmap.h>
#include <core/core_time.h>

#define NEWOS_VERSION "0.3.0-gui"
#define NEWOS_TARGET  "x86_64"

int run_phase2_tests(void);

/* -------------------------------------------------------------------------
 * Basic kernel self tests. Each returns 0 on success.
 * ---------------------------------------------------------------------- */

static int ktest_pmm_alloc_free(void)
{
    u64 f1 = PMM_INVALID_FRAME, f2 = PMM_INVALID_FRAME, f3 = PMM_INVALID_FRAME;

    if (!pmm_is_initialized())
        return -1;

    if (pmm_frame_alloc(&f1) != 0 || f1 == PMM_INVALID_FRAME)
        return -1;
    if (pmm_frame_alloc(&f2) != 0 || f2 == PMM_INVALID_FRAME)
        return -1;
    pmm_frame_free(f2);

    if (pmm_block_alloc(2, &f3) != 0)
        return -1;
    pmm_block_free(f3, 2);

    pmm_frame_free(f1);
    return 0;
}

#define TEST_STR "Hello from NEWOS VFS!"

static int ktest_heap_rw(void)
{
    void *p = kmalloc(64);
    if (!p)
        return -1;

    memcpy(p, TEST_STR, sizeof(TEST_STR));
    if (memcmp(p, TEST_STR, sizeof(TEST_STR)) != 0) {
        kfree(p);
        return -1;
    }

    kfree(p);
    return 0;
}

static int ktest_vfs_tmpfs(void)
{
    struct vfs_file *f = vfs_open("/etc/version", VFS_O_READ);
    if (!f)
        return -1;

    char buf[64];
    int n = vfs_read(f, buf, sizeof(buf) - 1);
    vfs_close(f);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    if (strncmp(buf, "NEWOS", 5) != 0)
        return -1;

    /* unlink round-trip on nested paths (regression: split_parent ate
     * the first basename char, so every nested unlink failed). */
    if (vfs_create("/tmp/ktest-unlink") != 0)
        return -1;
    if (!vfs_lookup("/tmp/ktest-unlink"))
        return -1;
    if (vfs_unlink("/tmp/ktest-unlink") != 0)
        return -1;
    if (vfs_lookup("/tmp/ktest-unlink"))
        return -1;
    /* Single-char nested name (the old code produced an empty name). */
    if (vfs_create("/tmp/k") != 0)
        return -1;
    if (vfs_unlink("/tmp/k") != 0)
        return -1;
    if (vfs_lookup("/tmp/k"))
        return -1;
    /* Unlinking a missing nested file must still fail. */
    if (vfs_unlink("/tmp/ktest-unlink") == 0)
        return -1;
    return 0;
}

static int ktest_pipe(void)
{
    struct ipc_pipe *pipe = ipc_pipe_new();
    if (!pipe)
        return -1;
    struct ipc_pipe_fd *w = ipc_pipe_fd_new(pipe, 0);
    struct ipc_pipe_fd *r = ipc_pipe_fd_new(pipe, 1);
    if (!w || !r) {
        ipc_pipe_fd_close(w);
        ipc_pipe_fd_close(r);
        return -1;
    }

    char in[] = "pipe-data";
    char out[32];

    /* With an open reader the write must land in full. */
    if (ipc_pipe_write(pipe, in, sizeof(in)) != sizeof(in))
        return -1;
    if (ipc_pipe_read(pipe, out, sizeof(out)) != sizeof(in))
        return -1;
    if (memcmp(in, out, sizeof(in)) != 0)
        return -1;
    /* Once the reader is gone, writers must be refused (EPIPE rule). */
    ipc_pipe_fd_close(r);
    if (ipc_pipe_write(pipe, in, sizeof(in)) != 0)
        return -1;
    ipc_pipe_fd_close(w);   /* frees the pipe: both ends closed */
    return 0;
}

static int ktest_bitmap(void)
{
    size_t map[BITMAP_WORDS(64)];
    bitmap_clear_all(map, 64);

    for (int i = 0; i < 64; i += 2)
        bitmap_set(map, i, 1);

    for (int i = 0; i < 64; i++) {
        if (bitmap_get(map, i) != (i % 2 == 0))
            return -1;
    }

    size_t first = bitmap_find_first_bit(map, 64, 0);
    if (first != 1)
        return -1;

    first = bitmap_find_first_bit(map, 64, 2);
    if (first != 3)
        return -1;

    return 0;
}

/* ------------------------------------------------------------------------- */

struct ktest {
    const char *name;
    int (*fn)(void);
};

static const struct ktest ktests[] = {
    { "bitmap",    ktest_bitmap     },
    { "heap-rw",   ktest_heap_rw    },
    { "pmm-alloc", ktest_pmm_alloc_free },
    { "pipe",      ktest_pipe       },
    { "vfs-tmpfs", ktest_vfs_tmpfs  },
    { "pcnet",     pcnet_selftest   },
};

static int run_kernel_tests(void)
{
    int failures = 0;

    printk("=== Kernel self tests ===\n");
    for (size_t i = 0; i < sizeof(ktests) / sizeof(ktests[0]); i++) {
        int rc = ktests[i].fn();
        printk("  [%s] %s%s\n", rc == 0 ? "PASS" : "FAIL",
               ktests[i].name, rc ? "  <-- failure" : "");
        if (rc)
            failures++;
    }
    printk("=== %zu tests, %d failures ===\n",
           sizeof(ktests) / sizeof(ktests[0]), failures);
    return failures;
}

static int cmdline_has_test_mode(void)
{
    return boot_cmdline_has("test_mode=1");
}

static int cmdline_has_headless(void)
{
    return boot_cmdline_has("headless=1");
}

static int cmdline_has_gui(void)
{
    return boot_cmdline_has("gui=1");
}

/* -------------------------------------------------------------------------
 * Phase 2: userland bootstrap.
 *
 * BoredOS-style init: reads init= bootloader parameter, spawns the
 * configured init binary, with a fallback chain:
 *   gui=1 → /bin/desktop (default)
 *   init=<path> → user-configured init
 *   fallback → /init (shell)
 *   emergency → /bin/sh
 * ---------------------------------------------------------------------- */

static int s_user_done;
static int s_phase2_failures;

static void userland_finished(struct process *p)
{
    int ok = (p->exit_code == 0);

    if (ok && s_phase2_failures == 0)
        printk("=== PHASE 2: PASS ===\n");
    else
        printk("=== PHASE 2: FAIL ===\n");

    if (cmdline_has_test_mode())
        qemu_debug_exit((!ok || s_phase2_failures) ? 1 : 0);

    process_free(p);
    printk("NEWOS: boot complete.\n");
}

/* Runs on the kernel-half init stack. */
static void run_userland(u64 arg)
{
    struct process *p = (struct process *)arg;

    thread_capture_resume();

    if (s_user_done) {
        /* Process exited; we are back on the init stack. */
        s_user_done = 0;
        userland_finished(p);
    } else {
        /* First boot. */
        s_user_done = 1;
        process_current_set(p);
        vmm_switch_to(p->space);
        x64_tss_set_rsp0(p->thread.kernel_stack_top);
        /* Give the program its own clean x87/SSE image and record that the
         * register bank now belongs to it: the epilogue saves this image back
         * on the first handoff. */
        g_user_fpu_area = (u64)(uintptr_t)&p->thread.fpu[0];
        x64_fpu_load(p->thread.fpu);
        printk("userland: entering %s (pid %u) at ring 3\n", p->name,
               (unsigned)p->pid);
        x64_enter_user(&p->thread);   /* never returns */
    }

    for (;;)
        cpu_hlt();
}

static void boot_userland(void)
{
    /* Headless mode: launch shell directly.
     * Default: /init (interactive shell)
     * Fallback: /bin/sh (emergency rescue)
     * If gui=0, skip desktop and use /init directly. */
    const char *fallback1 = "/init";
    const char *fallback2 = "/bin/sh";
    struct process *p = NULL;

    if (cmdline_has_headless()) {
        printk("userland: headless mode, launching shell\n");
        p = process_create_from_vfs(fallback2, "shell");
        if (p) {
            p->is_init = 1;
            sched_add_process(p);
            u64 isp = thread_init_stack();
            x64_goto_stack(isp, run_userland, (u64)p);
            return;
        }
    } else if (!cmdline_has_gui() && boot_cmdline_has("gui=") == 0) {
        /* Default: headless mode when no gui= parameter specified */
        printk("userland: default headless mode, launching shell\n");
        p = process_create_from_vfs(fallback2, "shell");
        if (p) {
            p->is_init = 1;
            sched_add_process(p);
            u64 isp = thread_init_stack();
            x64_goto_stack(isp, run_userland, (u64)p);
            return;
        }
        printk("userland: shell failed, trying fallback\n");
    } else {
        printk("userland: headless mode, launching shell\n");
        p = process_create_from_vfs(fallback2, "shell");
        if (p) {
            p->is_init = 1;
            sched_add_process(p);
            u64 isp = thread_init_stack();
            x64_goto_stack(isp, run_userland, (u64)p);
            return;
        }
    }

    /* Fallback 1: /init */
    printk("userland: launching %s\n", fallback1);
    p = process_create_from_vfs(fallback1, "init");
    if (p) {
        p->is_init = 1;
        sched_add_process(p);
        u64 isp = thread_init_stack();
        x64_goto_stack(isp, run_userland, (u64)p);
        return;
    }

    /* Fallback 2: /bin/sh */
    printk("userland: %s failed, emergency fallback to /bin/sh\n", fallback1);
    p = process_create_from_vfs(fallback2, "sh");
    if (p) {
        p->is_init = 1;
        sched_add_process(p);
        u64 isp = thread_init_stack();
        x64_goto_stack(isp, run_userland, (u64)p);
        return;
    }

    /* Everything failed */
    s_phase2_failures = 1;
    printk("=== PHASE 2: FAIL ===\n");
    if (cmdline_has_test_mode())
        qemu_debug_exit(1);
    printk("NEWOS: boot complete (all init binaries failed).\n");
}

/* Everything after the banner runs from the direct-map init stack. The early
 * identity window (and its low-VA boot stack) is abandoned here: once we drop
 * the original PML4, no kernel code dereferences an identity-mapped address
 * again, so any CR3 write (address-space switch) is safe. */
static void kernel_boot_main(u64 arg)
{
    (void)arg;
    sched_init();
    int failures = run_kernel_tests();
    s_phase2_failures = run_phase2_tests();

    if (cmdline_has_test_mode() && (failures || s_phase2_failures)) {
        printk("Test mode: aborted by failures (%d + %d).\n",
               failures, s_phase2_failures);
        qemu_debug_exit(1);
    }

    /* Success must terminate the run too: falling through to boot_userland()
     * left `make qemu-test` waiting on an interactive shell, so the verdict
     * depended on serial EOF instead of the tests (a missing kernel ELF
     * reported the same "pass" status as a green suite). */
    if (cmdline_has_test_mode()) {
        printk("=== SELFTESTS: all phases 0 failures ===\n");
        qemu_debug_exit(0);
    }

    boot_userland();
}

void kernel_start(void) __attribute__((noreturn));
void kernel_start(void)
{
    printk("\n");
    printk("============================================================\n");
    printk("  NEWOS " NEWOS_VERSION " (" NEWOS_TARGET ") booted\n");
    printk("  kernel:         %p loaded\n", (void *)KERNEL_BASE_VA);
    printk("  physical memory: %llu MiB total, %llu MiB free\n",
           pmm_total_frames() >> 8, pmm_free_frames() >> 8);
    printk("============================================================\n");

    /* Run everything else from the direct-map init stack (see above). */
    u64 isp = thread_init_stack();
    printk("boot: moving to direct-map stack\n");
    x64_goto_stack(isp, kernel_boot_main, 0);   /* no return */

    /* unreachable */
    for (;;)
        cpu_hlt();
}