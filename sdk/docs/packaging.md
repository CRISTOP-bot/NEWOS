# Packaging SDK applications (`.new`)

NEWOS has no network stack, so packages are built on the host and installed in
the guest with `/bin/newpkg`. The format and manager are documented in
`../../docs/new-format.md` and `../../docs/newpkg.md`; this page is the SDK
view.

## Build a package

```sh
sdk/bin/newos-package build/myapp.elf \
  --name myapp --version 1.0.0 \
  --desc "My NEWOS app" --license MIT \
  --dest /bin/myapp \
  --out build/myapp-1.0.0-x86_64.new
```

The script writes a temporary `.newspec` and calls
`tools/newpkg/newpkg-build.py`. Generated metadata:

```
name = "myapp"
version = "1.0.0"
arch = "x86_64"
newos-min = "0.2.0"
depends = ""

files:
  <absolute path to ELF> /bin/myapp 0755
```

Edit the generated spec (or copy `packages/hello-new.newspec`) when you need
dependencies, multiple files or a different install path.

## Install and verify in the guest

```
newpkg info   /tmp/myapp-1.0.0-x86_64.new
newpkg verify /tmp/myapp-1.0.0-x86_64.new
newpkg install /tmp/myapp-1.0.0-x86_64.new
newpkg list
newpkg files myapp
myapp
newpkg remove myapp
```

`info` shows metadata, the file list and section CRCs; `verify` rechecks every
payload byte. Installation validates `arch`/`newos-min`/dependencies, scans
for conflicts, then streams files into the RAM VFS in 512-byte chunks with a
rollback on failure. Ownership is recorded in `/var/lib/newpkg/<name>.*`.

A conflict with a built-in file (for example installing to `/bin/hello`)
requires `newpkg install --force`. Prefer a distinct name instead: that is why
the existing `hello-new` package installs `/bin/hello-new`.

## Distributing the SDK itself

`make sdk-pkg` runs `scripts/pack-sdk.sh`, which packs the generated sysroot
plus TinyCC's private headers/`libtcc1.a` into `build/newos-sdk.new`. That
archive is embedded in the kernel image and appears at `/tmp/newos-sdk.new`,
so a booted machine can install the C SDK without a host connection:

```
newpkg install /tmp/newos-sdk.new
tcc -static -nostdlib -I/usr/lib/tcc/include -I/usr/include \
    -o /tmp/hello /usr/lib/crt0.o hello.c /usr/lib/libnsh.a /usr/lib/libc.a
```

The on-board compiler uses the same `libc.a`, `libnsh.a` and headers as the
host SDK; only its own builtins (`stdarg.h`, 64-bit division helpers) differ.
Run `make sdk sdk-pkg` after changing any header or library so both paths
stay in sync.

## Limitations

- No `newpkg build/update/search` on board — building needs host tools and
  there is no network stack.
- The VFS has no symlinks; packages install plain files and directories.
- `newos-min` is checked against the running release from `VERSION`.
- A package owns exactly the paths in its manifest; files you create later in
  the same directory are untouched by removal.
