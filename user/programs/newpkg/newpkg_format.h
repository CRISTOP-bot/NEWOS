/* newpkg_format.h: native NEWOS `.new` package format (spec v1).
 *
 * A `.new` file is a deterministic, verifiable container (NOT a renamed
 * tar: the header carries typed section lengths plus per-section CRC32 and
 * the manifest binds every payload byte to a path, size and CRC):
 *
 *   [ header 48 bytes ][ metadata ][ manifest ][ payload ]
 *
 * - header: magic "NEW1", spec version, flags (0 in v1), section lengths,
 *   CRC32 of each section, file count. All integers little-endian.
 * - metadata: UTF-8 `key: value\n` lines, keys sorted, LF only. Required
 *   keys: name, version, arch, desc, license, maintainer, build,
 *   newos-min. Optional: depends (comma separated, may be empty).
 * - manifest: one line per payload file, sorted by path:
 *   `<octmode> <size-dec> <crc32-hex8> <abspath>\n`
 *   e.g. `0755 4128 9a3f01c2 /bin/hello-new`
 * - payload: raw file bytes concatenated in manifest order, no compression
 *   in v1 (flags bit 0 reserved for a future compressor; must be 0 now).
 *
 * Portable: no syscalls, no libc, no FPU. The includer provides the integer
 * types: on NEWOS include "../../lib/nshlib.h" first (u8..u64, size_t); on
 * the host compile with -DNEWPKG_HOST (uses <stdint.h>/<stddef.h>).
 */

#ifndef NEWPKG_FORMAT_H
#define NEWPKG_FORMAT_H

#ifdef NEWPKG_HOST
#include <stdint.h>
#include <stddef.h>
typedef uint8_t  newpkg_u8;
typedef uint16_t newpkg_u16;
typedef uint32_t newpkg_u32;
typedef uint64_t newpkg_u64;
typedef size_t   newpkg_usize;
#else
/* NEWOS userland: integer types come from the shared userland library. */
#include "../../lib/nshlib.h"
typedef u8  newpkg_u8;
typedef u16 newpkg_u16;
typedef u32 newpkg_u32;
typedef u64 newpkg_u64;
typedef size_t newpkg_usize;
#endif

#define NEWPKG_MAGIC0 'N'
#define NEWPKG_MAGIC1 'E'
#define NEWPKG_MAGIC2 'W'
#define NEWPKG_MAGIC3 '1'

#define NEWPKG_SPEC_VERSION 1
#define NEWPKG_HEADER_SIZE  48

/* Hard limits (v1): a package violating any of them is rejected. */
#define NEWPKG_META_MAX     4096
#define NEWPKG_MANIFEST_MAX 65536
#define NEWPKG_PATH_MAX     256
#define NEWPKG_NAME_MAX     64
#define NEWPKG_VER_MAX      32
#define NEWPKG_FILES_MAX    1024
/* Sized for a real distribution package: GNU coreutils stages as ~10 MiB of
 * static musl ELFs in one archive, and the whole thing has to survive
 * header parse plus per-file extraction on a board with no swap. */
#define NEWPKG_FILE_MAX     (8u * 1024u * 1024u)
#define NEWPKG_PKG_MAX      (32u * 1024u * 1024u)

/* Decoded header (host-order integers). */
struct newpkg_header {
    newpkg_u16 spec_version;
    newpkg_u16 flags;
    newpkg_u32 meta_len;
    newpkg_u32 manifest_len;
    newpkg_u32 payload_len;
    newpkg_u32 header_crc;
    newpkg_u32 meta_crc;
    newpkg_u32 manifest_crc;
    newpkg_u32 payload_crc;
    newpkg_u32 file_count;
};

/* IEEE CRC32 (poly 0xEDB88320, init 0xFFFFFFFF, xorout 0xFFFFFFFF). */
newpkg_u32 newpkg_crc32(const newpkg_u8 *data, newpkg_usize len);
/* Incremental form for streaming (no lseek/seek on NEWOS: the installer
 * verifies payload bytes as they arrive in 512-byte chunks):
 *   c = newpkg_crc32_begin();
 *   c = newpkg_crc32_update(c, chunk, n); ...
 *   final = newpkg_crc32_end(c);   // == newpkg_crc32(whole)
 */
newpkg_u32 newpkg_crc32_begin(void);
newpkg_u32 newpkg_crc32_update(newpkg_u32 crc, const newpkg_u8 *data,
                              newpkg_usize len);
newpkg_u32 newpkg_crc32_end(newpkg_u32 crc);

/* Little-endian codecs (byte-wise, no alignment assumptions). */
newpkg_u16 newpkg_get_le16(const newpkg_u8 *p);
newpkg_u32 newpkg_get_le32(const newpkg_u8 *p);
void newpkg_put_le16(newpkg_u8 *p, newpkg_u16 v);
void newpkg_put_le32(newpkg_u8 *p, newpkg_u32 v);

/* Serialize `h` into 48 header bytes (header_crc field computed over the
 * other 44 bytes with itself zeroed). */
void newpkg_header_encode(const struct newpkg_header *h, newpkg_u8 out[48]);

/* Parse 48 header bytes: checks magic, spec version (== 1), flags (== 0),
 * internal consistency (section caps, file count) and the header CRC.
 * Returns 0 on success, -1 on any mismatch. */
int newpkg_header_decode(const newpkg_u8 hdr[48], struct newpkg_header *out);

/* Verify a full in-memory package: header decode + total length match +
 * per-section CRC32 + manifest line count == file_count. Returns 0 if the
 * container is sound (payload FILE bytes are NOT checked here; use
 * newpkg_manifest_parse_line per entry while streaming). */
int newpkg_header_verify(const newpkg_u8 *pkg, newpkg_usize pkg_len,
                         struct newpkg_header *out);

/* Look up `key` in a metadata block (`meta_len` bytes, `key: value\n`
 * lines). Copies the value (trimmed of trailing CR/space) into `out`
 * (NUL-terminated, at most out_cap-1 chars). Returns 0 if found, -1 if
 * the key is absent or the value does not fit. */
int newpkg_meta_get(const newpkg_u8 *meta, newpkg_u32 meta_len,
                    const char *key, char *out, newpkg_usize out_cap);

/* Parse one manifest line (NUL-terminated by the caller):
 * `<octmode> <size> <crc8hex> <abspath>`. Validates the mode digits, the
 * size bound and the path (see newpkg_path_valid). Returns 0 on success. */
int newpkg_manifest_parse_line(const char *line, char *path_out,
                               newpkg_u32 *size_out, newpkg_u32 *crc_out,
                               newpkg_u32 *mode_out);

/* Path policy (v1): absolute, 1..256 chars, charset [A-Za-z0-9._+~@%=-]
 * plus '/', no `//`, no trailing `/` (except root, which is never a file),
 * no `.`/`..` components, and must sit under an allowlisted prefix:
 * /bin/ /sbin/ /lib/ /etc/ /usr/ /opt/ /var/ /home/ /root/ /tmp/.
 * Kernel-managed trees (/dev /proc /sys /init and bare /) are rejected.
 * Returns 0 if the path may hold package payload, -1 otherwise. */
int newpkg_path_valid(const char *path);

/* Numeric-prefix dotted version compare ("1.10.0" > "1.2.0"; any `-suffix`
 * is ignored so "0.2.0-pre-alpha" == "0.2.0"). Returns -1/0/1. */
int newpkg_vercmp(const char *a, const char *b);

/* Dependency constraint check: does `installed` satisfy `op want`?
 * op is one of "", "=", "==", ">=", "<=", ">", "<" ("" means any version).
 * Returns 1 if satisfied, 0 if not, -1 on malformed input. */
int newpkg_dep_match(const char *installed, const char *op, const char *want);

/* Parse entry `idx` (0-based) of a comma-separated depends list
 * (`"libfoo (>= 1.2), bar"`). Writes name/op/ver (op "" when bare).
 * Returns 0 on success, -1 when idx is out of range or malformed. */
int newpkg_dep_parse(const char *list, int idx, char *name_out,
                     char *op_out, char *ver_out);

/* Architecture gate: exact match, or pkg_arch "any" (data-only packages).
 * Returns 0 if install may proceed, -1 otherwise. */
int newpkg_arch_ok(const char *pkg_arch, const char *machine);

/* Package file name convention: `<name>-<version>-<arch>.new`.
 * Builds it into `out` (cap >= 128). Returns 0 on success. */
int newpkg_file_name(const char *name, const char *version, const char *arch,
                     char *out, newpkg_usize out_cap);

#endif
