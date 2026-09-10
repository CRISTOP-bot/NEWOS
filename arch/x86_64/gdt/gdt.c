#include <arch/x86_64/gdt/gdt.h>
#include <arch/x86_64/cpu.h>
#include <kernel/types.h>
#include <libk/string.h>
#include <kernel/panic.h>

#define GDT_ENTRIES 7

struct gdt_entry_u64 {
    u64 value;
};

/* 64-bit TSS / system descriptor, byte-accurate Intel layout:
 *  0-1  limit[15:0]
 *  2-4  base[23:0]
 *  5    access
 *  6    limit[19:16] | flags
 *  7    base[31:24]
 *  8-11 base[63:32]
 *  12-15 reserved */
struct gdt_entry_tss {
    u16 limit_low;
    u8  base_low0;
    u8  base_low1;
    u8  base_low2;
    u8  access;
    u8  lim_flags;
    u8  base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct tss {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u32 reserved3;
    u32 iopb;
} __attribute__((packed));

/* GDT storage as bytes: 7 descriptors, the last (TSS) spans two slots. */
static u8 g_gdt_bytes[8 * 8];
static struct tss g_tss;
static struct { u16 limit; u64 base; } __attribute__((packed)) g_gdt_ptr;

static inline u64 gdt_read(int index)
{
    u64 v;
    memcpy(&v, &g_gdt_bytes[index * 8], sizeof(v));
    return v;
}

static inline void gdt_write(int index, u64 v)
{
    memcpy(&g_gdt_bytes[index * 8], &v, sizeof(v));
}

static void tss_setup(struct tss *t, struct gdt_entry_tss *desc)
{
    u64 base = (u64)(uintptr_t)t;
    u32 limit = sizeof(struct tss) - 1;

    desc->limit_low   = (u16)(limit & 0xFFFF);
    desc->base_low0   = (u8)base;
    desc->base_low1   = (u8)(base >> 8);
    desc->base_low2   = (u8)(base >> 16);
    desc->access      = 0x89;           /* present, DPL0, 64-bit TSS */
    desc->lim_flags   = (u8)((limit >> 16) & 0xF);
    desc->base_high   = (u8)(base >> 24);
    desc->base_upper  = (u32)(base >> 32);
    desc->reserved    = 0;
}

void x64_gdt_reload(void)
{
    __asm__ volatile(
        "lgdt %0\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        : : "m"(g_gdt_ptr) : "rax", "memory");
}

int x64_tss_install(u64 rsp0)
{
    g_tss.rsp0 = rsp0;
    tss_setup(&g_tss,
              (struct gdt_entry_tss *)(g_gdt_bytes + GDT_TSS));

    /* Load the TSS selector (RPL 0, TSS index 5). */
    __asm__ volatile("ltr %%ax" : : "a"((u16)GDT_TSS));
    return GDT_TSS / 8;
}

static void gdt_install_code(u64 *slot, u8 access, u8 flags)
{
    u64 base = 0;
    u64 limit = 0xFFFFF;
    *slot = (u64)base
          | ((u64)limit & 0xFFFF)
          | ((u64)(base & 0xFF) << 16)
          | ((u64)(base >> 16 & 0xFF) << 24)
          | ((u64)access << 40)
          | ((u64)((limit >> 16) & 0xF) << 48)
          | ((u64)flags << 52)
          | ((u64)((base >> 24) & 0xFF) << 56);
}

static u64 gdt_code_data_descriptor(u8 access, u8 flags)
{
    u64 slot;
    gdt_install_code(&slot, access, flags);
    return slot;
}

void x64_gdt_init(void)
{
    memset(g_gdt_bytes, 0, sizeof(g_gdt_bytes));
    memset(&g_tss, 0, sizeof(g_tss));

    /* 0x00 null                     */
    gdt_write(0, 0);
    /* 0x08 kernel code  (64-bit)    */
    gdt_write(1, gdt_code_data_descriptor(0x9A, 0xA));
    /* 0x10 kernel data              */
    gdt_write(2, gdt_code_data_descriptor(0x92, 0xC));
    /* 0x18 user code    (64-bit)    */
    gdt_write(3, gdt_code_data_descriptor(0xFA, 0xA));
    /* 0x20 user data                */
    gdt_write(4, gdt_code_data_descriptor(0xF2, 0xC));
    /* 0x28 TSS - filled by x64_tss_install() */

    g_gdt_ptr.limit = sizeof(g_gdt_bytes) - 1;
    g_gdt_ptr.base  = (u64)(uintptr_t)g_gdt_bytes;

    x64_gdt_reload();
}

u64 x64_current_kernel_stack(void)
{
    return g_tss.rsp0;
}

void x64_tss_set_rsp0(u64 rsp0)
{
    g_tss.rsp0 = rsp0;
}