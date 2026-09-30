#include <net/core/net_core.h>
#include <net/core/loopback.h>
#include <core/core_printk.h>
#include <mm/mm_heap.h>
#include <iru_string.h>

/* Loopback interface implementation */
static int loopback_transmit(struct net_iface *iface, const void *buf, size_t len)
{
    if (!iface || !buf || len == 0)
        return -1;
    
    /* For loopback, we just update statistics and pretend to transmit */
    /* In a real implementation, we would deliver the packet to the receive queue */
    
    iface->tx_packets++;
    iface->tx_bytes += len;
    
    /* Simulate immediate reception (loopback) */
    iface->rx_packets++;
    iface->rx_bytes += len;
    
    return len;
}

/* Initialize loopback interface */
int loopback_init(struct net_iface *iface)
{
    if (!iface)
        return -1;
    
    /* Set up loopback-specific values */
    iface->type = NET_IF_TYPE_LOOPBACK;
    iface->flags = NET_IF_LOOPBACK | NET_IF_UP | NET_IF_RUNNING;
    
    /* Set loopback IP (127.0.0.1) */
    memset(iface->ip_addr, 0, sizeof(iface->ip_addr));
    iface->ip_addr[0] = 127;  /* 127.0.0.1 */
    iface->ip_addr[3] = 1;
    
    /* Set netmask (255.0.0.0) */
    memset(iface->netmask, 0, sizeof(iface->netmask));
    iface->netmask[0] = 255;
    
    iface->transmit = loopback_transmit;
    
    printk("Loopback interface initialized\n");
    return 0;
}

/* Deinitialize loopback interface */
int loopback_deinit(struct net_iface *iface)
{
    if (!iface)
        return -1;
    
    iface->flags &= ~(NET_IF_UP | NET_IF_RUNNING);
    printk("Loopback interface deinitialized\n");
    return 0;
}
