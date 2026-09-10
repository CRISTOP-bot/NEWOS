#include <arch/x86_64/cpu.h>
#include <kernel/printk.h>
#include <libk/string.h>

struct cpuid_result cpuid_get(u32 leaf, u32 subleaf)
{
    struct cpuid_result r;
    __asm__ volatile(
        "cpuid"
        : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
        : "a"(leaf), "c"(subleaf));
    return r;
}

u32 cpuid_vendor_id(char out[13])
{
    struct cpuid_result r = cpuid_get(0, 0);
    u32 *vendor = (u32 *)out;
    vendor[0] = r.ebx;
    vendor[1] = r.edx;
    vendor[2] = r.ecx;
    out[12] = '\0';
    return r.eax;   /* highest basic leaf */
}

int cpuid_long_mode_supported(void)
{
    struct cpuid_result r = cpuid_get(0x80000001, 0);
    return (r.edx >> 29) & 1;
}

static void cpu_print_brand(void)
{
    struct cpuid_result r;

    /* CPUID leaf 0x80000000 reveals whether the extended brand string
     * leaves (0x80000002..0x80000004) are present. */
    r = cpuid_get(0x80000000, 0);
    if (r.eax < 0x80000004) {
        printk("CPU: brand string unavailable\n");
        return;
    }

    char brand[49];
    struct cpuid_result b1 = cpuid_get(0x80000002, 0);
    struct cpuid_result b2 = cpuid_get(0x80000003, 0);
    struct cpuid_result b3 = cpuid_get(0x80000004, 0);

    memcpy(&brand[0],  &b1, 16);
    memcpy(&brand[16], &b2, 16);
    memcpy(&brand[32], &b3, 16);
    brand[48] = '\0';

    printk("CPU: %s\n", brand);
}

void cpu_print_info(void)
{
    char vendor[13];
    cpuid_vendor_id(vendor);

    struct cpuid_result feat = cpuid_get(1, 0);

    printk("CPU: vendor '%s'  features ecx=%x edx=%x\n",
           vendor, feat.ecx, feat.edx);
    printk("CPU: 64-bit long mode %s\n",
           cpuid_long_mode_supported() ? "supported" : "NOT supported");
    cpu_print_brand();
}