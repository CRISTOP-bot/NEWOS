#ifndef ARCH_X86_64_FPU_H
#define ARCH_X86_64_FPU_H

#include <core/core_types.h>

/* x87 / SSE state handling.
 *
 * Every user program may use the FPU: gcc targets (x86-64 baseline) emit SSE2
 * for float/double and for vectorised memcpy, so the kernel must (a) turn the
 * hardware on and (b) move the register bank with each thread. The unit of
 * storage is the 512-byte FXSAVE image; struct thread embeds one, and the
 * interrupt epilogue swaps images on every scheduler handoff.
 *
 * Only the legacy FXSAVE region is supported (SSE2 baseline). AVX and other
 * extended state would need XSAVE/XRSTOR with XCR0 enabled, which this kernel
 * does not do: user binaries must be built without AVX.
 *
 * The field offsets were confirmed against a live FXSAVE image (a CPU with
 * x87 slots at 0x20 and XMM slots at 0xA0, MXCSR at 0x18).
 */

#define X64_FPU_AREA_SIZE  512
#define X64_FPU_AREA_ALIGN 16

#define X64_FPU_FCW_OFF         0x00   /* u16  x87 control word            */
#define X64_FPU_FSW_OFF         0x02   /* u16  x87 status word             */
#define X64_FPU_FTW_OFF         0x04   /* u8   abridged tag word           */
#define X64_FPU_FOP_OFF         0x06   /* u16  last opcode                 */
#define X64_FPU_FIP_OFF         0x08   /* u64  last instruction pointer    */
#define X64_FPU_FDP_OFF         0x10   /* u64  last operand pointer        */
#define X64_FPU_MXCSR_OFF       0x18   /* u32  SSE control/status          */
#define X64_FPU_MXCSR_MASK_OFF  0x1C   /* u32  writable MXCSR bits         */
#define X64_FPU_ST_OFF          0x20   /* 8 x 16 bytes, ST(0) first        */
#define X64_FPU_XMM_OFF         0xA0   /* 16 x 16 bytes, XMM0 first        */

/* Reset state: what `fninit` plus a default MXCSR produce. All x87 tags
 * empty (0xFF), no pending exception flags. */
#define X64_FPU_FCW_INIT        0x007F
#define X64_FPU_FTW_EMPTY       0xFF
#define X64_FPU_MXCSR_INIT      0x1F80

/* CPUID.1 feature bits. */
#define X64_CPUID_EDX_SSE       (1u << 0)
#define X64_CPUID_EDX_FXSR      (1u << 24)
#define X64_CPUID_EDX_SSE2      (1u << 26)

/* Control-register bits that gate the state. */
#define X64_CR0_MP              (1ull << 1)
#define X64_CR0_EM              (1ull << 2)
#define X64_CR0_TS              (1ull << 3)

/* CR4 bit 9 is OSFXSR (permits FXSAVE/FXRSTOR and SSE), bit 10 is
 * OSMMX/OSXMMEXCPT (unmasked SSE exceptions). Bit 14 is SMXE: writing it
 * without an enabled SMX/TXT leaf faults #GP, so it must never appear here. */
#define X64_CR4_OSFXSR          (1ull << 9)
#define X64_CR4_OSMMX           (1ull << 10)

/* Live register ownership, storage in x86_interrupt.S: g_user_fpu_area is
 * the FXSAVE image the registers currently belong to (0 = no user context
 * owns them), g_sched_next_fpu_area the image of a pending handoff target.
 * The epilogue saves into the former and restores from the latter. */
extern u64 g_user_fpu_area;
extern u64 g_sched_next_fpu_area;

/* FXSR (and with it SSE/SSE2 on every x86-64 part) present. */
int x64_fpu_available(void);
/* SSE2 present: without it no gcc-generated user code can run. */
int x64_fpu_sse2(void);

/* Turn the state on: CR0.TS/EM cleared with MP set, CR4.OSFXSR|OSMMX. A
 * no-op with a diagnostic if the CPU has no FXSR. */
void x64_fpu_enable(void);

/* Fill a 512-byte, 16-byte aligned area with the clean reset image. */
void x64_fpu_area_init(void *area);

/* Register bank <-> area (x86_fpu.S). The area must be 16-byte aligned:
 * FXSAVE/FXRSTOR fault with #GP otherwise. */
void x64_fpu_save(void *area);
void x64_fpu_load(const void *area);

#endif
