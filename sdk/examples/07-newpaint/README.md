# NewPaint

A real framebuffer application built with the NEWOS SDK — no kernel changes,
no built-in `/bin` registration, just `newos-cc` and `libnsh.a`.

## Build

```sh
make -C sdk check                       # builds sdk/build/07-newpaint.elf
sdk/bin/newos-package sdk/build/07-newpaint.elf \
    --name newpaint --version 1.0.0 \
    --desc "Framebuffer painting app for NEWOS"
```

## Run

NewPaint needs a 32-bpp framebuffer, which only the Limine boot path provides:

```sh
make CROSS_PREFIX= LIMINE_CMDLINE=gui=1 build/images/newos-x86_64-limine.iso
make qemu-limine
```

Install the `.new` in the guest and start it:

```
newpkg install /tmp/newpaint-1.0.0-x86_64.new
newpaint
```

On a PVH/GRUB boot it prints a clear message and exits with status 1 instead
of drawing into nothing.

## Controls

| Input | Action |
| --- | --- |
| Left mouse button | paint with the selected colour |
| Right mouse button | erase |
| Mouse wheel | cycle through the 8 palette colours |
| `1`–`8` | select a palette colour |
| `[` / `]` | shrink / grow the brush (1–12 px) |
| `c` | clear the canvas |
| `q` or `Esc` | quit and release the panel/keyboard |

## How it works

- `SYS_FBINFO` locates the panel; the 320×200 canvas plus a 32-px palette
  strip is centred in it.
- `SYS_CONSOLE_GUI(1)` suspends fbcon text and the kernel's legacy cursor;
  teardown releases the claim.
- `SYS_MOUSE_GET` yields device pixels and button bits (bit 0 left, bit 1
  right). The app keeps its own cursor and restores the pixels underneath it
  on every move.
- `SYS_KBD_GRAB(1)` routes decoded ASCII bytes to `SYS_KBD_READ`, which is
  non-blocking; the loop polls it and calls `SYS_SLEEP` to yield.
- Dirty rows are blitted with `SYS_FBWRITE`. Because the kernel reads supplied
  pixels with a stride of `rq.w`, the app sends full canvas rows and varies
  only the row range.
- The backbuffer (~300 KiB) is static BSS, so it does not touch libc's 256 KiB
  malloc arena.

The serial console stays authoritative for the startup banner and exit
message, which makes headless verification straightforward.
