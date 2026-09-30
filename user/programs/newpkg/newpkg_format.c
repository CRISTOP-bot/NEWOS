/* newpkg_format.c: portable implementation of the `.new` spec v1.
 *
 * Pure memory operations only (no syscalls, no libc, no FPU) so this file
 * compiles both for NEWOS userland (USER_CFLAGS, freestanding) and for the
 * host test driver (-DNEWPKG_HOST). All string helpers are local (nf_*)
 * to avoid depending on either nshlib or the host libc.
 */

#include "newpkg_format.h"

#ifdef NEWPKG_HOST
/* Host build: nothing else needed (only integer types + size_t). */
#else
/* NEWOS build: the includer (newpkg_main.c) pulls nshlib.h first. */
#endif

/* --- tiny local string helpers ------------------------------------------ */

static newpkg_usize nf_strlen(const char *s)
{
    newpkg_usize n = 0;
    while (s[n])
        n++;
    return n;
}

static int nf_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(newpkg_u8)*a - (int)(newpkg_u8)*b;
}

static int nf_strncmp(const char *a, const char *b, newpkg_usize n)
{
    newpkg_usize i = 0;
    for (; i < n; i++) {
        if (a[i] != b[i] || !a[i])
            return (int)(newpkg_u8)a[i] - (int)(newpkg_u8)b[i];
    }
    return 0;
}

static void nf_memcpy(newpkg_u8 *d, const newpkg_u8 *s, newpkg_usize n)
{
    newpkg_usize i = 0;
    for (; i < n; i++)
        d[i] = s[i];
}

static void nf_memset(newpkg_u8 *d, int c, newpkg_usize n)
{
    newpkg_usize i = 0;
    for (; i < n; i++)
        d[i] = (newpkg_u8)c;
}

static int nf_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

static int nf_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* --- CRC32 (IEEE, table-driven, ~1 KiB table) ---------------------------- */

static const newpkg_u32 nf_crc_tab[256] = {
    0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u,
    0x706af48fu, 0xe963a535u, 0x9e6495a3u, 0x0edb8832u, 0x79dcb8a4u,
    0xe0d5e91eu, 0x97d2d988u, 0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u,
    0x90bf1d91u, 0x1db71064u, 0x6ab020f2u, 0xf3b97148u, 0x84be41deu,
    0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u, 0x136c9856u,
    0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu, 0x14015c4fu, 0x63066cd9u,
    0xfa0f3d63u, 0x8d080df5u, 0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u,
    0xa2677172u, 0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu,
    0x35b5a8fau, 0x42b2986cu, 0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u,
    0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u, 0x26d930acu, 0x51de003au,
    0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u, 0xcfba9599u,
    0xb8bda50fu, 0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u,
    0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du, 0x76dc4190u,
    0x01db7106u, 0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu,
    0x9fbfe4a5u, 0xe8b8d433u, 0x7807c9a2u, 0x0f00f934u, 0x9609a88eu,
    0xe10e9818u, 0x7f6a0dbbu, 0x086d3d2du, 0x91646c97u, 0xe6635c01u,
    0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu, 0x6c0695edu,
    0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u, 0x65b0d9c6u, 0x12b7e950u,
    0x8bbeb8eau, 0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u,
    0xfbd44c65u, 0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u,
    0x4adfa541u, 0x3dd895d7u, 0xa4d1c46du, 0xd3d6f4fbu, 0x4369e96au,
    0x346ed9fcu, 0xad678846u, 0xda60b8d0u, 0x44042d73u, 0x33031de5u,
    0xaa0a4c5fu, 0xdd0d7cc9u, 0x5005713cu, 0x270241aau, 0xbe0b1010u,
    0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
    0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u,
    0x2eb40d81u, 0xb7bd5c3bu, 0xc0ba6cadu, 0xedb88320u, 0x9abfb3b6u,
    0x03b6e20cu, 0x74b1d29au, 0xead54739u, 0x9dd277afu, 0x04db2615u,
    0x73dc1683u, 0xe3630b12u, 0x94643b84u, 0x0d6d6a3eu, 0x7a6a5aa8u,
    0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u, 0xf00f9344u,
    0x8708a3d2u, 0x1e01f268u, 0x6906c2feu, 0xf762575du, 0x806567cbu,
    0x196c3671u, 0x6e6b06e7u, 0xfed41b76u, 0x89d32be0u, 0x10da7a5au,
    0x67dd4accu, 0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u,
    0xd6d6a3e8u, 0xa1d1937eu, 0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u,
    0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu, 0xd80d2bdau, 0xaf0a1b4cu,
    0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u, 0x316e8eefu,
    0x4669be79u, 0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u,
    0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu, 0xc5ba3bbeu,
    0xb2bd0b28u, 0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u, 0xb5d0cf31u,
    0x2cd99e8bu, 0x5bdeae1du, 0x9b64c2b0u, 0xec63f226u, 0x756aa39cu,
    0x026d930au, 0x9c0906a9u, 0xeb0e363fu, 0x72076785u, 0x05005713u,
    0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u, 0x92d28e9bu,
    0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u, 0x86d3d2d4u, 0xf1d4e242u,
    0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u,
    0x18b74777u, 0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu,
    0x8f659effu, 0xf862ae69u, 0x616bffd3u, 0x166ccf45u, 0xa00ae278u,
    0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u, 0xa7672661u, 0xd06016f7u,
    0x4969474du, 0x3e6e77dbu, 0xaed16a4au, 0xd9d65adcu, 0x40df0b66u,
    0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
    0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u,
    0xcdd70693u, 0x54de5729u, 0x23d967bfu, 0xb3667a2eu, 0xc4614ab8u,
    0x5d681b02u, 0x2a6f2b94u, 0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu,
    0x2d02ef8du
};

newpkg_u32 newpkg_crc32_begin(void)
{
    return 0xffffffffu;
}

newpkg_u32 newpkg_crc32_update(newpkg_u32 c, const newpkg_u8 *data,
                              newpkg_usize len)
{
    newpkg_usize i = 0;
    if (!data)
        return c;
    for (; i < len; i++)
        c = nf_crc_tab[(c ^ data[i]) & 0xffu] ^ (c >> 8);
    return c;
}

newpkg_u32 newpkg_crc32_end(newpkg_u32 c)
{
    return c ^ 0xffffffffu;
}

newpkg_u32 newpkg_crc32(const newpkg_u8 *data, newpkg_usize len)
{
    if (!data)
        return 0;
    return newpkg_crc32_end(
        newpkg_crc32_update(newpkg_crc32_begin(), data, len));
}

/* --- little-endian codecs ------------------------------------------------ */

newpkg_u16 newpkg_get_le16(const newpkg_u8 *p)
{
    return (newpkg_u16)((newpkg_u16)p[0] | ((newpkg_u16)p[1] << 8));
}

newpkg_u32 newpkg_get_le32(const newpkg_u8 *p)
{
    return (newpkg_u32)p[0] | ((newpkg_u32)p[1] << 8) |
           ((newpkg_u32)p[2] << 16) | ((newpkg_u32)p[3] << 24);
}

void newpkg_put_le16(newpkg_u8 *p, newpkg_u16 v)
{
    p[0] = (newpkg_u8)(v & 0xffu);
    p[1] = (newpkg_u8)((v >> 8) & 0xffu);
}

void newpkg_put_le32(newpkg_u8 *p, newpkg_u32 v)
{
    p[0] = (newpkg_u8)(v & 0xffu);
    p[1] = (newpkg_u8)((v >> 8) & 0xffu);
    p[2] = (newpkg_u8)((v >> 16) & 0xffu);
    p[3] = (newpkg_u8)((v >> 24) & 0xffu);
}

/* --- header ----------------------------------------------------------------
 *
 * Layout (offsets):
 *   0  magic[4] "NEW1"      20 header_crc (of the other 44 bytes)
 *   4  u16 spec_version      24 u32 meta_crc
 *   6  u16 flags             28 u32 manifest_crc
 *   8  u32 meta_len          32 u32 payload_crc
 *   12 u32 manifest_len      36 u32 file_count
 *   16 u32 payload_len       40 u8 reserved[8] (zero)
 */

void newpkg_header_encode(const struct newpkg_header *h, newpkg_u8 out[48])
{
    newpkg_u32 c;
    nf_memset(out, 0, 48);
    out[0] = NEWPKG_MAGIC0;
    out[1] = NEWPKG_MAGIC1;
    out[2] = NEWPKG_MAGIC2;
    out[3] = NEWPKG_MAGIC3;
    newpkg_put_le16(out + 4, h->spec_version);
    newpkg_put_le16(out + 6, h->flags);
    newpkg_put_le32(out + 8, h->meta_len);
    newpkg_put_le32(out + 12, h->manifest_len);
    newpkg_put_le32(out + 16, h->payload_len);
    newpkg_put_le32(out + 24, h->meta_crc);
    newpkg_put_le32(out + 28, h->manifest_crc);
    newpkg_put_le32(out + 32, h->payload_crc);
    newpkg_put_le32(out + 36, h->file_count);
    /* CRC over bytes 0..19 and 24..47 (crc field itself zeroed). */
    c = 0xffffffffu;
    {
        newpkg_usize i = 0;
        for (; i < 20; i++)
            c = nf_crc_tab[(c ^ out[i]) & 0xffu] ^ (c >> 8);
        for (i = 24; i < 48; i++)
            c = nf_crc_tab[(c ^ out[i]) & 0xffu] ^ (c >> 8);
    }
    newpkg_put_le32(out + 20, c ^ 0xffffffffu);
}

int newpkg_header_decode(const newpkg_u8 hdr[48],
                         struct newpkg_header *out)
{
    newpkg_u8 tmp[48];
    newpkg_u32 want, got;
    newpkg_usize i;
    newpkg_u32 c;

    if (!hdr || !out)
        return -1;
    if (hdr[0] != NEWPKG_MAGIC0 || hdr[1] != NEWPKG_MAGIC1 ||
        hdr[2] != NEWPKG_MAGIC2 || hdr[3] != NEWPKG_MAGIC3)
        return -1;

    out->spec_version = newpkg_get_le16(hdr + 4);
    out->flags = newpkg_get_le16(hdr + 6);
    out->meta_len = newpkg_get_le32(hdr + 8);
    out->manifest_len = newpkg_get_le32(hdr + 12);
    out->payload_len = newpkg_get_le32(hdr + 16);
    out->header_crc = newpkg_get_le32(hdr + 20);
    out->meta_crc = newpkg_get_le32(hdr + 24);
    out->manifest_crc = newpkg_get_le32(hdr + 28);
    out->payload_crc = newpkg_get_le32(hdr + 32);
    out->file_count = newpkg_get_le32(hdr + 36);

    if (out->spec_version != NEWPKG_SPEC_VERSION)
        return -1;
    if (out->flags != 0)
        return -1;
    for (i = 40; i < 48; i++) {
        if (hdr[i] != 0)
            return -1;
    }
    if (out->meta_len == 0 || out->meta_len > NEWPKG_META_MAX)
        return -1;
    if (out->manifest_len == 0 || out->manifest_len > NEWPKG_MANIFEST_MAX)
        return -1;
    if (out->payload_len > NEWPKG_PKG_MAX)
        return -1;
    if (out->file_count == 0 || out->file_count > NEWPKG_FILES_MAX)
        return -1;
    if ((newpkg_u64)out->meta_len + out->manifest_len + out->payload_len >
        NEWPKG_PKG_MAX)
        return -1;

    nf_memcpy(tmp, hdr, 48);
    tmp[20] = 0;
    tmp[21] = 0;
    tmp[22] = 0;
    tmp[23] = 0;
    c = 0xffffffffu;
    for (i = 0; i < 20; i++)
        c = nf_crc_tab[(c ^ tmp[i]) & 0xffu] ^ (c >> 8);
    for (i = 24; i < 48; i++)
        c = nf_crc_tab[(c ^ tmp[i]) & 0xffu] ^ (c >> 8);
    want = out->header_crc;
    got = c ^ 0xffffffffu;
    if (want != got)
        return -1;
    return 0;
}

int newpkg_header_verify(const newpkg_u8 *pkg, newpkg_usize pkg_len,
                         struct newpkg_header *out)
{
    struct newpkg_header h;
    newpkg_usize total;
    const newpkg_u8 *meta, *manifest, *payload;
    newpkg_u32 lines = 0;
    newpkg_usize i = 0;

    if (!pkg || pkg_len < NEWPKG_HEADER_SIZE)
        return -1;
    if (newpkg_header_decode(pkg, &h) != 0)
        return -1;
    total = (newpkg_usize)NEWPKG_HEADER_SIZE + h.meta_len + h.manifest_len +
            h.payload_len;
    if (total != pkg_len)
        return -1;

    meta = pkg + NEWPKG_HEADER_SIZE;
    manifest = meta + h.meta_len;
    payload = manifest + h.manifest_len;

    if (newpkg_crc32(meta, h.meta_len) != h.meta_crc)
        return -1;
    if (newpkg_crc32(manifest, h.manifest_len) != h.manifest_crc)
        return -1;
    if (h.payload_len > 0) {
        if (newpkg_crc32(payload, h.payload_len) != h.payload_crc)
            return -1;
    } else if (h.payload_crc != newpkg_crc32((const newpkg_u8 *)"", 0)) {
        return -1;
    }

    /* Manifest must hold exactly file_count newline-terminated lines and
     * end with a newline (deterministic builder output). */
    if (h.manifest_len == 0 ||
        manifest[h.manifest_len - 1] != '\n')
        return -1;
    for (; i < h.manifest_len; i++) {
        if (manifest[i] == '\n')
            lines++;
    }
    if (lines != h.file_count)
        return -1;

    if (out)
        *out = h;
    return 0;
}

/* --- metadata lookup ------------------------------------------------------- */

int newpkg_meta_get(const newpkg_u8 *meta, newpkg_u32 meta_len,
                    const char *key, char *out, newpkg_usize out_cap)
{
    newpkg_u32 pos = 0;
    newpkg_usize klen;

    if (!meta || !key || !out || out_cap == 0)
        return -1;
    klen = nf_strlen(key);
    if (klen == 0)
        return -1;

    while (pos < meta_len) {
        newpkg_u32 line_start = pos;
        newpkg_u32 line_end = pos;
        newpkg_u32 ci, vi, vend, olen = 0;
        int ki = 0;

        while (line_end < meta_len && meta[line_end] != '\n')
            line_end++;
        /* Compare "key:" at line start. */
        while (ki < (int)klen && line_start + (newpkg_u32)ki < line_end &&
               meta[line_start + (newpkg_u32)ki] == (newpkg_u8)key[ki])
            ki++;
        if (ki == (int)klen && line_start + (newpkg_u32)ki < line_end &&
            meta[line_start + (newpkg_u32)ki] == ':') {
            ci = line_start + (newpkg_u32)klen + 1;
            while (ci < line_end &&
                   (meta[ci] == ' ' || meta[ci] == '\t'))
                ci++;
            vi = ci;
            vend = line_end;
            while (vend > vi &&
                   (meta[vend - 1] == ' ' || meta[vend - 1] == '\t' ||
                    meta[vend - 1] == '\r'))
                vend--;
            while (vi + olen < vend) {
                if (olen + 1 >= out_cap)
                    return -1;
                out[olen] = (char)meta[vi + olen];
                olen++;
            }
            out[olen] = '\0';
            return 0;
        }
        pos = (line_end < meta_len) ? line_end + 1 : meta_len;
    }
    return -1;
}

/* --- manifest line --------------------------------------------------------- */

static int nf_hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int newpkg_manifest_parse_line(const char *line, char *path_out,
                               newpkg_u32 *size_out, newpkg_u32 *crc_out,
                               newpkg_u32 *mode_out)
{
    const char *p = line;
    newpkg_u32 mode = 0, size = 0, crc = 0;
    int digits = 0;
    newpkg_usize plen = 0;

    if (!line || !path_out || !size_out || !crc_out || !mode_out)
        return -1;

    /* octal mode, 3-4 digits */
    while (*p >= '0' && *p <= '7' && digits < 4) {
        mode = mode * 8u + (newpkg_u32)(*p - '0');
        p++;
        digits++;
    }
    if (digits < 3 || *p != ' ')
        return -1;
    p++;
    if (!nf_isdigit(*p))
        return -1;
    while (nf_isdigit(*p)) {
        newpkg_u32 d = (newpkg_u32)(*p - '0');
        if (size > (NEWPKG_PKG_MAX - d) / 10u)
            return -1;
        size = size * 10u + d;
        p++;
    }
    if (size > NEWPKG_FILE_MAX || *p != ' ')
        return -1;
    p++;
    {
        int i = 0;
        for (; i < 8; i++) {
            int v = nf_hexval(p[i]);
            if (v < 0)
                return -1;
            crc = (crc << 4) | (newpkg_u32)v;
        }
        p += 8;
    }
    if (*p != ' ')
        return -1;
    p++;
    while (p[plen] && p[plen] != '\n' && p[plen] != '\r')
        plen++;
    if (plen == 0 || plen >= NEWPKG_PATH_MAX)
        return -1;
    {
        newpkg_usize i = 0;
        for (; i < plen; i++)
            path_out[i] = p[i];
        path_out[plen] = '\0';
    }
    if (newpkg_path_valid(path_out) != 0)
        return -1;

    *size_out = size;
    *crc_out = crc;
    *mode_out = mode;
    (void)nf_strcmp;
    (void)nf_strncmp;
    return 0;
}

/* --- path policy ----------------------------------------------------------- */

int newpkg_path_valid(const char *path)
{
    static const char *allowed[] = {
        "/bin/", "/sbin/", "/lib/", "/etc/", "/usr/", "/opt/",
        "/var/", "/home/", "/root/", "/tmp/", 0
    };
    newpkg_usize len, i = 0;
    int ai;

    if (!path)
        return -1;
    len = nf_strlen(path);
    if (len < 2 || len >= NEWPKG_PATH_MAX)
        return -1;
    if (path[0] != '/')
        return -1;
    /* Charset + no `//` + no trailing `/`. */
    for (; i < len; i++) {
        char c = path[i];
        int ok = nf_isalpha(c) || nf_isdigit(c) || c == '/' || c == '.' ||
                 c == '_' || c == '-' || c == '+' || c == '~' ||
                 c == '@' || c == '%' || c == '=';
        if (!ok)
            return -1;
        if (c == '/' && i + 1 < len && path[i + 1] == '/')
            return -1;
    }
    if (path[len - 1] == '/')
        return -1;
    /* No `.` / `..` components. */
    {
        newpkg_usize s = 1;
        while (s < len) {
            newpkg_usize e = s;
            while (e < len && path[e] != '/')
                e++;
            if ((e - s == 1 && path[s] == '.') ||
                (e - s == 2 && path[s] == '.' && path[s + 1] == '.'))
                return -1;
            s = (e < len) ? e + 1 : len;
        }
    }
    /* Must sit under an allowlisted prefix. */
    for (ai = 0; allowed[ai]; ai++) {
        newpkg_usize al = nf_strlen(allowed[ai]);
        newpkg_usize k = 0;
        int match = 1;
        if (al > len)
            continue;
        for (; k < al; k++) {
            if (path[k] != allowed[ai][k]) {
                match = 0;
                break;
            }
        }
        if (match)
            return 0;
    }
    return -1;
}

/* --- versions ----------------------------------------------------------------
 *
 * Compare dotted numeric prefixes; a `-suffix` (e.g. "-pre-alpha") is cut
 * before comparing so the running "0.2.0-pre-alpha" satisfies "0.2.0".
 */

static newpkg_u32 nf_seg(const char *s, newpkg_usize *pos)
{
    newpkg_u32 v = 0;
    while (nf_isdigit(s[*pos])) {
        v = v * 10u + (newpkg_u32)(s[*pos] - '0');
        (*pos)++;
    }
    return v;
}

static newpkg_usize nf_core_len(const char *s)
{
    newpkg_usize n = 0;
    while (s[n] && s[n] != '-')
        n++;
    return n;
}

int newpkg_vercmp(const char *a, const char *b)
{
    newpkg_usize ia = 0, ib = 0;
    newpkg_usize la, lb;

    if (!a || !b)
        return 0;
    la = nf_core_len(a);
    lb = nf_core_len(b);
    for (;;) {
        newpkg_u32 va, vb;
        if (ia >= la && ib >= lb)
            return 0;
        va = (ia < la && nf_isdigit(a[ia])) ? nf_seg(a, &ia) : 0;
        vb = (ib < lb && nf_isdigit(b[ib])) ? nf_seg(b, &ib) : 0;
        if (va < vb)
            return -1;
        if (va > vb)
            return 1;
        /* Skip one separator on each side (anything non-digit). */
        if (ia < la && !nf_isdigit(a[ia]))
            ia++;
        if (ib < lb && !nf_isdigit(b[ib]))
            ib++;
        if (ia >= la && ib >= lb)
            return 0;
        /* A side that ran out of segments compares 0 from here on. */
        if (ia >= la || ib >= lb) {
            int rest_a = 0, rest_b = 0;
            newpkg_usize ta = ia, tb = ib;
            while (ta < la) {
                if (nf_isdigit(a[ta])) {
                    newpkg_usize q = ta;
                    if (nf_seg(a, &q) != 0)
                        rest_a = 1;
                    ta = q;
                } else {
                    ta++;
                }
            }
            while (tb < lb) {
                if (nf_isdigit(b[tb])) {
                    newpkg_usize q = tb;
                    if (nf_seg(b, &q) != 0)
                        rest_b = 1;
                    tb = q;
                } else {
                    tb++;
                }
            }
            if (rest_a != rest_b)
                return rest_a ? 1 : -1;
            return 0;
        }
    }
}

int newpkg_dep_match(const char *installed, const char *op, const char *want)
{
    int c;

    if (!installed || !op || !want)
        return -1;
    if (op[0] == '\0')
        return 1;
    c = newpkg_vercmp(installed, want);
    if (nf_strcmp(op, "=") == 0 || nf_strcmp(op, "==") == 0)
        return c == 0;
    if (nf_strcmp(op, ">=") == 0)
        return c >= 0;
    if (nf_strcmp(op, "<=") == 0)
        return c <= 0;
    if (nf_strcmp(op, ">") == 0)
        return c > 0;
    if (nf_strcmp(op, "<") == 0)
        return c < 0;
    return -1;
}

int newpkg_dep_parse(const char *list, int idx, char *name_out,
                     char *op_out, char *ver_out)
{
    int cur = 0;
    const char *p;

    if (!list || idx < 0 || !name_out || !op_out || !ver_out)
        return -1;
    p = list;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '\0')
        return -1;

    while (cur <= idx) {
        const char *comma = p;
        const char *seg_end;
        const char *paren;
        newpkg_usize nl = 0, k;

        while (*comma && *comma != ',')
            comma++;
        seg_end = comma;
        /* Trim trailing blanks of the segment. */
        while (seg_end > p &&
               (seg_end[-1] == ' ' || seg_end[-1] == '\t'))
            seg_end--;
        if (cur == idx) {
            if (seg_end == p)
                return -1;
            paren = p;
            while (paren < seg_end && *paren != '(')
                paren++;
            if (paren == seg_end) {
                /* Bare name. */
                nl = (newpkg_usize)(seg_end - p);
                if (nl == 0 || nl >= NEWPKG_NAME_MAX)
                    return -1;
                for (k = 0; k < nl; k++) {
                    char c = p[k];
                    if (!nf_isalpha(c) && !nf_isdigit(c) && c != '-' &&
                        c != '_' && c != '+') {
                        if (!(k == 0 && c == ' '))
                            return -1;
                    }
                    name_out[k] = c;
                }
                name_out[nl] = '\0';
                op_out[0] = '\0';
                ver_out[0] = '\0';
                return 0;
            }
            /* name (op ver) */
            {
                const char *ne = paren;
                const char *q;
                const char *ve;
                const char *close;
                newpkg_usize ol = 0, vl = 0;

                while (ne > p && (ne[-1] == ' ' || ne[-1] == '\t'))
                    ne--;
                nl = (newpkg_usize)(ne - p);
                if (nl == 0 || nl >= NEWPKG_NAME_MAX)
                    return -1;
                for (k = 0; k < nl; k++) {
                    char c = p[k];
                    if (!nf_isalpha(c) && !nf_isdigit(c) && c != '-' &&
                        c != '_' && c != '+')
                        return -1;
                    name_out[k] = c;
                }
                name_out[nl] = '\0';
                q = paren + 1;
                while (q < seg_end && (*q == ' ' || *q == '\t'))
                    q++;
                while (q < seg_end && (*q == '=' || *q == '<' ||
                                       *q == '>') &&
                       ol < 2) {
                    op_out[ol++] = *q++;
                }
                op_out[ol] = '\0';
                if (ol == 0)
                    return -1;
                while (q < seg_end && (*q == ' ' || *q == '\t'))
                    q++;
                ve = q;
                while (ve < seg_end && *ve != ' ' && *ve != '\t' &&
                       *ve != ')')
                    ve++;
                vl = (newpkg_usize)(ve - q);
                if (vl == 0 || vl >= NEWPKG_VER_MAX)
                    return -1;
                for (k = 0; k < vl; k++)
                    ver_out[k] = q[k];
                ver_out[vl] = '\0';
                close = ve;
                while (close < seg_end &&
                       (*close == ' ' || *close == '\t'))
                    close++;
                if (close >= seg_end || *close != ')')
                    return -1;
                close++;
                while (close < seg_end &&
                       (*close == ' ' || *close == '\t'))
                    close++;
                if (close != seg_end)
                    return -1;
                return 0;
            }
        }
        if (*comma == '\0')
            return -1;
        p = comma + 1;
        while (*p == ' ' || *p == '\t')
            p++;
        cur++;
    }
    return -1;
}

/* --- arch gate --------------------------------------------------------------- */

int newpkg_arch_ok(const char *pkg_arch, const char *machine)
{
    if (!pkg_arch || !machine)
        return -1;
    if (nf_strcmp(pkg_arch, "any") == 0)
        return 0;
    return nf_strcmp(pkg_arch, machine) == 0 ? 0 : -1;
}

/* --- file name convention ---------------------------------------------------- */

int newpkg_file_name(const char *name, const char *version, const char *arch,
                     char *out, newpkg_usize out_cap)
{
    newpkg_usize i = 0, k = 0;

    if (!name || !version || !arch || !out || out_cap < 16)
        return -1;
    if (nf_strlen(name) == 0 || nf_strlen(name) >= NEWPKG_NAME_MAX)
        return -1;
    if (nf_strlen(version) == 0 || nf_strlen(version) >= NEWPKG_VER_MAX)
        return -1;
    for (; name[k]; k++) {
        char c = name[k];
        if (!nf_isalpha(c) && !nf_isdigit(c) && c != '-' && c != '_' &&
            c != '+')
            return -1;
    }
    /* name */
    for (k = 0; name[k]; k++) {
        if (i + 1 >= out_cap)
            return -1;
        out[i++] = name[k];
    }
    if (i + 1 >= out_cap)
        return -1;
    out[i++] = '-';
    for (k = 0; version[k]; k++) {
        char c = version[k];
        if (!nf_isalpha(c) && !nf_isdigit(c) && c != '.' && c != '-' &&
            c != '_' && c != '+')
            return -1;
        if (i + 1 >= out_cap)
            return -1;
        out[i++] = c;
    }
    if (i + 1 >= out_cap)
        return -1;
    out[i++] = '-';
    for (k = 0; arch[k]; k++) {
        if (i + 1 >= out_cap)
            return -1;
        out[i++] = arch[k];
    }
    {
        static const char suf[] = ".new";
        int s = 0;
        for (; s < 4; s++) {
            if (i + 1 >= out_cap)
                return -1;
            out[i++] = suf[s];
        }
    }
    out[i] = '\0';
    return 0;
}
