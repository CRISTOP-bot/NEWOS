#include <x86_idt.h>
#include <x86_cpu.h>
#include <x86_gdt.h>
#include <x86_pic.h>
#include <x86_irq.h>
#include <x86_frame.h>
#include <core/core_printk.h>
#include <core/core_panic.h>
#include <process/proc_process.h>
#include <process/sched.h>
#include <syscall/syscall.h>
#include <core/core_types.h>
#include <iru_string.h>

#define IDT_GATE_PRESENT (1ull << 47)
#define IDT_GATE_DPL(dpl) ((u64)(dpl) << 45)
#define IDT_GATE_TYPE(t) ((u64)(t) << 40)

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

static struct idt_entry g_idt[IDT_ENTRIES];
static struct idt_ptr g_idt_ptr;

extern void *isr_stubs_all[IDT_ENTRIES];
extern void isr128(void);   /* int $0x80 syscall gate */

static void idt_set_gate_raw(int vec, void (*handler)(void), u8 dpl, u8 type)
{
    u64 offset = (u64)(uintptr_t)handler;

    g_idt[vec].offset_low  = (u16)(offset & 0xFFFF);
    g_idt[vec].selector    = GDT_KERNEL_CODE;
    g_idt[vec].ist         = 0;
    g_idt[vec].type_attr   = (u8)(0x80 | (dpl << 5) | (type & 0xF));
    g_idt[vec].offset_mid  = (u16)((offset >> 16) & 0xFFFF);
    g_idt[vec].offset_high = (u32)((offset >> 32) & 0xFFFFFFFF);
    g_idt[vec].zero        = 0;
}

void x64_idt_set_gate(int vec, void (*handler)(void), u8 dpl)
{
    idt_set_gate_raw(vec, handler, dpl, IDT_TYPE_INTERRUPT);
}

void x64_idt_reload(void)
{
    __asm__ volatile("lidt %0" : : "m"(g_idt_ptr) : "memory");
}

void x64_idt_init(void)
{
    memset(g_idt, 0, sizeof(g_idt));

    for (int i = 0; i < IDT_ENTRIES; i++)
        idt_set_gate_raw(i, isr_stubs_all[i], 0, IDT_TYPE_INTERRUPT);

    /* int $0x80 syscall gate: DPL 3 so user mode can enter it. An
     * INTERRUPT gate (IF cleared on entry) keeps the stub pushes and the
     * return epilogue atomic; the C handler below re-enables IF
     * explicitly for the syscall body so the PIT time base and the
     * scheduler's yield points keep flowing while a thread waits. */
    idt_set_gate_raw(0x80, isr128, 3, IDT_TYPE_INTERRUPT);

    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base  = (u64)(uintptr_t)g_idt;

    /* Remap the legacy PIC off the CPU exception vectors (IRQ0 hits vector
     * 8 by default) and mask all lines until drivers service them. */
    x64_pic_init();

    x64_idt_reload();
}


/* -------------------------------------------------------------------------
 * Exception names
 * ---------------------------------------------------------------------- */
static const char *exception_name(int vec)
{
    static const char *names[32] = {
        [0]  = "Divide-by-zero",
        [1]  = "Debug",
        [2]  = "Non-maskable interrupt",
        [3]  = "Breakpoint",
        [4]  = "Overflow",
        [5]  = "Bounds",
        [6]  = "Invalid opcode",
        [7]  = "Device not available",
        [8]  = "Double fault",
        [9]  = "Coprocessor segment overrun",
        [10] = "Invalid TSS",
        [11] = "Segment not present",
        [12] = "Stack-segment fault",
        [13] = "General protection",
        [14] = "Page fault",
        [16] = "x87 FP exception",
        [17] = "Alignment check",
        [18] = "Machine check",
        [19] = "SIMD FP exception",
        [20] = "Virtualization exception",
        [30] = "Security exception",
    };
    if (vec >= 0 && vec < 32 && names[vec])
        return names[vec];
    return "Unknown";
}

void x64_dump_iframe(struct x64_iframe *f)
{
    printk("RIP=%p  CS=%x  RFLAGS=%p  RSP=%p\n",
           f->rip, (unsigned)f->cs, f->rflags, f->rsp);
    printk("RAX=%p RBX=%p RCX=%p RDX=%p\n", f->rax, f->rbx, f->rcx, f->rdx);
    printk("RSI=%p RDI=%p RBP=%p  R8=%p\n", f->rsi, f->rdi, f->rbp, f->r8);
    printk("R9=%p R10=%p R11=%p R12=%p R13=%p R14=%p R15=%p\n",
           f->r9, f->r10, f->r11, f->r12, f->r13, f->r14, f->r15);
}

void isr_handler(struct x64_iframe *f)
{
    int vec = (int)f->vec;
    int from_user = (f->cs & 3) == 3;

    /* int $0x80 syscall from ring 3. The interrupt gate entered with IF
     * clear; re-enable it for the body (time base + scheduler yields),
     * the return epilogue closes it again before touching stacks. */
    if (vec == 0x80 && from_user) {
        cpu_sti();
        f->rax = (u64)syscall_dispatch(f);   /* SYS_EXIT never returns */
        return;
    }

    /* Device IRQs (8259 remapped onto 0x20..0x2F) go through the generic
     * dispatch table; a line nobody claimed is EOI'd anyway. The PIT tick
     * also gives the scheduler its preemption point. */
    if (vec >= X86_IRQ_BASE && vec < X86_IRQ_BASE + 16) {
        if (vec == X86_IRQ_BASE + X86_IRQ_PIT)
            sched_on_tick(f);
        x86_irq_dispatch(vec);
        return;
    }

    /* Page faults need the faulting address from CR2 and can originate
     * from either privilege level. */
    if (vec == 14) {
        u64 fault_addr = read_cr2();
        if (from_user)
            process_page_fault(f, fault_addr, (u64)f->err);

        printk("\nPAGE FAULT at address %p, error=%x (%s), RIP=%p\n",
               fault_addr, (unsigned)f->err,
               (f->err & 1) ? "protection" : "not-present",
               f->rip);
        x64_dump_iframe(f);
        panic("Unhandled page fault");
    }

    /* User exceptions kill the offending process. */
    if (from_user) {
        printk("\nUSER EXCEPTION %d: %s, error=%x\n", vec,
               exception_name(vec), (unsigned)f->err);
        x64_dump_iframe(f);
        process_exception(f, vec, (u64)f->err);
    }

    printk("\nEXCEPTION %d: %s, error=%x\n", vec, exception_name(vec),
           (unsigned)f->err);
    x64_dump_iframe(f);

    switch (vec) {
    case 8:
    case 18:
        cpu_hlt();
        for (;;)
            ;
    default:
        panic("Unhandled exception");
    }
}