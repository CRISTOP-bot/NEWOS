# NEWOS repository layout

The central `CRISTOP-bot/NEWOS` repository is the build/boot/ISO orchestration superproject. First-party code is separated into pinned Git submodules:

| Repository | Superproject path | Contents |
|---|---|---|
| [`NEWOS-kernel`](https://github.com/CRISTOP-bot/NEWOS-kernel) | `kernel/` | ABI, architecture, core, crypto, drivers, filesystems, IPC, kernel library, memory, networking, process, system services, syscalls |
| [`NEWOS-userland`](https://github.com/CRISTOP-bot/NEWOS-userland) | `userland/` | `user/` programs/runtime/rootfs and `libc/` |
| [`NEWOS-toolchain`](https://github.com/CRISTOP-bot/NEWOS-toolchain) | `toolchain/` | Cross-toolchain scripts/docs/examples; optional shallow GCC 16.1.0 submodule |
| [`NEWOS-ports`](https://github.com/CRISTOP-bot/NEWOS-ports) | `ports/` | Port integration, staged assets, pinned nested graphics/desktop source modules |

## Preserved build paths

The superproject tracks symlinks to the matching directories in `kernel/` and `userland/`: `abi -> kernel/abi`, and `arch`, `core`, `crypto`, `drivers`, `fs`, `include`, `ipc`, `lib`, `mm`, `net`, `process`, `sys`, `syscall` -> `kernel/<same-name>`; `user -> userland/user`; `libc -> userland/libc`. This keeps existing Makefile source lists and include paths unchanged. These shims require a filesystem and Git client that support symbolic links; Linux CI does.

## Build from a fresh clone

```sh
git clone https://github.com/CRISTOP-bot/NEWOS.git
cd NEWOS
git submodule update --init kernel userland toolchain ports
make check-newpkg
make all
```

The top-level command intentionally omits `--recursive`: the default build needs no third-party checkout. GCC source is optional and shallow; the toolchain script has a release-tarball fallback. Nova and the `glxgears-newos` demo are nested modules in `ports`; initialize each only for source inspection or an explicit port/demo build. The checked-in port data is retained as compressed assets and restored automatically by `make` before GNU packaging.

## Pinned component revisions for this import

The superproject uses Git gitlinks (not branch names) to pin `NEWOS-kernel` `083b9986b8d5222b063e624f19b94487d6be88d1`, `NEWOS-userland` `f4cc9f1b7080a9e9fe1aceaf05ff1e011718eb2c`, `NEWOS-toolchain` `93745ade464ae04e5e18bc594ad130b4679ab50b`, and `NEWOS-ports` `7d49f504dd5fd4b5b0f06150c6d99ebff6f5fde4`. Nested refs are likewise explicit: GCC `6afcc4f6da931eb93f3ab001a0dd9650ea71d1ea`, `glxgears-newos` `b44f4f622d9a63888acd5b59c07c465483143aed`, Nova `d586f1bfc4687172ca097298ba3c91d6f7d40069`.

Component content was imported as source snapshots from the existing NEWOS PR branch; the original monorepo history was not rewritten into each new repository.
