# Changelog

All notable changes to NEWOS are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- `nano` is now color-capable on the framebuffer console. The port falls back
  to `TERM=linux` when the shell passes no environment, and fbcon implements
  the ANSI/VT100 subset ncurses emits: SGR 16/256-color foreground and
  background, bold, reverse, cursor movement/positioning, erase display/line,
  insert/delete line and character, save/restore cursor and DECTCEM. The old
  ghost cursor is replaced by a real reverse-video block cursor, and C0
  controls such as SI are dropped instead of being painted as `?`. initramfs
  installs `/usr/etc/nanorc` (autoindent, line numbers and C/C++/shell/
  Makefile/nanorc highlighting) from `user/rootfs/nanorc`.
- VOXEL (`ports/voxel`, MIT) is vendored and shipped as `/bin/voxel`: the
  software surface/compositor stack plus its classic-desktop demo (taskbar,
  Start menu, File Explorer, Editor, Terminal, Settings and more), built from
  source with the host compiler against the NEWOS sysroot
  (`scripts/build-voxel.sh`) and embedded by `initramfs`. It uses the public
  framebuffer/mouse syscalls only; `--frames N` renders N frames, prints a
  completion marker and exits, which is what the QEMU check runs. One
  `patches/0001` diff records the fixes needed for current GCC under
  `-Werror` (unused variables, a stale identifier, a hook signature and
  `-Wmisleading-indentation`).
- Legacy-PIO ATA driver (`kernel/drivers/block/ata.c`) exposing the
  primary-master disk as the `disk0` block device: polling-only command
  completion with nIEN set (no IRQ14 path, no DMA, no PCI programming),
  IDENTIFY-DEVICE geometry (LBA28, with LBA48 when the drive advertises
  it), and bounds-checked sector transfers with FAT writes mirrored
  across every FAT copy. `/dev/disk0` joins devfs when a plain ATA disk
  is attached (`-drive if=ide` in QEMU; ATAPI signatures are skipped,
  not claimed). Partial head/tail sector traffic on the device node
  uses a read-modify-write scratch sector, so userland sees a
  byte-addressable, seekable stream.
- FAT32 read-write filesystem under `kernel/fs/fat32/`, mounted as `/fat`
  from `disk0` when the BPB parses (FAT16 refused). Directory children
  materialize as real VFS inodes (lookup/readdir stay in the core);
  `vfs_create`/`vfs_mkdir` gained `inode_ops` hooks for disk-backed
  parents and `ops->destroy` performs the on-disk delete (0xE5 dirent,
  freed cluster chain) on unlink. Semantics and honest limits (8.3
  namespace, LFN entries skipped on listing, overwrite-or-append-only
  writes, no rename/no FSInfo) are documented in `docs/FILESYSTEM.md`.
- Poll-mode IPv4 milestone 1: PCnet-PCI II driver
  (`kernel/drivers/net/pcnet.c`, QEMU `-netdev user -device pcnet`) and
  the kernel net stack (`kernel/net/`) under the new `net/` layering
  tier — ARP cache + request/reply, IPv4 TX/RX with checksum
  validation, ICMP echo with a single waiter slot, 60-byte TX padding
  and a strict in-order TX cursor (documented in `docs/NETWORK.md`
  with the PCnet gotchas). The `net-ping` ktest ARP-resolves the
  slirp gateway (10.0.2.2) and round-trips an ICMP payload.
- ISO 9660 read-only filesystem (`kernel/fs/iso9660/iso9660.c`) mounted
  as `/iso` from `disk0`: PVD at LBA 16 only, 2048-byte blocks,
  `;version` suffix stripped, `.`/`..`/hidden skipped, multi-extent
  files joined (≤8 extents, name-mismatch guarded), depth ≤8. No
  Joliet/Rock Ridge/El Torito, by declaration.
- romfs v1 read-only filesystem (`kernel/fs/romfs/romfs.c`) mounted as
  `/rom`: superblock checksum verified (additive 32-bit over the first
  512 bytes), inode chains materialized with the format's
  addr/type-nibble encoding, data offsets derived from
  `header + padded name`, hard links honored, symlinks/devices skipped,
  bounded against corrupt images (per-dir 4096-entry and depth-16
  caps).
- `scripts/fs-tools/mkfs.romfs.py`: romfs 1.0 image builder (genromfs
  style 16-byte alignment of headers AND file data) with hardlink and
  symlink entry support and a built-in round-trip self-check that
  re-parses the produced image byte-for-byte before writing it.
- VFS read-only mount rule: `vfs_mkdir`/`vfs_create` now refuse on any
  foreign directory that carries no create/mkdir hook (romfs, iso9660,
  procfs mounts) instead of materializing an unacknowledged in-memory
  child; unlink keeps refusing by the parent-hooks rule.
- ELF64 loader profile hardening in `kernel/process/elf_loader.c`: dynamic
  objects (`ET_DYN`, PIE/shared) and `PT_INTERP`-bearing images are now
  rejected outright with an explicit console reason instead of a silent
  generic failure — NEWOS has no dynamic loader, so they could only fault
  later inside the process. `PT_TLS` is detected and carries a visible
  warning (loader ignores it; static musl binaries set their own FS base
  via `SYS_ARCH_PRCTL`, which is how `busybox` runs). `ET_DYN`,
  `PT_INTERP` and `PT_TLS` constants added to `include/process/proc_elf.h`.
  The accepted-image policy (static `ET_EXEC` x86_64, bounds-checked
  segments, FS-based TLS, `SYS_EXECVE` still a stub) is documented as an
  "ELF64 loader profile" section in `docs/abi.md`.
- Top-level `sysroot/` install tree for the C SDK: `make sdk` now populates
  `sysroot/usr/{include,lib}` in-tree (libc.a, libnsh.a, crt0.o, app.ld and
  the `newos/` ABI headers) instead of `build/newos-sdk`, with a documented
  `README.md` and a generated-payload `.gitignore`. `toolchain/newos-gcc`
  consumes the sysroot directly — headers, libc/libnsh link path, `crt0.o`
  and the `app.ld` link script all resolve from it, so cross builds are
  self-contained.
- Windows 95-style taskbar replacing the centered bottom dock: full-width
  bar with a pressed-state START button (NEWOS flag + bold label), one
  beveled task button per open window (active = sunken/inverted colors,
  minimized included), and a sunken clock tray on the right that shows the
  date in a toast when clicked. The start menu now hangs directly above the
  START button; the top panel, window layout (workspace now extends to the
  bar) and drag/resize clamps follow the new geometry. Hit-tested from per
  identity `taskbar_button_rect` so draw and input can never drift.
- `/bin/frotz`, a native port of the Frotz 2.44 Z-machine interpreter
  (GPL-2+, upstream DavidGriffith/frotz; dumb terminal driver). It plays
  Infocom-style `.z3`–`.z8` story files from the console: Quetzal
  save/restore, status line, runtime settings and stable-random without
  FP registers. A deterministic demo story ships at
  `/usr/share/zcode/mini.z3`; `make check-frotz` drives the same story on
  the host through the ported units (`tools/frotz/`). Userland Z-machine
  core compiles clean under `-Wall -Wextra -Werror`; deltas are documented
  in `userland/user/programs/frotz/README` and `docs/frotz.md`.
- Native x86-64 Linux `syscall` entry (`LSTAR`/`STAR`/`FMASK`, TSS RSP0 mirror)
  with `readv`/`writev` and the Linux syscall-number subset. This runs the
  official static BusyBox 1.35.0 x86_64-linux-musl binary at `/bin/busybox`
  (402 applets); `nsh` falls back to `/bin/busybox <applet>` for unshadowed
  names because the VFS has no symlinks. Applets that need Linux kernel
  interfaces NEWOS still lacks are documented in `ports/busybox/README.md`.
  Provenance, GPLv2 text and SHA-256 checksums live in the same directory.
- GNU nano 8.4 at `/bin/nano`, a static x86_64-linux-musl `ET_EXEC` linked
  against a static ncursesw 6.5 with `vt220`/`xterm`/`linux` terminfo
  fallbacks compiled in, and the `doc/`+`po/` trees dropped from the build.
  Full-screen editing over the serial/VGA console works (opening a file
  reads it through the VFS and `Ctrl+X` exits); external commands and
  syntax highlighting stay out of scope (`ports/nano/README.md`).
- TinyCC 0.9.27 at `/bin/tcc`, the on-system C compiler: static
  x86_64-linux-musl `ET_EXEC`, upstream source with only the SDK search
  paths patched (`ports/tcc/patches/`). `tcc -c` and `tcc -nostdlib -run`
  work out of the box; full links use the C SDK package
  (`newpkg install /tmp/newos-sdk-*.new`) with `crt0.o`, `libnsh.a`,
  `libc.a` and tcc's `libtcc1.a`. `/etc/tcc-hello.c` is the freestanding
  demo.
- A built-in `/bin/glxgears` TinyGL demo, compiled with the host/newos
  toolchain and embedded into every ISO by both `make` and `make fast`. It
  clears the panel, renders a 300-frame 640x480 gear scene, exposes
  `--frames N`, and keeps kernel text/legacy cursor output off the panel.
  Boot-verified on QEMU x86_64 TCG: real frames, visible gears animation in
  framebuffer screendumps, clean console release back to `nsh`
  (`ports/glxgears-newos/README.md`).
- `SYS_CONSOLE_GUI` (462): a foreground compositor can suspend fbcon and the
  kernel mouse arrow while it owns the panel. Text apps launched from the
  desktop release the claim; graphical apps keep it; process teardown releases
  it automatically. Phase-1 test `fbcon-gui` covers ownership and crash release.
- The desktop is now a modular five-window session instead of one translation
  unit: `desktop_main`, `state`, `gfx`, `surface`, `widgets`, `wm`, `theme`,
  `shell`, `apps`, `input` and a damage-based `compositor`. Cursor motion,
  hover, clock/memory updates and toast expiry repaint only dirty rectangles;
  `SYS_FBWRITE` receives 16-row chunks instead of a full frame. Startup, theme
  and structural changes are the only full redraws.
- NEWOS-owned desktop drawing layer: `desktop_surface` (clipped XRGB canvas
  with alpha blending, rounded rectangles, circles and lines) and
  `desktop_widgets` (caller-owned button, progress, scrollbar and separator).
  Both are integer-only, allocation-free and linked into `desktop`, replacing
  the borrowed widget toolkit and its separate host-test target.
- Win95 visual language: a four-pane waving flag logo, a Win95-beveled
  icon family for the desktop and every `/bin` app, navy/indigo wallpaper
  shards, `Midnight`/`Win95`/`Periwinkle` themes, a 1920x1080 Limine wallpaper
  with `interface_branding`, a 2-second branded boot menu at 1280x800, and a
  regenerated 640x400 splash. The website favicon, header mark, app-icon copies
  and 1024px brand image use the same generated assets.
- Opt-in graphical boot session: the default Limine entry is a text shell;
  the separate **NEWOS (GUI)** entry passes `gui=1`, and
  `make qemu-limine-gui` applies `gui=1` to the default entry for a direct
  visual check. The shell then opens `/bin/desktop`, and closing the last
  window or pressing `Ctrl+Q` returns to a clean `nsh` prompt. A missing
  framebuffer or a failed desktop spawn falls back to the shell instead of
  stranding the machine.
- Desktop keyboard input through the new `SYS_KBD_GRAB` (460) and
  `SYS_KBD_READ` (461) ABI: a foreground session owns the PS/2 keyboard
  without stealing serial input, while the kernel releases the grab on
  process teardown. The desktop uses it for `Tab`/`1`–`5` focus, arrow-key
  `/bin` selection, `Enter` launch and a `?` shortcut card; spawned text
  programs get the keyboard back for their lifetime.
- The first user process now receives its `argc`/`argv` correctly:
  `x64_enter_user` loads the SysV entry registers from the prepared iframe
  instead of leaving leftover kernel values, which the boot session flag
  depends on. Covered by the `kbd-grab` phase-1 test; `fbcon-gui` brings the
  phase-1 suite to 11 tests and the total to 20.
- Read-only `procfs` mounted at `/proc`: `version`, `uptime`, `meminfo`
  (live PMM counters), `filesystems`, `mounts` and `cmdline`, with
  offset-aware reads and a phase-1 kernel test.
- Read-only `sysfs` mounted at `/sys`: kernel `ostype`/`osrelease`/`version`
  plus `bus/pci/devices/<bdf>/{vendor,device,class}` built from the device
  model, with a phase-1 kernel test.
- `free`, `df` and `uptime` now read `/proc` (with the existing syscall
  path as fallback), and `nshlib` gained `/proc` parsing helpers.
- The VFS now keeps synthetic trees read-only: unlink/rename only operate
  on tmpfs files and VFS core directories, so `/proc` and `/sys` cannot be
  torn down by `rm` or `mv`.
- `qemu-shell.py` tolerates nsh's ANSI prompt redraw and waits for the
  newline that submits a command, eliminating flaky/lost shell commands.
- The boot command line is copied into kernel-owned storage instead of
  retaining a pointer into loader memory that the PMM may reuse.
- Native `/bin/find` command with recursive directory traversal, basename
  wildcard matching, file/directory filters and depth bounds; `/bin` and
  shell help now expose it. The userland library also exports `sys_stat()`.
- Syscall ABI `SYS_OPEN` (7) and `SYS_CLOSE` (8) (`abi/syscall_abi.h`):
  per-process fd allocation backed by the VFS, with user-pointer validation
  and `O_CREATE` support; `SYS_READ` now serves any open fd through the VFS
  while stdin keeps its blocking console semantics. Covered by the new
  phase-2 `open-close` test (open/read/close + `O_CREATE` write/read-back
  round trip + error cases, all through the real dispatcher).
- GCC 16 (C23) build fix: `bool`/`true`/`false` in `core_types.h` now defer
  to the compiler builtins instead of redefining the keywords.
- Multi-run RAM registration (`pmm_allocator_init_ranges()`): boot parsers
  now hand every RAM E820 region (clamped to the direct-map window) to the
  PMM instead of one collapsed interval, so no MMIO hole is ever handed out
  as free memory and all free frames stay within the identity map.
- VGA text-mode console (`drivers/console/vga_text_console.c`): the kernel
  mirrors every `printk` line to the 80x25 screen (`0xB8000`) in addition to
  serial, so a VGA-only machine (e.g. VirtualBox default BIOS) displays the
  boot log.
- Robust GRUB config for the ISO: both the console (VGA) and the serial
  terminal are enabled; a missing serial port falls back to the screen.
- Reproducible ISO: `make iso` pins every time-derived byte (volume
  timestamps, the xorriso `.uuid` marker, the `efi.img` FAT volume serial)
  via `scripts/iso/fixup_iso.py`, so two clean ISO builds are byte-identical.
- Usage documentation: `docs/virtualbox-and-hardware.md` (VirtualBox legacy
  BIOS + real hardware + validation matrix).
- Native `.new` package system (`docs/packages.md`, `docs/new-format.md`,
  `docs/newpkg.md`): deterministic `NEW1` container (header + `key: value`
  metadata + sorted manifest + raw payload, CRC32 throughout), userspace
  `/bin/newpkg` (`info/verify/install/remove/list/files`, dependency
  checks with cycle detection, conflict scan, install rollback), host
  builder (`tools/newpkg/newpkg-build.py`, `packages/*.newspec`), first
  real package `hello-new-1.0.0-x86_64.new` (staged at
  `/tmp/hello-new-1.0.0-x86_64.new`, installs runnable `/bin/hello-new`),
  and `make check-newpkg` (C unit driver + end-to-end suite driving the
  shipped installer code).
- Brand identity (`brand/`) now feeds every asset the build consumes:
  `/etc/splash.bmp` installs from `brand/newos-splash.bmp` instead of a
  generated gradient placeholder (`scripts/gen-images.py` is reduced to the
  PPM decoder test image), the website serves the brand mark as favicon,
  header and footer logo (mono variant, so it follows the site theme), and
  `README.md` opens with `brand/newos.svg`. The per-tool icons under
  `brand/app-icons/` and `brand/newos-1024.png` are consumed by the website.
- Self-healing pull requests (`.github/workflows/pr-self-heal.yml`,
  `scripts/ci/self-heal.sh`, `scripts/ci/fix_whitespace.py`): failing CI runs
  are repaired deterministically (whitespace and base-branch integration) and
  otherwise handed to the free CodeRabbit OSS GitHub App
  (`@coderabbitai fix-ci commit`); conflicting branches are merged and
  generated-asset conflicts are resolved automatically, with source conflicts
  handed to `@coderabbitai resolve merge conflict`. Repairs are pushed with
  `GITHUB_TOKEN`, CI is dispatched explicitly, and `auto-merge.yml` then
  merges the green result. No external API key is required; see
  `docs/self-healing.md` and `.coderabbit.yaml`.
- Generic device-MMIO mapping window (`kernel/arch/x86_64/memory/
  x86_devmap.c`): PCI BARs map as uncached (PCD|PWT) 2 MiB pages (never
  1 GiB, per the VirtualBox PDPE1GB lesson) into a private kernel-half
  window at PML4[506] next to the framebuffer's [507], wired with the
  fb.c table discipline. `devmap_map(phys, bytes)` is probe-time,
  never freed, and took the HDA BAR0 without touching fb.c.
- Intel HD-Audio poll-mode playback (`kernel/drivers/audio/hda.c`,
  `include/drivers/hda.h`): PCI probe (8086:2668/293e), spec-1.0
  controller power-up with codec link presence via `STATESTS`,
  CORB/RIRB rings inside one 4 KiB frame (CORB 1024 + RIRB 2048 + BDL,
  all 128-byte aligned), link proven by the `GET_SUBSYSTEM_ID` echo
  (QEMU answers `0x1af40022`), then one-shot playback: codec converter
  format + stream-tag verbs, SD slot 4 (first output) BDL programming,
  run, `LPIB` poll to completion, stop. No interrupts, no DMA position
  buffer, no capture — documented. The phase-2 `hda` ktest composes a
  0.25 s 440 Hz A4 sine integer-only (65-entry quarter-wave table,
  32-bit phase accumulator) in one contiguous physical block and
  delivers it at 48 kHz/S16/stereo; `make qemu-test` attaches
  `intel-hda` + `hda-duplex` (recorder backend probed `file` vs `wav`
  across QEMU builds — its property is `path`, and the recorder
  ignores the codec side's rate, so it is pinned to 48 kHz stereo S16)
  and `run_qemu_test.sh` re-validates the produced WAV on the host
  (RIFF container, 48 kHz/S16/2ch, ≥50% nonzero samples, peak window,
  both polarities). Phase-2 suite is now 15 tests (26 total).

- Thin NIC registry (`kernel/include/drivers/nic.h`, `kernel/net/nic.c`):
  every probe()d Ethernet adapter hands the net stack one `struct net_nic`
  (open/get_mac/send/poll); the first registered is the boot default and
  `net_nic_select()` lets ktests switch adapters (ARP cache flushes per
  switch — ARP/ICMP answers travel the slirp network of the selected card).
  `net_start()`/`net_start_nic()` replace the pcnet-specific boot bring-up in
  `kernel/net/net_stack.c`, which now talks to whichever NIC is active.
- Intel e1000 driver (`kernel/drivers/net/e1000.c`, `include/drivers/e1000.h`):
  Intel 82540EM (8086:100e) polled MMIO playback over the devmap window;
  TX legacy descriptors with TDT-write ring drain and RS write-back, RX
  descriptors pre-armed with 2 KiB buffers consumed on DD/EOP + RDT tail,
  MAC read from RAL/RAH (AV set by the emulated device at reset), no MSI,
  static 10.0.2.15/24 over slirp. Phase-2 `net-e1000` ktest pings the
  slirp gateway and byte-compares the echo ("NEWOS e1000 ping"); the stock
  `net-ping` ktest keeps passing through PCnet. `make qemu-test` adds the
  second NIC (`-netdev user,id=net1 -device e1000,netdev=net1`). Phase-2
  suite is now 16 tests (27 total).
### Fixed
- `nsh` append redirection (`cmd >> file`) opened the target with `O_TRUNC`,
  so the file was emptied before the seek-to-end, which then landed at
  offset 0 and made the appended text replace the whole file. It now opens
  with `O_APPEND`; `>` still truncates.
- Static links made by the on-board `tcc` faulted on the first libc call
  (`USER PAGE FAULT` at a bogus RIP such as `0x2825ffffff`). On x86-64 every
  call is an `R_X86_64_PLT32` relocation, so tcc emitted a PLT stub and a
  GOT slot, but a `-static` binary has no dynamic loader to fill that slot
  and `relocate_plt()` only runs for dynamic output; the stub therefore
  jumped through unrelocated PLT bytes. Static links now resolve `PLT32`
  directly as `PC32` (`ports/tcc/patches/0002`). Undefined functions now
  fail the final relocation instead of producing a binary that crashes.
- The tcc port's SDK search-path patch ran `tcc_add_sysinclude_path()` /
  `tcc_add_library_path()` before `tccpp_new()`, which initialises the
  CString allocator those helpers use. A binary rebuilt from `ports/tcc/src`
  therefore segfaulted in `tal_realloc_impl()` before even printing `-v`.
  The calls now run after `tccpp_new()`, and only `/usr/lib` is added
  (`/usr/include` already comes from `CONFIG_TCC_SYSINCLUDEPATHS`).
- Scheduler handoff resumed a TLS thread with the previous thread's FS base:
  the epilogue's ring check examined the frame being *left* (ring 0 for a
  blocking syscall) instead of the frame being *resumed*, so the
  `MSR_FS_BASE` write was skipped and `nano`/`tcc` faulted on the next
  `%fs:0` errno access (`USER PAGE FAULT at 0x0`). The handoff path now
  always installs `g_sched_next_fsbase`; only the no-handoff path tests the
  ring.
- `read()` on a regular file was capped at the kernel's 512-byte bounce
  buffer, returning a short count where Linux returns the whole request.
  tcc's archive loader does a single unchecked `read()` for the `ar` symbol
  table and fails silently in that case, so linking `/usr/lib/libnsh.a`
  exited 1 with no message and no output file. `SYS_READ` now loops over
  the bounce buffer (pipes and the serial console keep their short-read
  semantics).
- Cursor ghosting had two independent causes: the kernel kept painting its
  own `fb_mouse_place` arrow and restoring old text cells while the compositor
  drew a second pointer, and the compositor could leave its previous sprite in
  the canvas. The cursor is now a compositor staging-buffer overlay (never part
  of `canvas`), every old/new cursor rectangle is uploaded first, and fbcon's
  kernel arrow is suppressed while the GUI owns the panel. QEMU interactive
  targets use `grab-on-hover=on,show-cursor=off` so the host pointer cannot add
  a third arrow.
- The Mouse window showed a stale position whenever the pointer moved outside
  its body, because only the old/new pointer rectangles were damaged. Pointer
  motion and button/wheel changes now damage the Mouse window body too.
- `glxgears` used to paint its banner and leave kernel `proc:` lines over the
  desktop, and its letterbox area showed the previous desktop. It now claims
  the framebuffer console while running and clears the full panel to black
  before rendering the 640x480 viewport; desktop launches restore the desktop
  through the normal damage pass.
- The desktop launched `img` with only a file path, so the viewer waited for a
  key on an invisible fbcon prompt while the compositor owned the panel. `img`
  now accepts `--no-wait`; the desktop passes it, while shell invocations still
  wait for a key as before.
- `newfetch` now clears the panel before drawing. The kernel logs every spawn
  before user code runs, and that line clipped the top of the fixed 14-row logo.
- `/etc/nsh.rc` reinstalled GNU coreutils on every shell, making each new
  terminal rewrite 71 files. `newpkg install` now reports `already installed`
  for an identical installed version and skips the payload; `--force` still
  reinstalls or repairs files.
- `/etc/version` still contained a hard-coded `0.2.0-pre-alpha` banner even
  though `VERSION` and every other release string had moved on. It now uses
  `NEWOS_VERSION`, so the image cannot report two different versions.
- `desktop` microfont glyph rows were read least-significant-bit first while
  the embedded glyphs are stored most-significant-bit first, mirroring text.
  The corrected table has unique character mappings and a `?` fallback;
  desktop labels now render at readable scale. Window painting is clipped to
  each frame, layout uses the reported framebuffer dimensions, cursor rendering
  avoids trails, and the taskbar reserves space for its live clock.
- Desktop click routing now honors the topmost window before its controls,
  preventing covered windows from receiving close/minimize actions. Files
  entries remain sourced from `/bin`, with visible-row selection and wheel
  scrolling; live memory changes trigger refreshes.
- Memory-map parsers previously collapsed all RAM between the lowest and
  highest map entries, feeding MMIO holes to the physical allocator. The
  PMM now receives strictly the RAM runs and rejects nothing else.
- `vfs_unlink()` in `split_parent()` dropped the first basename character
  of nested paths (`/bin/x` was looked up as `ello`), so unlinking anything
  below the root always failed (this broke `rm` on nested paths and would
  have blocked package removal). Covered by new `vfs-tmpfs` unlink
  round-trip assertions (nested + single-char names).
- `make qemu-test` was a false green: `all` builds only the Limine ISO, so the
  PVH kernel the suite boots was never a prerequisite and never existed. A
  missing kernel made QEMU exit 1, which the `exit 1 = PASS` convention
  reported as success. The target now depends on the kernel it boots and
  fails unless QEMU exits with the pass status.
- The PVH kernel did not link: `fb.c` wrote the framebuffer PML4 slot through
  `x64_pml4`, whose `.early_bss` address is out of reach of a high-half
  RIP-relative displacement (`-mcmodel=kernel` truncated `R_X86_64_PC32`).
  It now reaches the table through the direct map, as `x86_paging.c` does.
- `test_mode=1` only terminated the run on failure; a green run fell through
  into the interactive shell, so the suite's verdict depended on serial EOF.
  Success now prints a summary and exits through `isa-debug-exit` too.
- The `open-close` self-test failed since the ABI switched to Linux `O_*`
  flags: it still passed internal `VFS_O_*` values, so `O_CREATE` was never
  decoded and the create round trip was skipped. The test now speaks the
  syscall ABI, and `SYS_OPEN` rejects the reserved `O_ACCMODE` value 3
  instead of silently treating it as read-write.

### Verified
- `BOOT=PASS` - ISO boots to `NEWOS: boot complete.` under SeaBIOS (legacy
  BIOS path) and under OVMF/UEFI; PVH still passes the automated suite.
- `REPRO`     - `make clean && make iso` twice is byte-for-byte identical
  for the normalized GRUB ISO; ELF outputs are deterministic. The Limine ISO
  embeds build timestamps and is not byte-identical across rebuilds yet.

### Changed
- `VERSION` is now the single source of truth for the system version. The
  Makefile injects `NEWOS_VERSION`/`NEWOS_VERSION_ID` into every kernel and
  userland translation unit, and the boot banner, `/etc/os-release`, MOTD,
  `uname` release and the nsh banner all use it instead of five independent
  hard-coded strings.
- `scripts/iso/fixup_iso.py` is invoked by `make iso`; the reproducibility
  claim in the changelog was previously not wired to the build.
- `packages/glxgears-newos.newspec` now has a `make glxgears-pkg` target and
  `make all` produces the native package after building the demo.
- `scripts/qemu-shell.py` and `scripts/gen-brand.py` have `make qemu-shell`
  and `make brand` targets. `make brand` also refreshes the website copies.
- The static site has a real gate: `make check-web` (also in CI) validates
  every local link and rejects unreferenced files under `web/assets` and
  `web/dl`; `make web` serves it locally.

### Removed
- Nova: the pinned `ports/nova` submodule (BoredOS compositor, GPL-3.0), the
  adapted NTK slice under `user/programs/desktop/nova`, the `WNOVA` preview
  window, `docs/nova-porting.md` and the `check-nova` host tests. The NTK code
  was GPL-3.0-only inside an otherwise MIT tree; the desktop now draws through
  `desktop_surface`/`desktop_widgets`. NEWOS's graphics direction is the
  separate MIT VOXEL stack (<https://github.com/CRISTOP-bot/voxel>), which is
  not integrated into this image.
- `/bin/kilo`: superseded by the genuine GNU nano 8.4 port at `/bin/nano`.
- `/bin/ltest`: the ad-hoc libc/loader test program. libc stays exercised by
  the phase-2 QEMU suite and by the frotz/nano/tcc ports.
- Native-GCC plans: `docs/porting-roadmap.md` no longer lists a native GCC
  milestone. `toolchain/` remains a host-only `x86_64-elf` cross compiler and
  TinyCC is the on-system compiler.
- The fake IPv4 socket syscall block (`SYS_SOCKET` through `SYS_RECV`) that
  allocated state but returned dummy data and carried `TODO: actual network
  transmit`, together with its ABI numbers and per-process socket record.
  There is no network stack, so the ABI no longer advertises calls it cannot
  honor. The non-building `user/programs/ping` scaffold that depended on it
  was deleted as well.
- Dead scaffolding that no build target consumed: the `net/` stub tree
  (ARP/DNS/ICMP/IPv4/TCP/UDP/socket; none of it was compiled or callable),
  the unused duplicate `drivers/net/ip.c` (included a nonexistent header)
  and `drivers/console/kconsole.c` (superseded by `vga_text_console.c`).
- The stale `bc/` build cache: prebuilt GNU binaries, Limine trees, duplicate
  splash/PPM images and sample packages. `bc/font8x8_basic.h` - the font
  source consumed by `scripts/fetch-font.sh` - is the only file kept.
- `brand/app-icons/lua.svg` (no `lua` program exists) and IDE-local `.qoder/`
  settings.

## [0.2.0-pre-alpha] - 2026-09-10

Source-layout rework: every subsystem moved under a canonical, layering-owned
tree (`abi/`, `arch/*/`, `core/`, `drivers/`, `fs/`, `include/`, `ipc/`,
`lib/kernel/`, `mm/`, `process/`, `syscall/`, `user/`), the build was
replaced with a single explicit-source `Makefile`, and the output hierarchy
was moved under `build/`.

### Added
- Layering contract lint (`scripts/ci/lint_layering.sh`, `make lint-layers`)
  enforced on every build and in CI; no cross-domain `#include` shows up.
- Documentation set: `docs/architecture.md`, `docs/build.md`, `docs/boot.md`,
  `docs/abi.md`, `docs/development/layering.md`.
- Explicit `PHDRS` in the x86_64 linker script so no PT_LOAD is RWE.

### Changed
- Tree restructured to the layered layout; files renamed to prefixed,
  self-describing names (`restructure`).
- Build unified on one `Makefile` with an explicit per-domain source ledger
  (`cmake/kbuild` removed); artifacts consolidated under `build/images/`.
- `include/core/core_types.h` uses the real `<stdint.h>` types; `size_t`
  widened to 64-bit and `intptr_t`/`uintptr_t` fixed.
- CI workflows (`build`, `qemu-test`, `static-analysis`) reconciled with the
  new layout and lint rule.
- `.early_bss` declared `nobits` in the entry assembly (NASM warning gone).

### Fixed
- `abi/multiboot2.h` is self-contained (no longer depends on kernel types).

### Verified
- `BUILD=PASS`  - two clean builds are byte-for-byte identical.
- `BOOT=PASS`   - PVH boot reaches `NEWOS: boot complete.` plus userland.
- `SELF-TEST`   - all kernel self-tests and the userland hello pass.
- `QEMU=PASS`   - `scripts/ci/run_qemu_test.sh` exits 0 only on success.

## [0.1.0-pre-alpha] - 2026-09-06

Phase 1 milestone: the x86_64 kernel builds clean under `-Werror`, boots in
QEMU through two independent boot-loading paths, and passes its kernel
self-tests with an automated, machine-checkable test exit code.

### Added
- Bootable ISO target (`make iso`) via GRUB (multiboot2).
- Automated in-QEMU test runner with the `isa-debug-exit` device
  (`ci/qemu/run_test.sh`, `make qemu-test`) so CI observes a real exit status.
- GitHub Actions workflows: `build`, `qemu-test`, `static-analysis`.
- `LICENSE` (MIT), `VERSION`, `CHANGELOG.md`, `README.md`.

### Changed
- `lib/libk/lock.h`: `spinlock_unlock` now uses a plain store instead of
  `lock; movl` (LOCK-prefixed MOV is an invalid `#UD` instruction).
- `arch/x86_64/gdt/gdt.c`: TSS descriptor rewritten to the byte-accurate
  Intel layout (access byte at offset 5) so `ltr` no longer faults.
- `arch/x86_64/idt/idt.c`: legacy 8259 PIC remapped off the CPU exception
  vectors (IRQ0 no longer collides with the double-fault gate); all PIC lines
  masked until drivers claim them.
- `arch/x86_64/boot/*`: PVH start-info parsing now keys off populated fields
  instead of QEMU's (unset) flag bits; memory-map parsing spans all available
  RAM regions above the kernel load base.
- `fs/vfs/vfs.c`: `vfs_mkdir`/`vfs_create` strip leading slashes on
  single-component paths (`/etc` created as `etc`).
- Build flags add `-mcmodel=kernel`; `printk` drops the `format(printf,...)`
  attribute in favor of `u64`-friendly `%p`.

### Fixed
- Early high-half kernel map: six-megabyte PTE window at
  `0xffffffff80000000` now maps kernel physical memory 1:1 (was mapping VA
  window to physical frame 0, jumping into zeros).
- MV scoped fixes otherwise recorded across PMM/heap/VFS boot integration.

### Verified
- `BUILD=PASS`  - links `build/newos-x86_64.elf` under `-Werror`.
- `BOOT=PASS`   - boots via PVH (QEMU `-kernel`) and via multiboot2 (GRUB
  under OVMF/UEFI), serial console active, 126 MiB / 125 MiB RAM tracked.
- `SELF-TEST`   - bitmap, heap-rw, pmm-alloc, pipe, vfs-tmpfs all pass.
- `QEMU=PASS`   - `ci/qemu/run_test.sh` exits clean only when tests pass.
