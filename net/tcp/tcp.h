#ifndef NET_TCP_TCP_H
#define NET_TCP_TCP_H

#include <net/core/net_core.h>

/* TCP functions */
int tcp_init(void);
int tcp_deinit(void);
int tcp_send(struct net_iface *iface, uint16_t src_port, uint16_t dst_port,
             const void *data, size_t len, const uint8_t *dest_ip);
int tcp_receive(struct net_iface *iface, void *buf, size_t len,
                uint16_t *src_port, uint16_t *dst_port,
                uint8_t *src_ip, uint8_t *dst_ip);

#endif /* NET_TCP_TCP_H */
