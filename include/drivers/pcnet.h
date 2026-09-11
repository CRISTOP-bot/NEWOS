#ifndef DRIVERS_PCNET_H
#define DRIVERS_PCNET_H

/* AMD PCnet/PCI Ethernet driver.
 *
 * Drives the PCnet-PCI II (Am79C970A/Am79C973, PCI vendor 0x1022, device
 * 0x2000) - the NIC QEMU has natively (and the 'pcnet' SMSC/AMD card
 * VirtualBox offers) - in 32-bit DWIO mode with SWSTYLE-2 descriptors.
 *
 * The binder configures the PCI function (I/O BAR, bus mastering, IRQ);
 * bring-up (soft reset, BCR/init block, START) and the loopback selftest
 * are driven from pcnet_selftest(), which the kernel self-test suite runs. */

void pcnet_driver_register(void);
int  pcnet_selftest(void);

#endif