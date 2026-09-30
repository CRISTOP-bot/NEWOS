# NEWOS Roadmap

Where NEWOS is going, in what order, and what is explicitly **not** planned.
This is a living document: completed items move to `CHANGELOG.md`,
and every milestone must keep `BUILD=PASS`, `BOOT=PASS`, `QEMU=PASS`
(16/16 self-tests green) or it does not ship.

## Where we are

- **Version:** `0.2.0-pre-alpha` (+ an `Unreleased` stack in `CHANGELOG.md`).
- **Works today:** triple boot (PVH / Multiboot2 / Limine BIOS+UEFI),
  PMM bitmap+buddy, VMM, VFS (tmpfs/devfs/initramfs), a Linux-numbered
  syscall ABI with NEWOS extensions,
  `nsh` + native `/bin` toolbox (including recursive `find`), framebuffer
  (`img`, `vid`, `desktop`),
  native `.new` packages with rollback, reproducible ISOs, MIT.
- **Honest gaps:** no disk driver (RAM-only FS), no IP stack
  (PCnet loopback only), no `truncate/mmap/sbrk/signals`,
  no USB/AHCI/NVMe/audio, no APIC/ACPI/SMP, JPEG/PNG refused
  (no userspace heap+FPU yet), package repos need net+persistence.
  See `docs/virtualbox-and-hardware.md`, `docs/packages.md`,
  `docs/new-format.md` for the recorded details.

## Ground rules

1. **Layering is law.** Every change respects `docs/development/layering.md`
   and passes `make lint-layers`. No exceptions for "quick hacks".
2. **No fake features.** If it is not wired and tested, it is documented
   as future work, not shipped (packages v1 set the pattern).
3. **Tests travel with code.** New subsystems land with phase-1/phase-2
   self-tests or host tests (`make check-newpkg` style).
4. **Reproducible builds.** New artifacts must be byte-identical across
   clean rebuilds (`scripts/iso/fixup_iso.py` pattern).
5. **One milestone at a time.** Finish, verify (`BOOT=PASS` on
   SeaBIOS+OVMF+PVH), changelog, then move on.

## Milestones

### v0.3 — Persistence ("survive reboot")

Goal: the filesystem lives on real storage.

- Block layer (`drivers/storage/`): request queue, partitioning basics.
- **AHCI driver** (SATA, PCI-enumerated) before NVMe: simpler, QEMU-testable
  (`-drive if=none,id=d0,file=...,format=raw -device ich9-ahci …`).
- On-disk filesystem: a small native FS (preferred, spec it in
  `docs/newfs.md`) or read-only ext2 as bootstrap — decide in an RFC issue.
- `SYS_RENAME` exists for in-memory filesystems; persistent storage still
  needs durable metadata and crash-safe transactions before package updates
  can be considered atomic.
- `mount/umount` for the disk FS next to tmpfs/devfs; `/var/lib/newpkg`
  becomes persistent.
- Acceptance: install `.new`, reboot, package still installed;
  `make qemu-disk` runs the suite from disk.

### v0.4 — Memory & processes ("grow up")

Goal: real userspace memory management.

- `SYS_MMAP/MUNMAP`, `SYS_BRK/SBRK` (per-address-space VMM already exists
  in `mm/` — expose it safely with `copy_*_user`-grade validation).
- Userspace heap + FPU context (`-mgeneral-regs-only` is lifted for
  userland first, kernel keeps it): unblocks vendored `stb_image`
  so `img` opens **JPEG/PNG**, and a real `vid` codec path.
- Basic signals (`SYS_KILL` exists; add delivery + `signal()` semantics).
- Live `/proc` (ps/uptime/free read it instead of raw syscalls).
- Acceptance: `img photo.jpg` works; `ltest` heap stress passes;
  no regressions in phase-2 `usercopy`/`address-spaces` tests.

### v0.5 — Network ("talk to the world")

Goal: a minimal, honest IP stack over the existing PCnet driver.

- Full PCnet TX/RX interrupts (today: loopback only), then ARP → IP →
  ICMP → UDP → minimal TCP.
- Socket syscalls (`SYS_SOCKET/BIND/CONNECT/SEND/RECV`… new numbers
  from 22 up, documented in `abi/syscall_abi.h` + `docs/abi.md`).
- DHCP client + DNS stub resolver in userland.
- `make qemu-network` runs a network self-test (ping + UDP echo
  against host user-mode networking).
- Acceptance: fetch a file over the virtual network; loopback tests stay green.
- Explicitly **not** in v0.5: Wi-Fi, TLS (userspace later), virtio-net
  (nice-to-have after PCnet is solid).

### v0.6 — Packages v2 ("a real distro feel")

Needs v0.3 (atomic rename) + v0.5 (fetch). Goal: repositories.

- Spec v2 (`docs/new-format.md` § Future extensions): compression
  (flag bit0), signature blocks, per the reserved fields.
- `repo/index` format + `newpkg update/search/install <name>`.
- Signature verification against vendored keys; CRC32 stays as
  corruption check, signatures as trust.
- Atomic transactions via `SYS_RENAME`; dep solver beyond DFS-16
  only if proven necessary.
- Acceptance: `newpkg update && newpkg install <name>` from a local
  repo dir, then over QEMU user-mode networking.

### v0.7 — Hardware ("bare metal manners")

Goal: behave on real machines and use all their CPUs.

- ACPI table parsing (MADT) + APIC/IOAPIC bring-up; keep 8259/PIC
  path until APIC is proven on SeaBIOS+OVMF+VB.
- **SMP**: AP bring-up, per-CPU TSS/stacks, scheduler with runqueues
  (`CONFIG_SMP` exists as `n` — flip it here, not before).
- USB: UHCI companion first (QEMU `-usb`), keyboard/mass-storage;
  xHCI only after UHCI ships.
- Intel HDA stub (enumerate + beep) — full audio is post-1.0.
- Acceptance: `sysinfo` reports N CPUs; SMP suite passes;
  USB keyboard works in QEMU and on one real laptop (documented model).

### v0.8 — Ports ("beyond x86_64")

Goal: second architecture booting to shell.

- ARM64 first (`configs/arm64/debug.config` + cross prefix already
  reserved): new `arch/arm64/` backend (boot, paging, GDT→EL model,
  IDT→GIC, PL011 serial), reuse L0/L1/L4–L10 untouched.
- RISC-V 64 next, same pattern.
- `toolchain/build.sh` gains `aarch64-elf`/`riscv64-elf` targets.
- Acceptance: `make ARCH=arm64 qemu-test` → exit 1 (PASS) under
  `qemu-system-aarch64 -M virt`.

ARM64 is a planned port, not a supported build target yet. The current
implementation gaps and staged bring-up gates are tracked in
`docs/porting-roadmap.md`.
- Explicitly **not** in v0.8: dropping x86_64 as primary; x86_64
  stays the reference until a port passes the full matrix.

### v1.0 — "Self-sustaining"

Release criteria (all must hold):

- Install to disk from ISO, reboot, system + packages persist.
- Network fetch + signed repo install out of the box.
- SMP + APIC on real hardware (documented board list).
- Hosted or cross-built ports: x86_64 reference + one port green.
- Full docs set, man-style `help` for every `/bin` tool,
  release ISOs + SHA256 published on GitHub Releases.
- `CHANGELOG.md` 1.0.0 entry, version bump, release tag.

## Good first issues (jump in)

Ordered roughly by size. All must include tests + docs.

1. `help` text for every `/bin` tool (uniform `help <cmd>` output).
2. More `nsh` builtins parity + quoting edge cases (`user/programs/init/`).
3. `devfs` entries: `/dev/null`, `/dev/zero` as real nodes.
4. `df`/`free` reading a live `/proc` (after v0.4 starts it).
5. PCnet RX interrupt path (stepping stone to v0.5).
6. `stb_image` vendor spike behind a build flag (needs v0.4 heap first).
7. QEMU screendump gallery automation (`scripts/` + `web/assets/`).

## Non-goals

- **Linux/POSIX compatibility.** No ELF-Linux loader, no Linux syscall
  numbers, no `/proc`-compatible userspace ABI promises beyond our docs.
- **Porting X/Wayland/systemd.** The native framebuffer + `desktop`
  compositor is the graphics story.
- **SMP before APIC**, **repos before signatures design**, **ports before
  storage**: the milestone order is load-bearing, not decorative.
- **Enterprise filesystems** (ZFS/btrfs): native small FS or ext2-ro,
  nothing bigger, until v1.0 ships.

## Process

- Propose anything bigger than a fix as a GitHub issue (RFC) first.
- Land code + tests + docs together; CI (`build`, `qemu-test`,
  `static-analysis`) is the gate.
- When a milestone completes: bump `VERSION` if user-visible,
  write the `CHANGELOG.md` entry, refresh this file and `web/`.
