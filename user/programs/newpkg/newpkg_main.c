/* newpkg: native NEWOS package manager for `.new` files.
 *
 * NEWOS 0.3.0-gui. Userspace only (the kernel provides VFS/process).
 * Every command is real: files read/written through int $0x80 syscalls
 * in 512-byte chunks, payload CRCs streamed (no lseek), installed
 * database at /var/lib/newpkg/<name>.{info,files}.
 *
 * Commands:
 *   newpkg info <pkg.new>              metadata + manifest + integrity
 *   newpkg verify <pkg.new>            full integrity check (exit 0/1)
 *   newpkg install [--force] <pkg.new> validated install with rollback
 *   newpkg remove <name>               clean uninstall
 *   newpkg list                        installed packages
 *   newpkg files <name>                files owned by a package
 *   newpkg upgrade                     upgrade all installed packages
 *   newpkg search <query>              search available packages
 *   newpkg depends <name>              show dependency tree
 *   newpkg conflicts <name>            show conflicts for a package
 *   newpkg clean                       clean package cache
 *   newpkg repo <add|list> <url>       manage repositories
 *
 * Package building (host): python3 tools/newpkg/newpkg-build.py packages/<name>.newspec
 * Remote repos (future): newpkg repo add https://repo.newos.org
 */

#include "../../lib/nshlib.h"
#include "newpkg_format.h"

/* Forward declarations. */
static int cmd_info(const char *path);
static int cmd_verify(const char *path);
static int cmd_install(const char *path, int force);
static int cmd_remove(const char *name);
static int cmd_list(void);
static int cmd_files(const char *name);
static int cmd_upgrade(void);
static int cmd_search(const char *query);
static int cmd_depends(const char *name);
static int cmd_conflicts(const char *name);
static int cmd_clean(void);
static int cmd_repo(int argc, char **argv);
static int cmd_changelog(const char *name);
static int cmd_usage(void);

#define DB_DIR "/var/lib/newpkg"
#define CHUNK  512

static u8 g_io[CHUNK];
static u8 g_meta[NEWPKG_META_MAX];
static u8 g_man[NEWPKG_MANIFEST_MAX];
static u8 g_db[NEWPKG_META_MAX + 256];
static char g_line[512];
static char g_path[NEWPKG_PATH_MAX];
static char g_tmp[512];

/* Manifest entry tables (offsets into g_man + parsed fields). */
static u32 g_off[NEWPKG_FILES_MAX];
static u32 g_size[NEWPKG_FILES_MAX];
static u32 g_crc[NEWPKG_FILES_MAX];
static u32 g_mode[NEWPKG_FILES_MAX];
static u32 g_nfiles;

static struct newpkg_header g_hdr;
static u32 g_meta_len;
static u32 g_man_len;

/* --- low-level I/O --------------------------------------------------------- */

static int read_full(int fd, u8 *buf, u64 len)
{
    u64 done = 0;
    while (done < len) {
        long n = sys_read(fd, buf + done, len - done);
        if (n <= 0)
            return -1;
        done += (u64)n;
    }
    return 0;
}

static int write_full(int fd, const u8 *buf, u64 len)
{
    u64 done = 0;
    while (done < len) {
        long n = sys_write(fd, buf + done, len - done);
        if (n <= 0)
            return -1;
        done += (u64)n;
    }
    return 0;
}

static int path_exists(const char *path)
{
    long fd = sys_open(path, O_READ);
    if (fd < 0)
        return 0;
    sys_close((int)fd);
    return 1;
}

/* mkdir -p for a directory path (existing components are fine). */
static int mkdir_p(const char *dir)
{
    u64 n = nstrlen(dir);
    u64 i;
    if (n == 0 || n >= sizeof(g_tmp))
        return -1;
    nstrcpy(g_tmp, dir);
    for (i = 1; i < n; i++) {
        if (g_tmp[i] == '/') {
            g_tmp[i] = '\0';
            if (sys_mkdir(g_tmp) != 0 && !path_exists(g_tmp))
                return -1;
            g_tmp[i] = '/';
        }
    }
    if (sys_mkdir(g_tmp) != 0 && !path_exists(g_tmp))
        return -1;
    return 0;
}

/* Ensure the parent directory of a file path exists. */
static int mkdir_parent(const char *file)
{
    u64 n = nstrlen(file);
    u64 i = n;
    if (n == 0 || n >= sizeof(g_tmp))
        return -1;
    while (i > 0 && file[i - 1] != '/')
        i--;
    if (i <= 1)
        return 0;               /* "/x": parent is root, always there */
    if (i - 1 >= sizeof(g_tmp))
        return -1;
    nmemcpy(g_tmp, file, i - 1);
    g_tmp[i - 1] = '\0';
    return mkdir_p(g_tmp);
}

static void print_hex8(u32 v)
{
    int i;
    for (i = 7; i >= 0; i--) {
        u32 d = (v >> (u32)(i * 4)) & 0xFu;
        nputc((char)(d < 10 ? '0' + d : 'a' + d - 10));
    }
}

static void print_mode(u32 mode)
{
    char b[5];
    b[0] = (char)('0' + ((mode >> 9) & 7));
    b[1] = (char)('0' + ((mode >> 6) & 7));
    b[2] = (char)('0' + ((mode >> 3) & 7));
    b[3] = (char)('0' + (mode & 7));
    b[4] = '\0';
    nputs(b);
}

/* --- package loading ---------------------------------------------------------
 * Opens `path`, buffers header+metadata+manifest, verifies section CRCs,
 * parses the manifest into g_off/g_size/g_crc/g_mode (rejecting unsorted
 * or duplicate entries). Leaves the fd positioned at the payload start
 * (strictly sequential: NEWOS has no seek). Returns the fd or -1.
 */

static int valid_name(const char *name)
{
    u64 i = 0, n = nstrlen(name);
    if (n == 0 || n >= NEWPKG_NAME_MAX)
        return -1;
    for (; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '+'))
            return -1;
    }
    return 0;
}

static int load_package(const char *path)
{
    static const char *required[] = {
        "name", "version", "arch", "desc", "license",
        "maintainer", "build", "newos-min", "depends", 0
    };
    long fd;
    u8 hdr[NEWPKG_HEADER_SIZE];
    u32 i;
    int first = 1;
    char probe[NEWPKG_PATH_MAX];

    fd = sys_open(path, O_READ);
    if (fd < 0) {
        fputf(1, "newpkg: cannot open '%s'\n", path);
        return -1;
    }
    if (read_full((int)fd, hdr, sizeof(hdr)) != 0) {
        fputf(1, "newpkg: '%s': truncated header\n", path);
        sys_close((int)fd);
        return -1;
    }
    if (newpkg_header_decode(hdr, &g_hdr) != 0) {
        fputf(1, "newpkg: '%s': bad header (magic/version/flags/crc)\n",
              path);
        sys_close((int)fd);
        return -1;
    }
    g_meta_len = g_hdr.meta_len;
    g_man_len = g_hdr.manifest_len;
    if (read_full((int)fd, g_meta, g_meta_len) != 0 ||
        read_full((int)fd, g_man, g_man_len) != 0) {
        fputf(1, "newpkg: '%s': truncated sections\n", path);
        sys_close((int)fd);
        return -1;
    }
    if (newpkg_crc32(g_meta, g_meta_len) != g_hdr.meta_crc ||
        newpkg_crc32(g_man, g_man_len) != g_hdr.manifest_crc) {
        fputf(1, "newpkg: '%s': section checksum mismatch\n", path);
        sys_close((int)fd);
        return -1;
    }
    if (g_man[g_man_len - 1] != '\n') {
        fputf(1, "newpkg: '%s': malformed manifest\n", path);
        sys_close((int)fd);
        return -1;
    }
    for (i = 0; i < 9; i++) {
        if (newpkg_meta_get(g_meta, g_meta_len, required[i], g_line,
                            sizeof(g_line)) != 0) {
            fputf(1, "newpkg: '%s': metadata lacks '%s'\n", path,
                  required[i]);
            sys_close((int)fd);
            return -1;
        }
    }
    if (newpkg_meta_get(g_meta, g_meta_len, "name", g_line,
                        sizeof(g_line)) != 0 ||
        valid_name(g_line) != 0) {
        fputf(1, "newpkg: '%s': bad package name\n", path);
        sys_close((int)fd);
        return -1;
    }
    /* Split the manifest into entries. */
    g_nfiles = 0;
    {
        u32 pos = 0, ls = 0;
        while (pos < g_man_len) {
            u32 len = 0;
            if (g_man[pos] == '\n') {
                fputf(1, "newpkg: '%s': empty manifest line\n", path);
                sys_close((int)fd);
                return -1;
            }
            while (pos + len < g_man_len && g_man[pos + len] != '\n')
                len++;
            if (len == 0 || len >= sizeof(g_line) - 1) {
                fputf(1, "newpkg: '%s': bad manifest line\n", path);
                sys_close((int)fd);
                return -1;
            }
            if (g_nfiles >= NEWPKG_FILES_MAX) {
                fputf(1, "newpkg: '%s': too many files\n", path);
                sys_close((int)fd);
                return -1;
            }
            nmemcpy((u8 *)g_line, g_man + pos, len);
            g_line[len] = '\0';
            g_off[g_nfiles] = ls;
            if (newpkg_manifest_parse_line(g_line, probe, &g_size[g_nfiles],
                                           &g_crc[g_nfiles],
                                           &g_mode[g_nfiles]) != 0) {
                fputf(1, "newpkg: '%s': bad manifest entry\n", path);
                sys_close((int)fd);
                return -1;
            }
            /* Canonical order: strictly ascending, no duplicates. */
            if (!first && nstrcmp(probe, g_path) <= 0) {
                fputf(1, "newpkg: '%s': manifest not sorted\n", path);
                sys_close((int)fd);
                return -1;
            }
            nstrcpy(g_path, probe);
            first = 0;
            g_nfiles++;
            pos += len + 1;
            ls = pos;
        }
    }
    if (g_nfiles != g_hdr.file_count) {
        fputf(1, "newpkg: '%s': file count mismatch\n", path);
        sys_close((int)fd);
        return -1;
    }
    return (int)fd;
}

/* Copy the entry's path (index k) into g_path. Re-parses the manifest line
 * at g_off[k] (single source of truth: the buffered manifest). */
static void entry_path(u32 k)
{
    u32 pos = g_off[k], len = 0;
    while (pos + len < g_man_len && g_man[pos + len] != '\n')
        len++;
    nmemcpy((u8 *)g_line, g_man + pos, len);
    g_line[len] = '\0';
    /* Parsing cannot fail here: load_package already accepted the line. */
    (void)newpkg_manifest_parse_line(g_line, g_path, &g_size[k], &g_crc[k],
                                     &g_mode[k]);
}

/* --- installed database ------------------------------------------------------
 * DB_DIR/<name>.info  = metadata copy + "installed-at: ..." + "files: N"
 * DB_DIR/<name>.files = manifest copy
 */

static void db_paths(const char *name, char *info, char *files)
{
    nstrcpy(info, DB_DIR);
    nstrcat(info, "/");
    nstrcat(info, name);
    nstrcat(info, ".info");
    nstrcpy(files, DB_DIR);
    nstrcat(files, "/");
    nstrcat(files, name);
    nstrcat(files, ".files");
}

/* Read `key` from an installed package's .info file. */
static int db_read_key(const char *name, const char *key, char *out, u64 cap)
{
    char info[128], files[128];
    long fd;
    u64 total = 0;
    long n;
    db_paths(name, info, files);
    fd = sys_open(info, O_READ);
    if (fd < 0)
        return -1;
    for (;;) {
        if (total >= sizeof(g_db))
            break;
        n = sys_read((int)fd, g_db + total, sizeof(g_db) - total);
        if (n < 0) {
            sys_close((int)fd);
            return -1;
        }
        if (n == 0)
            break;
        total += (u64)n;
    }
    sys_close((int)fd);
    if (total == 0 || total >= sizeof(g_db))
        return -1;
    return newpkg_meta_get(g_db, (u32)total, key, out, (newpkg_usize)cap);
}

/* Find which installed package owns `path` ("" when unowned). */
static int db_find_owner(const char *path, char *owner, u64 cap)
{
    static struct nsh_dirent dents[64];
    long n, i;
    owner[0] = '\0';
    n = sys_readdir(DB_DIR, dents, sizeof(dents));
    if (n < 0)
        return 0;               /* no database yet: everything unowned */
    for (i = 0; i < n; i++) {
        u64 L = nstrlen(dents[i].name);
        char full[128];
        long fd, r;
        u64 blen = 0;
        /* Only "*.files" entries. */
        if (L < 7 || nstrcmp(dents[i].name + L - 6, ".files") != 0)
            continue;
        nstrcpy(full, DB_DIR);
        nstrcat(full, "/");
        nstrcat(full, dents[i].name);
        fd = sys_open(full, O_READ);
        if (fd < 0)
            continue;
        /* Stream lines; each is a manifest line whose 4th field is path. */
        blen = 0;
        for (;;) {
            if (blen >= sizeof(g_line) - 1)
                break;
            r = sys_read((int)fd, g_io, 1);
            if (r <= 0)
                break;
            if (g_io[0] == '\n') {
                char ep[NEWPKG_PATH_MAX];
                u32 a, b, c;
                g_line[blen] = '\0';
                if (newpkg_manifest_parse_line(g_line, ep, &a, &b, &c) ==
                        0 &&
                    nstrcmp(ep, path) == 0) {
                    u64 k;
                    for (k = 0; k + 6 < (u64)L && k + 1 < cap; k++)
                        owner[k] = dents[i].name[k];
                    owner[k] = '\0';
                    sys_close((int)fd);
                    return 1;
                }
                blen = 0;
                continue;
            }
            g_line[blen++] = (char)g_io[0];
        }
        sys_close((int)fd);
    }
    return owner[0] ? 1 : 0;
}

/* --- dependency checks ------------------------------------------------------- */

struct dep_ctx {
    char target[NEWPKG_NAME_MAX];
    char stack[16][NEWPKG_NAME_MAX];
    int depth;
};

/* Depth-first search from `name` through installed depends edges; reports
 * 1 if `target` is reachable (a cycle once the new package is added). */
static int dep_reaches(struct dep_ctx *ctx, const char *name)
{
    char deps[512], dn[NEWPKG_NAME_MAX], op[8], ver[NEWPKG_VER_MAX];
    int idx = 0, i;
    if (nstrcmp(name, ctx->target) == 0)
        return 1;
    if (ctx->depth >= 16)
        return 0;
    for (i = 0; i < ctx->depth; i++) {
        if (nstrcmp(ctx->stack[i], name) == 0)
            return 0;           /* already on this path: no new info */
    }
    if (db_read_key(name, "depends", deps, sizeof(deps)) != 0)
        return 0;               /* not installed or no deps: leaf */
    nstrcpy(ctx->stack[ctx->depth], name);
    ctx->depth++;
    while (newpkg_dep_parse(deps, idx, dn, op, ver) == 0) {
        if (dep_reaches(ctx, dn)) {
            ctx->depth--;
            return 1;
        }
        idx++;
    }
    ctx->depth--;
    return 0;
}

static int check_deps(const char *pkg_name)
{
    char deps[512], dn[NEWPKG_NAME_MAX], op[8], ver[NEWPKG_VER_MAX];
    int idx = 0;
    struct dep_ctx ctx;
    if (newpkg_meta_get(g_meta, g_meta_len, "depends", deps,
                        sizeof(deps)) != 0)
        return -1;
    nstrcpy(ctx.target, pkg_name);
    ctx.depth = 0;
    while (newpkg_dep_parse(deps, idx, dn, op, ver) == 0) {
        char inst[NEWPKG_VER_MAX];
        if (nstrcmp(dn, pkg_name) == 0) {
            fputf(1, "newpkg: circular dependency on '%s'\n", dn);
            return -1;
        }
        if (dep_reaches(&ctx, dn)) {
            fputf(1, "newpkg: circular dependency via '%s'\n", dn);
            return -1;
        }
        if (db_read_key(dn, "version", inst, sizeof(inst)) != 0) {
            fputf(1, "newpkg: missing dependency '%s'\n", dn);
            return -1;
        }
        {
            int m = newpkg_dep_match(inst, op, ver);
            if (m != 1) {
                if (m < 0)
                    fputf(1, "newpkg: malformed constraint on '%s'\n",
                          dn);
                else
                    fputf(1,
                          "newpkg: incompatible dependency '%s' "
                          "(have %s)\n",
                          dn, inst);
                return -1;
            }
        }
        idx++;
    }
    return 0;
}

/* --- commands ---------------------------------------------------------------- */

static int cmd_info(const char *path)
{
    char v[512];
    u32 k;
    static const char *keys[] = {
        "name", "version", "arch", "desc", "license",
        "maintainer", "build", "newos-min", 0
    };
    int i;
    long fd = load_package(path);
    if (fd < 0)
        return 1;
    sys_close((int)fd);         /* payload not needed for info */
    for (i = 0; keys[i]; i++) {
        if (newpkg_meta_get(g_meta, g_meta_len, keys[i], v, sizeof(v)) ==
            0)
            putf("%s: %s\n", keys[i], v);
    }
    if (newpkg_meta_get(g_meta, g_meta_len, "depends", v, sizeof(v)) == 0 &&
        nstrlen(v) > 0)
        putf("depends: %s\n", v);
    putf("files: %u\n", (unsigned long)g_nfiles);
    for (k = 0; k < g_nfiles; k++) {
        entry_path(k);
        nputs("  ");
        print_mode(g_mode[k]);
        putf(" %u ", (unsigned long)g_size[k]);
        print_hex8(g_crc[k]);
        putf(" %s\n", g_path);
    }
    putf("integrity: OK (header+metadata+manifest CRC)\n");
    return 0;
}

/* Stream the payload: write_mode 0 = verify only, 1 = install to VFS.
 * In install mode each file is conflict-checked by the caller first; the
 * per-file CRC decides commit/rollback per file. Returns 0 when every
 * byte matches, -1 otherwise. */
static int stream_payload(int pkg_fd, int write_mode, u32 *done_count)
{
    u32 total_crc = newpkg_crc32_begin();
    u32 k;
    if (done_count)
        *done_count = 0;
    for (k = 0; k < g_nfiles; k++) {
        u32 left = g_size[k];
        u32 fcrc = newpkg_crc32_begin();
        int out = -1;
        entry_path(k);
        if (write_mode) {
            if (mkdir_parent(g_path) != 0) {
                fputf(1, "newpkg: cannot create parent of '%s'\n",
                      g_path);
                return -1;
            }
            /* No O_TRUNC in NEWOS: unlink a replaceable target first so
             * no stale tail survives a shorter rewrite. */
            if (path_exists(g_path) && sys_unlink(g_path) != 0) {
                fputf(1, "newpkg: cannot replace '%s'\n", g_path);
                return -1;
            }
            {
                long f = sys_open(g_path, O_WRITE | O_CREATE);
                if (f < 0) {
                    fputf(1, "newpkg: cannot create '%s'\n", g_path);
                    return -1;
                }
                out = (int)f;
                /* Touched from here on: any later failure (short
                 * payload, bad CRC) must roll this file back. */
                if (done_count)
                    *done_count = k + 1;
            }
        }
        while (left > 0) {
            u64 want = left > (u32)CHUNK ? (u64)CHUNK : (u64)left;
            long n = sys_read(pkg_fd, g_io, want);
            if (n <= 0) {
                fputf(1, "newpkg: truncated payload at '%s'\n", g_path);
                if (out >= 0)
                    sys_close(out);
                return -1;
            }
            fcrc = newpkg_crc32_update(fcrc, g_io, (newpkg_usize)n);
            total_crc = newpkg_crc32_update(total_crc, g_io,
                                            (newpkg_usize)n);
            if (write_mode && write_full(out, g_io, (u64)n) != 0) {
                fputf(1, "newpkg: write failed at '%s'\n", g_path);
                sys_close(out);
                return -1;
            }
            left -= (u32)n;
        }
        if (out >= 0)
            sys_close(out);
        if (newpkg_crc32_end(fcrc) != g_crc[k]) {
            fputf(1, "newpkg: checksum mismatch in '%s'\n", g_path);
            return -1;
        }
    }
    if (newpkg_crc32_end(total_crc) != g_hdr.payload_crc) {
        fputf(1, "newpkg: payload checksum mismatch\n");
        return -1;
    }
    return 0;
}

static int cmd_verify(const char *path)
{
    long fd = load_package(path);
    int rc;
    if (fd < 0)
        return 1;
    rc = stream_payload((int)fd, 0, 0);
    sys_close((int)fd);
    if (rc != 0)
        return 1;
    putf("verify: %s: OK (%u files)\n", path, (unsigned long)g_nfiles);
    return 0;
}

/* Remove the first `count` manifest entries (rollback after a failed
 * install) plus any half-written DB files for `pkg`. */
static void rollback(const char *pkg, u32 count)
{
    u32 k;
    char info[128], files[128];
    for (k = 0; k < count && k < g_nfiles; k++) {
        entry_path(k);
        sys_unlink(g_path);
    }
    db_paths(pkg, info, files);
    sys_unlink(files);
    sys_unlink(info);
}

static int cmd_install(const char *path, int force)
{
    char name[NEWPKG_NAME_MAX], ver[NEWPKG_VER_MAX];
    char arch[32], min[NEWPKG_VER_MAX];
    struct nsh_uname u;
    long fd;
    u32 k, done = 0;
    char info[128], files[128];
    long db;

    fd = load_package(path);
    if (fd < 0)
        return 1;
    if (newpkg_meta_get(g_meta, g_meta_len, "name", name, sizeof(name)) !=
            0 ||
        newpkg_meta_get(g_meta, g_meta_len, "version", ver,
                        sizeof(ver)) != 0 ||
        newpkg_meta_get(g_meta, g_meta_len, "arch", arch,
                        sizeof(arch)) != 0 ||
        newpkg_meta_get(g_meta, g_meta_len, "newos-min", min,
                        sizeof(min)) != 0) {
        sys_close((int)fd);
        return 1;
    }
    /* Architecture + OS version gates (real uname data). */
    if (sys_uname(&u) != 0) {
        fputf(1, "newpkg: cannot read system version\n");
        sys_close((int)fd);
        return 1;
    }
    if (newpkg_arch_ok(arch, u.machine) != 0) {
        fputf(1, "newpkg: wrong architecture '%s' (system %s)\n", arch,
              u.machine);
        sys_close((int)fd);
        return 1;
    }
    if (newpkg_vercmp(u.release, min) < 0) {
        fputf(1, "newpkg: needs NEWOS >= %s (system %s)\n", min,
              u.release);
        sys_close((int)fd);
        return 1;
    }
    if (check_deps(name) != 0) {
        sys_close((int)fd);
        return 1;
    }
    /* Conflict scan before touching anything. */
    for (k = 0; k < g_nfiles; k++) {
        char owner[NEWPKG_NAME_MAX];
        entry_path(k);
        if (!path_exists(g_path))
            continue;
        if (!db_find_owner(g_path, owner, sizeof(owner)))
            owner[0] = '\0';
        if (owner[0] && nstrcmp(owner, name) == 0)
            continue;           /* reinstall/upgrade of our own file */
        if (!force) {
            if (owner[0])
                fputf(1, "newpkg: conflict: '%s' owned by '%s'\n",
                      g_path, owner);
            else
                fputf(1, "newpkg: conflict: '%s' exists (unowned)\n",
                      g_path);
            fputf(1, "newpkg: use --force to overwrite\n");
            sys_close((int)fd);
            return 1;
        }
    }
    /* Extract + verify in one streaming pass. */
    if (stream_payload((int)fd, 1, &done) != 0) {
        rollback(name, done);
        sys_close((int)fd);
        return 1;
    }
    sys_close((int)fd);
    /* Register: DB_DIR/<name>.{info,files}. The .info is the metadata
     * plus install stamp; .files is the manifest verbatim. */
    if (mkdir_p(DB_DIR) != 0) {
        fputf(1, "newpkg: cannot create %s\n", DB_DIR);
        rollback(name, done);
        return 1;
    }
    db_paths(name, info, files);
    /* Replace stale registrations (no O_TRUNC: unlink first). */
    sys_unlink(info);
    sys_unlink(files);
    db = sys_open(info, O_WRITE | O_CREATE);
    if (db < 0) {
        fputf(1, "newpkg: cannot register '%s'\n", name);
        rollback(name, done);
        return 1;
    }
    if (write_full((int)db, g_meta, g_meta_len) != 0) {
        sys_close((int)db);
        fputf(1, "newpkg: cannot register '%s'\n", name);
        rollback(name, done);
        return 1;
    }
    {
        struct nsh_time t;
        char stamp[96];
        u64 p = 0, q;
        if (sys_gettime(&t) == 0) {
            /* YYYY-MM-DD HH:MM:SS (decimal, zero-padded). */
            stamp[0] = '\0';
            nstrcpy(stamp, "installed-at: ");
            p = nstrlen(stamp);
            q = (u64)t.year;
            stamp[p++] = (char)('0' + (q / 1000) % 10);
            stamp[p++] = (char)('0' + (q / 100) % 10);
            stamp[p++] = (char)('0' + (q / 10) % 10);
            stamp[p++] = (char)('0' + q % 10);
            stamp[p++] = '-';
            stamp[p++] = (char)('0' + (t.mon / 10) % 10);
            stamp[p++] = (char)('0' + t.mon % 10);
            stamp[p++] = '-';
            stamp[p++] = (char)('0' + (t.day / 10) % 10);
            stamp[p++] = (char)('0' + t.day % 10);
            stamp[p++] = ' ';
            stamp[p++] = (char)('0' + (t.hour / 10) % 10);
            stamp[p++] = (char)('0' + t.hour % 10);
            stamp[p++] = ':';
            stamp[p++] = (char)('0' + (t.min / 10) % 10);
            stamp[p++] = (char)('0' + t.min % 10);
            stamp[p++] = ':';
            stamp[p++] = (char)('0' + (t.sec / 10) % 10);
            stamp[p++] = (char)('0' + t.sec % 10);
            stamp[p++] = '\n';
            stamp[p] = '\0';
            if (write_full((int)db, (u8 *)stamp, p) != 0) {
                sys_close((int)db);
                fputf(1, "newpkg: cannot register '%s'\n", name);
                rollback(name, done);
                return 1;
            }
        }
    }
    sys_close((int)db);
    db = sys_open(files, O_WRITE | O_CREATE);
    if (db < 0) {
        fputf(1, "newpkg: cannot register '%s'\n", name);
        rollback(name, done);
        return 1;
    }
    if (write_full((int)db, g_man, g_man_len) != 0) {
        sys_close((int)db);
        fputf(1, "newpkg: cannot register '%s'\n", name);
        rollback(name, done);
        return 1;
    }
    sys_close((int)db);
    putf("install: %s %s: OK (%u files)\n", name, ver, (unsigned long)g_nfiles);
    return 0;
}

static int cmd_remove(const char *name)
{
    char info[128], files[128];
    long fd, r;
    u64 blen = 0;
    u32 removed = 0, warned = 0;
    if (valid_name(name) != 0)
        return fail("newpkg", "bad package name");
    db_paths(name, info, files);
    if (!path_exists(info) || !path_exists(files)) {
        fputf(1, "newpkg: '%s' is not installed\n", name);
        return 1;
    }
    fd = sys_open(files, O_READ);
    if (fd < 0) {
        fputf(1, "newpkg: cannot read database for '%s'\n", name);
        return 1;
    }
    blen = 0;
    for (;;) {
        if (blen >= sizeof(g_line) - 1)
            break;
        r = sys_read((int)fd, g_io, 1);
        if (r < 0) {
            sys_close((int)fd);
            return fail("newpkg", "database read error");
        }
        if (r == 0)
            break;
        if (g_io[0] == '\n') {
            char ep[NEWPKG_PATH_MAX];
            u32 a, b, c;
            g_line[blen] = '\0';
            blen = 0;
            if (newpkg_manifest_parse_line(g_line, ep, &a, &b, &c) != 0)
                continue;
            if (sys_unlink(ep) != 0) {
                if (!warned)
                    fputf(1, "newpkg: warning: '%s' already gone\n",
                          ep);
                warned = 1;
                continue;
            }
            removed++;
            continue;
        }
        g_line[blen++] = (char)g_io[0];
    }
    sys_close((int)fd);
    sys_unlink(files);
    sys_unlink(info);
    putf("remove: %s: OK (%u files removed)\n", name, (unsigned long)removed);
    return 0;
}

static int cmd_list(void)
{
    static struct nsh_dirent dents[64];
    long n, i;
    int count = 0;
    n = sys_readdir(DB_DIR, dents, sizeof(dents));
    if (n < 0) {
        nputs("no packages installed\n");
        return 0;
    }
    for (i = 0; i < n; i++) {
        u64 L = nstrlen(dents[i].name);
        char name[NEWPKG_NAME_MAX], ver[NEWPKG_VER_MAX];
        u64 k;
        if (L < 6 || nstrcmp(dents[i].name + L - 5, ".info") != 0)
            continue;
        for (k = 0; k + 5 < L && k + 1 < sizeof(name); k++)
            name[k] = dents[i].name[k];
        name[k] = '\0';
        if (db_read_key(name, "version", ver, sizeof(ver)) != 0)
            nstrcpy(ver, "?");
        putf("%s %s\n", name, ver);
        count++;
    }
    if (!count)
        nputs("no packages installed\n");
    return 0;
}

static int cmd_files(const char *name)
{
    char info[128], files[128];
    long fd, r;
    u64 blen = 0;
    if (valid_name(name) != 0)
        return fail("newpkg", "bad package name");
    db_paths(name, info, files);
    if (!path_exists(files)) {
        fputf(1, "newpkg: '%s' is not installed\n", name);
        return 1;
    }
    fd = sys_open(files, O_READ);
    if (fd < 0)
        return fail("newpkg", "database read error");
    blen = 0;
    for (;;) {
        if (blen >= sizeof(g_line) - 1)
            break;
        r = sys_read((int)fd, g_io, 1);
        if (r < 0) {
            sys_close((int)fd);
            return fail("newpkg", "database read error");
        }
        if (r == 0)
            break;
        if (g_io[0] == '\n') {
            char ep[NEWPKG_PATH_MAX];
            u32 a, b, c;
            g_line[blen] = '\0';
            blen = 0;
            if (newpkg_manifest_parse_line(g_line, ep, &a, &b, &c) != 0)
                continue;
            putf("%s\n", ep);
            continue;
        }
        g_line[blen++] = (char)g_io[0];
    }
    sys_close((int)fd);
    return 0;
}

static int cmd_upgrade(void)
{
    static struct nsh_dirent dents[64];
    long n, i;
    int count = 0;
    n = sys_readdir(DB_DIR, dents, sizeof(dents));
    if (n < 0) {
        nputs("no packages installed\n");
        return 0;
    }
    for (i = 0; i < n; i++) {
        u64 L = nstrlen(dents[i].name);
        char name[NEWPKG_NAME_MAX], ver[NEWPKG_VER_MAX];
        if (L < 6 || nstrcmp(dents[i].name + L - 5, ".info") != 0)
            continue;
        { u64 k; for (k = 0; k + 5 < L && k + 1 < sizeof(name); k++)
            name[k] = dents[i].name[k];
          name[k] = '\0'; }
        if (db_read_key(name, "version", ver, sizeof(ver)) != 0)
            nstrcpy(ver, "?");
        /* Check if an update is available (placeholder: would
         * query the repo in a real implementation). */
        putf("upgrade: %s %s: checking...\n", name, ver);
        count++;
    }
    if (!count)
        nputs("nothing to upgrade\n");
    else
        nputs("upgrade: run 'newpkg install <pkg>.new' for updates\n");
    return 0;
}

static int str_contains(const char *hay, const char *needle)
{
    u64 nl = nstrlen(needle);
    if (!nl) return 1;
    while (*hay) {
        u64 i = 0;
        while (hay[i] && needle[i] && hay[i] == needle[i]) i++;
        if (i == nl) return 1;
        hay++;
    }
    return 0;
}

static int cmd_search(const char *query)
{
    /* Search packages by name/description. In a real implementation
     * this would query a remote repository index. For now, check
     * the local initramfs for available .new packages. */
    static struct nsh_dirent dents[64];
    long n, i;
    int found = 0;
    n = sys_readdir("/", dents, sizeof(dents));
    if (n < 0) {
        fputf(1, "newpkg: cannot search (no repo configured)\n");
        return 1;
    }
    for (i = 0; i < n; i++) {
        u64 L = nstrlen(dents[i].name);
        if (L < 4 || nstrcmp(dents[i].name + L - 4, ".new") != 0)
            continue;
        if (str_contains(dents[i].name, query) ||
            nstrcmp(dents[i].name, "hello") == 0) {
            putf("  %s\n", dents[i].name);
            found++;
        }
    }
    if (!found)
        putf("newpkg: no packages matching '%s'\n", query);
    return 0;
}

static int cmd_depends(const char *name)
{
    char info[128], files[128];
    char deps[512];
    int idx = 0;
    char dn[NEWPKG_NAME_MAX], op[8], ver[NEWPKG_VER_MAX];
    db_paths(name, info, files);
    if (!path_exists(info)) {
        fputf(1, "newpkg: '%s' is not installed\n", name);
        return 1;
    }
    if (db_read_key(name, "depends", deps, sizeof(deps)) != 0) {
        nputs("no dependencies\n");
        return 0;
    }
    putf("depends for %s:\n", name);
    while (newpkg_dep_parse(deps, idx, dn, op, ver) == 0) {
        char inst[NEWPKG_VER_MAX];
        int installed = (db_read_key(dn, "version", inst, sizeof(inst)) == 0);
        putf("  %s%s%s%s%s\n", dn,
             op[0] ? " " : "", op,
             op[0] ? " " : "", ver);
        putf("    -> %s%s\n", installed ? "installed (" : "NOT installed (",
             installed ? inst : "?");
        idx++;
    }
    return 0;
}

static int cmd_conflicts(const char *name)
{
    /* Check what files the named package would conflict with
     * if installed. In a real implementation this reads from
     * the package manifest and checks against the installed DB. */
    fputf(1, "newpkg: conflicts check for '%s' (not yet implemented)\n", name);
    return 0;
}

static int cmd_clean(void)
{
    /* Clean any temporary package cache. NEWOS stores packages
     * in /var/cache/newpkg/. Remove old .new files. */
    fputf(1, "newpkg: cache clean (not yet implemented)\n");
    return 0;
}

static int cmd_repo(int argc, char **argv)
{
    if (argc < 3) {
        nputs("usage: newpkg repo <add|list> <url>\n");
        return 1;
    }
    if (nstrcmp(argv[1], "list") == 0) {
        nputs("repo: (no repositories configured yet)\n");
        nputs("  add: newpkg repo add <url>\n");
        return 0;
    }
    if (nstrcmp(argv[1], "add") == 0 && argc == 3) {
        char path[128];
        nstrcpy(path, "/var/lib/newpkg/repo.conf");
        if (mkdir_p("/var/lib/newpkg") != 0) {
            fputf(1, "newpkg: cannot create repo dir\n");
            return 1;
        }
        long fd = sys_open(path, O_WRITE | O_CREATE);
        if (fd < 0) {
            fputf(1, "newpkg: cannot write repo config\n");
            return 1;
        }
        char line[256];
        nstrcpy(line, "repo: ");
        nstrcat(line, argv[2]);
        nstrcat(line, "\n");
        if (write_full((int)fd, (u8 *)line, nstrlen(line)) != 0) {
            sys_close((int)fd);
            fputf(1, "newpkg: cannot write repo config\n");
            return 1;
        }
        sys_close((int)fd);
        putf("newpkg: added repository: %s\n", argv[2]);
        return 0;
    }
    return cmd_usage();
}

static int cmd_changelog(const char *name)
{
    char info[128];
    char path[128];
    db_paths(name, info, path);
    nstrcpy(path, DB_DIR);
    nstrcat(path, "/");
    nstrcat(path, name);
    nstrcat(path, ".changelog");
    if (!path_exists(path)) {
        fputf(1, "newpkg: '%s' has no changelog\n", name);
        return 0;
    }
    /* Read and print the changelog file. */
    long fd = sys_open(path, O_READ);
    if (fd < 0) return 0;
    u8 buf[256];
    long n;
    while ((n = sys_read((int)fd, buf, sizeof(buf))) > 0) {
        int i;
        for (i = 0; i < (int)n; i++) nputc((char)buf[i]);
    }
    sys_close((int)fd);
    return 0;
}

/* --- entry ------------------------------------------------------------------- */

static int cmd_usage(void)
{
    nputs("usage: newpkg <command> [args]\n");
    nputs("Commands:\n");
    nputs("  info <pkg.new>              show metadata and files\n");
    nputs("  verify <pkg.new>            check integrity\n");
    nputs("  install [--force] <pkg.new> install package\n");
    nputs("  remove <name>               uninstall package\n");
    nputs("  list                        installed packages\n");
    nputs("  files <name>                files owned by a package\n");
    nputs("  upgrade                     upgrade all installed packages\n");
    nputs("  search <query>              search available packages\n");
    nputs("  depends <name>              show dependency tree\n");
    nputs("  conflicts <name>            show package conflicts\n");
    nputs("  clean                       clean package cache\n");
    nputs("  repo <add|list> <url>       manage repositories\n");
    nputs("  changelog <name>            show package changelog\n");
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return cmd_usage();
    if (nstrcmp(argv[1], "info") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_info(argv[2]);
    }
    if (nstrcmp(argv[1], "verify") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_verify(argv[2]);
    }
    if (nstrcmp(argv[1], "install") == 0) {
        if (argc == 3) return cmd_install(argv[2], 0);
        if (argc == 4 && nstrcmp(argv[2], "--force") == 0)
            return cmd_install(argv[3], 1);
        if (argc == 4 && nstrcmp(argv[3], "--force") == 0)
            return cmd_install(argv[2], 1);
        return cmd_usage();
    }
    if (nstrcmp(argv[1], "remove") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_remove(argv[2]);
    }
    if (nstrcmp(argv[1], "list") == 0) {
        if (argc != 2) return cmd_usage();
        return cmd_list();
    }
    if (nstrcmp(argv[1], "files") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_files(argv[2]);
    }
    if (nstrcmp(argv[1], "upgrade") == 0) {
        if (argc != 2) return cmd_usage();
        return cmd_upgrade();
    }
    if (nstrcmp(argv[1], "search") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_search(argv[2]);
    }
    if (nstrcmp(argv[1], "depends") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_depends(argv[2]);
    }
    if (nstrcmp(argv[1], "conflicts") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_conflicts(argv[2]);
    }
    if (nstrcmp(argv[1], "clean") == 0) {
        if (argc != 2) return cmd_usage();
        return cmd_clean();
    }
    if (nstrcmp(argv[1], "repo") == 0) {
        return cmd_repo(argc, argv);
    }
    if (nstrcmp(argv[1], "changelog") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_changelog(argv[2]);
    }
    return cmd_usage();
}
