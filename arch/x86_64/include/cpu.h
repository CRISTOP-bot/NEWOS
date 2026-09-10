#ifndef ARCH_X86_64_CPU_H
#define ARCH_X86_64_CPU_H

#include <kernel/types.h>

static inline void cpu_cli(void)  { __asm__ volatile("cli"); }
static inline void cpu_sti(void)  { __asm__ volatile("sti"); }
static inline void cpu_hlt(void)  { __asm__ volatile("hlt"); }
static inline void cpu_pause(void){ __asm__ volatile("pause"); }

static inline u64 read_cr0(void)
{
    u64 v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline u64 read_cr2(void)
{
    u64 v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline u64 read_cr3(void)
{
    u64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline u64 read_cr4(void)
{
    u64 v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(u64 v)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

static inline void invlpg(u64 addr)
{
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline void cpu_sfence(void) { __asm__ volatile("sfence"); }
static inline void cpu_lfence(void) { __asm__ volatile("lfence"); }
static inline void cpu_mfence(void) { __asm__ volatile("mfence"); }

struct cpuid_result {
    u32 eax, ebx, ecx, edx;
};

struct cpuid_result cpuid_get(u32 leaf, u32 subleaf);
u32 cpuid_vendor_id(char out[13]);
int cpuid_long_mode_supported(void);
void cpu_print_info(void);

static inline void rdmsr(u32 msr, u32 *lo, u32 *hi)
{
    __asm__ volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

static inline void wrmsr(u32 msr, u32 lo, u32 hi)
{
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

#endif