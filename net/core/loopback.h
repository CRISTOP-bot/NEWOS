#ifndef NET_CORE_LOOPBACK_H
#define NET_CORE_LOOPBACK_H

#include <net/core/net_core.h>

/* Loopback interface functions */
int loopback_init(struct net_iface *iface);
int loopback_deinit(struct net_iface *iface);

#endif /* NET_CORE_LOOPBACK_H */
