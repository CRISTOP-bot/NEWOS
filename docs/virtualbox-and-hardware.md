# Booting the ISO: VirtualBox and real hardware

`make iso` produces `build/images/newos-x86_64.iso`: a GRUB 2 (multiboot2)
ISO with **both** legacy BIOS (El Torito, `eltorito.img`) and UEFI
(`/efi.img`) boot entries, and a kernel that mirrors its console on serial
COM1 (115200 8N1) **and** the VGA text mode screen.

## Oracle VirtualBox (legacy BIOS)

The ISO is **not** hybrid/MBR-bootable; it must be attached to VirtualBox as
a CD/DVD-ROM drive.

1. New machine or existing VM:
   - *Operating System*: `Linux`, *Version*: `Other Linux (64-bit)`.
   - RAM: 128 MB or more.
   - **Keep the default firmware (legacy BIOS)**. No need for "Enable EFI".
2. *Storage* -> the optical drive (empty) -> *Choose a disk file* ->
   `build/images/newos-x86_64.iso`.
3. Start the machine. A GRUB menu flashes (VGA console) and NEWOS boots;
   the suite prints `NEWOS: boot complete.` and the machine pauses.

Optional serial debugging (VirtualBox legacy BIOS has no UEFI console plumbing):

- VM *Settings* -> *Serial Ports* -> Port 1: `COM1`, Port mode:
  *TCP* or *Host Pipe* -> create a **named pipe** on the host path.
- Leave port I/O address at `0x3F8`, IRQ 4. The kernel already logs to
  `0x3F8` at 115200 8N1 even when no pipe is connected (writes to a missing
  port are no-ops), so the VGA screen alone is always safe.

## Real (bare metal) hardware

- Hardware: x86-64 CPU, legacy BIOS or UEFI (CSM/legacy recommended unless
  the board boots the ISO's `efi.img`; most UEFI boards in CSM mode boot the
  BIOS entry).
- Write the ISO to a CD/DVD or USB (USB requires your board to boot
  El Torito *and* the ISO to be hybrid; use a real CD or the UEFI entry for
  USB). Boot the medium from the boot menu.
- The kernel prints to the VGA text screen regardless of serial presence.
- For a serial log on real hardware connect COM1 (16550 UART, `0x3F8`,
  **115200 8N1**, no flow control) to a serial adapter / null-modem.

## Validated matrix

| Path | Firmware | Result |
| ---- | -------- | ------ |
| multiboot2 via GRUB ISO | SeaBIOS (QEMU `-boot d`) | `NEWOS: boot complete.` |
| multiboot2 via GRUB ISO | OVMF/UEFI (QEMU `-bios` + `OVMF.fd`) | `NEWOS: boot complete.` |
| PVH | QEMU `-kernel` (`run_qemu_test.sh`) | 6 + 8 self-tests pass |

The GRUB menu is configured for the serial console (`terminal_input
serial` + `terminal_output serial`, 115200 8N1), so a headless rig with
COM1 works; on a VGA-only machine the menu itself is not visible but the
default entry boots NEWOS anyway and the kernel mirrors output on the VGA
text screen. A missing serial port is non-fatal.

## Reproducible ISO

`make iso` pins every time-derived byte (volume timestamps, the xorriso
`.uuid` volume marker, and the `efi.img` FAT volume serial) so that two
`make clean && make iso` runs produce **byte-identical** ISOs. Verify with
two consecutive builds and compare hashes:

```sh
make clean && make iso && sha256sum build/images/newos-x86_64.iso
make clean && make iso && sha256sum build/images/newos-x86_64.iso
```

(The hashes move with the tree — new tools or initramfs content change
the image — so no hash is pinned here; equality of the two runs is the
property.) The kernel ELF is reproducible the same way.