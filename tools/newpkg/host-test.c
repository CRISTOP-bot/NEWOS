/* host-test.c: host unit driver for newpkg_format.c pure functions.
 *
 * Compiled with the HOST gcc (not the NEWOS freestanding flags):
 *   gcc -DNEWPKG_HOST -Wall -Wextra -Werror -o /tmp/newpkg-host-test \
 *       user/programs/newpkg/newpkg_format.c tools/newpkg/host-test.c
 * Uses stdio only; the format code itself stays libc-free.
 */

#include <stdio.h>
#include <string.h>

#include "../../user/programs/newpkg/newpkg_format.h"

static int s_fail;
static int s_count;

#define CHECK(cond)                                                     \
    do {                                                                \
        s_count++;                                                      \
        if (!(cond)) {                                                  \
            s_fail++;                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                               \
    } while (0)

/* Build a minimal valid in-memory package for header_verify tests. */
static size_t build_sample(unsigned char *out, size_t cap, int tamper)
{
    static const char meta[] = "arch: x86_64\n"
                               "build: host-test\n"
                               "depends: \n"
                               "desc: sample\n"
                               "license: MIT\n"
                               "maintainer: test\n"
                               "name: hello-new\n"
                               "newos-min: 0.2.0\n"
                               "version: 1.0.0\n";
    static const char payload[] = "HELLO-PAYLOAD-BYTES";
    char manifest[128];
    struct newpkg_header h;
    unsigned char hdr[48];
    size_t meta_len = sizeof(meta) - 1;
    size_t pay_len = sizeof(payload) - 1;
    unsigned int pcrc = (unsigned int)newpkg_crc32(
        (const newpkg_u8 *)payload, (newpkg_usize)pay_len);
    size_t mlen;
    size_t total;

    sprintf(manifest, "0755 %u %08x /bin/hello-new\n", (unsigned)pay_len,
            pcrc);
    mlen = strlen(manifest);

    h.spec_version = NEWPKG_SPEC_VERSION;
    h.flags = 0;
    h.meta_len = (newpkg_u32)meta_len;
    h.manifest_len = (newpkg_u32)mlen;
    h.payload_len = (newpkg_u32)pay_len;
    h.meta_crc = newpkg_crc32((const newpkg_u8 *)meta,
                              (newpkg_usize)meta_len);
    h.manifest_crc = newpkg_crc32((const newpkg_u8 *)manifest,
                                  (newpkg_usize)mlen);
    h.payload_crc = pcrc;
    h.file_count = 1;
    newpkg_header_encode(&h, hdr);

    total = 48 + meta_len + mlen + pay_len;
    if (total > cap)
        return 0;
    memcpy(out, hdr, 48);
    memcpy(out + 48, meta, meta_len);
    memcpy(out + 48 + meta_len, manifest, mlen);
    memcpy(out + 48 + meta_len + mlen, payload, pay_len);
    if (tamper)
        out[48 + meta_len + mlen] ^= 0xff;
    return total;
}

int main(void)
{
    /* CRC32 vectors */
    CHECK(newpkg_crc32((const newpkg_u8 *)"", 0) == 0u);
    CHECK(newpkg_crc32((const newpkg_u8 *)"123456789", 9) == 0xCBF43926u);
    CHECK(newpkg_crc32((const newpkg_u8 *)"hello", 5) !=
          newpkg_crc32((const newpkg_u8 *)"hello!", 6));
    /* Incremental == one-shot, even across odd chunk splits. */
    {
        static const char *msg = "The quick brown fox jumps over the dog";
        size_t len = strlen(msg);
        newpkg_u32 whole =
            newpkg_crc32((const newpkg_u8 *)msg, (newpkg_usize)len);
        newpkg_u32 c = newpkg_crc32_begin();
        size_t off = 0;
        while (off < len) {
            size_t n = (off % 7) + 1;
            if (n > len - off)
                n = len - off;
            c = newpkg_crc32_update(
                c, (const newpkg_u8 *)msg + off, (newpkg_usize)n);
            off += n;
        }
        CHECK(newpkg_crc32_end(c) == whole);
        CHECK(newpkg_crc32_end(newpkg_crc32_update(
                  newpkg_crc32_begin(), (const newpkg_u8 *)"123456789",
                  9)) == 0xCBF43926u);
    }

    /* LE codecs */
    {
        unsigned char b[6];
        newpkg_put_le16(b, 0x1234);
        newpkg_put_le32(b + 2, 0x89ABCDEFu);
        CHECK(newpkg_get_le16(b) == 0x1234);
        CHECK(newpkg_get_le32(b + 2) == 0x89ABCDEFu);
        CHECK(b[0] == 0x34 && b[2] == 0xEF);
    }

    /* header roundtrip + tamper */
    {
        struct newpkg_header h, d;
        unsigned char hdr[48];
        h.spec_version = 1;
        h.flags = 0;
        h.meta_len = 100;
        h.manifest_len = 50;
        h.payload_len = 1000;
        h.meta_crc = 0x11111111u;
        h.manifest_crc = 0x22222222u;
        h.payload_crc = 0x33333333u;
        h.file_count = 3;
        newpkg_header_encode(&h, hdr);
        CHECK(hdr[0] == 'N' && hdr[3] == '1');
        CHECK(newpkg_header_decode(hdr, &d) == 0);
        CHECK(d.meta_len == 100 && d.file_count == 3);
        hdr[10] ^= 1;
        CHECK(newpkg_header_decode(hdr, &d) != 0);
    }
    /* bad magic / version / flags / reserved / bounds */
    {
        struct newpkg_header h, d;
        unsigned char hdr[48];
        h.spec_version = 1;
        h.flags = 0;
        h.meta_len = 10;
        h.manifest_len = 20;
        h.payload_len = 30;
        h.meta_crc = 1;
        h.manifest_crc = 2;
        h.payload_crc = 3;
        h.file_count = 1;
        newpkg_header_encode(&h, hdr);
        {
            unsigned char bad[48];
            memcpy(bad, hdr, 48);
            bad[0] = 'X';
            CHECK(newpkg_header_decode(bad, &d) != 0);
        }
        {
            unsigned char bad[48];
            memcpy(bad, hdr, 48);
            bad[4] = 2; /* version 2 */
            CHECK(newpkg_header_decode(bad, &d) != 0);
        }
        {
            unsigned char bad[48];
            /* flip flags then fix crc manually is complex; instead craft */
            memcpy(bad, hdr, 48);
            bad[6] = 1; /* flags != 0 -> crc mismatch or flags reject */
            CHECK(newpkg_header_decode(bad, &d) != 0);
        }
        {
            unsigned char bad[48];
            memcpy(bad, hdr, 48);
            bad[40] = 1; /* reserved nonzero */
            CHECK(newpkg_header_decode(bad, &d) != 0);
        }
    }

    /* full package verify */
    {
        static unsigned char pkg[4096];
        size_t len = build_sample(pkg, sizeof(pkg), 0);
        struct newpkg_header h;
        CHECK(len > 0);
        CHECK(newpkg_header_verify(pkg, len, &h) == 0);
        CHECK(h.file_count == 1);
        CHECK(newpkg_header_verify(pkg, len - 1, NULL) != 0);
        CHECK(newpkg_header_verify(pkg, len + 1, NULL) != 0);
    }
    {
        static unsigned char pkg[4096];
        size_t len = build_sample(pkg, sizeof(pkg), 1);
        CHECK(newpkg_header_verify(pkg, len, NULL) != 0);
    }

    /* meta_get */
    {
        static const unsigned char meta[] = "name: hello-new\n"
                                            "version: 1.0.0  \n"
                                            "depends: \n"
                                            "arch: x86_64\r\n";
        char out[64];
        CHECK(newpkg_meta_get(meta, sizeof(meta) - 1, "name", out,
                              sizeof(out)) == 0 &&
              strcmp(out, "hello-new") == 0);
        CHECK(newpkg_meta_get(meta, sizeof(meta) - 1, "version", out,
                              sizeof(out)) == 0 &&
              strcmp(out, "1.0.0") == 0);
        CHECK(newpkg_meta_get(meta, sizeof(meta) - 1, "depends", out,
                              sizeof(out)) == 0 &&
              strcmp(out, "") == 0);
        CHECK(newpkg_meta_get(meta, sizeof(meta) - 1, "arch", out,
                              sizeof(out)) == 0 &&
              strcmp(out, "x86_64") == 0);
        CHECK(newpkg_meta_get(meta, sizeof(meta) - 1, "nope", out,
                              sizeof(out)) != 0);
        CHECK(newpkg_meta_get(meta, sizeof(meta) - 1, "name", out, 4) !=
              0);
    }

    /* manifest parse */
    {
        char path[256];
        newpkg_u32 size, crc, mode;
        CHECK(newpkg_manifest_parse_line(
                  "0755 19 cbf43926 /bin/hello-new", path, &size, &crc,
                  &mode) == 0 &&
              size == 19 && mode == 0755 && strcmp(path, "/bin/hello-new") == 0);
        CHECK(newpkg_manifest_parse_line("0755 19 cbf43926 /etc/app.conf",
                                         path, &size, &crc, &mode) == 0);
        CHECK(newpkg_manifest_parse_line("0644 0 00000000 /tmp/empty",
                                         path, &size, &crc, &mode) == 0);
        CHECK(newpkg_manifest_parse_line("55 19 cbf43926 /bin/x", path,
                                         &size, &crc, &mode) != 0);
        CHECK(newpkg_manifest_parse_line("0755 19 xxxxxxxx /bin/x", path,
                                         &size, &crc, &mode) != 0);
        CHECK(newpkg_manifest_parse_line("0755 19 cbf43926 bin/relative",
                                         path, &size, &crc, &mode) != 0);
        CHECK(newpkg_manifest_parse_line("0755 19 cbf43926 /bin/../etc/x",
                                         path, &size, &crc, &mode) != 0);
        CHECK(newpkg_manifest_parse_line("0755 19 cbf43926 /dev/null",
                                         path, &size, &crc, &mode) != 0);
    }

    /* path policy */
    {
        CHECK(newpkg_path_valid("/bin/hello-new") == 0);
        CHECK(newpkg_path_valid("/etc/app.conf") == 0);
        CHECK(newpkg_path_valid("/opt/firefox/firefox") == 0);
        CHECK(newpkg_path_valid("/tmp/x") == 0);
        CHECK(newpkg_path_valid("/var/lib/newpkg/a.info") == 0);
        CHECK(newpkg_path_valid("relative/path") != 0);
        CHECK(newpkg_path_valid("/bin//double") != 0);
        CHECK(newpkg_path_valid("/bin/trailing/") != 0);
        CHECK(newpkg_path_valid("/bin/../escape") != 0);
        CHECK(newpkg_path_valid("/bin/./dot") != 0);
        CHECK(newpkg_path_valid("/dev/null") != 0);
        CHECK(newpkg_path_valid("/proc/x") != 0);
        CHECK(newpkg_path_valid("/sys/x") != 0);
        CHECK(newpkg_path_valid("/init") != 0);
        CHECK(newpkg_path_valid("/") != 0);
        CHECK(newpkg_path_valid("/nope/x") != 0);
        CHECK(newpkg_path_valid("/bin/bad char") != 0);
        CHECK(newpkg_path_valid("/bin/a;b") != 0);
    }

    /* vercmp */
    {
        CHECK(newpkg_vercmp("1.0.0", "1.0.0") == 0);
        CHECK(newpkg_vercmp("1.10.0", "1.2.0") > 0);
        CHECK(newpkg_vercmp("2.0", "1.9.9") > 0);
        CHECK(newpkg_vercmp("1.0", "1.0.1") < 0);
        CHECK(newpkg_vercmp("0.2.0-pre-alpha", "0.2.0") == 0);
        CHECK(newpkg_vercmp("0.3.0", "0.2.0-pre-alpha") > 0);
        CHECK(newpkg_vercmp("1.0.0", "1.0") == 0);
    }

    /* dep_match */
    {
        CHECK(newpkg_dep_match("1.2.0", "", "0.0.0") == 1);
        CHECK(newpkg_dep_match("1.2.0", "=", "1.2.0") == 1);
        CHECK(newpkg_dep_match("1.2.0", "=", "1.2.1") == 0);
        CHECK(newpkg_dep_match("1.2.0", ">=", "1.0.0") == 1);
        CHECK(newpkg_dep_match("1.2.0", ">=", "1.3.0") == 0);
        CHECK(newpkg_dep_match("1.2.0", "<", "2.0.0") == 1);
        CHECK(newpkg_dep_match("1.2.0", ">>", "1.0.0") == -1);
        CHECK(newpkg_dep_match("1.2.0", "!=", "1.2.0") == -1);
    }

    /* dep_parse */
    {
        char n[64], o[8], v[32];
        CHECK(newpkg_dep_parse("libfoo (>= 1.2), bar", 0, n, o, v) == 0 &&
              strcmp(n, "libfoo") == 0 && strcmp(o, ">=") == 0 &&
              strcmp(v, "1.2") == 0);
        CHECK(newpkg_dep_parse("libfoo (>= 1.2), bar", 1, n, o, v) == 0 &&
              strcmp(n, "bar") == 0 && o[0] == '\0');
        CHECK(newpkg_dep_parse("libfoo (>= 1.2), bar", 2, n, o, v) != 0);
        CHECK(newpkg_dep_parse("", 0, n, o, v) != 0);
        CHECK(newpkg_dep_parse("a (= 1.0)", 0, n, o, v) == 0 &&
              strcmp(o, "=") == 0);
        CHECK(newpkg_dep_parse("bad entry!!!", 0, n, o, v) != 0);
        CHECK(newpkg_dep_parse("a (>= )", 0, n, o, v) != 0);
    }

    /* arch gate */
    {
        CHECK(newpkg_arch_ok("x86_64", "x86_64") == 0);
        CHECK(newpkg_arch_ok("any", "x86_64") == 0);
        CHECK(newpkg_arch_ok("aarch64", "x86_64") != 0);
    }

    /* file name */
    {
        char out[128];
        CHECK(newpkg_file_name("hello-new", "1.0.0", "x86_64", out,
                               sizeof(out)) == 0 &&
              strcmp(out, "hello-new-1.0.0-x86_64.new") == 0);
        CHECK(newpkg_file_name("bad name", "1.0", "x86_64", out,
                               sizeof(out)) != 0);
        CHECK(newpkg_file_name("ok", "1.0", "x86_64", out, 8) != 0);
    }

    if (s_fail == 0)
        printf("host-test: %d/%d PASS\n", s_count, s_count);
    else
        printf("host-test: %d failures of %d\n", s_fail, s_count);
    return s_fail ? 1 : 0;
}
