#include <net/core/net_core.h>
#include <core/core.h>
#include <core/core_printk.h>
#include <mm/mm_pmm.h>
#include <mm/mm_heap.h>
#include <iru_string.h>

/* Linked list of network interfaces */
static struct net_iface *net_iface_list = NULL;
/* Default network interface */
static struct net_iface *net_default_iface = NULL;

/* Initialize the network stack */
int net_stack_init(void)
{
    printk("Initializing network stack...\n");
    
    /* Initialize loopback interface */
    static struct net_iface loopback_iface = {
        .name = "lo",
        .type = NET_IF_TYPE_LOOPBACK,
        .flags = NET_IF_LOOPBACK | NET_IF_UP | NET_IF_RUNNING,
        .mac_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .ip_addr = {127, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        .netmask = {255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        .init = NULL,  /* Loopback doesn't need special init */
        .deinit = NULL,
        .transmit = NULL,  /* Loopback transmit handled specially */
        .set_addr = NULL
    };
    
    /* Register loopback interface */
    net_iface_register(&loopback_iface);
    net_default_iface = &loopback_iface;
    
    printk("Network stack initialized\n");
    return 0;
}

/* Register a network interface */
int net_iface_register(struct net_iface *iface)
{
    if (!iface)
        return -1;
    
    /* Add to the linked list */
    iface->next = net_iface_list;
    net_iface_list = iface;
    
    printk("Registered network interface: %s\n", iface->name);
    return 0;
}

/* Find interface by name */
struct net_iface *net_iface_find_by_name(const char *name)
{
    struct net_iface *iface = net_iface_list;
    
    while (iface) {
        if (strcmp(iface->name, name) == 0)
            return iface;
        iface = iface->next;
    }
    
    return NULL;
}

/* Get default interface */
struct net_iface *net_iface_get_default(void)
{
    return net_default_iface;
}

/* Transmit a packet through an interface (stub) */
int net_iface_transmit(struct net_iface *iface, const void *buf, size_t len)
{
    if (!iface || !iface->transmit)
        return -1;
    
    return iface->transmit(iface, buf, len);
}
