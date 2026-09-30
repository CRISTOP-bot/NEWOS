#include <net/core/net_core.h>
#include <net/arp/arp.h>
#include <core/core_printk.h>
#include <iru_string.h>

/* ARP implementation (stub) */
int arp_init(void)
{
    printk("ARP stack initialized (stub)\n");
    return 0;
}

int arp_deinit(void)
{
    printk("ARP stack deinitialized (stub)\n");
    return 0;
}

/* Resolve an IP address to MAC address (stub) */
int arp_resolve(struct net_iface *iface, const uint8_t *ip_addr, uint8_t *mac_addr)
{
    if (!iface || !ip_addr || !mac_addr)
        return -1;
    
    printk("ARP: Resolving %d.%d.%d.%d to MAC on interface %s (stub)\n",
           ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3],
           iface ? iface->name : "unknown");
    
    /* Without a neighbor cache and Ethernet RX path, resolution is unknown. */
    return -1;
}

/* Process an ARP packet (stub) */
int arp_process(struct net_iface *iface, const void *buf, size_t len)
{
    if (!iface || !buf || len == 0)
        return -1;
    
    printk("ARP: receive path unavailable on interface %s (%zu bytes)\n",
           iface->name, len);
    return -1;
}
