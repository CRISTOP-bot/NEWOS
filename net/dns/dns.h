#ifndef NET_DNS_DNS_H
#define NET_DNS_DNS_H

#include <net/core/net_core.h>

/* DNS functions */
int dns_init(void);
int dns_deinit(void);
int dns_resolve(const char *hostname, uint8_t *ip_addr, size_t ip_addr_len);
int dns_process_response(const void *buf, size_t len, uint8_t *ip_addr, size_t ip_addr_len);

#endif /* NET_DNS_DNS_H */
