#include <x86_cpu.h>
#include <x86_fpu.h>
#include <core/core_printk.h>
#include <iru_string.h>

int x64_fpu_available(void)
{
    return (cpuid_get(1, 0).edx & X64_CPUID_EDX_FXSR) ? 1 : 0;
}

int x64_fpu_sse2(void)
{
    u32 edx = cpuid_get(1, 0).edx;
    u32 want = X64_CPUID_EDX_SSE | X64_CPUID_EDX_SSE2;
    return (edx & want) == want ? 1 : 0;
}

void x64_fpu_enable(void)
{
    if (!x64_fpu_available()) {
        printk("fpu: CPU has no FXSR: x87/SSE state cannot be switched, "
               "user programs that use FP or SSE will fault\n");
        return;
    }

    /* TS clear means the state is always accessible: no #NM trap and no
     * lazy-save protocol to implement. MP only gates instructions while TS
     * is set, but every x86-64 part is brought up with MP set. */
    u64 cr0 = read_cr0();
    write_cr0((cr0 & ~(X64_CR0_TS | X64_CR0_EM)) | X64_CR0_MP);

    /* CR4 writes are silently filtered by the CPU, so read the bits back:
     * the interrupt epilogue runs FXSAVE unconditionally once a handoff is
     * published, and that instruction #UDs without OSFXSR. */
    write_cr4(read_cr4() | X64_CR4_OSFXSR | X64_CR4_OSMMX);
    u64 got = read_cr4();
    u64 want = X64_CR4_OSFXSR | X64_CR4_OSMMX;
    if ((got & want) != want) {
        printk("fpu: CR4 refused OSFXSR/OSMMX (cr4=%llx) - "
               "FP/SSE user binaries are NOT supported on this CPU\n", got);
        return;
    }

    printk("fpu: x87+SSE%s enabled (cr0=%llx cr4=%llx), FXSAVE image per thread\n",
           x64_fpu_sse2() ? "/SSE2" : "", read_cr0(), got);
}

void x64_fpu_area_init(void *area)
{
    u8 *a = (u8 *)area;

    /* Everything the CPU does not read on FXRSTOR (FOP/FIP/FDP, the MXCSR
     * mask, the register data) stays zero: FXSAVE rewrites those fields and
     * empty tags make the x87 slot contents irrelevant. */
    memset(a, 0, X64_FPU_AREA_SIZE);
    *(u16 *)(a + X64_FPU_FCW_OFF) = X64_FPU_FCW_INIT;
    *(u8 *)(a + X64_FPU_FTW_OFF) = X64_FPU_FTW_EMPTY;
    *(u32 *)(a + X64_FPU_MXCSR_OFF) = X64_FPU_MXCSR_INIT;
}
