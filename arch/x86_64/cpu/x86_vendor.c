#include <x86_vendor.h>
#include <x86_cpu.h>
#include <core/core_printk.h>
#include <iru_string.h>

static struct x86_cpu_info g_cpu_info;

static const char *vendor_to_name(enum x86_vendor v)
{
    switch (v) {
    case X86_VENDOR_INTEL:   return "Intel";
    case X86_VENDOR_AMD:     return "AMD";
    case X86_VENDOR_HYGON:   return "Hygon";
    case X86_VENDOR_CENTAUR: return "VIA";
    case X86_VENDOR_ZHAOXIN: return "Zhaoxin";
    default:                 return NULL;
    }
}

enum x86_vendor detect_vendor(const char *str)
{
    if (!strcmp(str, "GenuineIntel"))
        return X86_VENDOR_INTEL;
    if (!strcmp(str, "AuthenticAMD"))
        return X86_VENDOR_AMD;
    if (!strcmp(str, "HygonGenuine"))
        return X86_VENDOR_HYGON;
    if (!strcmp(str, "CentaurHauls"))
        return X86_VENDOR_CENTAUR;
    if (!strcmp(str, "Shanghai"))
        return X86_VENDOR_ZHAOXIN;
    return X86_VENDOR_UNKNOWN;
}

void x86_cpu_detect(struct x86_cpu_info *info)
{
    char raw_vendor[13];
    struct cpuid_result r, ext, leaf7;

    memset(info, 0, sizeof(*info));

    cpuid_vendor_id(raw_vendor);
    info->vendor_str = vendor_to_name(detect_vendor(raw_vendor));
    info->vendor     = info->vendor_str ? detect_vendor(raw_vendor)
                                         : X86_VENDOR_UNKNOWN;

    info->max_basic_leaf = cpuid_get(0, 0).eax;
    info->max_ext_leaf   = cpuid_get(0x80000000, 0).eax;

    /* Standard features: leaf 1. */
    r = cpuid_get(1, 0);

    /* Family / model / stepping (Intel and AMD both use the same scheme
     * for current-generation CPUs; fold extended fields where needed). */
    u32 base_fam  = (r.eax >> 8) & 0xF;
    u32 base_model = (r.eax >> 4) & 0xF;
    u32 ext_fam   = (r.eax >> 20) & 0xFF;
    u32 ext_model = (r.eax >> 16) & 0xF;

    info->stepping = r.eax & 0xF;

    if (base_fam == 0x0F || base_fam == 0x06) {
        info->family = base_fam + ext_fam;
        if (base_fam == 0x0F)
            info->model = (ext_model << 4) | base_model;
        else
            info->model = (ext_model << 4) | base_model;
    } else {
        info->family  = base_fam;
        info->model   = base_model;
    }

    u32 ecx_feat = r.ecx;
    u32 edx_feat = r.edx;

    /* Extended features: leaf 0x80000001. */
    ext = cpuid_get(0x80000001, 0);

    /* Leaf 7, sub-leaf 0 (AVX2/BMI1/BMI2 live in EBX). */
    leaf7 = cpuid_get(7, 0);
    u32 ebx_feat = leaf7.ebx;

    /* Decode into feature bitmap. */
    u32 f = 0;

    /* Standard feature bits (leaf 1, ECX/EDX). */
    if (edx_feat & (1u << 5))   f |= (1u << X86_FEATURE_MSR);
    if (edx_feat & (1u << 9))   f |= (1u << X86_FEATURE_APIC);
    if (edx_feat & (1u << 4))   f |= (1u << X86_FEATURE_TSC);
    if (edx_feat & (1u << 23))  f |= (1u << X86_FEATURE_MMX);
    if (edx_feat & (1u << 24))  f |= (1u << X86_FEATURE_FXSR);
    if (edx_feat & (1u << 25))  f |= (1u << X86_FEATURE_SSE);
    if (edx_feat & (1u << 26))  f |= (1u << X86_FEATURE_SSE2);
    if (edx_feat & (1u << 28))  f |= (1u << X86_FEATURE_HTT);

    if (ecx_feat & (1u << 0))   f |= (1u << X86_FEATURE_SSE3);
    if (ecx_feat & (1u << 1))   f |= (1u << X86_FEATURE_PCLMULQDQ);
    if (ecx_feat & (1u << 9))   f |= (1u << X86_FEATURE_SSSE3);
    if (ecx_feat & (1u << 12))  f |= (1u << X86_FEATURE_FMA);
    if (ecx_feat & (1u << 19))  f |= (1u << X86_FEATURE_SSE41);
    if (ecx_feat & (1u << 20))  f |= (1u << X86_FEATURE_SSE42);
    if (ecx_feat & (1u << 23))  f |= (1u << X86_FEATURE_POPCNT);
    if (ecx_feat & (1u << 25))  f |= (1u << X86_FEATURE_AES);
    if (ecx_feat & (1u << 28))  f |= (1u << X86_FEATURE_AVX);

    /* Extended features (leaf 0x80000001, EDX/ECX). */
    if (ext.edx & (1u << 20))   f |= (1u << X86_FEATURE_NX);
    if (ext.edx & (1u << 11))   f |= (1u << X86_FEATURE_SYSCALL);
    if (ext.edx & (1u << 27))   f |= (1u << X86_FEATURE_RDTSCP);
    if (ext.ecx & (1u << 0))    f |= (1u << X86_FEATURE_LAHF64);

    /* Leaf 7, EBX. */
    if (ebx_feat & (1u << 3))   f |= (1u << X86_FEATURE_BMI1);
    if (ebx_feat & (1u << 5))   f |= (1u << X86_FEATURE_AVX2);
    if (ebx_feat & (1u << 8))   f |= (1u << X86_FEATURE_BMI2);

    info->features = f;
}

static const char *const feat_names[X86_FEATURE_COUNT] = {
    [X86_FEATURE_MSR]       = "MSR",
    [X86_FEATURE_APIC]      = "APIC",
    [X86_FEATURE_MMX]       = "MMX",
    [X86_FEATURE_FXSR]      = "FXSR",
    [X86_FEATURE_SSE]       = "SSE",
    [X86_FEATURE_SSE2]      = "SSE2",
    [X86_FEATURE_SSE3]      = "SSE3",
    [X86_FEATURE_SSSE3]     = "SSSE3",
    [X86_FEATURE_SSE41]     = "SSE4.1",
    [X86_FEATURE_SSE42]     = "SSE4.2",
    [X86_FEATURE_POPCNT]    = "POPCNT",
    [X86_FEATURE_AES]       = "AES",
    [X86_FEATURE_PCLMULQDQ] = "PCLMULQDQ",
    [X86_FEATURE_FMA]       = "FMA",
    [X86_FEATURE_AVX]       = "AVX",
    [X86_FEATURE_AVX2]      = "AVX2",
    [X86_FEATURE_BMI1]      = "BMI1",
    [X86_FEATURE_BMI2]      = "BMI2",
    [X86_FEATURE_NX]        = "NX/XD",
    [X86_FEATURE_SYSCALL]   = "SYSCALL",
    [X86_FEATURE_RDTSCP]    = "RDTSCP",
    [X86_FEATURE_LAHF64]    = "LAHF64",
    [X86_FEATURE_TSC]       = "TSC",
    [X86_FEATURE_HTT]       = "HTT",
};

void cpu_vendor_init(void)
{
    x86_cpu_detect(&g_cpu_info);

    const char *ven = g_cpu_info.vendor_str;
    printk("CPU: vendor '%s'  family %u model %u stepping %u\n",
           ven ? ven : "?", g_cpu_info.family, g_cpu_info.model,
           g_cpu_info.stepping);
    printk("CPU: max basic leaf %u  max ext leaf %u\n",
           g_cpu_info.max_basic_leaf, g_cpu_info.max_ext_leaf);

    /* Feature list. */
    printk("CPU: features:");
    int first = 1;
    for (unsigned i = 0; i < X86_FEATURE_COUNT; i++) {
        if (!feat_names[i])
            continue;
        if (g_cpu_info.features & (1u << i)) {
            printk("%s%s", first ? " " : " ", feat_names[i]);
            first = 0;
        }
    }
    printk("\n");

    /* Vendor-specific notes. */
    if (g_cpu_info.vendor == X86_VENDOR_INTEL) {
        pr_info("CPU: Intel family %u (display model %u) -- %s\n",
                g_cpu_info.family, g_cpu_info.model,
                "Intel VT-x available via CPUID.1:ecx[5]");
    } else if (g_cpu_info.vendor == X86_VENDOR_AMD) {
        pr_info("CPU: AMD family %u (display model %u) -- %s\n",
                g_cpu_info.family, g_cpu_info.model,
                "AMD SVM available via CPUID.80000001:ecx[1]");
    } else if (g_cpu_info.vendor == X86_VENDOR_HYGON) {
        pr_info("CPU: Hygon (AMD-compatible CPUID family %u)\n",
                g_cpu_info.family);
    }

    pr_info("CPU: %s init complete\n", ven ? ven : "unknown");
}

const struct x86_cpu_info *x86_cpu_info_get(void)
{
    return &g_cpu_info;
}