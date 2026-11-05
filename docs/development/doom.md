# `doom`: DOOM 1.10 engine port for NEWOS

`/bin/doom` (`user/programs/doom/`) runs the released DOOM 1.10 engine
(id Software, GPL-2.0; source release `linuxdoom-1.10`) on the NEWOS console.
The port is fork-for-fork upstream plus a hand-written NEWOS layer: the engine
keeps its own translation units and every platform seam lives in the four `I_`
files (`i_video`, `i_sound`, `i_system`, `i_net`) and in the `doom_main.c`
toolbox entry that replaces upstream's `i_main.c`.

## Usage

```sh
/bin/doom                           # shareware IWAD via the built-in search
/bin/doom -iwad /tmp/mydoom.wad    # explicit IWAD
DOOMWADDIR=/tmp /bin/doom          # redirect the IWAD search directory
/bin/doom -config /tmp/doom.cfg    # config file
/bin/doom -nomouse                 # no mouse look
/bin/doom -scale 2                 # pixels per game pixel (1..4)
/bin/doom -timedemo /tmp/demo.dm  # demo playback
```

- The shareware IWAD ships inside the system image and is installed at
  `/usr/share/doom/doom1.wad`; that is the first entry of the engine's IWAD
  search (`DOOMWADDIR`, default `/usr/share/doom`; `HOME` defaults to `/tmp`,
  so the engine's `~/.doomrc` lands in the writable tmpfs).
- `IdentifyVersion()` picks the game mode (shareware / registered /
  commercial) from the IWAD it found and the startup banner reports it.

## Rendering

`I_InitGraphics()` asks the kernel through `SYS_FBINFO`, takes the panel from
the framebuffer console (`SYS_CONSOLE_GUI`) and blits the 320x200 frame
through `SYS_FBWRITE`, scaled by `-scale` (2x game pixels by default, max 4).
There is no double buffer and no retrace wait: the panel is uncacheable
memory, so tearing is inherent (the same trade-off upstream took with the
X11 SHM path). When no panel is present (`SYS_FBINFO` fails or the panel is
smaller than 320x200) the run goes headless: frames are produced into the game
buffer and the console is left untouched.

## Input

Keyboard arrives through `SYS_KBD_READ` (decoded bytes; the arrow keys come
as ANSI escapes) and the mouse through `SYS_MOUSE_GET` (absolute coordinates,
sequenced per packet). Because NEWOS hands over decoded bytes, bare modifier
scancodes never reach userland; the fire/strafe/speed actions were rebound in
the port to the left-hand home column (`q` / `a` / `z`, use is `Space`) — see
`patches/0004-defaults-table-and-key-bindings.patch`.

## Sound

NEWOS has no DMX, no OSS, no PC speaker and no PCM path reachable from
userland (the HDA driver is kernel-internal; there is no `/dev/dsp` and no
audio syscall). `I_InitSound()` reports the absent device, `I_StartSound()`
returns "no channel" and music lumps register but never play (there is no OPL3
synthesiser). Volumes and song state are still tracked exactly as the engine
expects, so wiring a real backend later means touching `I_SubmitSound()` plus a
PCM syscall — nothing in the engine changes.

## Network

`-net` is refused explicitly (`I_InitNetwork()`): NEWOS has no IP stack (PCnet
loopback only), so the port runs single player (`-ticdup N` sets the tic
duplication).

## Build integration

`user/programs/doom/` holds one fork per upstream translation unit plus the
NEWOS `I_` layers and the toolbox entry. The root `Makefile` compiles them
under the port-specific flags (`-std=gnu99 -fcommon -Wno-error`, dropping
`-mgeneral-regs-only` — the engine needs SSE `atan`/`FixedDiv`, like the
TinyGL and frotz ports — and `-DLINUX -DNORMALUNIX`, upstream's own pair), and
links the result through the ordinary `/bin` embed path. `initramfs` maps
`/bin/doom` in the bin table and installs the shareware IWAD at
`/usr/share/doom/doom1.wad`.

## Provenance and patches

The upstream release is `data/linuxdoom-1.10.tar.gz` (id Software, GPL-2.0,
sha256 in `data/SHA256SUMS`); `patches/` holds the four NEWOS patches that turn
it into the port and the forks under `user/programs/doom/` reproduce them byte
for byte (`sha256sum -c ../data/SHA256SUMS`, then `patch -p1` in order).

| Patch | Why |
|-------|-----|
| `0001-compile-as-gnu99-on-newos-libc.patch` | gnu89 implicit-int storage classes rejected by `-std=gnu99`; `SNDSERV` off (no sndserver process exists). |
| `0002-32-bit-assumptions-in-wad-loading.patch` | on-disk pointer-typed fields (32→64 bit shift) and `Z_Malloc(count*4)` slot arrays sized for 4-byte pointers. |
| `0003-null-terminate-sprnames.patch` | sprite-name table walk needs a `NULL`. |
| `0004-defaults-table-and-key-bindings.patch` | `int` slot truncates `void*`/`long` defaults; `scanf` family absent; CTRL/ALT/SHIFT scancodes unreachable via decoded-byte keyboard. |

## Known limits

- Sound and network are honest stubs (see above); the framebuffer run is the
  real deal.
- 64-bit hazards left alone on purpose: the `(int)` pointer casts in `r_data.c`,
  `z_zone.c` and `p_saveg.c` — the brk heap stays in the low 2 GiB and userland
  links at `0x400000`, so they cannot lose a bit in this port.

## Testing

- `make all` builds the port end to end (cross toolchain → engine objects →
  `/bin/doom` → embed → IWAD blob).
- Boot NEWOS and run `/bin/doom` from the shell; with a Limine framebuffer it
  renders on the panel, without one it runs headless.
