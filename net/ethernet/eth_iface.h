#ifndef NET_ETHERNET_ETH_IFACE_H
#define NET_ETHERNET_ETH_IFACE_H

#include <net/core/net_core.h>

/* Ethernet interface functions */
int eth_iface_init(struct net_iface *iface);
int eth_iface_deinit(struct net_iface *iface);

#endif /* NET_ETHERNET_ETH_IFACE_H */
