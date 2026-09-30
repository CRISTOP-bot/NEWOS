# `newpkg`: package manager reference

`newpkg` (`user/programs/newpkg/`, installed as `/bin/newpkg`) is the
userspace tool that manipulates `.new` packages (spec: `docs/new-format.md`).
It lives entirely in userspace; the kernel only provides the VFS and
process primitives. Implementation:

- `newpkg_format.{h,c}` — portable format core (no syscalls, no libc, no
  FPU); shared with the host test drivers.
- `newpkg_main.c` — the on-board CLI (nshlib syscalls only).
- `tools/newpkg/newpkg-build.py` — host builder (NEWOS carries no
  toolchain on board, so building runs on the host).
- `packages/*.newspec` — package definitions.

## Commands (all real, exit 0 on success, 1 on error)

```
newpkg info <pkg.new>                metadata + file list + section CRCs
newpkg verify <pkg.new>              full integrity check (incl. payload)
newpkg install [--force] <pkg.new>   validated install with rollback
newpkg remove <name>                 uninstall (files + database entries)
newpkg list                          installed packages (name + version)
newpkg files <name>                  files owned by a package
```

`--force` (before or after the file name) allows overwriting files owned
by another package or files outside any package (built-ins). Without it,
any conflict aborts the install before anything is written.

Building runs on the host:

```
python3 tools/newpkg/newpkg-build.py packages/hello-new.newspec \
    -o build/packages/hello-new-1.0.0-x86_64.new
```

There is deliberately no `newpkg build` or `newpkg update/search` on
board: building needs a host toolchain and repositories need a network
stack (see `docs/packages.md`). They are documented as future work, not
stubbed as fake commands.

## Install pipeline

1. Load header/metadata/manifest; verify all section CRCs.
2. Require every metadata key; validate the package name.
3. `arch` must match `uname -m` (or be `any`).
4. The running release must be `>= newos-min`.
5. Dependencies: every entry must be installed and satisfy its constraint;
   self-dependencies and dependency cycles are rejected (DFS from the new
   package through installed `depends` edges, depth-capped at 16).
6. Conflict scan: every existing target must be absent, owned by the same
   package (reinstall/upgrade), or explicitly forced.
7. Streamed extraction: payload bytes flow package -> VFS in 512-byte
   chunks (no seek/rewind), verifying per-file and whole-payload CRCs on
   the fly. Targets are unlinked before rewrite (NEWOS has no `O_TRUNC`,
   so this avoids stale tails).
8. Registration: `/var/lib/newpkg/<name>.info` (metadata + `installed-at:`
   stamp from the CMOS clock + `files:` is implicit in `.files`) and
   `/var/lib/newpkg/<name>.files` (manifest verbatim). Stale registrations
   are replaced the same unlink-first way.

Any failure in 7-8 unlinks every file the run touched plus partial
database entries, leaving the previous state intact (failed upgrades keep
serving the old files). Known transaction limit: replace is
unlink+create (there is no `rename` syscall yet), so a crash between the
two leaves a missing file until reinstall — recorded as future work
(a `SYS_RENAME` primitive) in `docs/packages.md`.

## Removal

Reads the package's `.files` manifest, unlinks each path (already-gone
files warn once and are skipped), then unlinks both database files.
Files created after installation that are not in the manifest are left
alone; reinstalling never touches them either (the manifest is the single
source of ownership truth).

## I/O constraints the code respects

- Each `read`/`write` syscall moves at most 512 bytes (kernel bounce
  buffer): all copies loop on short counts.
- No seek: package input is consumed strictly sequentially; the database
  is re-read (not rewound) whenever it is needed twice.
- No `stat`/`truncate`/`rename`: existence is probed with `open(O_READ)`,
  truncation is emulated with unlink-then-create, directories with
  `mkdir -p` that tolerates existing components.
- `argv` is capped at 16 x 128 chars (ABI): package paths must fit.

## Examples (live system)

```
newpkg info /tmp/hello-new-1.0.0-x86_64.new
newpkg verify /tmp/hello-new-1.0.0-x86_64.new
newpkg install /tmp/hello-new-1.0.0-x86_64.new
newpkg list
newpkg files hello-new
hello-new
newpkg remove hello-new
```

## Tests

```
make check-newpkg   # C unit driver (80+ asserts) + end-to-end suite (19 tests)
```

The end-to-end suite compiles the shipped `newpkg_main.c` for the host
against `tools/newpkg/posix-shim.c` (a syscall jail under
`$NEWPKG_HOST_ROOT`) and exercises install/remove/list/files, owned and
unowned conflicts (+ `--force` ownership transfer), missing/incompatible/
circular dependencies, corrupt/truncated packages with rollback checks,
wrong-architecture and future-OS gates, builder path rejections, and
build determinism. `tools/newpkg/host-test.c` unit-covers the pure format
functions (CRC vectors incl. incremental streaming, header round-trip and
tamper cases, metadata/manifest parsing, path policy, version compare,
dependency matching/parsing, arch gate, file naming).
