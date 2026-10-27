# NEWOS networking (milestone 1: ICMP ping)

NEWOS ships a small, poll-mode IPv4 stack with one goal so far: get an
honest `ping` of the QEMU user-network gateway through the PCnet
interface, and grow from there. Everything is verifiable in
`make qemu-test` via the `net-ping` ktest.

## What exists

- **nic**: `kernel/drivers/net/pcnet.c` drives the AMD PCnet-PCI II
  (`-device pcnet` = QEMU's default NIC) over PCI bus mastering: a
  32-descriptor RX ring and 4 TDs in 16-bit descriptor style, interrupt
  masks OFF (`poll mode`), and a hard-earned in-order TX cursor. The
  driver exposes `pcnet_open/get_mac/send/poll` with ownership indices
  (`tx_index`/`rx_index`) that never rewind.
- **stack**: `kernel/net/net_stack.c` holds the ARP cache (8 entries) and
  the IPv4/ICMP logic: ARP request/reply + cache, IPv4 TX with proper
  checksum, RX validation (checksum, min-lengths), ICMP echo requests and
  replies with a single-slot waiter (id 0x4E45), and an address/mask/gw
  triple fixed at `10.0.2.15/255.255.255.0 gw 10.0.2.2` — the QEMU
  slirp default, not DHCP (nothing negotiates).
- **tx path**: `net_tx_frame` pads anything below 60 bytes to 60 —
  QEMU's slirp still grants ARP/ICMP to a 42-byte PCnet TX, but real
  hardware and the loopback path disagree with that; padding is the
  honest constant.
- **boot**: `net_init()` runs after `pci_init()` in the boot tail and
  prints `net: up, mac=... ip 10.0.2.15 mask ... gw ... (poll mode)`.

## Known gotchas (do not "simplify" these back)

1. **TX is in order.** The PCnet chip does NOT peek ahead at the TD ring
   and does NOT re-consume a slot re-written after use: one cursor
   (`tx_index`) that always steps `(slot + 1) % 4` after publishing OWN
   bit clears. Re-writing `tmd[0]` mid-flight (as a naive loopback
   selftest does) only works because the test re-INITs the card, which
   resets the internal TD head in step with the cursor.
2. **RX `msg_length` includes the 4-byte FCS.** A 60-byte loopback frame
   reports 64. Downstream consumers must slice by their own protocol
   lengths: `ipv4_rx` limits itself by the IPv4 total-length header
   field (`be16_get(ip+2)`, capped by available bytes) and ARP reads its
   28 bytes exactly. Never trust the ring length alone to size a packet.
3. **Poll, not IRQ.** The PCnet interrupt never surfaces under
   QEMU-TCG's coalescing; the stack polls `pcnet_poll()` from the ping
   path. Do not "fix" the driver into matching a textbook IRQ flow
   before IRQ plumbing (8259 glue exists, delivery timing does not).

## How it is tested

`make qemu-test` runs the kernel with a `-netdev user -device pcnet`
pair; the `net-ping` ktest ARP-resolves the gateway, sends `"NEWOS
net-ping"` as the ICMP payload and waits for the echo (single-wait slot,
bounded spin, timeout refuses silently like the disk tests). Without a
NIC attached the same invocation still passes — the ktest times out and
reports the attempt; CI looks at the phase counters, not at the payload.

## Honest limits

- No TCP/UDP, no IP fragmentation/reassembly, no routing beyond
  gateway-direct, no DHCP: the address is hard-coded to the slirp net.
- One NIC, one stack, no timers: every driver path runs from the
  ktest's own poll loop. Only one waiter exists (the ping slot).
- The stack is not reentrant. It is ktest-only today; userland sockets
  are a roadmap item, not a shipped feature.
