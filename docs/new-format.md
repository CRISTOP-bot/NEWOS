# `.new`: the native NEWOS package format (spec v1)

`.new` is NEWOS's native package container: deterministic, verifiable, and
streamable with 512-byte buffers (NEWOS has no `lseek`, so the installer
consumes a package strictly front to back). It is **not** a renamed tar:
the header binds typed section lengths to per-section CRC32 digests, and
the manifest binds every payload byte to a destination path, size, and
per-file CRC32.

```
[ header 48 bytes ][ metadata ][ manifest ][ payload ]
```

File name convention: `<name>-<version>-<arch>.new`
(e.g. `hello-new-1.0.0-x86_64.new`).

## Header (48 bytes, all integers little-endian)

| Off | Size | Field        | Value (v1)                              |
| --- | ---- | ------------ | --------------------------------------- |
| 0   | 4    | magic        | `NEW1` (`4E 45 57 31`)                  |
| 4   | 2    | spec_version | `1`                                     |
| 6   | 2    | flags        | `0` (bit 0 reserved for compression)    |
| 8   | 4    | meta_len     | 1 .. 4096                               |
| 12  | 4    | manifest_len | 1 .. 16384                              |
| 16  | 4    | payload_len  | 0 .. 8 MiB (sum of sections <= 8 MiB)   |
| 20  | 4    | header_crc   | CRC32 of bytes 0..19 + 24..47           |
| 24  | 4    | meta_crc     | CRC32 of the metadata section           |
| 28  | 4    | manifest_crc | CRC32 of the manifest section           |
| 32  | 4    | payload_crc  | CRC32 of the payload section            |
| 36  | 4    | file_count   | 1 .. 256 (== manifest line count)       |
| 40  | 8    | reserved     | zero                                    |

CRC32 is IEEE (poly `0xEDB88320`, init/xor `0xFFFFFFFF`); see
`user/programs/newpkg/newpkg_format.{h,c}` (`newpkg_crc32*`).

## Metadata (`key: value\n`, UTF-8, LF only, keys sorted)

Required keys: `name`, `version`, `arch`, `desc`, `license`,
`maintainer`, `build`, `newos-min`. Optional: `depends` (empty or a comma
separated list, e.g. `libfoo (>= 1.2), bar`). A package missing any
required key is rejected.

- `arch`: `x86_64` (exact match against `uname -m`) or `any` for
  data-only packages.
- `newos-min`: minimum NEWOS release; compared numerically so the running
  `0.2.0-pre-alpha` satisfies `0.2.0` (any `-suffix` is ignored).
- `depends` entries: `name` (any version) or `name (op ver)` with
  `op` in `= == >= <= > <`.

## Manifest (one line per file, sorted by path, duplicates rejected)

```
<octmode> <size-dec> <crc32-hex8> <abspath>\n
```

Example: `0755 1328 1d8b791c /bin/hello-new`. Size caps at 2 MiB per
file. Both the builder and the installer enforce strict ascending order,
so identical inputs always produce identical bytes.

## Payload

Raw file bytes concatenated in manifest order, no compression in v1
(`flags` must be 0; any nonzero flag is rejected today and reserves room
for a future compressor and signature blocks).

## Path policy (enforced by builder AND installer)

Absolute paths only, 2..255 chars, charset `[A-Za-z0-9._+~@%=-/]`, no
`//`, no trailing `/`, no `.`/`..` components. Destinations must sit
under `/bin/ /sbin/ /lib/ /etc/ /usr/ /opt/ /var/ /home/ /root/ /tmp/`.
Kernel-managed trees (`/dev /proc /sys`, `/init`, bare `/`) are rejected.
This closes path traversal at both ends: a malicious `.new` cannot escape
its allowlist even if it was crafted by hand.

## Verification order (installer)

magic/version/flags/reserved -> header CRC -> section lengths vs file
size -> metadata/manifest CRCs -> required keys -> manifest parse (order,
paths, sizes) -> arch gate -> NEWOS version gate -> dependencies
(missing/incompatible/circular) -> conflict scan -> streamed
extract+per-file CRC -> payload CRC -> database registration. Any failure
aborts before the next stage, and extraction failures roll back every
file the run touched.

## Future extensions (reserved, not implemented)

- `flags` bit 0: compressed payload (whole-section algorithm TBD).
- Appended signature block + `repo/` index/signatures (see
  `docs/packages.md`); the header layout leaves room via `reserved` and a
  spec version bump (`spec_version` 2), never by silent reinterpretation.
