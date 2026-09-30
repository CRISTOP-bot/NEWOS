#include "../../lib/nshlib.h"
#include <string.h>
#include <stdio.h>

#define PACKET_SIZE 64
#define ICMP_ECHO 8
#define ICMP_ECHOREPLY 0
#define IPPROTO_ICMP 1

#define MAX_RETRIES 3
#define TIMEOUT_MS 1000

/* Prototypes */
static int ping_host(const char *host, int count);
static u16_t checksum(void *b, int len);

/* Estructura de encabezado IP */
struct ip_header {
    u8_t ip_hl_vi;
    u8_t ip_tos;
    u16_t ip_len;
    u16_t ip_id;
    u16_t ip_off;
    u8_t ip_ttl;
    u8_t ip_p;
    u16_t ip_sum;
    u32_t ip_src;
    u32_t ip_dst;
};

/* Estructura de encabezado ICMP */
struct icmp_header {
    u8_t icmp_type;
    u8_t icmp_code;
    u16_t icmp_sum;
    u16_t icmp_id;
    u16_t icmp_seq;
    char data[PACKET_SIZE - 8];
};

/* Función principal */
int main(int argc, char *argv[]) {
    const char *host = "8.8.8.8"; /* default */
    int count = 4;
    int i;
    
    /* Parse arguments */
    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        count = atoi(argv[2]);
    }
    
    printk("PING %s: %d datos de datos %d bytes\n", host, count, PACKET_SIZE);
    
    /* Resolve hostname (simple: use IP directly) */
    u32_t addr = 0;
    if (host[0] >= '0' && host[0] <= '9') {
        /* Parse IP address */
        sscanf(host, "%d.%d.%d.%d", 
               (int *)&((u8_t*)&addr)[0], 
               (int *)&((u8_t*)&addr)[1], 
               (int *)&((u8_t*)&addr)[2], 
               (int *)&((u8_t*)&addr)[3]);
    }
    
    if (!addr) {
        printk("ping: unknown host %s\n", host);
        return 1;
    }
    
    /* Abrir socket ICMP */
    int fd = syscall_socket(2, 1, 1); /* AF_INET=2, SOCK_RAW=1, IPPROTO_ICMP=1 */
    if (fd < 0) {
        printk("ping: socket failed\n");
        return 1;
    }
    
    /* Configurar sockaddr */
    struct sockaddr_in {
        u8_t sin_family; /* AF_INET */
        u16_t sin_port;
        u32_t sin_addr;
    } *addr;
    
    /* Enviar paquetes ICMP echo */
    for (i = 0; i < count; i++) {
        struct icmp_header icmp;
        struct ip_header ip;
        u8_t packet[sizeof(struct ip_header) + sizeof(struct icmp_header)];
        int sent;
        
        /* Construir encabezado ICMP */
        memset(&icmp, 0, sizeof(icmp));
        icmp.icmp_type = ICMP_ECHO;
        icmp.icmp_code = 0;
        icmp.icmp_id = 1234; /* ID simple */
        icmp.icmp_seq = i + 1;
        /* Llenar datos */
        memset(icmp.data, 0xAA, sizeof(icmp.data));
        icmp.icmp_sum = checksum(&icmp, sizeof(icmp));
        
        /* Construir encabezado IP */
        memset(&ip, 0, sizeof(ip));
        ip.ip_hl_vi = 0x45; /* IPv4, 20-byte header */
        ip.ip_tos = 0;
        ip.ip_len = sizeof(struct ip_header) + sizeof(struct icmp_header);
        ip.ip_id = (u16_t)(i + 1);
        ip.ip_off = 0;
        ip.ip_ttl = 64;
        ip.ip_p = IPPROTO_ICMP;
        ip.ip_src = ...; /* IP de origen */
        ip.ip_dst = addr;
        /* Calcular checksum IP */
        ip.ip_sum = checksum(&ip, sizeof(ip));
        
        /* Ensamblar paquete */
        memcpy(packet, &ip, sizeof(struct ip_header));
        memcpy(packet + sizeof(struct ip_header), &icmp, sizeof(struct icmp_header));
        
        /* Enviar */
        sent = syscall_sendto(fd, packet, sizeof(packet), 0, 
                              (struct sockaddr*)&addr, sizeof(addr));
        
        /* Esperar respuesta (simplificado) */
        /* En un implementación completa, usaríamos select/poll con timeout */
        
        /* Pequeña pausa */
        for (volatile int j = 0; j < 100000; j++) /* delay */;
    }
    
    /* Cerrar socket */
    syscall_close(fd);
    
    printk("\n--- %s ping statistics ---\n", host);
    printk("%d packets transmitted, %d received, %d%% packet loss\n", 
           count, i, (count - i) * 100 / count);
    
    return 0;
}

/* Función de checksum de Internet */
static u16_t checksum(void *b, int len) {
    u16_t *buf = b;
    unsigned long sum = 0;
    int i;
    
    for (i = 0; i < (len + 1) / 2; i++) {
        sum += buf[i];
    }
    if (len % 2) sum += buf[i] << 8;
    
    /* Fold 32-bit to 16-bit */
    while (sum >> 16) sum = (sum & 0FFFF) + (sum >> 16);
    return (u16_t)(~sum);
}