#include <drivers/pcnet.h>
#include <drivers/net.h>
#include <core/core.h>
#include <core/core_printk.h>
#include <mm/mm_pmm.h>
#include <string.h>
#include <iru_string.h>

/* IP protocol numbers */
#define IPPROTO_IP    0
#define IPPROTO_ICMP  1
#define IPPROTO_UDP   17
#define IPPROTO_TCP   6

/* IP header structure */
struct ip_hdr {
    u8_t  ip_hl_vi;    /* header length + version */
    u8_t  ip_tos;      /* type of service */
    u16_t ip_len;      /* total length */
    u16_t ip_id;       /* identification */
    u16_t ip_off;      /* fragment offset */
    u8_t  ip_ttl;      /* time to live */
    u8_t  ip_p;        /* protocol */
    u16_t ip_sum;      /* checksum */
    u32_t ip_src;      /* source address */
    u32_t ip_dst;      /* destination address */
};

/* ICMP header structure */
struct icmp_hdr {
    u8_t  icmp_type;
    u8_t  icmp_code;
    u16_t icmp_sum;
    u16_t icmp_id;
    u16_t icmp_seq;
};

/* UDP header structure */
struct udp_hdr {
    u16_t udp_src;     /* source port */
    u16_t udp_dst;     /* destination port */
    u16_t udp_len;     /* udg length */
    u16_t udp_sum;     /* udp checksum */
};

/* ARP table entry (simple, single-entry for same-subnet) */
struct arp_entry {
    u32_t ip_addr;
    u8_t  mac_addr[6];
};

/* Global variables */
static u8_t local_mac[6] = {0};
static u32_t local_ip = 0;
static u8_t local_ip_set = 0;

/* Forward declaration */
static void ip_input(struct pbuf *p);
static void ip_send_struct(struct ip_hdr *ip, struct pbuf *p);
static void icmp_send(struct icmp_hdr *icmp, u32_t src, u32_t dst, u8_t type);

/* Ethernet type for IP */
#define ETYPE_IP 0x0800

/* Broadcast IP */
#define IP_BROADCAST 0xFFFFFFFF

/* IP input - called from pcnet driver when a frame is received */
void ip_init(void) {
    /* MAC address could be obtained from PROM or configured */
    core_printk("IP init\n");
}

void ip_input(struct pbuf *p) {
    struct ip_hdr *ip;
    
    if (p->len < sizeof(struct ip_hdr)) {
        kfree(p);
        return;
    }
    
    ip = (struct ip_hdr *)p->payload;
    
    /* Check version (IPv4) */
    if ((ip->ip_hl_vi & 0xF0) != 0x40) {
        kfree(p);
        return;
    }
    
    /* Check our IP address */
    if (local_ip_set && ip->ip_dst != local_ip) {
        kfree(p);
        return; /* Not for us */
    }
    
    /* Check header length */
    if (ip->ip_hl_vi & 0x0F < 5) {
        kfree(p);
        return;
    }
    
    /* IP protocol */
    if (ip->ip_p == IPPROTO_ICMP) {
        /* Process ICMP */
        if (p->len >= sizeof(struct ip_hdr) + sizeof(struct icmp_hdr)) {
            struct icmp_hdr *icmp = (struct icmp_hdr *)( (u8_t*)ip + ip->ip_hl_vi * 4 );
            /* Check if it's a ping request */
            if (icmp->icmp_type == 8) { /* Echo request */
                icmp_send(icmp, ip->ip_src, ip->ip_dst, 0); /* Echo reply */
            }
        }
    } else if (ip->ip_p == IPPROTO_UDP) {
        /* Process UDP - simple pass-through for now */
    }
    
    kfree(p);
}

/* Send an IP packet */
void ip_output(u32_t src, u32_t dst, u8_t protocol, void *data, u16_t data_len) {
    /* Simple IP output - just send as ARP request or direct */
    core_printk("IP output: %d.%d.%d.%d -> %d.%d.%d.%d\n",
                (src & 0xFF), ((src >> 8) & 0xFF), ((src >> 16) & 0xFF), ((src >> 24) & 0xFF),
                (dst & 0xFF), ((dst >> 8) & 0xFF), ((dst >> 16) & 0xFF), ((dst >> 24) & 0xFF));
    
    /* For now, just kfree the data - full implementation would need */
    /* ARP resolution and frame construction */
    if (data) kfree(data);
}

/* ICMP send - generates an ICMP packet */
void icmp_send(struct icmp_hdr *icmp, u32_t src, u32_t dst, u8_t type) {
    struct ip_hdr *ip;
    struct pbuf *p;
    
    /* Build IP header */
    ip = kzalloc(sizeof(struct ip_hdr));
    if (!ip) return;
    
    ip->ip_hl_vi = 0x45; /* IPv4, 20-byte header */
    ip->ip_tos = 0;
    ip->ip_len = sizeof(struct ip_hdr) + sizeof(struct icmp_hdr);
    ip->ip_id = 0;
    ip->ip_off = 0;
    ip->ip_ttl = 64;
    ip->ip_p = IPPROTO_ICMP;
    ip->ip_sum = 0; /* checksum will be calculated */
    ip->ip_src = src;
    ip->ip_dst = dst;
    
    /* Calculate IP checksum */
    ip->ip_sum = ~(~ip->ip_sum + ...); /* simplified */
    
    /* Build ICMP header */
    if (icmp) {
        icmp->icmp_type = type;
        icmp->icmp_code = 0;
        icmp->icmp_sum = 0;
    }
    
    /* TODO: Actually transmit via pcnet */
    kfree(ip);
}

/* ARP - simple implementation */
void arp_resolve(u32_t ip_addr) {
    /* In a full implementation, this would send an ARP request */
    /* For now, just mark as pending */
    core_printk("ARP resolve for %d.%d.%d.%d\n",
                (ip_addr & 0xFF), ((ip_addr >> 8) & 0xFF),
                ((ip_addr >> 16) & 0xFF), ((ip_addr >> 24) & 0xFF));
}

/* Network interface input - called by pcnet driver */
void netif_input(struct pbuf *p) {
    /* Check Ethernet type */
    if (p->len < 2) {
        kfree(p);
        return;
    }
    
    u16_t etype = *(u16_t *)p->payload;
    if (etype == ETYPE_IP) {
        /* IP packet */
        ip_input(p);
    } else {
        kfree(p);
    }
}

/* Initialize the IP module */
void ip_module_init(void) {
    ip_init();
    core_printk("IP module initialized\n");
}