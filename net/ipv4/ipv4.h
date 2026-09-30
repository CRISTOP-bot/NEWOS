#ifndef NET_IPV4_IPV4_H
#define NET_IPV4_IPV4_H

#include <net/core/net_core.h>

/* IPv4 functions */
int ipv4_init(void);
int ipv4_deinit(void);
int ipv4_output(struct net_iface *iface, const void *buf, size_t len,
                const uint8_t *dest_addr);
int ipv4_input(struct net_iface *iface, const void *buf, size_t len);

#endif /* NET_IPV4_IPV4_H */
