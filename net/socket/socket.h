#ifndef NET_SOCKET_SOCKET_H
#define NET_SOCKET_SOCKET_H

#include <net/core/net_core.h>
#include <fs/vfs.h>

/* Socket functions */
int socket_init(void);
int socket_deinit(void);
int socket_create(int domain, int type, int protocol, struct net_iface *iface);

#endif /* NET_SOCKET_SOCKET_H */
