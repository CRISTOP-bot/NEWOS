#include <net/core/net_core.h>
#include <net/ipv4/ipv4.h>
#include <core/core_printk.h>
#include <iru_string.h>

/* IPv4 implementation (stub) */
int ipv4_init(void)
{
    printk("IPv4: no packet input/output path registered\n");
    return -1;
}

int ipv4_deinit(void)
{
    printk("IPv4 stack deinitialized (stub)\n");
    return 0;
}

/* Output an IPv4 packet (stub) */
int ipv4_output(struct net_iface *iface, const void *buf, size_t len,
                const uint8_t *dest_addr)
{
    if (!iface || !buf || len == 0 || !dest_addr)
        return -1;
    
    (void)dest_addr;
    printk("IPv4: packet output unavailable\n");
    return -1;
}

/* Input an IPv4 packet (stub) */
int ipv4_input(struct net_iface *iface, const void *buf, size_t len)
{
    if (!iface || !buf || len == 0)
        return -1;
    
    printk("IPv4: packet input unavailable on %s\n", iface->name);
    return -1;
}
