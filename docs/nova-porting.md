# Nova on NEWOS: compatibility and port status

Nova is the selected upstream desktop/compositor source for a future NEWOS
graphical environment. Its source is pinned in `NEWOS-ports` as `ports/nova` at
`d586f1bfc4687172ca097298ba3c91d6f7d40069`. NEWOS now carries a real but
deliberately narrow **alpha** of the Nova NTK layer and its own damage-based
compositor. It is not the upstream Nova compositor, window manager, protocol or
application suite.

## Upstream inspection

- Repository: [`BoredOS/nova`](https://github.com/BoredOS/nova), branch `main`,
  pinned commit above.
- License: GNU GPL version 3.0 (upstream `LICENSE`, 35,225 bytes). Preserve the
  license and copyright notices if any files are copied or adapted; distributing
  modified binaries requires complying with GPLv3 source and license obligations.
  The NEWOS modules are MIT-licensed; the upstream GPL component remains separate
  and must not be treated as relicensed.
- Actual source layout at the pinned commit includes `src/nova.c` (compositor),
  `src/taskbar.c`, `src/wallpaperd.c`, apps, `ntk/` (widget toolkit), and
  `libnovaproto/` (client/compositor protocol). The README refers to `libui/` and
  `libtheme/`, but those directories are absent at this commit; it also describes
  a BoredOS SDK build and an automatic `boredos/libc` bootstrap. The actual
  Makefile sets `CC=x86_64-boredos-gcc`, `AR=x86_64-boredos-ar`, uses a BoredOS
  SDK, and links with `/usr/lib/ld.so` and `-lm`.

## What the alpha actually ports

`NEWOS-userland/user/programs/desktop/nova/` contains caller-owned,
allocation-free, integer-only adaptations of:

| Module | Status | Used by |
| --- | --- | --- |
| `ntk_color` | ported | every gradient and palette conversion |
| `ntk_gradient` | ported | title bars, panel and wallpaper |
| `ntk_painter` | ported | clipping, alpha, primitives and 8x8 text |
| `ntk_style` | ported | theme roles, metrics and widget colours |
| `ntk_progress` | ported | system-memory bar |
| `ntk_button` | ported | About buttons and keycaps |
| `ntk_scrollbar` | ported | `/bin` program list |
| `ntk_separator` | ported | About, Log, menu and help rules |

Every module is compiled into the desktop and exercised by `make check-nova`
(host color tests plus a widget test that checks painter clipping, gradients,
scrollbar geometry/paint, separator paint, progress fill and button hit tests).
The GPLv3 license and original notices are retained.

The desktop itself is split by responsibility instead of living in one
translation unit:

- `desktop_main.c` — startup, event loop, data refresh and damage scheduling.
- `desktop_state.c` — shared canvas, window state, themes and log/toast state.
- `desktop_gfx.c` — primitives, 8x8 text, formatting and the Win95/BoredOS icons.
- `desktop_wm.c` — z-order, focus, layout, maximize/minimize/close and chrome.
- `desktop_theme.c` — Nova style synchronization and theme cycling.
- `desktop_shell.c` — wallpaper, panel, dock, start menu, help and cursor.
- `desktop_apps.c` — System, Files, Mouse, About, Log and the Nova preview.
- `desktop_input.c` — keyboard, mouse and menu actions.
- `desktop_compositor.c` — dirty-rectangle union, clipped repaint and chunked
  `SYS_FBWRITE` upload.

The sixth window (`WNOVA`) is a **real, running adaptation**, not a screenshot:
it paints a Nova/NTK card with the gradient and painter modules, a live NTK
progress bar, an 8x8 font preview and **FILES / ABOUT / GEARS** buttons. Those
actions open the Files window, the About window and the built-in
`/bin/glxgears` TinyGL demo. This follows the role of upstream Nova's
`helloworld`, `about`, `explorer` and `taskbar` examples without claiming to
be their source code; the upstream widget tree and compositor are still not
running. The system panel, dock and start menu provide the taskbar/explorer
concepts, while the window manager remains NEWOS's own damage-based one.

The compositor does **not** stream a full frame on every event. Cursor motion,
hover changes, clock/memory updates and toast expiry mark only the affected
rectangles. Startup, theme changes and structural layout changes request a full
redraw. `SYS_FBWRITE` requires contiguous pixels, so the compositor copies
damaged rows through a small staging buffer and submits them in 16-row chunks.

## Reusable pieces versus OS-bound code

The compositor is tightly coupled to its target environment: it opens `/dev/fb0`;
issues Linux fbdev `FBIOGET_VSCREENINFO`, `FBIOGET_FSCREENINFO`,
`FBIOPAN_DISPLAY`, and `FBIOSET_DIRTY`; requests `KDSETMODE` for a Linux virtual
terminal; reads `/dev/keyboard` and `/dev/mouse`; binds an `AF_UNIX` stream socket
at `/tmp/nova.sock`; exchanges mapped surfaces through `/dev/shm`; uses `poll`,
`mmap`, process/signal/pipe calls, and pthread declarations; and loads
configuration/fonts/assets from BoredOS `/Library` paths. Those are platform
interfaces, not portable widget code. `ntk_window.c`, `ntk_nova.c`, `ntk_app.c`,
and `libnovaproto/novaproto.c` include transport, shared-memory, display or
event-loop responsibilities and cannot be taken unchanged.

## NEWOS ABI comparison and remaining work

NEWOS has a Limine framebuffer and an explicit user ABI (`SYS_FBINFO` /
`SYS_FBWRITE`) for querying geometry and blitting XRGB8888 pixels; it does not
expose the Linux fbdev ioctl/KDSETMODE ABI Nova uses. `SYS_CONSOLE_GUI` lets a
foreground compositor suspend fbcon and the legacy kernel cursor while it owns
the panel; process teardown releases that claim automatically. This is enough
for the built-in desktop and `/bin/glxgears`, but not for Nova's fbdev/vt
assumptions. NEWOS currently implements a
limited `mmap` path, but that is not by itself a shared-memory object
implementation. There is no socket ABI or network stack yet; AF_UNIX stream
sockets and a compositor protocol transport are not present. NEWOS has no
pthread/threaded userspace ABI, and the POSIX `poll` compatibility path has no
event readiness engine. The syscall source also marks process plumbing such as
`execve` incomplete; Nova's app launching cannot be assumed to work. NEWOS does
not provide Nova's BoredOS SDK/compiler or its `/usr/lib/ld.so` dynamic-linker
environment.

A genuine upstream Nova compositor port still needs: (1) a NEWOS-targeted
build/SDK recipe and compatible libc/math/compiler support; (2) per-window
surfaces and broader framebuffer/surface handling; (3) input-device APIs and
pointer/keyboard event integration; (4) local IPC and surface-buffer sharing
(implement an appropriate NEWOS mechanism or redesign the protocol); (5) event
readiness, process launch, and any synchronization/threading semantics needed by
chosen Nova features; (6) NEWOS paths/configuration/font/asset installation;
(7) license-compliant packaging; and (8) real compilation, self-tests, and
interactive framebuffer validation. The existing-syscall slice is not a
substitute for these interfaces.

## Alpha scope and next half

The first half is wired and tested: the NTK slice, the modular desktop, the
damage compositor and the BoredOS/Win95 visual language. The second half is the
upstream runtime integration:

1. Per-window backing surfaces and a surface lifecycle owned by the compositor
   (the current canvas is damage-tracked but still a single buffer).
2. A minimal NEWOS IPC/shared-memory mechanism and a reworked `libnovaproto`
   transport.
3. Upstream `ntk_window`/widget-tree integration and a real app host.
4. `poll`/event readiness and `execve`-based app launching.
5. Font/pixmap assets and BoredOS `/Library` path mapping.

The existing `user/programs/desktop` remains NEWOS's active built-in desktop.
Nova's upstream compositor, window manager, widget toolkit and applications are
not operational on NEWOS. `glxgears-newos` is now built into the normal image
as `/bin/glxgears` (and remains available as a standalone ELF/`.new` package);
it is a software-rendered TinyGL demo, not GLX/X11, and it is not the OS
graphical environment. Launching it from `nsh` or the desktop claims the
framebuffer console so kernel text cannot corrupt its pixels.
