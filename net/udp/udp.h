#ifndef NET_UDP_UDP_H
#define NET_UDP_UDP_H

#include <net/core/net_core.h>

/* UDP functions */
int udp_init(void);
int udp_deinit(void);
int udp_send(struct net_iface *iface, uint16_t src_port, uint16_t dst_port,
             const void *data, size_t len, const uint8_t *dest_ip);
int udp_receive(struct net_iface *iface, void *buf, size_t len,
                uint16_t *src_port, uint16_t *dst_port,
                uint8_t *src_ip, uint8_t *dst_ip);

#endif /* NET_UDP_UDP_H */
