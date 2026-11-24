# The NEWOS guided installer (`/bin/newinstall`)

`newinstall` is an archinstall-style installer that runs **inside the live
ISO**. It asks a handful of questions, applies them to the running system,
installs `.new` packages and writes a snapshot to the FAT32 disk mounted at
`/fat`. A small boot-time service, `/sbin/sysrestore`, replays that snapshot
on every subsequent boot.

The interactive flow shows a colored banner, refreshes the package index and
prints a numbered list of available `.new` packages (entries can be chosen by
number or name), then shows a **summary** and asks for confirmation before
anything is written. `newinstall /etc/installer.answers` skips all prompts.

## What it actually installs (honest scope)

NEWOS boots a kernel whose root filesystem is built into the ELF/ISO
(`initramfs_init()`), and has no `mount(2)`, no chroot and no bootable
filesystem writer. Therefore an installation is a **userspace overlay on a
host-built boot medium**, not a self-contained disk install:

- Persistent, on `/fat/NEWOS.SYS`: configuration (`/etc`, `/home`, `/root`),
  base binaries (`/bin`, `/sbin`, `/usr/bin`, `/usr/lib`, `/lib`) and,
  optionally, games/assets (`/baseq2`, `/usr/share/doom`).
- Persisted by `newpkg` itself: the package database at `/fat/NEWPKG`.
- Still supplied by the boot medium: the kernel, Limine/GRUB and the built-in
  root image. There is no in-guest `mkfs.fat` and no way to make a disk bootable
  from NEWOS; the FAT32 volume has to be prepared on the host (e.g.
  `make qemu-disk` or `scripts/test-installer.sh`).

This is the first step of the v1.0 "install to disk from ISO" roadmap item;
it deliberately does not claim a disk-only boot.

## Running it

Boot the ISO **with an ATA disk attached** and run:

```sh
newinstall                          # interactive questions
newinstall /etc/installer.answers   # non-interactive, from an answer file
```

The answer file uses `key=value` lines (`#` comments allowed):

```ini
profile=desktop        # minimal | desktop | full
hostname=newos-installed
user=installer
session=text           # text | gui
copy_base=yes          # persist the base binaries too
assets=no              # persist games/assets (large)
packages=hello-new     # comma-separated .new package names
```

`profile=minimal` defaults `copy_base=no`; `profile=full` defaults
`assets=yes`. Explicit `copy_base`/`assets` lines override those defaults.
`/etc/installer.answers` is embedded in the image as a working example; it
uses `copy_base=no` so the automated reboot test stays small. Set it to `yes`
(or use the interactive prompt, which defaults to yes) to persist the base
toolbox — that takes considerably longer under QEMU-TCG.

### Questions

| Prompt | Effect |
| --- | --- |
| Profile | Selects which trees are collected into the snapshot. |
| Hostname | Rewrites `/etc/hostname` (informational: no `sethostname(2)` yet). |
| User name | Adds a `uid=1000` line to `/etc/passwd` and `/etc/group`. |
| Graphical session? | Writes `/etc/newos.conf` with `session=gui`; the boot shell then starts `/bin/voxel` even without `gui=1` (unless `test_mode=1`/`headless=1`). Also enables the `voxel` OpenRC runlevel service. VOXEL is the only graphical session shipped. |
| Copy base system? | Persists `/bin`, `/sbin`, `/usr/bin`, `/usr/lib`, `/lib`. |
| Include games/assets? | Persists `/baseq2` and `/usr/share/doom` (~22 MiB). |
| Packages | Runs `newpkg update` then `newpkg install <name>` for each name. |
| Write snapshot? | Confirms writing `/fat/NEWOS.SYS`. |

The selected packages are installed *before* the snapshot is taken, so their
files are included too.

## Snapshot format

`/fat/NEWOS.SYS` is a small deterministic container (all integers
little-endian):

```
header (32 B):
  "NEWSYS1\0"  magic (8 B)
  u32          version (1)
  u32          file count
  u32          total byte length
  u32          CRC32 of header bytes 0..19

per file:
  u16          path length
  u32          data length
  u32          CRC32 of data
  u16          mode (informational)
  path bytes   (no NUL)
  data bytes
```

`SYS_WRITE` is capped at 512 bytes per call and FAT32 can only grow a file
through `ftruncate`, so `newinstall` pre-sizes the image with
`sys_ftruncate()` and then streams it in 512-byte chunks.

## Boot-time restore

`userland/user/rootfs/openrc/etc/inittab` contains:

```
s1::sysinit:/sbin/sysrestore
c1::respawn:/bin/sh --session --rc-console
```

OpenRC runs `sysinit` entries before the runlevel chain, so restored configs
are visible to services and to the console shell. `sysrestore` validates the
header CRC and each file's CRC, creates parent directories and refuses to
abort the boot on a missing/corrupt image — the live ISO defaults remain.

`SKIP` roots in `newinstall` are `/fat`, `/proc`, `/sys`, `/dev`, `/run`,
`/tmp`, plus games unless requested; synthetic trees and runtime state are
never snapshotted.

## Verification

```sh
make check-installer   # needs QEMU + mkfs.fat, builds the kernel first
```

`scripts/test-installer.sh` creates a 64 MiB FAT32 image, boots twice with
the same disk (`scripts/qemu-shell.py`), installs with the embedded answer
file, reboots and asserts that hostname, user, session and package survived.
CI-style guarantees are the same as for `test-persistence`: no failures,
serial console authoritative.

## Known limitations

- Installed files are restored into the tmpfs root; the kernel and built-in
  `/bin` still come from the ISO. A truly disk-only boot needs a bootable
  native FS, a filesystem writer and a loader (roadmap v1.0).
- No partitioning, formatting or second disk from inside NEWOS (only primary
  master ATA; AHCI is a stub).
- FAT32 has 8.3 names, no timestamps and no durable rename; the snapshot is
  a single 8.3-safe file (`NEWOS.SYS`), which sidesteps name mangling.
- Passwords are not hashed/verified: NEWOS has no authentication model yet.
- `hostname` and file modes are informational only (no `sethostname(2)`,
  no enforced permissions).
- `sysrestore` writes into RAM first, then `newpkg` installs from its own
  FAT32 database; the two persistence mechanisms are independent by design.
