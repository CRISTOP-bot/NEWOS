#ifndef AHCI_H
#define AHCI_H

#include <core/core_types.h>

struct ahci_hba;
struct ahci_port;

struct ahci_hba {
    u32 *cap;
    u32 *version;
    u32 *ccc_ctl;
    u32 *ccc_ports;
    u32 *em_loc;
    u32 *em_ctrl;
    u32 *vendor;
    u32 *is;
    u32 *pi;
    u32 *vs;
    u32 *ohc_below_4g;
    u32 *clb;
    u32 *clb_upper;
    u32 *fb;
    u32 *fb_upper;
    u32 *port_list[32];
};

struct ahci_port {
    u32 *clb;
    u32 *clb_upper;
    u32 *fb;
    u32 *fb_upper;
    u32 *is;
    u32 *sie;
    u32 *cmd;
    u32 *ssts;
    u32 *serr;
    u32 *sact;
    u32 *ci;
    u32 *sntf;
    u32 *fbs;
    u32 *dev_slumber;
    u32 *vendor[10];
};

int ahci_init(void);
void ahci_scan(void);
struct ahci_port *ahci_get_port(struct ahci_hba *hba, u32 port_num);

#endif
