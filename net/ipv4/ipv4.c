#include <net/core/net_core.h>
#include <net/ipv4/ipv4.h>
#include <core/core_printk.h>
#include <iru_string.h>

/* IPv4 implementation (stub) */
int ipv4_init(void)
{
    printk("IPv4 stack initialized (stub)\n");
    return 0;
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
    
    printk("IPv4: Outputting %zu bytes to %d.%d.%d.%d (stub)\n",
           len, dest_addr[0], dest_addr[1], dest_addr[2], dest_addr[3]);
    
    /* For now, just pass to the interface transmit function */
    if (iface->transmit)
        return iface->transmit(iface, buf, len);
    
    return -1;
}

/* Input an IPv4 packet (stub) */
int ipv4_input(struct net_iface *iface, const void *buf, size_t len)
{
    if (!iface || !buf || len == 0)
        return -1;
    
    printk("IPv4: Input %zu bytes from interface %s (stub)\n",
           len, iface ? iface->name : "unknown");
    
    /* TODO: Process IPv4 header and pass to upper layer */
    return len;
}
