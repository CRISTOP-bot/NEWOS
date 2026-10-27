# NEWOS Roadmap

Where NEWOS is going, in what order, and what is explicitly **not** planned.
This is a living document: completed items move to `CHANGELOG.md`,
and every milestone must keep `BUILD=PASS`, `BOOT=PASS`, `QEMU=PASS`
(20/20 self-tests green) or it does not ship.

## Where we are

- **Version:** `0.3.0-gui` (+ an `Unreleased` stack in `CHANGELOG.md`).
- **Works today:** triple boot (PVH / Multiboot2 / Limine BIOS+UEFI),
  PMM bitmap+buddy, VMM, VFS (tmpfs/devfs/initramfs plus read-only
  procfs/sysfs), a Linux-numbered syscall ABI with NEWOS extensions,
  `nsh` + native `/bin` toolbox (including recursive `find`) and the complete
  static BusyBox 1.35.0 (402 applets) through the Linux-compatible `syscall`
  entry, anonymous `mmap`/`mprotect`/`munmap`/`brk` for static Linux binaries,
  framebuffer (`img`, `vid`, `desktop`, `glxgears`), opt-in graphical
  session (`gui=1` menu entry, keyboard/fbcon ownership, return to shell on
  exit), native `.new` packages with rollback, a normalized reproducible
  GRUB ISO, MIT.
  `/proc` exposes live identity/uptime/memory/mounts/cmdline, `/sys`
  exposes kernel identity and the PCI inventory; `free`, `df` and `uptime`
  consume `/proc` with a syscall fallback.
  The built-in desktop is a modular Nova NTK alpha: six windows (including the
  Nova preview), dock, start menu, help, BoredOS/Win95 themes, a Win95 icon
  family and a damage-based compositor that uploads only dirty rectangles.
  The upstream Nova compositor, protocol and app suite remain future work.
- **Honest gaps:** no disk driver (RAM-only FS), no IP stack
  (PCnet loopback only), no file-backed `mmap`, signal delivery or native
  `sbrk` allocator (`truncate` is path/fd-based for writable backends), no
  USB/AHCI/NVMe,
  no APIC/ACPI/SMP, JPEG/PNG refused (no image decoder yet; glxgears does use
  the FPU and a local math shim), package repos need net+persistence. The
  Limine ISO embeds build timestamps and is not byte-reproducible yet; the
  GRUB ISO and ELF outputs are normalized.
  See `docs/virtualbox-and-hardware.md`, `docs/packages.md`,
  `docs/new-format.md` for the recorded details.

## Ground rules

1. **Layering is law.** Every change respects `docs/development/layering.md`
   and passes `make lint-layers`. No exceptions for "quick hacks".
2. **No fake features.** If it is not wired and tested, it is documented
   as future work, not shipped (packages v1 set the pattern).
3. **Tests travel with code.** New subsystems land with phase-1/phase-2
   self-tests or host tests (`make check-newpkg` style).
4. **Reproducible artifacts.** ELF outputs and the GRUB ISO normalize
   timestamps/GUIDs (`scripts/iso/fixup_iso.py`). New artifacts should follow;
   the Limine ISO timestamp gap is tracked above.
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

- Native libc heap on the existing `SYS_MMAP`/`SYS_BRK` support (currently
  static arena; file-backed mappings are still unsupported).
- Userland FPU context (`-mgeneral-regs-only` is lifted for userland first,
  kernel keeps it): unblocks a vendored `stb_image` so `img` opens
  **JPEG/PNG**, and a real `vid` codec path. `glxgears` already uses the FPU
  with a local math shim.
- Basic signals (`SYS_KILL` exists; add delivery + `signal()` semantics).
- Extend the read-only `/proc` that already serves version/uptime/meminfo/
  mounts/cmdline: add a process provider for `ps` (the FS layer cannot
  reach `process/` directly), per-process files and live socket stats.
  `free`/`df`/`uptime` already read `/proc` with a syscall fallback.
- Acceptance: `img photo.jpg` works; `ltest` heap stress passes;
  no regressions in phase-2 `usercopy`/`address-spaces` tests.

### v0.5 — Network ("talk to the world")

Goal: a minimal, honest IP stack over the existing PCnet driver.

- Full PCnet TX/RX interrupts (today: loopback only), then ARP → IP →
  ICMP → UDP → minimal TCP.
- Socket syscalls and a matching network stack (`SYS_SOCKET/BIND/CONNECT/
  SEND/RECV`, vendor-numbered and documented in `abi/syscall_abi.h` +
  `docs/abi.md`) once frames can actually leave the machine.
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
- Intel HDA poll-mode playback: **done** (CORB/RIRB with the
  `GET_SUBSYSTEM_ID` echo, codec converter verbs, SD slot 4 BDL play,
  LPIB poll; A4 sine ktest with host-side WAV validation — see
  CHANGELOG). Remaining: IRQ mode + capture later; full audio stack
  post-1.0.
- e1000 NIC variant beside PCnet (same stack, same ktest pattern).
- Acceptance: `sysinfo` reports N CPUs; SMP suite passes;
  USB keyboard works in QEMU and on one real laptop (documented model).

### Graphics roadmap (honest versions, no fake drivers)

The graphics story advances by *hardware-model milestones*, and every
one lands with a working ktest (screendump comparison or byte checks)
before the next starts. Current state: the Limine-provided linear
framebuffer is the only display mode; `fbcon-gui` and the `desktop`
compositor live on top of it.

1. **BGA / VBE dispi (done first)** — the Bochs Graphics Adapter
   (`-device bochs-display` in QEMU; the same dispi register interface
   VirtualBox exposes through its legacy graphics adapter). PCI
   class-device enumeration, mode set via the 0x1ce/0x1cf
   index/data register pairs (display width, bpp, enable bits), linear
   framebuffer mapping through the PCI BAR. This is the REAL
   display path on two of our three platforms and replaces the
   Limine-copied fb for everything booted on Bochs/VirtualBox.
2. **PCI class driver layer** — VGA-class probing (`pci.c` inventory
   already prints them) extended to a small registry: BGA, std VGA,
   later VirtIO-GPU. Each backend exposes `fb_info {addr, w, h, pitch,
   bpp}`; fbcon/desktop pick the backend by probe order.
3. **VirtIO-GPU 2D (when the virtio layer lands)** — device model QEMU
   ships honestly; needs the shared virtio ring plumbing we do not have
   yet, so it waits for the virtio spike, NOT a shortcut hack.
4. **Intel i915 / AMD ATOMBIOS family — display-only first.** The
   KMS-style display bring-up (pipe programming) is gated on the v0.7
   ACPI+APIC/MSI work; below that base the drivers cannot honestly run
   outside of QEMU anyway (neither QEMU nor VirtualBox emulates them, so
   every line would be untestable fake). Phase A: display configuration
   only (scanout + linear fb), mode set from the display-controller
   tables. Phase B (3D rings, shaders, GEM, Mesa-stack integration) is
   post-1.0 and only after HW partners confirm register paths; no fake
   stubs, ever.

What we will NOT do: claim "GPU driver" before scanout pixels come from
the actual device under test.

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
