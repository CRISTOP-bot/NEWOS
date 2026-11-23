# Application SDK

The host-side SDK for writing NEWOS programs lives in [`sdk/`](../sdk/). It is
part of the superproject (not a submodule) and is built from the same headers,
libraries and linker script as the in-tree `/bin` tools.

```sh
make -C sdk check     # generate sysroot/ and build all examples
make sdk-examples     # same, from the repo root
```

- [`sdk/README.md`](../sdk/README.md) — overview, quickstart and limitations.
- [`sdk/docs/tutorial.md`](../sdk/docs/tutorial.md) — write → build → run →
  package, step by step.
- [`sdk/docs/api.md`](../sdk/docs/api.md) — libc and syscall API map.
- [`sdk/docs/packaging.md`](../sdk/docs/packaging.md) — `.new` packages.
- [`sdk/bin/newos-cc`](../sdk/bin/newos-cc) — compiler driver.
- [`sdk/template/`](../sdk/template/) — copyable standalone application.

The SDK does not replace the in-tree build of `/bin`: programs that must ship
inside the image are still registered in `PROG_NAMES` and `initramfs.c` (see
`AGENTS.md`). Use the SDK for applications developed outside the OS tree and
distributed as `.new` packages.
