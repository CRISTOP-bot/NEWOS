#ifndef NET_ARP_ARP_H
#define NET_ARP_ARP_H

#include <net/core/net_core.h>

/* ARP functions */
int arp_init(void);
int arp_deinit(void);
int arp_resolve(struct net_iface *iface, const uint8_t *ip_addr, uint8_t *mac_addr);
int arp_process(struct net_iface *iface, const void *buf, size_t len);

#endif /* NET_ARP_ARP_H */
