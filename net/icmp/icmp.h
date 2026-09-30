#ifndef NET_ICMP_ICMP_H
#define NET_ICMP_ICMP_H

#include <net/core/net_core.h>

/* ICMP functions */
int icmp_init(void);
int icmp_deinit(void);
int icmp_echo_send(struct net_iface *iface, uint16_t seq_num,
                   const void *data, size_t len,
                   const uint8_t *dest_ip);
int icmp_receive(struct net_iface *iface, void *buf, size_t len,
                 uint8_t *type, uint8_t *code,
                 uint8_t *src_ip, uint8_t *dst_ip);

#endif /* NET_ICMP_ICMP_H */
