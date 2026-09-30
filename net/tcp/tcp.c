#include <net/core/net_core.h>
#include <net/tcp/tcp.h>
#include <core/core_printk.h>
#include <iru_string.h>

/* TCP implementation (stub) */
int tcp_init(void)
{
    printk("TCP stack initialized (stub)\n");
    return 0;
}

int tcp_deinit(void)
{
    printk("TCP stack deinitialized (stub)\n");
    return 0;
}

/* Send TCP data (stub) */
int tcp_send(struct net_iface *iface, uint16_t src_port, uint16_t dst_port,
             const void *data, size_t len, const uint8_t *dest_ip)
{
    if (!iface || !data || len == 0 || !dest_ip)
        return -1;
    
    printk("TCP: Sending %zu bytes from port %u to %d.%d.%d.%d:%u (stub)\n",
           len, src_port, dest_ip[0], dest_ip[1], dest_ip[2], dest_ip[3], dst_port);
    
    /* TODO: Add TCP header and pass to IPv4 */
    return len;
}

/* Receive TCP data (stub) */
int tcp_receive(struct net_iface *iface, void *buf, size_t len,
                uint16_t *src_port, uint16_t *dst_port,
                uint8_t *src_ip, uint8_t *dst_ip)
{
    if (!iface || !buf || len == 0)
        return -1;
    
    printk("TCP: Receiving up to %zu bytes on interface %s (stub)\n",
           len, iface ? iface->name : "unknown");
    
    /* TODO: Extract TCP header and data */
    return 0;
}
