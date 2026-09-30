#ifndef NET_CORE_H
#define NET_CORE_H

#include <core/core_types.h>

/* Network interface types */
#define NET_IF_TYPE_ETHERNET 1
#define NET_IF_TYPE_LOOPBACK 2

/* Network interface flags */
#define NET_IF_UP        0x0001  /* Interface is up */
#define NET_IF_BROADCAST 0x0002  /* Broadcast address valid */
#define NET_IF_LOOPBACK  0x0004  /* Is a loopback netif */
#define NET_IF_POINTTOPOINT 0x0008  /* Is a point-to-point link */
#define NET_IF_RUNNING   0x0040  /* Interface is running */
#define NET_IF_NOARP     0x0080  /* No ARP protocol */
#define NET_IF_PROMISC   0x0100  /* Interface is in promiscuous mode */

/* Maximum number of network interfaces */
#define NET_IF_MAX 8

/* Network interface structure */
struct net_iface {
    const char *name;           /* Interface name */
    uint8_t type;               /* Interface type */
    uint16_t flags;             /* Interface flags */
    uint8_t mac_addr[6];        /* MAC address */
    uint8_t ip_addr[16];        /* IP address (IPv4 or IPv6) */
    uint8_t netmask[16];        /* Network mask */
    uint8_t gw_addr[16];        /* Gateway address */
    struct net_iface *next;     /* Linked list of interfaces */
    
    /* Statistics */
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
    
    /* Driver-specific data */
    void *driver_data;
    
    /* Function pointers for interface operations */
    int (*init)(struct net_iface *iface);
    int (*deinit)(struct net_iface *iface);
    int (*transmit)(struct net_iface *iface, const void *buf, size_t len);
    int (*set_addr)(struct net_iface *iface, const uint8_t *addr, size_t len);
};

/* Protocol family types */
#define PF_UNSPEC  0   /* Unspecified */
#define PF_LOCAL   1   /* Local to host (pipes, file-domain) */
#define PF_UNIX    PF_LOCAL  /* POSIX name for PF_LOCAL */
#define PF_INET    2   /* IP protocol family */
#define PF_INET6   10  /* IP version 6 */
#define PF_NETLINK 16
#define PF_PACKET  17  /* Packet family */
#define PF_MAX     17

/* Socket types */
#define SOCK_STREAM    1   /* Sequenced, reliable, two-way connection */
#define SOCK_DGRAM     2   /* Datagram (connectionless, unreliable) */
#define SOCK_RAW       3   /* Raw protocol interface */
#define SOCK_RDM       4   /* Reliably-delivered message */
#define SOCK_SEQPACKET 5   /* Sequenced, reliable, two-way connection */
#define SOCK_DCCP      6   /* Datagram congestion controlled */
#define SOCK_PACKET    10  /* Linux specific way of getting packets */

/* Socket options */
#define SOL_SOCKET  0xFFFF   /* Options for socket level */
#define SO_DEBUG    1
#define SO_REUSEADDR 2
#define SO_TYPE     3
#define SO_ERROR    4
#define SO_DONTROUTE 5
#define SO_BROADCAST 6
#define SO_SNDBUF   7
#define SO_RCVBUF   8
#define SO_SNDBUFFORCE 32
#define SO_RCVBUFFORCE 33
#define SO_KEEPALIVE 9
#define SO_OOBINLINE 10
#define SO_NO_CHECK 11
#define SO_PRIORITY 12
#define SO_LINGER   13
#define SO_BSDCOMPAT 14
#define SO_REUSEPORT 15
#define SO_PASSCRED 16
#define SO_PEERCRED 17
#define SO_RCVLOWAT 18
#define SO_SNDLOWAT 19
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21

/* Address family for sockaddr */
#define AF_UNSPEC  0   /* Unspecified */
#define AF_LOCAL   1   /* Local to host (pipes, file-domain) */
#define AF_UNIX    AF_LOCAL  /* POSIX name for AF_LOCAL */
#define AF_INET    2   /* IP protocol family */
#define AF_INET6   10  /* IP version 6 */
#define AF_NETLINK 16
#define AF_PACKET  17  /* Packet family */
#define AF_MAX     17

/* Address payloads precede sockaddr_in so these members are complete types. */
struct in_addr {
    uint32_t s_addr;
};

struct in6_addr {
    uint8_t s6_addr[16];
};

/* Socket address structure (generic) */
struct sockaddr {
    uint16_t sa_family;  /* Address family */
    char     sa_data[14]; /* Address data */
};

/* IPv4 socket address structure */
struct sockaddr_in {
    uint16_t sin_family;  /* Address family */
    uint16_t sin_port;    /* Port number */
    struct in_addr sin_addr; /* Internet address */
    unsigned char sin_zero[8]; /* Pad to size of 'struct sockaddr' */
};

/* IPv6 socket address structure */
struct sockaddr_in6 {
    uint16_t sin6_family;   /* Address family */
    uint16_t sin6_port;     /* Port number */
    uint32_t sin6_flowinfo; /* IPv6 flow information */
    struct in6_addr sin6_addr; /* IPv6 address */
    uint32_t sin6_scope_id; /* Scope ID */
};

/* Socket state */
#define SOCKET_CLOSED    0
#define SOCKET_LISTEN    1
#define SOCKET_CONNECTED 2
#define SOCKET_BOUND     3

/* Socket state structure */
struct socket_state {
    int fd;                    /* File descriptor */
    int domain;                /* Protocol family */
    int type;                  /* Socket type */
    int protocol;              /* Protocol */
    int state;                 /* Socket state */
    int port;                  /* Local port (for bound sockets) */
    struct sockaddr_in local_addr;  /* Local address */
    struct sockaddr_in remote_addr; /* Remote address (for connected sockets) */
    struct net_iface *iface;  /* Associated network interface */
    
    /* Buffer for data */
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    size_t rx_buffer_pos;
    
    /* Callback for when data is received */
    void (*on_data_received)(struct socket_state *sock, const void *data, size_t len);
};

/* Network stack initialization */
int net_stack_init(void);

/* Network interface registration */
int net_iface_register(struct net_iface *iface);

/* Find interface by name */
struct net_iface *net_iface_find_by_name(const char *name);

/* Get default interface */
struct net_iface *net_iface_get_default(void);

#endif /* NET_CORE_H */
