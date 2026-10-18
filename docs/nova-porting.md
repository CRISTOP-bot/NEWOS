# Nova on NEWOS: compatibility and port status

Nova is the selected upstream desktop/compositor source for a future NEWOS graphical environment. Its source is pinned in `NEWOS-ports` as `ports/nova` at `d586f1bfc4687172ca097298ba3c91d6f7d40069`. This is source integration only; no claim is made that the compositor or its applications currently compile or run on NEWOS.

## Upstream inspection

- Repository: [`BoredOS/nova`](https://github.com/BoredOS/nova), branch `main`, pinned commit above.
- License: GNU GPL version 3.0 (upstream `LICENSE`, 35,225 bytes). Preserve the license and copyright notices if any files are copied or adapted; distributing modified binaries requires complying with GPLv3 source and license obligations. The NEWOS modules are MIT-licensed; the upstream GPL component remains separate and must not be treated as relicensed.
- Actual source layout at the pinned commit includes `src/nova.c` (compositor), `src/taskbar.c`, `src/wallpaperd.c`, apps, `ntk/` (widget toolkit), and `libnovaproto/` (client/compositor protocol). The README refers to `libui/` and `libtheme/`, but those directories are absent at this commit; it also describes a BoredOS SDK build and an automatic `boredos/libc` bootstrap. The actual Makefile sets `CC=x86_64-boredos-gcc`, `AR=x86_64-boredos-ar`, uses a BoredOS SDK, and links with `/usr/lib/ld.so` and `-lm`.

## Reusable pieces versus OS-bound code

The `ntk` painting, color, gradient, pixmap, layout, and widget/control code is the most plausible reusable layer. It uses standard C types and can be evaluated for a port behind NEWOS-native rendering and file/font adapters. Theme/config/assets and app manifest data are also useful references. This is a candidate reuse boundary, not a completed import or a verified build. `ntk_window.c`, `ntk_nova.c`, `ntk_app.c`, and `libnovaproto/novaproto.c` include transport, shared-memory, display, or event-loop responsibilities and cannot be taken unchanged.

The compositor is tightly coupled to its target environment: it opens `/dev/fb0`; issues Linux fbdev `FBIOGET_VSCREENINFO`, `FBIOGET_FSCREENINFO`, `FBIOPAN_DISPLAY`, and `FBIOSET_DIRTY`; requests `KDSETMODE` for a Linux virtual terminal; reads `/dev/keyboard` and `/dev/mouse`; binds an `AF_UNIX` stream socket at `/tmp/nova.sock`; exchanges mapped surfaces through `/dev/shm`; uses `poll`, `mmap`, process/signal/pipe calls, and pthread declarations; and loads configuration/fonts/assets from BoredOS `/Library` paths. Those are platform interfaces, not portable widget code.

## NEWOS ABI comparison and remaining work

NEWOS has a Limine framebuffer and an explicit user ABI (`SYS_FBINFO`/`SYS_FBWRITE`) for querying geometry and blitting XRGB8888 pixels; it does not expose the Linux fbdev ioctl/KDSETMODE ABI Nova uses. NEWOS currently implements a limited `mmap` path, but that is not by itself a shared-memory object implementation. The current socket implementation is IPv4-specific; AF_UNIX stream sockets and a compositor protocol transport are not present. NEWOS has no pthread/threaded userspace ABI, and the POSIX `poll` compatibility path has no event readiness engine. The syscall source also marks process plumbing such as `execve` incomplete; Nova's app launching cannot be assumed to work. NEWOS does not provide Nova's BoredOS SDK/compiler or its `/usr/lib/ld.so` dynamic-linker environment.

A genuine port therefore still needs: (1) a NEWOS-targeted build/SDK recipe and compatible libc/math/compiler support; (2) a framebuffer adapter that uses `SYS_FBINFO`/`SYS_FBWRITE` or a deliberately designed NEWOS device ABI; (3) input-device APIs and pointer/keyboard event integration; (4) local IPC and surface-buffer sharing (implement an appropriate NEWOS mechanism or redesign the protocol); (5) event readiness, process launch, and any synchronization/threading semantics needed by chosen Nova features; (6) NEWOS paths/configuration/font/asset installation; (7) license-compliant packaging; and (8) real compilation, self-tests, and QEMU/interactive framebuffer validation. No such adapter has been implemented in this repository split.

The existing `user/programs/desktop` remains NEWOS's current built-in desktop. Nova is the intended replacement direction, but is not active yet. `glxgears-newos` remains available as an optional software-rendered graphics app/demo only; it is not selected as the OS graphical environment and is not GLX/X11.
