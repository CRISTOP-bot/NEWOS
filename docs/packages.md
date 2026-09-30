# Packages in NEWOS

This document describes how the native package system fits into NEWOS:
where files go, how the installed database works, how dependencies are
resolved, what the transaction guarantees are, and how repositories will
attach later. Format bytes: `docs/new-format.md`. Tool usage:
`docs/newpkg.md`.

## Placement

Packages install into the standard root filesystem populated by initramfs
(`fs/initramfs/initramfs.c`): `/bin /sbin /lib /etc /usr /opt /var /home
/root /tmp` (plus pre-existing `/dev /proc /sys`, which packages can never
touch). Conventions for future packages:

- executables -> `/bin` (the shell only runs plain `/bin` tool names),
  admin tools -> `/sbin`;
- libraries/data -> `/lib`, `/usr`, `/opt/<pkg>`;
- configuration -> `/etc`;
- package state -> `/var/lib/newpkg` (the database; nothing else writes
  there).

Because the shell resolves only `/bin/<tool>`, a package whose programs
must be interactive installs them under `/bin` (e.g. the smoke-test
`hello-new` package ships `/bin/hello-new`, deliberately avoiding the
built-in `/bin/hello`).

## Installed database (no heavy engine)

The VFS is currently in-RAM (tmpfs + devfs + built-in initramfs: no disk
driver yet, so the registry is volatile across reboots — the on-disk
format is already stable for the day a persistent filesystem lands).
Two flat files per package under `/var/lib/newpkg/`:

- `<name>.info` — the package metadata verbatim plus
  `installed-at: YYYY-MM-DD HH:MM:SS` (CMOS clock);
- `<name>.files` — the manifest verbatim (ownership truth).

Answering the registry questions is then trivial: `list` = readdir +
read each `.info` version; `files <pkg>` = dump its `.files`; dependency
edges = each `.info`'s `depends`; install time = `installed-at`. No
SQLite, no daemon, nothing the kernel must know about.

## Dependencies

Constraints (`depends = "b (>= 1.0), c"`) are checked against installed
`.info` versions with numeric-prefix comparison (`1.10 > 1.2`;
`-suffixes` ignored). Failures distinguish *missing* (not installed),
*incompatible* (installed but violating the operator), and *circular*
(self-edge, or the new package reachable again by DFS through installed
edges — depth-capped, so `A -> B -> A` upgrades are refused while the old
`A` keeps serving). Remote fetching is out of scope until the network
stack exists; dependency *resolution* (ordering a batch install) will ride
on the same graph walk when repositories land.

## Conflicts and ownership

A target path may be installed when it is absent, when the database says
it belongs to the same package (reinstall/upgrade), or when `--force` is
given (ownership transfers to the new package). Everything else — owned
by another package, or present but unowned (built-ins, `/etc` dotfiles) —
aborts pre-write with the owner named. Ownership is manifest-exact:
removal deletes exactly the manifested paths, nothing more.

## Transactions and rollback

Install is staged to be atomic *enough* for the syscalls that exist:
validate everything (checksums, arch, OS version, deps, conflicts)
before writing the first byte; stream payload with per-file CRCs so a
corrupt byte aborts mid-run; on any failure unlink every file the run
created plus partial database entries. Failed upgrades therefore keep
the old package fully working. The honest limitation is replace
semantics — NEWOS has no `rename`/`truncate` syscalls yet, so overwrite
is unlink+create and a crash in that window could drop a file (reinstall
repairs it). A future `SYS_RENAME` (plus `O_TRUNC`) enables true
stage-then-commit; the installer is already structured for it (conflict
scan -> extract -> register).

## Integrity and security

Every `.new` carries header, section, per-file, and whole-payload CRC32;
verification order is fixed (see `docs/new-format.md`) and any mismatch
aborts. Path traversal is closed at both ends (builder + installer share
the policy: absolute, allowlisted prefixes, no `..`, no kernel trees).
Overwrites across packages require explicit `--force`. Permissions travel
in the manifest (3-4 octal digits) and are recorded; enforcement beyond
recording awaits a UID/mode-checking VFS pass. Cryptographic repository
signatures are the planned next integrity layer (see below), not part of
v1 — CRC32 guards corruption, not malice.

## Package definitions (`packages/*.newspec`)

A newspec is `KEY = "value"` lines (required: `name version arch desc
license maintainer build newos-min`; optional `depends`) plus a `files:`
section of `<host-src> <vfs-dst> <octmode>` lines. The host builder
(`tools/newpkg/newpkg-build.py`) validates names, paths, modes, and caps,
then emits deterministic bytes (sorted metadata/manifest, LF, no
timestamps). Example: `packages/hello-new.newspec`.

## Repositories (future)

`.new` is already shaped for them; the on-board tool only needs new verbs
once the infrastructure exists:

```
repo/
  index          # name/version/arch/depends + per-package CRC + location
  packages/      # *.new files (exact bytes the builder emitted)
  signatures/    # detached signatures over index + packages
```

Then `newpkg update` (fetch index), `newpkg search <term>` (match index),
and `newpkg install <name>` (resolve deps from the index, fetch,
verify, install) become possible. Prerequisites NEWOS does not have yet:
a network stack past PCnet loopback, persistent storage, and signature
verification primitives. Until then, packages enter the system as files
(the build embeds a sample at `/tmp/hello-new-1.0.0-x86_64.new`) and every
repository-facing command stays out of the CLI rather than faked.

## What lives where (v1 map)

| Piece | Location |
| --- | --- |
| Format core (portable C) | `user/programs/newpkg/newpkg_format.{h,c}` |
| CLI (nshlib syscalls) | `user/programs/newpkg/newpkg_main.c` -> `/bin/newpkg` |
| Host builder | `tools/newpkg/newpkg-build.py` |
| Host unit driver | `tools/newpkg/host-test.c` |
| Host syscall jail (tests only) | `tools/newpkg/posix-shim.c` |
| End-to-end suite | `tools/newpkg/tests/test_newpkg.py` (`make check-newpkg`) |
| Package definitions | `packages/*.newspec` |
| Built artifacts | `build/sample.new`, `build/packages/*.new` (generated) |
| Live sample | `/tmp/hello-new-1.0.0-x86_64.new` (staged by initramfs) |
| Database | `/var/lib/newpkg/<name>.{info,files}` (created at boot) |
| Specs | `docs/new-format.md`, `docs/newpkg.md`, this file |
