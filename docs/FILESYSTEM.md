# NEWOS filesystem layout

NEWOS builds a small, Linux-style root hierarchy in tmpfs at boot. The
directories follow familiar Filesystem Hierarchy Standard roles, while their
contents are native to NEWOS and its current drivers.

| Path | Role |
| --- | --- |
| `/bin`, `/sbin` | Native command toolbox and system commands |
| `/boot` | Boot-related files and future boot configuration |
| `/dev` | Kernel device nodes such as console, serial, zero and mouse |
| `/etc` | Hostname, passwd, release metadata, shell startup and system config |
| `/home`, `/root` | User and administrator homes with Desktop, Documents, Downloads and Pictures |
| `/lib`, `/lib64` | Reserved for system libraries |
| `/media`, `/mnt` | Removable media and temporary mount points |
| `/opt` | Optional software |
| `/proc`, `/sys` | Kernel process and device information |
| `/run` | Runtime state, including lock files |
| `/srv` | Data served by system services |
| `/tmp` | Temporary files and staged packages |
| `/usr/bin`, `/usr/sbin` | Add-on command paths; the experimental GNU package installs here, but its Linux runtime ABI is unsupported |
| `/usr/include`, `/usr/lib`, `/usr/lib64`, `/usr/src` | Development files and libraries |
| `/usr/share` | Architecture-independent data, documentation and licenses |
| `/usr/local` | Locally installed software |
| `/var/cache`, `/var/lib`, `/var/log`, `/var/opt`, `/var/spool`, `/var/tmp` | Persistent-style cache, package state, logs and variable data |

The current VFS does not implement symbolic links or a persistent disk root.
For compatibility, NEWOS keeps its built-in commands in `/bin` and places
installable packages under `/usr/bin` instead of aliasing `/bin` to
`/usr/bin`. `/etc/os-release` identifies the distribution, and
`/usr/share/doc/newos/README` gives a brief in-system overview. The initial
root also includes `/etc/group`, `/etc/shells`, `/etc/issue`, and starter
`.profile` files under `/etc/skel`, `/home/user`, and `/root`.

## Synthetic kernel trees

`/proc` (procfs) and `/sys` (sysfs) are mounted by the boot tail after the
initramfs and PCI enumeration. Both are read-only: the VFS refuses unlink
and rename on synthetic inodes, and their files have no write operation.

`/proc` currently exposes:

| File | Contents |
| --- | --- |
| `version` | `NEWOS <version> (x86_64)` |
| `uptime` | Seconds and centiseconds since boot (a single field; idle time is not tracked yet) |
| `meminfo` | `MemTotal`, `MemFree`, `MemAvailable`, `MemUsed` in kB from the PMM, plus zero swap/cache fields |
| `filesystems` | Filesystem backends compiled into this kernel |
| `mounts` | Current synthetic mount table |
| `cmdline` | Boot-protocol command line, copied into a kernel-owned buffer |

`/sys` exposes `kernel/ostype`, `kernel/osrelease`, `kernel/version`, and
`bus/pci/devices/<domain:bus:slot.func>/{vendor,device,class}` for every
function discovered by the PCI bus. Writable sysfs attributes, driver
bind/unbind and a process-oriented `/proc` provider are future work.

## Disk filesystems

Two layers turn real block hardware into files. The legacy-PIO ATA driver
(`kernel/drivers/block/ata.c`) probes the primary-master port (0x1F0) with
polling only — nIEN is set so IRQ14 never fires, and there is no DMA and no
PCI programming. The drive is exposed as the `disk0` block device with a
freshly identified geometry, and registered on devfs:

| Path | Kind | Contents |
| --- | --- | --- |
| `/dev/disk0` | block, raw | Byte-addressable, seekable stream over the disk's sectors; partial head/tail sectors use a read-modify-write scratch buffer, so plain reads (BusyBox `dd`) and file writes both behave |
| `/fat` | directory tree | Root of the FAT32 volume that carries the disk |

`/fat` (`kernel/fs/fat32/fat32fs.c`) mounts the volume when the boot tail
can parse a valid FAT32 BIOS parameter block; volumes below 65525 data
clusters (effectively FAT16) are refused. Directories materialize their
children as real VFS inodes, so `ls`, lookup and path resolution stay in
the VFS core; create/mkdir/unlink run through the `inode_ops` hooks and
mutate the volume, and unlink reaches the disk via ops->destroy (the
mother dirent becomes 0xE5 and the cluster chain is returned to the FAT).

Honest limits of the current FAT32 implementation:

- The stored namespace is 8.3: long-file-name entries are skipped when
  listing (files appear under their short alias), names created in NEWOS
  are stored as 8.3 with truncation but no ~N collision suffix, and two
  distinct names that would collapse into the same short entry are
  rejected on create.
- Writes may overwrite byte ranges at/before EOF or append strictly from
  EOF; writes that would punch a zero gap beyond EOF return -1. Truncate
  extends with zeroed clusters and shrinks by freeing the chain tail.
- `rename` on volume files is refused by the VFS core; the FSInfo sector
  and volume timestamps are not maintained; there is no fsync (the block
  device writes sectors immediately).
- The driver talks to exactly one device (primary master) and mirrors FAT
  writes across every declared FAT copy.

## ISO 9660 read-only (`/iso`)

`kernel/fs/iso9660/iso9660.c` mounts an ISO 9660 image (or a physical
medium) from the primary-master disk under `/iso` when the Primary Volume
Descriptor at fixed LBA 16 carries the `CD001` signature. The parser is a
deliberate, honest subset of the standard:

- Only 2048-byte logical blocks (checked against the PVD field; the block
  maps onto whole 512-byte device sectors).
- The primary volume descriptor only: no Joliet (SVD), no Rock Ridge
  (SUSP/RR), no El Torito boot catalog, no audio sessions.
- Depth extends at most 8 directories (the standard's own level cap), with
  a 4096-record safety bound per directory against corrupt extents.
- Names come from the primary descriptor records with the `;version`
  suffix stripped at the first `;`; `.`/`..` records (name bytes 0x00/
  0x01) and hidden-flag records are skipped. Names beyond the 63-character
  VFS cap are skipped, never fatal.
- Single Extent-Size-Only files split across two or more extents are
  joined: the continuation records (flag bit 0x80) append up to eight
  extents into one VFS node whose byte size tracks every extent. A
  continuation record whose name does not match the chain head aborts the
  join with a console warning (a broken image) instead of silently
  miswiring two files.
- Files read through per-extent mapping with one-block (2048) bounces, so
  reads land correctly across interior block boundaries and the 512-byte
  device sectors underneath.

The tree materializes as plain VFS children exactly like FAT32:
`ls`/`cat`/`stat` walk in-memory inodes; the volume is read-only by
construction (no create/mkdir hooks, so the VFS refuses mkdir/create and,
by its parent-hooks rule, unlink). The ktest `iso9660-ro` verifies against
a real image: root-file round-trip, a multi-block file (byte pattern and
length), a nested directory, and the three write-refusal guards; it skips
when no ISO 9660 volume is mounted, same as fat32-rw.

## romfs v1 read-only (`/rom`)

`kernel/fs/romfs/romfs.c` mounts a classic romfs 1.0 image under `/rom`
when the superblock opens with `-rom1fs-`. Semantics follow the original
format definition (as also encoded by Linux `fs/romfs`):

- Superblock: magic, image size (BE32, used as the mount read bound), and
  the additive 32-bit checksum over `min(size, 512)` bytes that must sum
  to zero; the volume name sits right after the checksum and the root
  directory header lands at `(16 + namelen + 1 + 15) & ~15`.
- Inodes are 16-byte headers plus NUL-padded names. Every pointer is an
  absolute byte offset into the image; the low nibble of the `next` word
  carries the type (0 hardlink, 1 dir, 2 reg, 3 sym...) and an exec bit,
  and every dereference masks it away with `~15`.
- Directory children chain from the header's `spec` field until `next == 0`
  (with a 4096-step bound per directory and depth 16 guard: a corrupt
  chain bails, keeping whatever materialized so far mounted).
- File data offsets derive as `header_pos + padded-name-length` (the
  format's rule), never from the spec field, matching the reference
  implementation's behavior.
- Hard links (type 0) materialize under the link name pointing at their
  target's header (one redirect level; chains of hard links are refused).
- Symlinks and device/socket/FIFO entries are skipped quietly: NEWOS VFS
  has no symlinks, and there is no device file layer to back them.
- Read-only by construction, same refusal rules as `/iso` above. The
  `romfs-ro` ktest round-trips a root file, a hard-linked copy, a
  >4096-byte chunked read, a nested directory and the write refusals;
  it skips without a romfs volume mounted.

## Test images and tooling

- `scripts/fs-tools/mkfs.romfs.py` builds romfs 1.0 images from a plain
  directory tree: headers 16-aligned, file data padded to 16 (like
  genromfs, so every `next`/`spec` address keeps its type nibble
  unambiguous), `--link SRC=DSTNAME` hardlinks and `--symlink
  TARGET@NAME` entries, plus a superblock checksum computed over the
  first 512 bytes. The tool re-parses its own output and verifies the
  full tree (names, kinds, byte-exact data, chains, hardlink targets,
  per-inode offset formulas) before writing the file.
- ISO test images come from `xorriso -as mkisofs` (plain ISO 9660 with
  `-J -r`, names `;1`-suffixed); any standard tool works since the parser
  only relies on the primary descriptor.
- Neither is needed for `make qemu-test`: without a disk the
  `fat32-rw`/`iso9660-ro`/`romfs-ro` ktests skip with a console note.
