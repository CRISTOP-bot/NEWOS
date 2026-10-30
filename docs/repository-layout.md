# NEWOS repository layout

The central `CRISTOP-bot/NEWOS` repository is the build/boot/ISO orchestration superproject. First-party code is separated into pinned Git submodules:

| Repository | Superproject path | Contents |
|---|---|---|
| [`NEWOS-kernel`](https://github.com/CRISTOP-bot/NEWOS-kernel) | `kernel/` | ABI, architecture, core, crypto, drivers (including the PCnet NIC), filesystems, IPC, kernel library, memory, process, system services, syscalls |
| [`NEWOS-userland`](https://github.com/CRISTOP-bot/NEWOS-userland) | `userland/` | `user/` programs/runtime/rootfs and `libc/` |
| [`NEWOS-toolchain`](https://github.com/CRISTOP-bot/NEWOS-toolchain) | `toolchain/` | Cross-toolchain scripts/docs/examples; optional shallow GCC 16.1.0 submodule |
| [`NEWOS-ports`](https://github.com/CRISTOP-bot/NEWOS-ports) | `ports/` | Port integration, staged assets, pinned nested graphics/desktop source modules |

## Preserved build paths

The superproject tracks symlinks to the matching directories in `kernel/` and `userland/`: `abi -> kernel/abi`, and `arch`, `core`, `crypto`, `drivers`, `fs`, `include`, `ipc`, `lib`, `mm`, `process`, `sys`, `syscall` -> `kernel/<same-name>`; `user -> userland/user`; `libc -> userland/libc`. This keeps existing Makefile source lists and include paths unchanged. These shims require a filesystem and Git client that support symbolic links; Linux CI does.

## Build from a fresh clone

```sh
git clone https://github.com/CRISTOP-bot/NEWOS.git
cd NEWOS
git submodule update --init kernel userland toolchain ports
make check-newpkg
make all
```

The top-level command intentionally omits `--recursive`: the default build needs no third-party checkout. GCC source is optional and shallow; the toolchain script has a release-tarball fallback. The `glxgears-newos` demo is a nested module in `ports`; initialize it only for source inspection or an explicit demo build. The checked-in port data is retained as compressed assets and restored automatically by `make` before GNU packaging.

## Pinned component revisions for this import

The superproject uses Git gitlinks (not branch names) to pin `NEWOS-kernel` `711b9097ab989c67dbe3961147b2ce1d19802bf4`, `NEWOS-userland` `3d5c36b567979507db291fda0835546cea508894`, `NEWOS-toolchain` `93745ade464ae04e5e18bc594ad130b4679ab50b`, and `NEWOS-ports` `9514ef27092ae40c1966e9ea39c2195233614ef5`. Nested refs are likewise explicit: GCC `6afcc4f6da931eb93f3ab001a0dd9650ea71d1ea`, `glxgears-newos` `5a890511567cb8dc3da5bed7c409aeb51f7653d3`, TinyGL `c2e48591a6bfba1a85f1b87a78dcf1abf7dff57a`.

Component content was imported as source snapshots from the existing NEWOS PR branch; the original monorepo history was not rewritten into each new repository.
