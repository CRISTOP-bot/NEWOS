#include <net/core/net_core.h>
#include <net/ethernet/eth_iface.h>
#include <drivers/pcnet.h>
#include <drivers/pci.h>
#include <core/core_printk.h>
#include <mm/mm_heap.h>
#include <iru_string.h>

/* Ethernet interface implementation */
static int eth_transmit(struct net_iface *iface, const void *buf, size_t len)
{
    if (!iface || !buf || len == 0)
        return -1;
    
    /* TODO: Implement actual transmission using pcnet driver */
    /* For now, just update statistics */
    
    iface->tx_packets++;
    iface->tx_bytes += len;
    
    printk("ETH: Transmitting %zu bytes (not implemented)\n", len);
    return len;
}

/* Set MAC address for Ethernet interface */
static int eth_set_addr(struct net_iface *iface, const uint8_t *addr, size_t len)
{
    if (!iface || !addr || len != 6)
        return -1;
    
    memcpy(iface->mac_addr, addr, 6);
    printk("ETH: Setting MAC address to %02x:%02x:%02x:%02x:%02x:%02x\n",
           iface->mac_addr[0], iface->mac_addr[1], iface->mac_addr[2],
           iface->mac_addr[3], iface->mac_addr[4], iface->mac_addr[5]);
    return 0;
}

/* Initialize Ethernet interface */
int eth_iface_init(struct net_iface *iface)
{
    if (!iface)
        return -1;
    
    /* Set up Ethernet-specific values */
    iface->type = NET_IF_TYPE_ETHERNET;
    iface->flags = NET_IF_BROADCAST | NET_IF_UP;
    
    /* Initialize MAC address (will be overridden by pcnet driver) */
    memset(iface->mac_addr, 0x00, sizeof(iface->mac_addr));
    
    /* Set function pointers */
    iface->transmit = eth_transmit;
    iface->set_addr = eth_set_addr;
    
    printk("Ethernet interface initialized\n");
    return 0;
}

/* Deinitialize Ethernet interface */
int eth_iface_deinit(struct net_iface *iface)
{
    if (!iface)
        return -1;
    
    iface->flags &= ~(NET_IF_UP | NET_IF_RUNNING);
    printk("Ethernet interface deinitialized\n");
    return 0;
}
