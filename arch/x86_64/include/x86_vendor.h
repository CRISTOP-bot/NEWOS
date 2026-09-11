#ifndef ARCH_X86_64_VENDOR_H
#define ARCH_X86_64_VENDOR_H

#include <core/core_types.h>

/* x86 vendor IDs decoded from CPUID leaf 0. */
enum x86_vendor {
    X86_VENDOR_UNKNOWN = 0,
    X86_VENDOR_INTEL,       /* GenuineIntel */
    X86_VENDOR_AMD,         /* AuthenticAMD */
    X86_VENDOR_HYGON,       /* HygonGenuine */
    X86_VENDOR_CENTAUR,     /* CentaurHauls (VIA) */
    X86_VENDOR_ZHAOXIN,     /* Shanghai */
};

/* Vendor-neutral feature flags. Individual bits map 1:1 to entries in
 * X86_FEATURE_*, decoded from CPUID leaves 1, 7, and 0x80000001. */
enum x86_feature {
    X86_FEATURE_MSR        = 0,
    X86_FEATURE_APIC       = 1,
    X86_FEATURE_MMX        = 2,
    X86_FEATURE_FXSR       = 3,
    X86_FEATURE_SSE        = 4,
    X86_FEATURE_SSE2       = 5,
    X86_FEATURE_SSE3       = 6,
    X86_FEATURE_SSSE3      = 7,
    X86_FEATURE_SSE41      = 8,
    X86_FEATURE_SSE42      = 9,
    X86_FEATURE_POPCNT     = 10,
    X86_FEATURE_AES        = 11,
    X86_FEATURE_PCLMULQDQ  = 12,
    X86_FEATURE_FMA        = 13,
    X86_FEATURE_AVX        = 14,
    X86_FEATURE_AVX2       = 15,
    X86_FEATURE_BMI1       = 16,
    X86_FEATURE_BMI2       = 17,
    X86_FEATURE_NX         = 18,
    X86_FEATURE_SYSCALL    = 19,
    X86_FEATURE_RDTSCP     = 20,
    X86_FEATURE_LAHF64     = 21,
    X86_FEATURE_TSC        = 22,
    X86_FEATURE_HTT        = 23,
    X86_FEATURE_COUNT,
};

struct x86_cpu_info {
    enum x86_vendor vendor;
    const char     *vendor_str;
    u32            max_basic_leaf;
    u32            max_ext_leaf;
    u32            family;      /* display family (extended folded in) */
    u32            model;       /* display model   (extended folded in) */
    u32            stepping;
    u32            features;    /* X86_FEATURE_* bitmask */
};

/* Detects and caches the vendor+feature description in `info`. */
void x86_cpu_detect(struct x86_cpu_info *info);

/* Prints the vendor, model, and feature report and runs the small amount of
 * vendor-specific setup required on each family. Safe to call once at boot. */
void cpu_vendor_init(void);

/* Returns the global CPU descriptor filled by cpu_vendor_init(). */
const struct x86_cpu_info *x86_cpu_info_get(void);

static inline int cpu_has_feature(const struct x86_cpu_info *info,
                                  enum x86_feature feat)
{
    return !!(info->features & (1u << (unsigned)feat));
}

#endif