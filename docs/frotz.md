# `frotz`: Z-machine interpreter for NEWOS

`/bin/frotz` (`userland/user/programs/frotz/`) plays Infocom-era Z-code
story files on the NEWOS console. It is a native port of upstream Frotz
2.44 (<https://github.com/DavidGriffith/frotz>, GPL-2+, originally by
Stefan Jokisch; the dumb terminal driver used here is by Alva/Alembic
Petrofsky). The port keeps the Z-machine core untouched and reimplements
only the platform seams; per-file deltas are listed in
`userland/user/programs/frotz/README`.

## Usage

```sh
frotz /usr/share/zcode/mini.z3     # bundled demo story (1024 bytes)
frotz /tmp/zork1.z3                # any user-supplied story file
```

The dumb driver renders the Z-machine text stream as plain console
output with a frotz-drawn status line and reads input line by line,
exactly like upstream `dfrotz` piped to a teletype. Interpreter options
match Frotz itself (run `frotz` with no arguments for the list), and the
runtime backslash settings (`\sf`, `\ln`, `\mp`, ...) keep working —
type a backslash directly when you want the driver to treat the line as
a command.

## Story files

Story files are plain files; NEWOS has no network stack, so nothing but
the generated demo story is embedded in the ISO. Players fetch stories
on the host (freely licensed games and preserved Infocom files live in
the interactive fiction archive, <https://ifarchive.org/>) and move them
into the OS offline: release embeds (the `MINI_Z3_SRC` pattern in the
root `Makefile` shows how), `newpkg` packages staged to `/tmp`, or a
QEMU disk image containing the `.z3` file.

## Demo story

`tools/frotz/tests/mini.z3` is a minimal but valid Z-machine v3 story
(1024 bytes) generated deterministically by
`tools/frotz/tests/gen_mini_z3.py`. It prints a banner, reads one line,
prints `read ok` and quits — enough to exercise header decode, the main
loop, `print`/`new_line`, one line read (V3 status line included), and
`quit`. Opcode numbers were cross-checked against the ported
`process.c` dispatch tables (0xB2 print, 0xBB new_line, 0xBA quit,
0xE4 read; big-endian header words per the Z-machine standard).

## Testing

- `make check-frotz` — host functional test (`tools/frotz/`): compiles
  the same translation units against the host libc and drives
  `mini.z3` (banner, read path, status line, byte-deterministic
  regeneration). No QEMU needed; runs in seconds.
- QEMU end-to-end — boot `build/images/newos-x86_64.elf` with a serial
  console, run the demo story interactively from `nsh`.

## Build integration

The root `Makefile` compiles the port under `FROTZ_CFLAGS`
(`-std=gnu99 -fcommon`, scoped `-Wno-error` for upstream's exact-6-byte
serial records), links it like the other libc programs (libc.a + nshlib +
start + app.ld)
and embeds the binary via the generic `/bin` embed path. The initramfs
maps `/bin/frotz` (bin table) and installs `mini.z3` at
`/usr/share/zcode/mini.z3`.
