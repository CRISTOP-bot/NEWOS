#include <kernel/printk.h>
#include <kernel/types.h>

struct oops_cpu_state {
    u64 rax, rbx, rcx, rdx;
    u64 rsi, rdi, rbp, rsp, r8, r9, r10, r11, r12, r13, r14, r15;
    u64 rip, cs, flags;
    u64 cr2;
};

void oops_report(const char *reason, struct oops_cpu_state *st)
{
    printk("\n=== OOPS: %s ===\n", reason);
    printk("RIP=%llx  RSP=%llx  RBP=%llx  FLAGS=%llx  CR2=%llx\n",
           st->rip, st->rsp, st->rbp, st->flags, st->cr2);
    printk("RAX=%llx RBX=%llx RCX=%llx RDX=%llx\n", st->rax, st->rbx,
           st->rcx, st->rdx);
    printk("RSI=%llx RDI=%llx R8=%llx R9=%llx\n", st->rsi, st->rdi,
           st->r8, st->r9);
    printk("R10=%llx R11=%llx R12=%llx R13=%llx\n", st->r10, st->r11,
           st->r12, st->r13);
    printk("R14=%llx R15=%llx CS=%x\n", st->r14, st->r15,
           (unsigned)st->cs);
}