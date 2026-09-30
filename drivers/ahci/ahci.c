#include <drivers/ahci.h>
#include <core/core_printk.h>
#include <lib/kernel/iru_string.h>
#include <mm/mm_heap.h>

struct ahci_hba *g_ahci_hbas = NULL;

int ahci_init(void) {
    printk("AHCI: initializing\n");
    return 0;
}

void ahci_scan(void) {
    printk("AHCI: scanning for HBA\n");
}

struct ahci_port *ahci_get_port(struct ahci_hba *hba, u32 port_num) {
    if (!hba || port_num >= 32) return NULL;
    return (struct ahci_port *)hba->port_list[port_num];
}
